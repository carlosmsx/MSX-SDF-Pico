#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ff.h"
#include "archivos.h"
#include "imagenes.h"
#include "protocolo.h"

// El directorio actual en la forma de FatFs, con nombres cortos: "/JUEGOS/MSX2",
// y "" en la raiz. Vale mientras la FAT no se vuelva a montar.
static uint32_t generacion;
static char actual[FS_RUTA_MAX];

// El ultimo listado de FSDIR, ya ordenado
static uint8_t tabla[FS_ENTRADAS][FS_ENTRADA];
static int n_tabla;

// El archivo abierto: uno solo a la vez
static FIL arch;
static bool abierto, escribiendo;
static char ruta_abierta[FS_RUTA_MAX];

// Si la SD se cayo o se monto de nuevo (otra tarjeta, quiza), lo que habia
// no sirve: el directorio vuelve a la raiz y el archivo se da por cerrado.
static bool vigente(void)
{
    if (!img_sd_ok())
        return false;
    if (generacion != img_generacion()) {
        generacion = img_generacion();
        actual[0] = 0;
        n_tabla = 0;
        abierto = false;
    }
    return true;
}

static uint8_t error_de(FRESULT r)
{
    switch (r) {
    case FR_OK:             return 0;
    case FR_NO_FILE:        return ERR_FILE_NOT_FOUND;
    case FR_NO_PATH:        return ERR_PATH_NOT_FOUND;
    case FR_INVALID_NAME:   return ERR_BAD_FILE_NAME;
    case FR_EXIST:          return ERR_FILE_ALREADY_EXISTS;
    case FR_DENIED:         return ERR_DISK_FULL;   // directorio o SD llenos
    case FR_WRITE_PROTECTED: return ERR_WRITE_PROTECTED;
    case FR_NOT_ENABLED:
    case FR_NO_FILESYSTEM:  return ERR_DISK_OFFLINE;
    case FR_LOCKED:         return ERR_FILE_ALREADY_OPEN;
    default:
        img_fallo(r);
        return ERR_DISK_IO;
    }
}

static const char *para_fatfs(const char *abs)
{
    return abs[0] ? abs : "/";
}

static bool separador(char c)
{
    return c == '/' || c == '\\';
}

// Junta la ruta pedida con el directorio actual y resuelve . y ..: deja en
// abs la ruta absoluta, todavia sin preguntarle nada a la SD.
static uint8_t juntar(const char *ruta, uint8_t largo, char *abs)
{
    int i = 0;
    if (largo > 0 && separador(ruta[0]))
        abs[0] = 0;
    else
        strcpy(abs, actual);
    while (i < largo) {
        while (i < largo && separador(ruta[i]))
            i++;
        int ini = i;
        while (i < largo && !separador(ruta[i])) {
            // Sin unidades de FatFs ("0:") ni caracteres de control
            if (ruta[i] == ':' || (uint8_t)ruta[i] < ' ')
                return ERR_BAD_FILE_NAME;
            i++;
        }
        int l = i - ini;
        if (l == 0 || (l == 1 && ruta[ini] == '.'))
            continue;
        if (l == 2 && ruta[ini] == '.' && ruta[ini + 1] == '.') {
            char *u = strrchr(abs, '/');
            if (u)
                *u = 0;     // en la raiz, .. sigue en la raiz
            continue;
        }
        size_t la = strlen(abs);
        if (la + 1 + l >= FS_RUTA_MAX)
            return ERR_BAD_FILE_NAME;
        abs[la] = '/';
        memcpy(abs + la + 1, ruta + ini, l);
        abs[la + 1 + l] = 0;
    }
    return 0;
}

// Cambia cada tramo de abs por su nombre corto, preguntando a la SD. Con
// ultimo en false el ultimo tramo queda como vino (un archivo por crear).
// En fi deja lo que dijo f_stat del ultimo (si se pidio y no es la raiz).
static uint8_t canonica(char *abs, bool ultimo, FILINFO *fi)
{
    char res[FS_RUTA_MAX];
    FILINFO f;
    const char *p = abs;

    res[0] = 0;
    while (*p == '/') {
        const char *ini = p + 1, *fin = strchr(ini, '/');
        size_t l = fin ? (size_t)(fin - ini) : strlen(ini);
        size_t lr = strlen(res);
        res[lr] = '/';
        memcpy(res + lr + 1, ini, l);
        res[lr + 1 + l] = 0;
        p = ini + l;
        if (!fin && !ultimo)
            break;
        FRESULT r = f_stat(res, &f);
        if (r == FR_NO_FILE)
            return fin ? ERR_PATH_NOT_FOUND : ERR_FILE_NOT_FOUND;
        if (r != FR_OK)
            return error_de(r);
        if (fin && !(f.fattrib & AM_DIR))
            return ERR_PATH_NOT_FOUND;
        const char *corto = img_sfn(&f);
        if (lr + 1 + strlen(corto) >= FS_RUTA_MAX)
            return ERR_BAD_FILE_NAME;
        strcpy(res + lr + 1, corto);
        if (!fin && fi)
            *fi = f;
    }
    strcpy(abs, res);
    return 0;
}

// Resuelve una ruta que tiene que existir. raiz queda en true si es la raiz
// (no hay FILINFO de la raiz).
static uint8_t existente(const char *ruta, uint8_t largo, char *abs,
                         FILINFO *fi, bool *raiz)
{
    uint8_t err = juntar(ruta, largo, abs);
    if (!err)
        err = canonica(abs, true, fi);
    *raiz = abs[0] == 0;
    return err;
}

// Una imagen montada, el archivo abierto, o el directorio actual (o uno que
// lo contiene): no se borran ni se renombran.
static bool en_uso(const char *abs, const FILINFO *fi)
{
    if (strrchr(abs, '/') == abs && img_en_uso(img_sfn(fi)))
        return true;
    if (abierto && strcasecmp(abs, ruta_abierta) == 0)
        return true;
    size_t l = strlen(abs);
    return strncasecmp(actual, abs, l) == 0 && (actual[l] == 0 || actual[l] == '/');
}

// Copia hasta max - 1 letras y el 0
static void copiar(uint8_t *dst, const char *src, size_t max)
{
    size_t l = strnlen(src, max - 1);
    memcpy(dst, src, l);
    dst[l] = 0;
}

static void llenar(uint8_t *e, const FILINFO *fi)
{
    memset(e, 0, FS_ENTRADA);
    e[0] = fi->fattrib;
    e[1] = fi->fsize;
    e[2] = fi->fsize >> 8;
    e[3] = fi->fsize >> 16;
    e[4] = fi->fsize >> 24;
    e[5] = fi->fdate;
    e[6] = fi->fdate >> 8;
    e[7] = fi->ftime;
    e[8] = fi->ftime >> 8;
    copiar(e + 9, img_sfn(fi), 13);
    copiar(e + 22, fi->fname, FS_ENTRADA - 22);
}

static void llenar_dir(uint8_t *e, const char *nombre)
{
    memset(e, 0, FS_ENTRADA);
    e[0] = AM_DIR;
    strcpy((char *)e + 9, nombre);
    strcpy((char *)e + 22, nombre);
}

void fs_reiniciar(void)
{
    fs_cerrar();
    actual[0] = 0;
    n_tabla = 0;
}

// ---- Directorio actual ----

uint8_t fs_cd(const char *ruta, uint8_t largo)
{
    char abs[FS_RUTA_MAX];
    FILINFO fi;
    bool raiz;
    if (!vigente())
        return ERR_DISK_OFFLINE;
    uint8_t err = existente(ruta, largo, abs, &fi, &raiz);
    if (err == ERR_FILE_NOT_FOUND)
        return ERR_PATH_NOT_FOUND;
    if (err)
        return err;
    if (!raiz && !(fi.fattrib & AM_DIR))
        return ERR_PATH_NOT_FOUND;
    strcpy(actual, abs);
    return 0;
}

int fs_pwd(char *txt)
{
    vigente();
    if (actual[0] == 0) {
        strcpy(txt, "\\");
        return 2;
    }
    int n = 0;
    for (const char *p = actual; *p; p++)
        txt[n++] = *p == '/' ? '\\' : *p;
    txt[n++] = 0;
    return n;
}

// ---- Listado ----

// * y ? sin distinguir mayusculas. "*.*" es todo, con o sin extension.
static bool coincide(const char *m, const char *s)
{
    for (; *m; m++, s++) {
        if (*m == '*') {
            while (*m == '*')
                m++;
            if (*m == 0)
                return true;
            for (; *s; s++)
                if (coincide(m, s))
                    return true;
            return false;
        }
        if (*s == 0)
            return false;
        if (*m != '?' && toupper((uint8_t)*m) != toupper((uint8_t)*s))
            return false;
    }
    return *s == 0;
}

static bool tiene_comodin(const char *s, size_t l)
{
    return memchr(s, '*', l) || memchr(s, '?', l);
}

// Primero los directorios, despues por el nombre para mostrar
static int comparar(const void *a, const void *b)
{
    const uint8_t *x = a, *y = b;
    int dx = x[0] & AM_DIR, dy = y[0] & AM_DIR;
    if (dx != dy)
        return dx ? -1 : 1;
    return strcasecmp((const char *)x + 22, (const char *)y + 22);
}

uint8_t fs_dir(uint8_t opciones, const char *ruta, uint8_t largo,
               uint16_t *cantidad, bool *cortado)
{
    char abs[FS_RUTA_MAX], mascara[FS_RUTA_MAX] = "*";
    FILINFO fi;
    DIR dir;
    bool raiz;

    *cantidad = 0;
    *cortado = false;
    n_tabla = 0;
    if (!vigente())
        return ERR_DISK_OFFLINE;

    // Si el ultimo tramo tiene comodines, es el filtro
    int u = largo;
    while (u > 0 && !separador(ruta[u - 1]))
        u--;
    if (tiene_comodin(ruta + u, largo - u)) {
        memcpy(mascara, ruta + u, largo - u);
        mascara[largo - u] = 0;
        if (strcmp(mascara, "*.*") == 0)
            strcpy(mascara, "*");
        largo = u;
    }
    uint8_t err = existente(ruta, largo, abs, &fi, &raiz);
    if (err == ERR_FILE_NOT_FOUND)
        return ERR_PATH_NOT_FOUND;
    if (err)
        return err;
    if (!raiz && !(fi.fattrib & AM_DIR))
        return ERR_PATH_NOT_FOUND;

    int primero = 0;
    if ((opciones & FS_PADRE) && !raiz) {
        llenar_dir(tabla[n_tabla++], "..");
        primero = 1;
    }
    FRESULT r = f_opendir(&dir, para_fatfs(abs));
    while (r == FR_OK) {
        r = f_readdir(&dir, &fi);
        if (r != FR_OK || fi.fname[0] == 0)
            break;
        if ((fi.fattrib & (AM_HID | AM_SYS)) && !(opciones & FS_OCULTOS))
            continue;
        if (!coincide(mascara, fi.fname) && !coincide(mascara, img_sfn(&fi)))
            continue;
        if (n_tabla == FS_ENTRADAS) {
            *cortado = true;
            break;
        }
        llenar(tabla[n_tabla++], &fi);
    }
    if (r == FR_OK)
        r = f_closedir(&dir);
    if (r != FR_OK) {
        n_tabla = 0;
        return error_de(r);
    }
    qsort(tabla[primero], n_tabla - primero, FS_ENTRADA, comparar);
    *cantidad = n_tabla;
    return 0;
}

int fs_listar(uint16_t desde, int max, uint8_t *dst)
{
    if (!vigente() || desde >= n_tabla)
        return 0;
    if (max > n_tabla - desde)
        max = n_tabla - desde;
    memcpy(dst, tabla[desde], max * FS_ENTRADA);
    return max;
}

// ---- Un archivo ----

uint8_t fs_abrir(uint8_t modo, const char *ruta, uint8_t largo, uint32_t *tam)
{
    char abs[FS_RUTA_MAX];
    FILINFO fi;
    bool raiz;
    FRESULT r;

    *tam = 0;
    if (!vigente())
        return ERR_DISK_OFFLINE;
    fs_cerrar();

    if (modo == FS_LEER) {
        uint8_t err = existente(ruta, largo, abs, &fi, &raiz);
        if (err)
            return err;
        if (raiz || (fi.fattrib & AM_DIR))
            return ERR_IS_DIR;
        r = f_open(&arch, abs, FA_READ);
    } else if (modo == FS_CREAR || modo == FS_REEMPLAZAR) {
        uint8_t err = juntar(ruta, largo, abs);
        if (!err)
            err = canonica(abs, false, NULL);
        if (err)
            return err;
        if (abs[0] == 0)
            return ERR_IS_DIR;
        r = f_stat(abs, &fi);
        if (r == FR_OK) {
            if (fi.fattrib & AM_DIR)
                return ERR_IS_DIR;
            if (modo == FS_CREAR)
                return ERR_FILE_ALREADY_EXISTS;
            if (fi.fattrib & AM_RDO)
                return ERR_WRITE_PROTECTED;
            if (strrchr(abs, '/') == abs && img_en_uso(img_sfn(&fi)))
                return ERR_FILE_ALREADY_OPEN;
        } else if (r != FR_NO_FILE) {
            return error_de(r);
        }
        r = f_open(&arch, abs, FA_WRITE | FA_CREATE_ALWAYS);
        if (r == FR_OK)
            canonica(abs, true, NULL);  // ya existe: con su nombre corto
    } else {
        return ERR_ILLEGAL_FUNCTION;
    }
    if (r != FR_OK)
        return error_de(r);

    abierto = true;
    escribiendo = modo != FS_LEER;
    strcpy(ruta_abierta, abs);
    *tam = f_size(&arch);
    return 0;
}

uint8_t fs_leer(uint8_t *dst, uint32_t max, uint32_t *n)
{
    UINT leidos = 0;
    *n = 0;
    if (!vigente())
        return ERR_DISK_OFFLINE;
    if (!abierto || escribiendo)
        return ERR_FILE_NOT_OPEN;
    FRESULT r = f_read(&arch, dst, max, &leidos);
    *n = leidos;
    return error_de(r);
}

uint8_t fs_escribir(const uint8_t *src, uint32_t n)
{
    UINT escritos = 0;
    if (!vigente())
        return ERR_DISK_OFFLINE;
    if (!abierto || !escribiendo)
        return ERR_FILE_NOT_OPEN;
    FRESULT r = f_write(&arch, src, n, &escritos);
    if (r == FR_OK && escritos != n)
        return ERR_DISK_FULL;
    return error_de(r);
}

uint8_t fs_cerrar(void)
{
    if (!vigente() || !abierto)
        return 0;
    abierto = false;
    return error_de(f_close(&arch));
}

// ---- Borrar, crear, renombrar ----

uint8_t fs_borrar(const char *ruta, uint8_t largo)
{
    char abs[FS_RUTA_MAX];
    FILINFO fi;
    bool raiz;
    if (!vigente())
        return ERR_DISK_OFFLINE;
    uint8_t err = existente(ruta, largo, abs, &fi, &raiz);
    if (err)
        return err;
    if (raiz)
        return ERR_ILLEGAL_FUNCTION;
    if (en_uso(abs, &fi))
        return ERR_FILE_ALREADY_OPEN;
    if (fi.fattrib & AM_RDO)
        return ERR_WRITE_PROTECTED;
    if (fi.fattrib & AM_DIR) {
        // FatFs contestaria FR_DENIED, igual que con la SD llena
        DIR dir;
        FRESULT r = f_opendir(&dir, abs);
        if (r == FR_OK)
            r = f_readdir(&dir, &fi);
        if (r == FR_OK)
            f_closedir(&dir);
        if (r != FR_OK)
            return error_de(r);
        if (fi.fname[0])
            return ERR_DIR_NOT_EMPTY;
    }
    return error_de(f_unlink(abs));
}

uint8_t fs_mkdir(const char *ruta, uint8_t largo)
{
    char abs[FS_RUTA_MAX];
    if (!vigente())
        return ERR_DISK_OFFLINE;
    uint8_t err = juntar(ruta, largo, abs);
    if (!err)
        err = canonica(abs, false, NULL);
    if (err)
        return err;
    if (abs[0] == 0)
        return ERR_FILE_ALREADY_EXISTS;
    return error_de(f_mkdir(abs));
}

// Si el nombre nuevo es un nombre solo, queda en el mismo directorio; si
// trae directorios, se mueve ahi (relativo al directorio actual).
uint8_t fs_renombrar(const char *ruta, uint8_t largo, const char *nuevo, uint8_t largo_nuevo)
{
    char abs[FS_RUTA_MAX], destino[FS_RUTA_MAX];
    FILINFO fi;
    bool raiz;
    if (!vigente())
        return ERR_DISK_OFFLINE;
    uint8_t err = existente(ruta, largo, abs, &fi, &raiz);
    if (err)
        return err;
    if (raiz)
        return ERR_ILLEGAL_FUNCTION;
    if (en_uso(abs, &fi))
        return ERR_FILE_ALREADY_OPEN;

    bool con_ruta = false;
    for (int i = 0; i < largo_nuevo; i++)
        con_ruta |= separador(nuevo[i]);
    if (con_ruta) {
        err = juntar(nuevo, largo_nuevo, destino);
    } else {
        // el directorio del original, y el nombre nuevo
        size_t d = strrchr(abs, '/') - abs;
        if (largo_nuevo == 0 || d + 1 + largo_nuevo >= FS_RUTA_MAX)
            return ERR_BAD_FILE_NAME;
        memcpy(destino, abs, d + 1);
        memcpy(destino + d + 1, nuevo, largo_nuevo);
        destino[d + 1 + largo_nuevo] = 0;
        if (strchr(destino + d + 1, ':') || strcmp(destino + d + 1, ".") == 0
            || strcmp(destino + d + 1, "..") == 0)
            return ERR_BAD_FILE_NAME;
    }
    if (!err)
        err = canonica(destino, false, NULL);
    if (err)
        return err;
    if (destino[0] == 0)
        return ERR_FILE_ALREADY_EXISTS;
    // Un directorio dentro de si mismo: FatFs no lo controla
    size_t l = strlen(abs);
    if (strncasecmp(destino, abs, l) == 0 && destino[l] == '/')
        return ERR_ILLEGAL_FUNCTION;
    return error_de(f_rename(abs, destino));
}

uint8_t fs_stat(const char *ruta, uint8_t largo, uint8_t *entrada)
{
    char abs[FS_RUTA_MAX];
    FILINFO fi;
    bool raiz;
    memset(entrada, 0, FS_ENTRADA);
    if (!vigente())
        return ERR_DISK_OFFLINE;
    uint8_t err = existente(ruta, largo, abs, &fi, &raiz);
    if (err)
        return err;
    if (raiz)
        llenar_dir(entrada, "\\");
    else
        llenar(entrada, &fi);
    return 0;
}

// En FAT32 la cuenta de libres viene del sector FSINFO; si no lo hay, FatFs
// recorre la FAT entera y en una tarjeta grande puede tardar.
uint8_t fs_libre(uint32_t *libres_kb, uint32_t *total_kb)
{
    FATFS *fs;
    DWORD libres;
    *libres_kb = *total_kb = 0;
    if (!vigente())
        return ERR_DISK_OFFLINE;
    FRESULT r = f_getfree("", &libres, &fs);
    if (r != FR_OK)
        return error_de(r);
    *libres_kb = (uint64_t)libres * fs->csize / 2;
    *total_kb = (uint64_t)(fs->n_fatent - 2) * fs->csize / 2;
    return 0;
}
