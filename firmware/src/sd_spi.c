// Tarjeta SD por SPI1, y la capa de disco de FatFs (disk_read, disk_write...).
//
// Sin interrupciones ni DMA: el nucleo 1 espera cada transferencia. Mientras
// tanto el estado esta en OCUPADO y lo que mande el MSX queda en el anillo.

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pins.h"
#include "sd_spi.h"
#include "reloj.h"
#include "ff.h"
#include "diskio.h"

#define SD_SPI          spi1
#define HZ_INICIO       (400 * 1000)
// El modulo tiene adaptador de niveles y va por cables: 12,5 MHz alcanza
// (mas de 1 MB/s) y deja margen. Las tarjetas aceptan hasta 25 MHz.
#define HZ_TRABAJO      (12500 * 1000)

#define CMD0    0       // GO_IDLE_STATE
#define CMD8    8       // SEND_IF_COND
#define CMD12   12      // STOP_TRANSMISSION
#define CMD16   16      // SET_BLOCKLEN
#define CMD17   17      // READ_SINGLE_BLOCK
#define CMD18   18      // READ_MULTIPLE_BLOCK
#define CMD24   24      // WRITE_BLOCK
#define CMD25   25      // WRITE_MULTIPLE_BLOCK
#define CMD55   55      // APP_CMD
#define CMD58   58      // READ_OCR
#define ACMD41  41      // SD_SEND_OP_COND

static bool lista;
static bool pines;          // los pines del SPI estan configurados
static bool por_bloque;     // SDHC/SDXC: direcciones en bloques, no en bytes
static int ultimo_error = SD_SIN_PROBAR;
static uint8_t diag_cmd0;       // lo que contesto el ultimo CMD0
static bool diag_miso;          // MISO con la tarjeta sin elegir, antes de empezar

static inline void cs(bool elegida)
{
    gpio_put(PIN_SD_CS, !elegida);
}

static uint8_t xfer(uint8_t b)
{
    uint8_t r;
    spi_write_read_blocking(SD_SPI, &b, &r, 1);
    return r;
}

// Espera a que la tarjeta suelte la linea (FFh): termino de grabar.
static bool esperar_lista(uint32_t ms)
{
    absolute_time_t fin = make_timeout_time_ms(ms);
    do {
        if (xfer(0xFF) == 0xFF)
            return true;
    } while (!time_reached(fin));
    return false;
}

static void soltar(void)
{
    cs(false);
    xfer(0xFF);     // la tarjeta suelta MISO un byte despues
}

static bool elegir(void)
{
    cs(true);
    xfer(0xFF);
    if (esperar_lista(500))
        return true;
    soltar();
    return false;
}

// Manda un comando y devuelve R1 (bit 7 en 1: no contesto). Deja la tarjeta
// elegida, para leer lo que siga; el que llama suelta.
static uint8_t comando(uint8_t c, uint32_t arg)
{
    if (c == CMD0) {
        // Sin esperar a que este lista: una tarjeta colgada puede tener MISO
        // en bajo, y CMD0 es justamente lo que la saca de ahi.
        soltar();
        cs(true);
        xfer(0xFF);
    } else if (c != CMD12) {
        soltar();
        if (!elegir())
            return 0xFF;
    }
    uint8_t pkt[6] = {
        0x40 | c, arg >> 24, arg >> 16, arg >> 8, arg,
        c == CMD0 ? 0x95 : c == CMD8 ? 0x87 : 0x01,     // CRC, solo importa en estos
    };
    spi_write_blocking(SD_SPI, pkt, 6);
    if (c == CMD12)
        xfer(0xFF);     // byte de relleno
    uint8_t r;
    int n = 10;
    do {
        r = xfer(0xFF);
    } while ((r & 0x80) && --n);
    return r;
}

static uint8_t acomando(uint8_t c, uint32_t arg)
{
    uint8_t r = comando(CMD55, 0);
    if (r > 1)
        return r;
    return comando(c, arg);
}

// Un bloque de datos de la tarjeta: espera el token FEh y lee n bytes.
static bool recibir(uint8_t *buf, uint32_t n)
{
    uint8_t t;
    absolute_time_t fin = make_timeout_time_ms(300);
    do {
        t = xfer(0xFF);
    } while (t == 0xFF && !time_reached(fin));
    if (t != 0xFE)
        return false;
    spi_read_blocking(SD_SPI, 0xFF, buf, n);
    xfer(0xFF);     // CRC
    xfer(0xFF);
    return true;
}

// Un bloque de 512 hacia la tarjeta, con su token (FEh, o FCh en CMD25).
static bool enviar(const uint8_t *buf, uint8_t token)
{
    if (!esperar_lista(600))
        return false;
    xfer(token);
    if (token == 0xFD)      // fin de CMD25: no lleva datos
        return true;
    spi_write_blocking(SD_SPI, buf, 512);
    xfer(0xFF);     // CRC
    xfer(0xFF);
    return (xfer(0xFF) & 0x1F) == 0x05;     // datos aceptados
}

int sd_error(void)
{
    return ultimo_error;
}

void sd_diag(uint8_t *cmd0, bool *miso)
{
    *cmd0 = diag_cmd0;
    *miso = diag_miso;
}

// El modulo se alimenta del MSX. Con el MSX apagado y la Pico encendida por
// USB, las lineas del SPI en alto alimentan la tarjeta a medias por sus
// entradas, y cuando vuelve la alimentacion no arranca limpia: queda colgada
// hasta que se la saca. Con el bus muerto los pines quedan en alta impedancia.
void sd_apagar(void)
{
    lista = false;
    pines = false;
    ultimo_error = SD_SIN_PROBAR;
    spi_deinit(SD_SPI);
    const uint p[] = {PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO};
    for (int i = 0; i < 4; i++) {
        gpio_init(p[i]);        // entrada
        gpio_disable_pulls(p[i]);
    }
}

// Una tarjeta que quedo a mitad de una lectura de varios bloques no atiende
// comandos: sigue mandando datos. Se le dan pulsos para vaciar el bloque en
// curso y un CMD12 para cortarla.
static void destrabar(void)
{
    cs(true);
    for (int i = 0; i < 600; i++)
        xfer(0xFF);
    comando(CMD12, 0);
    soltar();
}

bool sd_iniciar(void)
{
    if (!pines) {
        pines = true;
        gpio_init(PIN_SD_CS);
        gpio_put(PIN_SD_CS, 1);
        gpio_set_dir(PIN_SD_CS, GPIO_OUT);
        gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
        gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
        gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
        gpio_pull_up(PIN_SD_MISO);
    }

    lista = false;
    spi_init(SD_SPI, HZ_INICIO);
    cs(false);
    for (int i = 0; i < 10; i++)    // 80 pulsos con la tarjeta sin elegir
        xfer(0xFF);
    diag_miso = gpio_get(PIN_SD_MISO);

    uint8_t r = 0xFF;
    for (int i = 0; i < 5 && r != 1; i++)
        r = comando(CMD0, 0);
    if (r != 1) {
        destrabar();
        for (int i = 0; i < 5 && r != 1; i++)
            r = comando(CMD0, 0);
    }
    diag_cmd0 = r;
    if (r != 1) {
        soltar();
        ultimo_error = SD_NO_CONTESTA;  // sin tarjeta, modulo o cableado
        return false;
    }

    bool v2 = false;
    if (comando(CMD8, 0x1AA) == 1) {
        uint8_t ocr[4];
        spi_read_blocking(SD_SPI, 0xFF, ocr, 4);
        v2 = ocr[2] == 0x01 && ocr[3] == 0xAA;
        if (!v2) {
            soltar();
            ultimo_error = SD_ERROR_INICIO;
            return false;
        }
    }

    absolute_time_t fin = make_timeout_time_ms(1500);
    do {
        r = acomando(ACMD41, v2 ? 1u << 30 : 0);
    } while (r != 0 && !time_reached(fin));
    if (r != 0) {
        soltar();
        ultimo_error = SD_ERROR_INICIO;
        return false;
    }

    por_bloque = false;
    if (v2 && comando(CMD58, 0) == 0) {
        uint8_t ocr[4];
        spi_read_blocking(SD_SPI, 0xFF, ocr, 4);
        por_bloque = ocr[0] & 0x40;     // CCS
    }
    if (!por_bloque)
        comando(CMD16, 512);
    soltar();

    spi_set_baudrate(SD_SPI, HZ_TRABAJO);
    lista = true;
    ultimo_error = SD_OK;
    return true;
}

static bool leer(uint32_t lba, uint8_t *buf, uint32_t n)
{
    if (!por_bloque)
        lba *= 512;
    bool ok = false;
    if (n == 1) {
        ok = comando(CMD17, lba) == 0 && recibir(buf, 512);
    } else if (comando(CMD18, lba) == 0) {
        do {
            if (!recibir(buf, 512))
                break;
            buf += 512;
        } while (--n);
        comando(CMD12, 0);
        ok = n == 0;
    }
    soltar();
    return ok;
}

static bool escribir(uint32_t lba, const uint8_t *buf, uint32_t n)
{
    if (!por_bloque)
        lba *= 512;
    bool ok = false;
    if (n == 1) {
        ok = comando(CMD24, lba) == 0 && enviar(buf, 0xFE);
    } else if (comando(CMD25, lba) == 0) {
        do {
            if (!enviar(buf, 0xFC))
                break;
            buf += 512;
        } while (--n);
        if (!enviar(NULL, 0xFD))
            n = 1;
        ok = n == 0;
    }
    if (ok)
        ok = esperar_lista(600);
    soltar();
    return ok;
}

// ---- Capa de disco de FatFs: un solo volumen, la SD ----

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0 && lista) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NOINIT;
    return sd_iniciar() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !lista)
        return RES_NOTRDY;
    if (leer(sector, buff, count))
        return RES_OK;
    lista = false;      // se saco la tarjeta, o fallo: reiniciar antes de seguir
    ultimo_error = SD_ERROR_ES;
    return RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !lista)
        return RES_NOTRDY;
    if (escribir(sector, buff, count))
        return RES_OK;
    lista = false;
    ultimo_error = SD_ERROR_ES;
    return RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)buff;
    if (pdrv != 0 || !lista)
        return RES_NOTRDY;
    if (cmd == CTRL_SYNC) {
        bool ok = elegir();
        soltar();
        return ok ? RES_OK : RES_ERROR;
    }
    return RES_PARERR;
}

// Fecha de los archivos que crea o cambia FatFs: la hora local del reloj
// (reloj.c). Si todavia no se puso, sale 1980-01-01.
DWORD get_fattime(void)
{
    fecha_t f;
    reloj_leer(&f);
    return (DWORD)(f.anio - 1980) << 25 | (DWORD)f.mes << 21 | (DWORD)f.dia << 16
         | (DWORD)f.hora << 11 | (DWORD)f.min << 5 | f.seg / 2;
}
