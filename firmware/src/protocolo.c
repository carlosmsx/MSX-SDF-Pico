// Protocolo con la DiskROM, en el nucleo 1.
//
// Cada escritura del MSX llega como una palabra del anillo de bus.c. Un byte
// en 01h es un comando: pone OCUPADO en el estado, descarta lo que hubiera
// para leer y junta los parametros que siguen en 00h. Con los parametros
// completos se ejecuta y se encola la respuesta por DMA. La ROM espera
// OCUPADO en 0 antes de leer (docs/PROTOCOLO.md).
//
// OCUPADO solo baja con la respuesta encolada Y el anillo vacio: si mientras
// se ejecutaba un comando largo (la SD) llego otro, el estado sigue en
// OCUPADO hasta atenderlo, y la ROM nunca lee la respuesta del anterior.
//
// La excepcion es CMD_DEBUG: la ROM lo manda entre sector y sector de un
// CMD_READ, con el checksum del sector. No corta la lectura en curso.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "bus.h"
#include "consola.h"
#include "imagenes.h"
#include "protocolo.h"
#include "sd_spi.h"

// ---- Parametros de cada comando ----

enum { P_FIJO, P_NOMBRE, P_ASCIIZ };

typedef struct {
    uint8_t tipo;
    uint8_t n;      // P_FIJO: cuantos; P_NOMBRE: fijos antes del largo
} params_t;

static params_t params_de(uint8_t cmd)
{
    switch (cmd) {
    case CMD_WRITE: case CMD_READ:      return (params_t){P_FIJO, 7};
    case CMD_DSKCHG: case CMD_DSKFMT:   return (params_t){P_FIJO, 1};
    case CMD_DEBUG:                     return (params_t){P_FIJO, 1};
    case CMD_RTCSETDA: case CMD_RTCSETTI: return (params_t){P_FIJO, 3};
    case CMD_SDFUMOUNT: case CMD_SDFDRIVES: return (params_t){P_FIJO, 1};
    case CMD_SDFMOUNT: case CMD_SDFNEW: return (params_t){P_NOMBRE, 1};
    case CMD_DEVNAME:                   return (params_t){P_ASCIIZ, 0};
    case CMD_DEVOPEN:                   return (params_t){P_NOMBRE, 2};
    case CMD_DEVCLOSE:                  return (params_t){P_FIJO, 2};
    case CMD_DEVOUT:                    return (params_t){P_FIJO, 1};
    default:                            return (params_t){P_FIJO, 0};
    }
}

// ---- Estado de la transaccion ----

enum { E_LIBRE, E_PARAMS, E_DATOS, E_ECO };

#define PAR_MAX     64
#define IO_SECTORES 255     // lo maximo que pide un DSKIO

static int etapa = E_LIBRE;
static uint8_t cmd;
static uint8_t par[PAR_MAX];
static int npar;
static uint8_t resp[128];

// Los sectores de un DSKIO entero: se leen de la SD de una vez y salen por
// DMA; en escritura se juntan aca y se graban al final.
static uint8_t io_buf[IO_SECTORES * SECTOR];

// CMD_READ en curso: para comparar el checksum que manda CMD_DEBUG
static const uint8_t *lec_ptr;
static uint32_t lec_quedan;

// CMD_WRITE en curso
static uint8_t esc_drive;
static uint32_t esc_sector, esc_total, esc_n;

static uint8_t contador[256];

static struct {
    uint32_t lecturas, escrituras, sectores_leidos, sectores_escritos;
    uint32_t chk_ok, chk_mal, errores;
    absolute_time_t desde;
} st;

// ---- Registro de estado ----

static bool libre;          // la respuesta esta encolada: el Z80 puede leer
static bool error_st;       // el ultimo comando termino con error
static uint8_t publicado;

static void publicar(uint8_t v)
{
    if (v != publicado) {
        bus_estado(v);
        publicado = v;
    }
}

static void ocupado(void)
{
    libre = false;
    publicar(EST_FIJO | EST_OCUPADO | (error_st ? EST_ERROR : 0));
}

static void soltar(void)
{
    libre = true;   // el bucle publica "listo" cuando el anillo este vacio
}

static bool params_completos(void)
{
    params_t p = params_de(cmd);
    if (npar >= PAR_MAX)
        return true;
    switch (p.tipo) {
    case P_FIJO:   return npar >= p.n;
    case P_NOMBRE: return npar > p.n && npar >= p.n + 1 + par[p.n];
    case P_ASCIIZ: return npar > 0 && par[npar - 1] == 0;
    }
    return true;
}

// ---- Comandos del disco ----

static void cmd_read(void)
{
    // drive, sectores, media, direccion (H, L), sector (H, L)
    uint32_t n = par[1], sector = (par[5] << 8) | par[6];
    uint8_t err = img_leer(par[0], sector, n, io_buf);
    st.lecturas++;
    bus_tx_byte(err);
    if (err) {
        con_printf("READ %c: %lu+%lu: error %u\n", 'A' + par[0],
                   (unsigned long)sector, (unsigned long)n, err);
        return;
    }
    bus_tx_enviar(io_buf, n * SECTOR);
    lec_ptr = io_buf;
    lec_quedan = n;
    st.sectores_leidos += n;
}

static void cmd_debug(void)
{
    // Checksum XOR del sector que la ROM acaba de leer.
    if (lec_quedan == 0)
        return;
    uint8_t x = 0;
    for (int i = 0; i < SECTOR; i++)
        x ^= lec_ptr[i];
    if (x == par[0]) {
        st.chk_ok++;
    } else {
        st.chk_mal++;
        con_printf("checksum: MSX %02X, Pico %02X\n", par[0], x);
    }
    lec_ptr += SECTOR;
    lec_quedan--;
}

static void cmd_write(void)
{
    uint32_t n = par[1], sector = (par[5] << 8) | par[6];
    uint8_t err = img_validar_escritura(par[0], sector, n, par[2]);
    st.escrituras++;
    bus_tx_byte(err);
    if (err) {
        con_printf("WRITE %c: %lu+%lu: error %u\n", 'A' + par[0],
                   (unsigned long)sector, (unsigned long)n, err);
        return;
    }
    esc_drive = par[0];
    esc_sector = sector;
    esc_total = n * SECTOR;
    esc_n = 0;
    etapa = E_DATOS;
}

static void dato_de_escritura(uint8_t b)
{
    // OCUPADO desde el primer byte de cada sector hasta tenerlo: la ROM
    // espera eso despues de cada sector. Los sectores se juntan en memoria y
    // van a la SD todos juntos con el ultimo, que es el que tarda.
    if (esc_n % SECTOR == 0)
        ocupado();
    io_buf[esc_n++] = b;
    if (esc_n % SECTOR)
        return;

    if (esc_n == esc_total) {
        uint8_t err = img_escribir(esc_drive, esc_sector, esc_total / SECTOR, io_buf);
        if (err) {
            // La ROM lo ve en el bit ERROR del estado, despues del ultimo sector.
            st.errores++;
            error_st = true;
            con_printf("WRITE %c: fallo la SD\n", 'A' + esc_drive);
        }
        st.sectores_escritos += esc_total / SECTOR;
        etapa = E_LIBRE;
    }
    soltar();
}

// ---- CALL SDFNEW ----

static absolute_time_t nueva_aviso;

// Llenar la imagen de ceros puede pasar el limite de espera de la ROM. Cada
// segundo se le da un SDFNEW_BUSY: lo lee, espera y vuelve a consultar.
static void nueva_avance(void)
{
    if (!time_reached(nueva_aviso))
        return;
    bus_tx_byte(SDFNEW_BUSY);
    publicar(EST_FIJO);
    absolute_time_t fin = make_timeout_time_ms(3000);
    while (bus_tx_pendiente() && !time_reached(fin))
        ;
    publicar(EST_FIJO | EST_OCUPADO);
    nueva_aviso = make_timeout_time_ms(1000);
}

// ---- CALL SDFDEBUG ----

// Las cuentas desde el SDFDEBUG anterior, con el tiempo y la velocidad: sirve
// para medir una prueba sin reloj en el MSX. Despues las pone en cero.
static int informe(char *txt, int max)
{
    uint32_t ms = absolute_time_diff_us(st.desde, get_absolute_time()) / 1000;
    uint32_t kb = (st.sectores_leidos + st.sectores_escritos) / 2;
    uint32_t kbs10 = ms ? kb * 10000u / ms : 0;
    int n = snprintf(txt, max,
                     "R %lu/%lu W %lu/%lu CHK %lu/%lu\r\n"
                     "%lu.%lu s %lu KB %lu.%lu KB/s\r\n",
                     (unsigned long)st.lecturas, (unsigned long)st.sectores_leidos,
                     (unsigned long)st.escrituras, (unsigned long)st.sectores_escritos,
                     (unsigned long)st.chk_ok, (unsigned long)st.chk_mal,
                     (unsigned long)(ms / 1000), (unsigned long)(ms % 1000 / 100),
                     (unsigned long)kb,
                     (unsigned long)(kbs10 / 10), (unsigned long)(kbs10 % 10));
    memset(&st, 0, sizeof st);
    st.desde = get_absolute_time();
    return n + 1;   // con el 0 final
}

// ---- Ejecucion ----

static void ejecutar(void)
{
    int n = 0;
    etapa = E_LIBRE;

    switch (cmd) {
    case CMD_READ:      cmd_read();  break;
    case CMD_WRITE:     cmd_write(); break;

    case CMD_DRIVES:    resp[n++] = img_drives(); break;
    case CMD_DSKCHG:    resp[n++] = img_cambio(par[0]); break;
    case CMD_DSKFMT:    resp[n++] = img_media(par[0]); break;

    case CMD_RTCCHK:    resp[n++] = 0; break;           // sin reloj
    case CMD_RTCGET:    memset(resp, 0, 7); n = 7; break;

    case CMD_SDFTEST:
        resp[n++] = PROTOCOL_VERSION;
        n += snprintf((char *)resp + n, 33, "%s\r\nSD %s", FW_VERSION, img_sd_texto()) + 1;
        break;
    case CMD_SDFDEBUG:
        n = informe((char *)resp, sizeof resp);
        break;

    case CMD_SDFFILES:
        bus_tx_enviar(io_buf, img_listar(io_buf, sizeof io_buf));
        break;
    case CMD_SDFMOUNT:  resp[n++] = img_montar(par[0], (char *)par + 2, par[1]); break;
    case CMD_SDFUMOUNT: resp[n++] = img_desmontar(par[0]); break;
    case CMD_SDFDRIVES: resp[n++] = img_poner_drives(par[0]); break;
    case CMD_SDFNEW:
        nueva_aviso = make_timeout_time_ms(1000);
        resp[n++] = img_nueva(par[0], (char *)par + 2, par[1],
                              io_buf, 64 * SECTOR, nueva_avance);
        break;

    // Sin dispositivos todavia: DEVNAME no reconoce ningun nombre.
    case CMD_DEVNAME:   resp[n++] = DEV_NONE; break;
    case CMD_DEVOPEN:   resp[n++] = ERR_DEVICE_IO; break;
    case CMD_DEVIN:     resp[n++] = 0; break;
    case CMD_DEVEOF:    resp[n++] = 0xFF; break;
    case CMD_DEVOUT:    if (par[0] == 13) resp[n++] = ERR_DEVICE_IO; break;

    // Prueba del bus
    case CMD_ECO:       etapa = E_ECO; break;
    case CMD_CONTADOR:  bus_tx_enviar(contador, sizeof contador); break;

    default:            break;  // INIHRD, INIENV, MTOFF, GETDPB...: sin respuesta
    }

    if (n)
        bus_tx_enviar(resp, n);
    soltar();   // en CMD_WRITE, para que la ROM lea el estado antes de los datos
}

static void comando(uint8_t c)
{
    // CMD_DEBUG no corta la lectura en curso: solo junta su parametro.
    if (c != CMD_DEBUG) {
        error_st = false;
        ocupado();
        bus_tx_cortar();
        if (etapa == E_DATOS)
            con_printf("WRITE cortado: faltaban %lu bytes\n",
                       (unsigned long)(esc_total - esc_n));
        lec_quedan = 0;
    }
    cmd = c;
    npar = 0;
    etapa = E_PARAMS;
    if (params_completos())
        ejecutar();
}

static void dato(uint8_t b)
{
    switch (etapa) {
    case E_PARAMS:
        par[npar++] = b;
        if (params_completos()) {
            if (cmd == CMD_DEBUG) {
                cmd_debug();
                etapa = E_LIBRE;
            } else {
                ejecutar();
            }
        }
        break;
    case E_DATOS:
        dato_de_escritura(b);
        break;
    case E_ECO:
        bus_tx_byte(b);
        break;
    default:
        con_printf("dato suelto en 00h: %02X\n", b);
        break;
    }
}

// Sin SD (o si dejo de contestar) se reintenta cada medio segundo, con el
// estado en OCUPADO mientras dura el intento y solo si no hay nada a medias.
// Un intento de iniciar la SD. A la consola va el resultado, con lo que
// contesto la tarjeta y cuanto tardo: siempre al encender el MSX, y en los
// reintentos (sin tarjeta, cada medio segundo) solo cuando cambia.
static void iniciar_sd(bool siempre)
{
    static char anterior[64];
    char txt[64];
    uint8_t cmd0;
    bool miso;

    absolute_time_t t0 = get_absolute_time();
    bool ok = img_iniciar();
    uint32_t ms = absolute_time_diff_us(t0, get_absolute_time()) / 1000;
    sd_diag(&cmd0, &miso);
    if (ok)
        snprintf(txt, sizeof txt, "ok");
    else
        snprintf(txt, sizeof txt, "%s (CMD0=%02X MISO=%d)", img_sd_texto(), cmd0, miso);
    if (siempre || strcmp(txt, anterior) != 0) {
        con_printf("SD: %s, %lu ms\n", txt, (unsigned long)ms);
        strcpy(anterior, txt);
    }
}

static void tarea_sd(void)
{
    static absolute_time_t prox;
    if (img_sd_ok() || !libre || etapa != E_LIBRE || bus_tx_pendiente())
        return;
    if (!time_reached(prox))
        return;
    ocupado();
    iniciar_sd(false);
    prox = make_timeout_time_ms(500);
    soltar();
}

void protocolo_core1(void)
{
    for (int i = 0; i < 256; i++)
        contador[i] = (uint8_t)i;

    st.desde = get_absolute_time();

    for (;;) {
        // Con el MSX apagado el modulo SD no tiene alimentacion: los pines
        // del SPI quedan sueltos hasta que vuelva.
        img_apagar();
        publicado = 0;
        ocupado();
        bus_despertar();
        con_printf("bus vivo: atendiendo 00h/01h\n");
        etapa = E_LIBRE;
        error_st = false;

        // La SD, con el estado todavia en OCUPADO: la ROM espera, y asi su
        // primer DRIVES ya recibe lo que dice la configuracion. Antes, un
        // momento para que el regulador del modulo y la tarjeta arranquen.
        busy_wait_ms(250);
        iniciar_sd(true);
        soltar();

        while (!bus_vigilar()) {
            while (bus_rx_hay()) {
                uint32_t w = bus_rx_leer();
                uint8_t b = w & 0xFF;
                if (w & (1u << BUS_A0))
                    comando(b);
                else
                    dato(b);
            }
            if (libre && !bus_rx_hay())
                publicar(EST_FIJO | (error_st ? EST_ERROR : 0));
            tarea_sd();
        }

        con_printf("bus muerto (MSX apagado?): Pico en espera\n");
    }
}
