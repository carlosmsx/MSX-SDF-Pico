#include <string.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "ajustes.h"
#include "camara.h"
#include "protocolo.h"
#include "red.h"
#include "reloj.h"
#include "wifi.h"
#include "wifi_dos.h"

// ---- Buzon entre nucleos ----
//
// Como el de menu_wifi.c: un pedido por vez, pedido lo escribe solo el
// nucleo 1 y atendido solo el nucleo 0.

#define PAR_MAX 100

static volatile uint32_t pedido, atendido;
static uint8_t p_cmd;
static uint8_t p_par[PAR_MAX];
static int p_n;
static uint8_t salida[600];     // 16 redes de hasta 35 bytes, y el encabezado
static int n_salida;

void wdos_pedir(uint8_t cmd, const uint8_t *par, int n)
{
    if (n > PAR_MAX)
        n = PAR_MAX;
    p_cmd = cmd;
    memcpy(p_par, par, n);
    p_n = n;
    __mem_fence_release();
    pedido = pedido + 1;
}

int wdos_respuesta(const uint8_t **datos)
{
    if (atendido != pedido)
        return -1;
    __mem_fence_acquire();
    *datos = salida;
    return n_salida;
}

// ---- Respuestas ----

static int n_out;

static void out(uint8_t c)
{
    if (n_out < (int)sizeof salida)
        salida[n_out++] = c;
}

static void outz(const char *s)
{
    while (*s && n_out < (int)sizeof salida - 1)
        out(*s++);
    out(0);
}

static uint8_t rssi_byte(int rssi)
{
    return (uint8_t)(int8_t)(rssi < -128 ? -128 : rssi > 0 ? 0 : rssi);
}

// estado, resultado, senal, red, IP y motivo del fallo (ASCIIZ)
static void estado(void)
{
    int e = wifi_estado();
    const char *motivo;
    int r = wifi_resultado(&motivo);
    out(e);
    out(r == 0 ? WDOS_EN_CURSO : r > 0 ? WDOS_CONECTO : WDOS_FALLO);
    out(e == WIFI_CONECTADO ? rssi_byte(wifi_senal()) : 0);
    // la que se usa; si no hay, la guardada (WIFI_ESPERA reintenta con ella)
    outz(wifi_red()[0] ? wifi_red() : wifi_red_guardada());
    outz(e == WIFI_CONECTADO ? wifi_ip() : "");
    outz(r < 0 ? motivo : "");
}

// buscando, n y n redes: senal, marcas, nombre (ASCIIZ)
static void redes(void)
{
    if (wifi_buscando()) {
        out(1);
        out(0);
        return;
    }
    const wifi_red_t *r;
    int n = wifi_redes(&r);
    out(0);
    out(n);
    for (int i = 0; i < n; i++) {
        out(rssi_byte(r[i].rssi));
        out((r[i].abierta ? WDOS_ABIERTA : 0)
            | (strcmp(r[i].ssid, wifi_red_guardada()) == 0 ? WDOS_GUARDADA : 0));
        outz(r[i].ssid);
    }
}

// largo, nombre, largo, clave (clave vacia: red abierta)
static uint8_t conectar(void)
{
    if (wifi_estado() == WIFI_APAGADO)
        return WDOS_APAGADO;
    int ns = p_n > 0 ? p_par[0] : 0;
    if (ns == 0 || ns > WIFI_SSID_MAX || p_n < 2 + ns)
        return WDOS_MAL;
    int nc = p_par[1 + ns];
    if ((nc > 0 && nc < WIFI_CLAVE_MIN) || nc > WIFI_CLAVE_MAX || p_n < 2 + ns + nc)
        return WDOS_MAL;
    char ssid[WIFI_SSID_MAX + 1], clave[WIFI_CLAVE_MAX + 1];
    memcpy(ssid, p_par + 1, ns);
    ssid[ns] = 0;
    memcpy(clave, p_par + 2 + ns, nc);
    clave[nc] = 0;
    wifi_conectar(ssid, clave);
    memset(clave, 0, sizeof clave);
    return WDOS_OK;
}

void wdos_borrar(void)
{
    wifi_olvidar();
    red_conectar("", 0);
    cam_olvidar();
    ajustes_borrar();
    reloj_poner_zona(ajustes_zona());
}

void wdos_tarea(void)
{
    uint32_t p = pedido;
    if (p == atendido)
        return;
    __mem_fence_acquire();

    n_out = 0;
    switch (p_cmd) {
    case CMD_WIFISTAT:
        estado();
        break;
    case CMD_WIFISCAN:
        out(wifi_estado() == WIFI_APAGADO ? WDOS_APAGADO
            : wifi_buscar() ? WDOS_OK : WDOS_MAL);
        break;
    case CMD_WIFIREDES:
        redes();
        break;
    case CMD_WIFIJOIN:
        out(conectar());
        break;
    case CMD_SDFBORRAR:
        wdos_borrar();
        out(0);
        break;
    }
    memset(p_par, 0, sizeof p_par);     // la clave no queda en la RAM
    n_salida = n_out;
    __mem_fence_release();
    atendido = p;
}
