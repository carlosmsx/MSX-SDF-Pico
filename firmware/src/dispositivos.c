#include <stdio.h>
#include <string.h>
#include "dispositivos.h"
#include "protocolo.h"
#include "reloj.h"

#define DEV_RTC         0
#define LINEA_MAX       32

#define MODO_ENTRADA    1
#define MODO_SALIDA     2
#define MODO_ALEATORIO  4
#define MODO_AGREGAR    8

enum { F_COMPLETO, F_FECHA, F_HORA, F_NUMEROS, F_DIA, F_MAL = 0xFF };

static uint8_t formato_nombre = F_COMPLETO;     // el que vino pegado al nombre
static uint8_t entrada = DEV_NONE, salida = DEV_NONE;
static uint8_t formato = F_COMPLETO;
static char linea[LINEA_MAX];
static int pos = -1;                            // -1: hay que armarla de nuevo
static char recibida[LINEA_MAX];
static int n_recibida;

static uint8_t formato_de(char c)
{
    switch (c) {
    case 0:             return F_COMPLETO;
    case 'D': case 'd': return F_FECHA;
    case 'T': case 't': return F_HORA;
    case 'N': case 'n': return F_NUMEROS;
    case 'W': case 'w': return F_DIA;
    }
    return F_MAL;
}

uint8_t dev_nombre(const char *nombre)
{
    if (strncmp(nombre, "RTC", 3) != 0)
        return DEV_NONE;
    uint8_t f = nombre[3] && nombre[4] ? F_MAL : formato_de(nombre[3]);
    if (f == F_MAL)
        return DEV_NONE;    // "RTCX" no es de nadie
    formato_nombre = f;
    return DEV_RTC;
}

uint8_t dev_abrir(uint8_t id, uint8_t modo, const char *texto, uint8_t largo)
{
    if (id != DEV_RTC)
        return ERR_DEVICE_IO;
    if (modo & MODO_ALEATORIO)
        return ERR_BAD_FILE_MODE;
    // Sin texto tras los ":", vale el que venia pegado al nombre
    uint8_t f = largo == 0 ? formato_nombre : largo == 1 ? formato_de(texto[0]) : F_MAL;
    if (f == F_MAL)
        return ERR_BAD_FILE_NAME;
    if (modo & (MODO_SALIDA | MODO_AGREGAR)) {
        salida = DEV_RTC;
        n_recibida = 0;
    } else {
        entrada = DEV_RTC;
        formato = f;
        pos = -1;
    }
    return 0;
}

void dev_cerrar(uint8_t id, uint8_t modo)
{
    if (id != DEV_RTC)
        return;
    if (modo & (MODO_SALIDA | MODO_AGREGAR))
        salida = DEV_NONE;
    else
        entrada = DEV_NONE;
}

// La linea siempre termina en CR, que es lo que cierra el LINE INPUT.
static void armar_linea(void)
{
    static const char dias[] = "DOMLUNMARMIEJUEVIESAB";
    fecha_t f;
    reloj_leer(&f);
    switch (formato) {
    case F_NUMEROS:
        snprintf(linea, sizeof linea, "%u,%u,%u,%u,%u,%u,%u\r", f.anio, f.mes, f.dia,
                 f.hora, f.min, f.seg, f.dia_semana);
        break;
    case F_DIA:
        snprintf(linea, sizeof linea, "%.3s\r", dias + (f.dia_semana - 1) * 3);
        break;
    case F_FECHA:
        snprintf(linea, sizeof linea, "%04u-%02u-%02u\r", f.anio, f.mes, f.dia);
        break;
    case F_HORA:
        snprintf(linea, sizeof linea, "%02u:%02u:%02u\r", f.hora, f.min, f.seg);
        break;
    default:
        snprintf(linea, sizeof linea, "%04u-%02u-%02u %02u:%02u:%02u\r",
                 f.anio, f.mes, f.dia, f.hora, f.min, f.seg);
        break;
    }
}

uint8_t dev_leer(void)
{
    if (entrada != DEV_RTC)
        return 0;           // nada abierto: fin de datos
    if (pos < 0) {
        armar_linea();
        pos = 0;
    }
    char c = linea[pos++];
    if (c == '\r' || c == 0)
        pos = -1;           // linea entregada: la proxima es hora nueva
    return c;
}

uint8_t dev_eof(void)
{
    // Ademas del fin de datos, avisa que la hora no es confiable
    return reloj_origen() == RELOJ_SIN_PONER ? 0xFF : 0;
}

// Pone el reloj con la linea de PRINT #. Acepta cualquier separador que no
// sea un digito, asi entran "2026-09-20 14:35:07" y "2026,9,20,14,35,7".
static uint8_t ejecutar_linea(void)
{
    uint32_t n[6];
    int cnt = 0;
    const char *p = recibida;
    while (*p && cnt < 6) {
        if (*p < '0' || *p > '9') {
            p++;
            continue;
        }
        uint32_t v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p++ - '0');
            if (v > 9999)
                return ERR_ILLEGAL_FUNCTION;
        }
        n[cnt++] = v;
    }
    if (cnt < 6)
        return ERR_ILLEGAL_FUNCTION;
    if (n[0] < 100)
        n[0] += 2000;       // anio de dos digitos
    fecha_t f = { .anio = n[0], .mes = n[1], .dia = n[2],
                  .hora = n[3], .min = n[4], .seg = n[5] };
    if (n[1] > 12 || n[2] > 31 || n[3] > 23 || n[4] > 59 || n[5] > 59)
        return ERR_ILLEGAL_FUNCTION;
    return reloj_poner(&f) ? 0 : ERR_ILLEGAL_FUNCTION;
}

int dev_escribir(uint8_t c)
{
    if (c != '\r') {
        if (n_recibida < LINEA_MAX - 1)
            recibida[n_recibida++] = c;
        return -1;
    }
    recibida[n_recibida] = 0;
    n_recibida = 0;
    if (salida != DEV_RTC)
        return ERR_DEVICE_IO;
    return ejecutar_linea();
}
