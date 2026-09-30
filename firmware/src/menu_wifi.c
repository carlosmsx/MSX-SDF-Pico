#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "menu_wifi.h"
#include "wifi.h"

// ---- Buzon entre nucleos ----
//
// Un pedido por vez: la ROM no manda otro hasta leer la respuesta. pedido lo
// escribe solo el nucleo 1 y atendido solo el nucleo 0. Si la ROM se cansa de
// esperar y manda otro comando, el nucleo 1 corta el DMA de la respuesta
// vieja antes de pedir la nueva, asi que salida nunca se pisa mientras sale.

static volatile uint32_t pedido, atendido;
static volatile uint8_t p_tecla, p_ancho, p_filas;
static uint8_t salida[3072];
static volatile int n_salida;

void menu_pedir(uint8_t tecla, uint8_t ancho, uint8_t filas)
{
    p_tecla = tecla;
    p_ancho = ancho;
    p_filas = filas;
    __mem_fence_release();
    pedido = pedido + 1;
}

int menu_respuesta(const uint8_t **datos)
{
    if (atendido != pedido)
        return -1;
    __mem_fence_acquire();
    *datos = salida;
    return n_salida;
}

// ---- Pantalla ----
//
// El firmware recuerda lo que tiene la pantalla del MSX y en cada respuesta
// manda solo las filas que cambiaron: ESC Y para ubicar el cursor, el texto
// y ESC K para borrar el resto. Asi mover el cursor por la lista son dos
// filas y no la pantalla entera. Ninguna fila llega a la ultima columna ni se
// usa la ultima fila (la de las teclas de funcion): la pantalla nunca se
// desplaza.

#define ANCHO_MAX   80
#define FILAS_MAX   24
#define ESC         0x1B
#define CLS         0x0C

static char pantalla[FILAS_MAX][ANCHO_MAX + 1];    // lo que tiene el MSX
static char nueva[FILAS_MAX][ANCHO_MAX + 1];
static int ancho, filas;                           // lo que se usa
static int n_out;

static void out(uint8_t c)
{
    if (n_out < (int)sizeof salida - 1)
        salida[n_out++] = c;
}

static void outs(const char *s)
{
    while (*s)
        out(*s++);
}

static void linea(int f, const char *fmt, ...)
{
    if (f < 0 || f >= filas)
        return;
    char txt[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(txt, sizeof txt, fmt, ap);
    va_end(ap);
    int i;
    for (i = 0; txt[i] && i < ancho; i++) {
        uint8_t c = txt[i];
        nueva[f][i] = (c >= 0x20 && c < 0x7F) ? c : '?';   // SSID con UTF-8
    }
    nueva[f][i] = 0;
}

static void volcar(void)
{
    for (int f = 0; f < filas; f++) {
        if (strcmp(nueva[f], pantalla[f]) == 0)
            continue;
        out(ESC);
        out('Y');
        out(32 + f);
        out(32);
        outs(nueva[f]);
        out(ESC);
        out('K');
        strcpy(pantalla[f], nueva[f]);
    }
}

// ---- Menu ----

#define T_BS        0x08
#define T_RETURN    0x0D
#define T_ESC       0x1B
#define T_ARRIBA    0x1E
#define T_ABAJO     0x1F
#define T_DEL       0x7F

enum { M_FUERA, M_LISTA, M_CLAVE, M_CONECTANDO, M_RESULTADO, M_SIN_WIFI };
enum { O_BUSCAR, O_OLVIDAR, O_SALIR };

static int modo = M_FUERA;
static int cursor;
static char elegida[WIFI_SSID_MAX + 1];
static char clave[WIFI_CLAVE_MAX + 1];
static int n_clave;
static const char *aviso = "";
static absolute_time_t desde;           // del intento de conexion
static bool conecto;
static const char *motivo = "";

static void borrar_clave(void)
{
    memset(clave, 0, sizeof clave);
    n_clave = 0;
}

// Redes que entran en la pantalla: lo que sobra del encabezado (4 filas), las
// opciones (hasta 3, con una fila en blanco antes) y la ayuda (con otra).
static int redes_visibles(const wifi_red_t **r)
{
    int n = wifi_redes(r);
    int lugar = filas - 10;
    return n < lugar ? n : (lugar > 0 ? lugar : 0);
}

static int opciones(int *opc)
{
    int n = 0;
    opc[n++] = O_BUSCAR;
    if (wifi_red_guardada()[0])
        opc[n++] = O_OLVIDAR;
    opc[n++] = O_SALIR;
    return n;
}

static void conectar_elegida(void)
{
    wifi_conectar(elegida, clave);
    borrar_clave();
    modo = M_CONECTANDO;
    desde = get_absolute_time();
}

// Una tecla. true para salir del menu.
static bool teclear(uint8_t t)
{
    switch (modo) {
    case M_LISTA: {
        const wifi_red_t *r;
        int nvis = redes_visibles(&r);
        int opc[3];
        int nopc = opciones(opc);
        int total = nvis + nopc;
        if (t == T_ESC)
            return true;
        if (t == T_ARRIBA)
            cursor = (cursor + total - 1) % total;
        else if (t == T_ABAJO)
            cursor = (cursor + 1) % total;
        else if (t == T_RETURN && cursor < nvis) {
            strcpy(elegida, r[cursor].ssid);
            borrar_clave();
            if (r[cursor].abierta) {
                conectar_elegida();
            } else {
                aviso = "";
                modo = M_CLAVE;
            }
        } else if (t == T_RETURN) {
            switch (opc[cursor - nvis]) {
            case O_BUSCAR:  wifi_buscar(); break;
            case O_OLVIDAR: wifi_olvidar(); break;
            case O_SALIR:   return true;
            }
        }
        break;
    }
    case M_CLAVE:
        if (t == T_ESC) {
            borrar_clave();
            modo = M_LISTA;
        } else if (t == T_BS || t == T_DEL) {
            if (n_clave > 0)
                clave[--n_clave] = 0;
            aviso = "";
        } else if (t == T_RETURN) {
            if (n_clave > 0 && n_clave < WIFI_CLAVE_MIN)
                aviso = "La clave tiene al menos 8 caracteres";
            else
                conectar_elegida();
        } else if (t >= 0x20 && t < 0x7F && n_clave < WIFI_CLAVE_MAX) {
            clave[n_clave++] = t;
            aviso = "";
        }
        break;
    case M_CONECTANDO:
        if (t == T_ESC)
            modo = M_LISTA;     // la conexion sigue; el encabezado la muestra
        break;
    case M_RESULTADO:
        if (t != MENU_NADA)
            modo = M_LISTA;
        break;
    case M_SIN_WIFI:
        if (t != MENU_NADA)
            return true;
        break;
    }
    return false;
}

static void encabezado(void)
{
    linea(0, "SDF-Pico: red Wi-Fi");
    switch (wifi_estado()) {
    case WIFI_APAGADO:
        linea(1, "El Wi-Fi no responde");
        break;
    case WIFI_CONECTADO:
        linea(1, "Conectado a %s", wifi_red());
        linea(2, "IP %s", wifi_ip());
        break;
    case WIFI_CONECTANDO:
        linea(1, "Conectando a %s...", wifi_red());
        break;
    case WIFI_SIN_RED:
        linea(1, "Sin red guardada");
        break;
    case WIFI_ESPERA:
        linea(1, "Sin conexion a %s", wifi_red_guardada());
        break;
    }
}

static void dibujar_lista(void)
{
    const wifi_red_t *r;
    int nvis = redes_visibles(&r);
    int opc[3];
    int nopc = opciones(opc);
    if (cursor >= nvis + nopc)
        cursor = nvis + nopc - 1;

    int f = 4;
    if (nvis == 0)
        linea(f++, "  %s", wifi_buscando() ? "Buscando redes..." : "No se encontraron redes");
    // "> nombre             -52*": el * marca la red guardada
    int w = ancho - 8;
    if (w > WIFI_SSID_MAX)
        w = WIFI_SSID_MAX;
    for (int i = 0; i < nvis; i++)
        linea(f++, "%c %-*.*s %4d%c", cursor == i ? '>' : ' ', w, w, r[i].ssid,
              r[i].rssi, strcmp(r[i].ssid, wifi_red_guardada()) == 0 ? '*' : ' ');
    f++;
    static const char *const nombre[] = {
        [O_BUSCAR] = "Buscar redes", [O_OLVIDAR] = "Olvidar la red guardada",
        [O_SALIR] = "Salir",
    };
    for (int i = 0; i < nopc; i++)
        linea(f++, "%c %s", cursor == nvis + i ? '>' : ' ', nombre[opc[i]]);

    linea(filas - 1, "%s", wifi_buscando() && nvis > 0 ? "Buscando redes..."
                                                       : "Cursor y RETURN elige, ESC sale");
}

static void dibujar_clave(void)
{
    linea(2, "Red: %s", elegida);
    linea(4, "Clave:");
    // Asteriscos, los ultimos que entran, y el cursor
    char txt[ANCHO_MAX + 1];
    int n = n_clave < ancho - 1 ? n_clave : ancho - 1;
    memset(txt, '*', n);
    txt[n] = '_';
    txt[n + 1] = 0;
    linea(5, "%s", txt);
    linea(7, "%s", aviso);
    linea(filas - 1, "RETURN conecta, ESC vuelve");
}

static void dibujar(void)
{
    memset(nueva, 0, sizeof nueva);

    if (modo == M_CONECTANDO) {
        const char *m;
        int r = wifi_resultado(&m);
        if (r != 0) {
            conecto = r > 0;
            motivo = m;
            modo = M_RESULTADO;
        }
    }

    switch (modo) {
    case M_LISTA:
        encabezado();
        dibujar_lista();
        break;
    case M_CLAVE:
        linea(0, "SDF-Pico: red Wi-Fi");
        dibujar_clave();
        break;
    case M_CONECTANDO: {
        int s = absolute_time_diff_us(desde, get_absolute_time()) / 1000000;
        linea(0, "SDF-Pico: red Wi-Fi");
        linea(2, "Red: %s", elegida);
        linea(4, "Conectando%.*s", s % 4, "...");
        linea(filas - 1, "ESC vuelve al menu");
        break;
    }
    case M_RESULTADO:
        linea(0, "SDF-Pico: red Wi-Fi");
        linea(2, "Red: %s", elegida);
        if (conecto) {
            linea(4, "Conectado. IP %s", wifi_ip());
            linea(5, "La red quedo guardada.");
        } else {
            linea(4, "No se pudo conectar:");
            linea(5, "%s", motivo);
        }
        linea(filas - 1, "Pulse una tecla");
        break;
    case M_SIN_WIFI:
        linea(0, "SDF-Pico: red Wi-Fi");
        linea(2, "El Wi-Fi no responde.");
        linea(filas - 1, "Pulse una tecla");
        break;
    }
    volcar();
}

static void empezar(void)
{
    modo = wifi_estado() == WIFI_APAGADO ? M_SIN_WIFI : M_LISTA;
    cursor = 0;
    borrar_clave();
    wifi_buscar();
    memset(pantalla, 0, sizeof pantalla);
    out(ESC);           // sin cursor
    outs("x5");
    out(CLS);
}

static void cerrar(void)
{
    modo = M_FUERA;
    borrar_clave();
    out(CLS);
    out(ESC);           // cursor otra vez
    outs("y5");
}

static int limitar(int v, int min, int max)
{
    return v < min ? min : v > max ? max : v;
}

void menu_tarea(void)
{
    uint32_t p = pedido;
    if (p == atendido)
        return;
    __mem_fence_acquire();
    uint8_t t = p_tecla;
    ancho = limitar(p_ancho, 16, ANCHO_MAX) - 1;
    filas = limitar(p_filas, 8, FILAS_MAX) - 1;

    // Respuesta: 0 sigue o 1 fin, y el texto para CHPUT, ASCIIZ. Si la ROM
    // manda una tecla sin haber empezado (la Pico se reinicio con el menu en
    // pantalla), se empieza de nuevo.
    n_out = 1;
    bool fin = false;
    if (t == MENU_EMPEZAR || modo == M_FUERA)
        empezar();
    else if (t == MENU_CORTAR)
        fin = true;
    else
        fin = teclear(t);

    if (fin)
        cerrar();
    else
        dibujar();
    salida[0] = fin;
    salida[n_out++] = 0;
    n_salida = n_out;
    __mem_fence_release();
    atendido = p;
}
