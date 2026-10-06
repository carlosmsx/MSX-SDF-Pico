#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "consola.h"
#include "sd_spi.h"
#include "imagenes.h"
#include "protocolo.h"

#define DSK_720K_SIZE   737280u
#define DSK_360K_SIZE   368640u
#define NOMBRE_LEN      13      // 8.3 mas el 0 final
#define CFG             "SDFPICO.CFG"

static FATFS fs;
static bool sd_ok;
static FRESULT res_montaje = FR_NOT_READY;

// A:, B: y NEW_DRIVE. Vacio = drive sin imagen. Las imagenes quedan abiertas
// de un DSKIO al siguiente.
static char nombre[3][NOMBRE_LEN];
static FIL arch[3];
static bool abierta[3], solo_lectura[3];
static uint8_t cambio[2];       // lo levanta SDFMOUNT, lo consume DSKCHG
static uint8_t drives = 2;

// La SD dejo de contestar (la sacaron, o fallo): todo lo abierto se da por
// perdido y hay que volver a iniciarla.
static void caida(void)
{
    sd_ok = false;
    res_montaje = FR_NOT_READY;
    memset(abierta, 0, sizeof abierta);
}

static bool fallo_de_disco(FRESULT r)
{
    if (r == FR_DISK_ERR || r == FR_NOT_READY || r == FR_INT_ERR) {
        con_printf("SD: fallo de disco (FatFs %d), se reinicia\n", (int)r);
        caida();
        return true;
    }
    return false;
}

static void cerrar(uint8_t d)
{
    if (abierta[d])
        f_close(&arch[d]);
    abierta[d] = false;
}

static bool abrir(uint8_t d)
{
    if (abierta[d])
        return true;
    if (nombre[d][0] == 0)
        return false;
    solo_lectura[d] = false;
    FRESULT r = f_open(&arch[d], nombre[d], FA_READ | FA_WRITE);
    if (r == FR_DENIED) {
        // De solo lectura en la SD: se lee igual.
        solo_lectura[d] = true;
        r = f_open(&arch[d], nombre[d], FA_READ);
    }
    if (r != FR_OK) {
        fallo_de_disco(r);
        return false;
    }
    abierta[d] = true;
    return true;
}

// Verifica que n sea una imagen que la ROM sabe leer. Devuelve 0 o el error
// de BASIC; si esta bien, deja en n su nombre corto. Mira solo el largo, no
// la FAT: una imagen de juego con cargador propio tambien tiene que montar.
static uint8_t verificar(char *n)
{
    FILINFO fi;
    if (n[0] == 0)
        return ERR_BAD_FILE_NAME;
    FRESULT r = f_stat(n, &fi);
    if (r != FR_OK) {
        fallo_de_disco(r);
        return ERR_FILE_NOT_FOUND;
    }
    if (fi.fattrib & AM_DIR)
        return ERR_FILE_NOT_FOUND;
    if (fi.fsize != DSK_720K_SIZE && fi.fsize != DSK_360K_SIZE)
        return ERR_BAD_FILE_MODE;
    strncpy(n, fi.fname, NOMBRE_LEN - 1);
    n[NOMBRE_LEN - 1] = 0;
    return 0;
}

// ---- Configuracion: lo montado y la cantidad de drives, en la SD ----

static void guardar_cfg(void)
{
    FIL f;
    char txt[64];
    UINT n;
    if (!sd_ok)
        return;
    if (f_open(&f, CFG, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return;
    int len = snprintf(txt, sizeof txt, "A=%s\r\nB=%s\r\nDRIVES=%u\r\n",
                       nombre[0], nombre[1], drives);
    f_write(&f, txt, len, &n);
    f_close(&f);
}

// Si el archivo no esta, o una imagen ya no esta en la SD, el drive queda sin
// imagen y DSKIO le contesta "not ready" al DOS.
static void cargar_cfg(void)
{
    FIL f;
    char txt[96];
    UINT n = 0;

    nombre[0][0] = nombre[1][0] = 0;
    drives = 2;
    if (f_open(&f, CFG, FA_READ) != FR_OK)
        return;
    f_read(&f, txt, sizeof txt - 1, &n);
    f_close(&f);
    txt[n] = 0;

    for (char *l = strtok(txt, "\r\n"); l; l = strtok(NULL, "\r\n")) {
        if ((l[0] == 'A' || l[0] == 'B') && l[1] == '=') {
            char *dst = nombre[l[0] - 'A'];
            strncpy(dst, l + 2, NOMBRE_LEN - 1);
            dst[NOMBRE_LEN - 1] = 0;
            if (verificar(dst) != 0)
                dst[0] = 0;
        } else if (strncmp(l, "DRIVES=", 7) == 0) {
            drives = atoi(l + 7) == 1 ? 1 : 2;
        }
    }
}

// ---- SD ----

bool img_iniciar(void)
{
    memset(abierta, 0, sizeof abierta);
    res_montaje = f_mount(&fs, "", 1);
    sd_ok = res_montaje == FR_OK;
    if (sd_ok) {
        cargar_cfg();
        cambio[0] = cambio[1] = 1;  // puede ser otra tarjeta: que el DOS relea
    }
    return sd_ok;
}

void img_apagar(void)
{
    caida();
    sd_apagar();
}

bool img_sd_ok(void)
{
    return sd_ok;
}

const char *img_sd_texto(void)
{
    if (sd_ok)
        return "ok";
    switch (sd_error()) {
    case SD_SIN_PROBAR:   return "iniciando";
    case SD_NO_CONTESTA:  return "no contesta";
    case SD_ERROR_INICIO: return "no inicia";
    case SD_ERROR_ES:     return "error de E/S";
    }
    return res_montaje == FR_NO_FILESYSTEM ? "no es FAT" : "error de FAT";
}

uint8_t img_drives(void)
{
    return drives;
}

uint8_t img_poner_drives(uint8_t n)
{
    if (n < 1 || n > 2)
        return ERR_ILLEGAL_FUNCTION;
    drives = n;
    guardar_cfg();
    return 0;
}

// ---- DSKIO ----

static uint8_t preparar(uint8_t d, uint32_t sector, uint32_t n)
{
    if (!sd_ok || d > NEW_DRIVE || !abrir(d))
        return DSKIO_ERR_NOT_READY;         // sin SD, sin imagen o ya no esta
    if (n == 0 || (sector + n) * SECTOR > f_size(&arch[d]))
        return DSKIO_ERR_RECORD_NOT_FOUND;  // la imagen no crece
    return 0;
}

uint8_t img_leer(uint8_t d, uint32_t sector, uint32_t n, uint8_t *buf)
{
    UINT leidos;
    uint8_t err = preparar(d, sector, n);
    if (err)
        return err;
    FRESULT r = f_lseek(&arch[d], sector * SECTOR);
    if (r == FR_OK)
        r = f_read(&arch[d], buf, n * SECTOR, &leidos);
    if (r != FR_OK || leidos != n * SECTOR) {
        fallo_de_disco(r);
        return DSKIO_ERR_NOT_READY;
    }
    return 0;
}

uint8_t img_validar_escritura(uint8_t d, uint32_t sector, uint32_t n, uint8_t media)
{
    uint8_t err = preparar(d, sector, n);
    if (err)
        return err;
    if (solo_lectura[d])
        return DSKIO_ERR_NOT_READY;
    // DPB de 720 KB sobre una imagen de 360: escribir la corromperia.
    if (media == MEDIA_720K && f_size(&arch[d]) != DSK_720K_SIZE)
        return DSKIO_ERR_WRITE_FAULT;
    return 0;
}

uint8_t img_escribir(uint8_t d, uint32_t sector, uint32_t n, const uint8_t *buf)
{
    UINT escritos;
    if (!sd_ok || !abierta[d])
        return DSKIO_ERR_NOT_READY;
    FRESULT r = f_lseek(&arch[d], sector * SECTOR);
    if (r == FR_OK)
        r = f_write(&arch[d], buf, n * SECTOR, &escritos);
    if (r == FR_OK)
        r = f_sync(&arch[d]);
    if (r != FR_OK || escritos != n * SECTOR) {
        fallo_de_disco(r);
        return DSKIO_ERR_WRITE_FAULT;
    }
    return 0;
}

uint8_t img_cambio(uint8_t d)
{
    if (d > 1 || !cambio[d])
        return 1;
    cambio[d] = 0;
    return 0xFF;
}

uint8_t img_media(uint8_t d)
{
    if (!sd_ok || d > NEW_DRIVE || !abrir(d))
        return 0;
    if (d < NEW_DRIVE)
        cambio[d] = 1;  // se va a formatear: que el DOS la relea
    FSIZE_t t = f_size(&arch[d]);
    return t == DSK_720K_SIZE ? MEDIA_720K : t == DSK_360K_SIZE ? MEDIA_360K : 0;
}

// ---- CALL SDF... ----

uint8_t img_montar(uint8_t d, const char *n, uint8_t largo)
{
    char nuevo[NOMBRE_LEN];
    if (d > 1)
        return ERR_BAD_DRIVE_NAME;
    if (!sd_ok)
        return ERR_DISK_OFFLINE;
    if (largo >= NOMBRE_LEN)
        return ERR_BAD_FILE_NAME;
    memcpy(nuevo, n, largo);
    nuevo[largo] = 0;
    // Sin extension se busca el .DSK, igual que CALL SDFNEW la agrega.
    if (!strchr(nuevo, '.')) {
        if (largo + 4 >= NOMBRE_LEN)
            return ERR_BAD_FILE_NAME;
        strcat(nuevo, ".DSK");
    }
    uint8_t err = verificar(nuevo);
    if (err)
        return err;
    cerrar(d);
    strcpy(nombre[d], nuevo);
    cambio[d] = 1;
    guardar_cfg();
    return 0;
}

const char *img_nombre(uint8_t d)
{
    return d < NEW_DRIVE ? nombre[d] : "";
}

// Desmontar un drive que ya estaba vacio no es error.
uint8_t img_desmontar(uint8_t d)
{
    if (d > 1)
        return ERR_BAD_DRIVE_NAME;
    if (!sd_ok)
        return ERR_DISK_OFFLINE;
    cerrar(d);
    nombre[d][0] = 0;
    cambio[d] = 1;
    guardar_cfg();
    return 0;
}

// Pasa el nombre a 8.3 en mayusculas, con .DSK si no trae extension. Devuelve
// false si no es un nombre corto valido.
static bool nombre_nuevo(const char *src, uint8_t largo, char *dst)
{
    uint8_t base = 0, ext = 0;
    bool punto = false;
    for (uint8_t i = 0; i < largo; i++) {
        char c = toupper((unsigned char)src[i]);
        if (c == '.') {
            if (punto || base == 0)
                return false;
            punto = true;
        } else if (c <= ' ' || c > '~' || strchr("\"*+,/:;<=>?[\\]|", c)) {
            return false;
        } else if (punto ? ++ext > 3 : ++base > 8) {
            return false;
        }
        *dst++ = c;
    }
    if (base == 0 || (punto && ext == 0))
        return false;
    strcpy(dst, punto ? "" : ".DSK");
    return true;
}

// La reserva contigua si se puede y la llena de ceros, para que no arrastre
// restos de archivos borrados de la SD. Si algo falla la borra. Queda como
// NEW_DRIVE: por ahi la ROM le escribe los sectores de sistema.
uint8_t img_nueva(uint8_t media, const char *n, uint8_t largo,
                  uint8_t *ceros, uint32_t n_ceros, void (*avance)(void))
{
    char nuevo[NOMBRE_LEN];
    FILINFO fi;

    if (!sd_ok)
        return ERR_DISK_OFFLINE;
    if (media != MEDIA_720K && media != MEDIA_360K)
        return ERR_BAD_FILE_MODE;
    if (largo >= NOMBRE_LEN || !nombre_nuevo(n, largo, nuevo))
        return ERR_BAD_FILE_NAME;
    if (f_stat(nuevo, &fi) == FR_OK)
        return ERR_FILE_ALREADY_EXISTS;

    cerrar(NEW_DRIVE);
    nombre[NEW_DRIVE][0] = 0;
    FIL *f = &arch[NEW_DRIVE];
    FRESULT r = f_open(f, nuevo, FA_READ | FA_WRITE | FA_CREATE_NEW);
    if (r != FR_OK) {
        fallo_de_disco(r);
        return ERR_DISK_IO;     // raiz llena, o la SD no escribe
    }

    uint32_t falta = media == MEDIA_360K ? DSK_360K_SIZE : DSK_720K_SIZE;
    f_expand(f, falta, 1);      // si no hay lugar contiguo, f_write la agranda igual
    memset(ceros, 0, n_ceros);
    while (falta > 0 && r == FR_OK) {
        UINT escritos, tramo = falta < n_ceros ? falta : n_ceros;
        r = f_write(f, ceros, tramo, &escritos);
        if (r == FR_OK && escritos != tramo)
            r = FR_DENIED;      // SD llena
        falta -= tramo;
        if (avance)
            avance();
    }
    if (r == FR_OK)
        r = f_sync(f);
    if (r != FR_OK) {
        f_close(f);
        if (!fallo_de_disco(r))
            f_unlink(nuevo);
        return r == FR_DENIED ? ERR_DISK_FULL : ERR_DISK_IO;
    }

    solo_lectura[NEW_DRIVE] = false;
    abierta[NEW_DRIVE] = true;
    strcpy(nombre[NEW_DRIVE], nuevo);
    return 0;
}

uint32_t img_listar(uint8_t *buf, uint32_t max)
{
    DIR dir;
    FILINFO fi;
    uint32_t n = 0;

    if (sd_ok) {
        FRESULT r = f_opendir(&dir, "/");
        while (r == FR_OK) {
            r = f_readdir(&dir, &fi);
            if (r != FR_OK || fi.fname[0] == 0)
                break;
            if (fi.fattrib & (AM_DIR | AM_HID | AM_SYS))
                continue;
            size_t l = strlen(fi.fname);
            if (l <= 4 || strcmp(fi.fname + l - 4, ".DSK") != 0)
                continue;
            if (n + l + 2 > max)
                break;
            memcpy(buf + n, fi.fname, l + 1);
            n += l + 1;
        }
        if (r == FR_OK)
            f_closedir(&dir);
        else
            fallo_de_disco(r);
    }
    buf[n++] = 0;   // nombre vacio: fin de la lista
    return n;
}
