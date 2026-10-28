#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "boot/picobin.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "mbedtls/sha256.h"
#include "actualizar.h"
#include "consola.h"
#include "https.h"
#include "protocolo.h"

#define URL_RELEASE "https://api.github.com/repos/" ACT_REPO "/releases/latest"
#define URL_ARCHIVO "https://github.com/" ACT_REPO "/releases/download/"
#define JSON_MAX    (256 * 1024)    // lo que se lee de la API buscando la version

static_assert(ACT_ZONA + ACT_MAX <= PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE,
              "la zona de la actualizacion pisa los ajustes");

// ---- Del nucleo 1 al 0 ----
//
// Como camara.c: estado lo escribe el nucleo 1 al pedir (de LIBRE o LISTA a
// EN_CURSO) y el 0 al terminar. Lo demas lo escribe el 0.

static volatile uint32_t pedido, atendido;
static volatile uint8_t estado, fase;
static uint8_t p_modo;

static uint8_t res;
static uint16_t detalle;
static volatile uint32_t bajados, total;
static char tag[ACT_VERSION_MAX + 1];       // el del release en GitHub: "v0.14"
static char version[ACT_VERSION_MAX + 1];   // sin la v
static bool nueva;

uint8_t act_pedir(uint8_t modo, uint8_t si)
{
    if (estado == ACT_EN_CURSO)
        return ACT_OCUPADA;
    if (modo > ACT_ACTUALIZAR || (modo == ACT_ACTUALIZAR && si != ACT_SI))
        return ACT_PEDIDO_MAL;
    p_modo = modo;
    fase = ACT_CONSULTANDO;
    estado = ACT_EN_CURSO;
    __mem_fence_release();
    pedido = pedido + 1;
    return ACT_PEDIDO_OK;
}

static const char *instalada(void)
{
    const char *v = strchr(FW_VERSION, ' ');    // "SDF-Pico 0.14"
    return v ? v + 1 : FW_VERSION;
}

static int poner_z(uint8_t *out, const char *s, int max)
{
    int n = 0;
    while (n < max && s[n])
        out[n] = s[n], n++;
    out[n++] = 0;
    return n;
}

int act_leer_estado(uint8_t *out)
{
    uint8_t e = estado;
    __mem_fence_acquire();
    uint32_t kb = (bajados + 1023) / 1024, kt = (total + 1023) / 1024;
    memset(out, 0, 9);
    out[0] = e;
    out[1] = e == ACT_LISTA ? res : e == ACT_EN_CURSO ? fase : 0;
    if (e == ACT_LISTA) {
        out[2] = detalle & 0xFF;
        out[3] = detalle >> 8;
        out[8] = nueva;
    }
    out[4] = kb & 0xFF;
    out[5] = kb >> 8;
    out[6] = kt & 0xFF;
    out[7] = kt >> 8;
    int n = 9;
    n += poner_z(out + n, instalada(), ACT_VERSION_MAX);
    n += poner_z(out + n, e == ACT_LISTA ? version : "", ACT_VERSION_MAX);
    return n;
}

// ---- Versiones ----

// "0.14" contra "0.9": por numeros, de izquierda a derecha
static int comparar(const char *a, const char *b)
{
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y)
            return x < y ? -1 : 1;
        if (*a == '.')
            a++;
        if (*b == '.')
            b++;
        if ((*a && (*a < '0' || *a > '9')) || (*b && (*b < '0' || *b > '9')))
            return strcmp(a, b);    // "0.14-prueba": lo que queda, como texto
    }
    return 0;
}

// ---- La consulta ----
//
// De la respuesta de la API (JSON) solo hace falta "tag_name": "v0.14".

static uint8_t consultar(void)
{
    static uint8_t buf[512];
    static const char clave[] = "\"tag_name\"";
    int32_t largo;
    int r = https_get(URL_RELEASE, &largo);
    detalle = https_detalle;
    if (r != HTTPS_OK)
        return r;

    int k = 0, etapa = 0, n_tag = 0;   // etapa: 0 busca la clave, 1 los :, 2 el texto
    for (uint32_t leidos = 0; etapa < 3 && leidos < JSON_MAX;) {
        int m = https_leer(buf, sizeof buf);
        if (m <= 0) {
            detalle = https_detalle;
            return m < 0 ? -m : ACT_RESPUESTA;
        }
        leidos += m;
        for (int i = 0; i < m && etapa < 3; i++) {
            char c = buf[i];
            if (etapa == 0) {
                k = c == clave[k] ? k + 1 : c == clave[0] ? 1 : 0;
                if (!clave[k])
                    etapa = 1;
            } else if (etapa == 1) {
                if (c == '"')
                    etapa = 2;
                else if (c != ':' && c != ' ')
                    return ACT_RESPUESTA;
            } else if (c == '"') {
                etapa = 3;
            } else if (n_tag < ACT_VERSION_MAX) {
                tag[n_tag++] = c;
            } else {
                return ACT_RESPUESTA;
            }
        }
    }
    if (etapa < 3 || n_tag == 0)
        return ACT_RESPUESTA;
    tag[n_tag] = 0;
    strcpy(version, tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
    nueva = comparar(version, instalada()) > 0;
    return ACT_OK;
}

// ---- La bajada ----

static uint8_t sector[FLASH_SECTOR_SIZE];

static void grabar_sector(uint32_t off, const uint8_t *datos)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, FLASH_SECTOR_SIZE);
    flash_range_program(off, datos, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// La suma de sdf_pico.bin.sha256: 64 cifras hexadecimales (como las deja
// sha256sum, con el nombre despues)
static uint8_t bajar_suma(uint8_t *suma)
{
    static char url[160];
    char txt[64];
    int32_t largo;
    snprintf(url, sizeof url, URL_ARCHIVO "%s/" ACT_ARCHIVO ".sha256", tag);
    int r = https_get(url, &largo);
    int n = 0;
    while (r == HTTPS_OK && n < (int)sizeof txt) {
        int m = https_leer((uint8_t *)txt + n, sizeof txt - n);
        if (m < 0)
            r = -m;
        if (m <= 0)
            break;
        n += m;
    }
    detalle = https_detalle;
    https_cerrar();
    if (r != HTTPS_OK)
        return r;
    if (n < 64)
        return ACT_SUMA;
    for (int i = 0; i < 32; i++) {
        int a = hex(txt[2 * i]), b = hex(txt[2 * i + 1]);
        if (a < 0 || b < 0)
            return ACT_SUMA;
        suma[i] = a << 4 | b;
    }
    return ACT_OK;
}

static void avance(void);

// El .bin, a la zona de la flash
static uint8_t bajar_bin(void)
{
    static char url[160];
    int32_t largo;
    snprintf(url, sizeof url, URL_ARCHIVO "%s/" ACT_ARCHIVO, tag);
    int r = https_get(url, &largo);
    detalle = https_detalle;
    if (r != HTTPS_OK)
        return r;
    if (largo < (int32_t)FLASH_SECTOR_SIZE || largo > (int32_t)ACT_MAX)
        return ACT_TAMANO;
    total = largo;

    uint32_t n = 0;
    int lleno = 0;
    while (n < (uint32_t)largo) {
        int m = https_leer(sector + lleno, sizeof sector - lleno);
        if (m <= 0) {
            detalle = https_detalle;
            return m < 0 ? -m : ACT_CONEXION;
        }
        lleno += m;
        n += m;
        if (lleno == (int)sizeof sector || n == (uint32_t)largo) {
            memset(sector + lleno, 0xFF, sizeof sector - lleno);
            grabar_sector(ACT_ZONA + n - lleno, sector);
            lleno = 0;
        }
        bajados = n;
        avance();
    }
    return ACT_OK;
}

// Lo grabado en la zona: la suma, el bloque IMAGE_DEF del RP2350 al
// principio (lo que busca el bootrom) y el nombre del firmware
static uint8_t verificar(const uint8_t *suma, uint32_t largo)
{
    const uint8_t *img = (const uint8_t *)(XIP_BASE + ACT_ZONA);
    uint8_t hecha[32];
    if (mbedtls_sha256(img, largo, hecha, 0) != 0)
        return ACT_FLASH;
    if (memcmp(hecha, suma, sizeof hecha) != 0)
        return ACT_SUMA;

    bool bloque = false;
    for (uint32_t i = 0; i + 4 <= FLASH_SECTOR_SIZE && i + 4 <= largo && !bloque; i += 4) {
        uint32_t w;
        memcpy(&w, img + i, 4);
        bloque = w == PICOBIN_BLOCK_MARKER_START;
    }
    static const char nombre[] = "SDF-Pico ";
    bool nuestro = false;
    for (uint32_t i = 0; i + sizeof nombre <= largo && !nuestro; i++)
        nuestro = img[i] == 'S' && memcmp(img + i, nombre, sizeof nombre - 1) == 0;
    return bloque && nuestro ? ACT_OK : ACT_IMAGEN;
}

// ---- La copia ----
//
// Con las interrupciones cortadas hasta el reinicio: en la flash que se
// pisa estan lwIP, el driver del CYW43 y mbedTLS (ld/), y nada puede
// llamarlos. Esta funcion, la de la flash y memcpy estan en la RAM. El
// nucleo 1 sigue atendiendo el bus desde la RAM y contesta ACT_GRABANDO.

static void __no_inline_not_in_flash_func(instalar)(uint32_t largo)
{
    const uint8_t *img = (const uint8_t *)(XIP_BASE + ACT_ZONA);
    save_and_disable_interrupts();
    for (uint32_t off = 0; off < largo; off += FLASH_SECTOR_SIZE) {
        memcpy(sector, img + off, sizeof sector);
        for (int intento = 0; intento < 3; intento++) {
            flash_range_erase(off, FLASH_SECTOR_SIZE);
            flash_range_program(off, sector, FLASH_SECTOR_SIZE);
            if (memcmp((const void *)(XIP_BASE + off), sector, sizeof sector) == 0)
                break;
        }
    }
    watchdog_reboot(0, 0, 0);
    for (;;)
        __wfi();
}

// ---- Nucleo 0 ----

static bool consola_espera;     // la consola lo pidio: mostrar el avance y el resultado
static uint32_t avance_kb;

static void avance(void)
{
    if (!consola_espera || bajados / (64 * 1024) == avance_kb)
        return;
    avance_kb = bajados / (64 * 1024);
    printf("actualizar: %lu de %lu KB\n", (unsigned long)(bajados / 1024),
           (unsigned long)(total / 1024));
}

static void informar(void)
{
    static const char *const que[] = {
        [ACT_OK] = "listo", [ACT_SIN_WIFI] = "sin Wi-Fi",
        [ACT_NOMBRE] = "el nombre no se resolvio", [ACT_CONEXION] = "fallo la conexion",
        [ACT_TLS] = "fallo el TLS, error -", [ACT_HTTP] = "GitHub contesto",
        [ACT_TIEMPO] = "GitHub dejo de mandar", [ACT_URL] = "URL no valida",
        [ACT_MEMORIA] = "no alcanzo la RAM para el TLS",
        [ACT_RESPUESTA] = "GitHub no dijo la version",
        [ACT_TAMANO] = "el .bin no tiene un tamano posible",
        [ACT_SUMA] = "la suma SHA-256 no coincide",
        [ACT_IMAGEN] = "no es un firmware del SDF-Pico",
        [ACT_FLASH] = "fallo la flash",
    };
    printf("actualizar: %s", res < sizeof que / sizeof que[0] ? que[res] : "?");
    if (res == ACT_TLS)
        printf("0x%04X", detalle);
    else if (res == ACT_HTTP)
        printf(" %u", detalle);
    printf("\n");
    if (res == ACT_OK)
        printf("instalada %s, en GitHub %s%s\n", instalada(), version,
               nueva ? " (mas nueva: actualizar si)" : "");
}

static void correr(void)
{
    uint8_t suma[32];
    res = ACT_OK;
    detalle = 0;
    bajados = total = 0;
    avance_kb = 0;
    tag[0] = version[0] = 0;
    nueva = false;

    uint8_t r = consultar();
    https_cerrar();
    if (r == ACT_OK && p_modo == ACT_ACTUALIZAR) {
        fase = ACT_BAJANDO;
        r = bajar_suma(suma);
        if (r == ACT_OK)
            r = bajar_bin();
        https_cerrar();
        if (r == ACT_OK) {
            fase = ACT_VERIFICANDO;
            r = verificar(suma, total);
        }
        if (r == ACT_OK) {
            fase = ACT_GRABANDO;
            if (consola_espera)
                printf("actualizar: verificado; grabando %s y reiniciando\n", version);
            consola_tarea();
            sleep_ms(200);      // que salga por el USB
            instalar(total);
        }
    }
    res = r;
    __mem_fence_release();
    estado = ACT_LISTA;
}

void act_tarea(void)
{
    uint32_t p = pedido;
    if (p == atendido)
        return;
    __mem_fence_acquire();
    atendido = p;
    correr();
    if (consola_espera) {
        consola_espera = false;
        informar();
    }
}

bool act_comando(const char *linea)
{
    uint8_t modo;
    if (strcmp(linea, "actualizar") == 0)
        modo = ACT_CONSULTAR;
    else if (strcmp(linea, "actualizar si") == 0)
        modo = ACT_ACTUALIZAR;
    else
        return false;
    // Desde el nucleo 0: si justo pide el MSX, uno de los dos se pierde
    consola_espera = true;
    if (act_pedir(modo, ACT_SI) != ACT_PEDIDO_OK) {
        consola_espera = false;
        printf("actualizar: hay una en curso\n");
    } else {
        printf("actualizar: preguntando a GitHub\n");
    }
    return true;
}
