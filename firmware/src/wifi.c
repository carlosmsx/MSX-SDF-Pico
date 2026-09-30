#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "wifi.h"

// ---- Credenciales en la flash ----
//
// Ultimo sector de la flash (4 KB), lejos del programa. Grabar un .uf2 no lo
// toca: las credenciales sobreviven a las actualizaciones del firmware.
// Grabar sin detener al nucleo 1 es seguro porque todo el programa corre de
// la RAM (copy_to_ram) y el nucleo 1 nunca lee la flash. Lo unico que queda
// en la flash es el firmware del CYW43, y solo se lee en wifi_init.

#define CRED_OFFSET     (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CRED_MAGIA      0x49464957u     // "WIFI"

typedef struct {
    uint32_t magia;
    char ssid[WIFI_SSID_MAX + 1];
    char clave[WIFI_CLAVE_MAX + 1];
    uint32_t suma;
} cred_t;

static cred_t guardada;         // la de la flash; ssid vacio = ninguna

static uint32_t suma_de(const cred_t *c)
{
    const uint8_t *p = (const uint8_t *)c;
    uint32_t s = 0x5DF;
    for (size_t i = 0; i < offsetof(cred_t, suma); i++)
        s = (s << 5) + s + p[i];
    return s;
}

static void cred_leer(void)
{
    const cred_t *f = (const cred_t *)(XIP_BASE + CRED_OFFSET);
    memset(&guardada, 0, sizeof guardada);
    if (f->magia == CRED_MAGIA && f->suma == suma_de(f)
        && f->ssid[WIFI_SSID_MAX] == 0 && f->clave[WIFI_CLAVE_MAX] == 0)
        guardada = *f;
}

// c = NULL borra el sector (olvidar la red).
static void cred_grabar(const cred_t *c)
{
    static uint8_t pagina[FLASH_PAGE_SIZE];
    memset(pagina, 0xFF, sizeof pagina);
    if (c) {
        cred_t *p = (cred_t *)pagina;
        *p = *c;
        p->magia = CRED_MAGIA;
        p->suma = suma_de(p);
    }

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CRED_OFFSET, FLASH_SECTOR_SIZE);
    if (c)
        flash_range_program(CRED_OFFSET, pagina, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    memset(pagina, 0, sizeof pagina);   // que la clave no quede en la RAM
    cred_leer();
}

// ---- Conexion ----

#define CONECTAR_MS     30000   // mas que esto conectando: fallo
#define REINTENTO_MS    30000   // espera antes de volver a la red guardada
#define SONDEO_MS       250

static int estado = WIFI_APAGADO;
static cred_t intento;          // la red con la que se esta conectando
static bool intento_nuevo;      // la pidio el usuario: grabarla si conecta
static absolute_time_t plazo;   // fin del intento, o del reintento
static int ultimo_link = CYW43_LINK_DOWN;
static int resultado = 1;       // de la ultima wifi_conectar
static const char *motivo_fallo = "";

static const char *texto_link(int s)
{
    switch (s) {
    case CYW43_LINK_DOWN:    return "sin conexion";
    case CYW43_LINK_JOIN:    return "asociado";
    case CYW43_LINK_NOIP:    return "esperando IP";
    case CYW43_LINK_UP:      return "conectado";
    case CYW43_LINK_FAIL:    return "fallo la conexion";
    case CYW43_LINK_NONET:   return "no se encuentra la red";
    case CYW43_LINK_BADAUTH: return "clave incorrecta";
    default:                 return "?";
    }
}

static int link(void)
{
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
}

static void conectar(const cred_t *c, bool nuevo)
{
    if (link() != CYW43_LINK_DOWN)
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    intento = *c;
    intento_nuevo = nuevo;
    printf("wifi: conectando a \"%s\"\n", c->ssid);
    int r = cyw43_arch_wifi_connect_async(c->ssid,
                c->clave[0] ? c->clave : NULL,
                c->clave[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
    if (r) {
        printf("wifi: no se pudo iniciar la conexion (%d)\n", r);
        if (nuevo) {
            resultado = -1;
            motivo_fallo = "no se pudo iniciar";
        }
        estado = WIFI_ESPERA;
        plazo = make_timeout_time_ms(REINTENTO_MS);
        return;
    }
    estado = WIFI_CONECTANDO;
    plazo = make_timeout_time_ms(CONECTAR_MS);
}

// Tras un fallo: vuelve a la red guardada, mas tarde.
static void fallo(const char *motivo)
{
    printf("wifi: \"%s\": %s\n", intento.ssid, motivo);
    if (intento_nuevo) {
        resultado = -1;
        motivo_fallo = motivo;
    }
    memset(&intento, 0, sizeof intento);
    intento_nuevo = false;
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    if (guardada.ssid[0]) {
        estado = WIFI_ESPERA;
        plazo = make_timeout_time_ms(REINTENTO_MS);
    } else {
        estado = WIFI_SIN_RED;
    }
}

static void buscar_terminar(void);

bool wifi_init(void)
{
    cred_leer();
    if (cyw43_arch_init()) {
        printf("wifi: el CYW43 no contesta\n");
        return false;
    }
    cyw43_arch_enable_sta_mode();
    estado = WIFI_SIN_RED;
    if (guardada.ssid[0])
        conectar(&guardada, false);
    else
        printf("wifi: sin red guardada (\"ayuda\" para configurarla)\n");
    return true;
}

void wifi_tarea(void)
{
    static absolute_time_t prox;
    if (estado == WIFI_APAGADO || !time_reached(prox))
        return;
    prox = make_timeout_time_ms(SONDEO_MS);
    buscar_terminar();

    int s = link();
    bool cambio = s != ultimo_link;
    ultimo_link = s;

    switch (estado) {
    case WIFI_CONECTANDO:
        if (s == CYW43_LINK_UP) {
            printf("wifi: conectado a \"%s\", IP %s\n", intento.ssid, wifi_ip());
            estado = WIFI_CONECTADO;
            if (intento_nuevo) {
                if (strcmp(intento.ssid, guardada.ssid) != 0
                    || strcmp(intento.clave, guardada.clave) != 0) {
                    cred_grabar(&intento);
                    printf("wifi: red guardada\n");
                }
                resultado = 1;
            }
            intento_nuevo = false;
        } else if (s == CYW43_LINK_BADAUTH || s == CYW43_LINK_NONET
                   || s == CYW43_LINK_FAIL) {
            fallo(texto_link(s));
        } else if (time_reached(plazo)) {
            fallo("no conecto a tiempo");
        }
        break;
    case WIFI_CONECTADO:
        if (s != CYW43_LINK_UP) {
            printf("wifi: se perdio la conexion (%s)\n", texto_link(s));
            fallo("reconectando");
        }
        break;
    case WIFI_ESPERA:
        if (time_reached(plazo) && guardada.ssid[0])
            conectar(&guardada, false);
        break;
    default:
        if (cambio)
            printf("wifi: %s\n", texto_link(s));
        break;
    }
}

int wifi_estado(void)
{
    return estado;
}

const char *wifi_red(void)
{
    if (estado == WIFI_CONECTANDO || estado == WIFI_CONECTADO)
        return intento.ssid;
    return "";
}

const char *wifi_red_guardada(void)
{
    return guardada.ssid;
}

const char *wifi_ip(void)
{
    return ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
}

int wifi_senal(void)
{
    int32_t rssi = 0;
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    return rssi;
}

void wifi_conectar(const char *ssid, const char *clave)
{
    if (estado == WIFI_APAGADO) {
        resultado = -1;
        motivo_fallo = "el Wi-Fi no responde";
        return;
    }
    cred_t c;
    memset(&c, 0, sizeof c);
    strncpy(c.ssid, ssid, WIFI_SSID_MAX);
    strncpy(c.clave, clave, WIFI_CLAVE_MAX);
    resultado = 0;
    conectar(&c, true);
    memset(&c, 0, sizeof c);
}

int wifi_resultado(const char **motivo)
{
    if (motivo)
        *motivo = motivo_fallo;
    return resultado;
}

void wifi_olvidar(void)
{
    if (estado == WIFI_APAGADO)
        return;
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    memset(&intento, 0, sizeof intento);
    intento_nuevo = false;
    if (guardada.ssid[0])
        cred_grabar(NULL);
    estado = WIFI_SIN_RED;
    printf("wifi: red olvidada\n");
}

// ---- Busqueda ----
//
// El callback corre en la interrupcion del driver: solo junta resultados,
// sin imprimir. wifi_tarea los ordena cuando la busqueda termina.

#define REDES_MAX 16

static wifi_red_t redes[REDES_MAX];
static wifi_red_t encontradas[REDES_MAX];   // la busqueda en curso
static volatile int n_encontradas;
static int n_redes;
static bool buscando;
static bool listar_al_terminar;             // la pidio la consola

static int al_encontrar(void *env, const cyw43_ev_scan_result_t *r)
{
    (void)env;
    if (!r || r->ssid_len == 0 || r->ssid_len > WIFI_SSID_MAX)
        return 0;       // red oculta
    char ssid[WIFI_SSID_MAX + 1];
    memcpy(ssid, r->ssid, r->ssid_len);
    ssid[r->ssid_len] = 0;

    int n = n_encontradas;
    for (int i = 0; i < n; i++) {
        if (strcmp(encontradas[i].ssid, ssid) == 0) {
            if (r->rssi > encontradas[i].rssi) {
                encontradas[i].rssi = r->rssi;
                encontradas[i].canal = r->channel;
            }
            return 0;
        }
    }
    if (n < REDES_MAX) {
        wifi_red_t *d = &encontradas[n];
        strcpy(d->ssid, ssid);
        d->rssi = r->rssi;
        d->canal = r->channel;
        d->abierta = r->auth_mode == 0;
        n_encontradas = n + 1;
    }
    return 0;
}

static int por_senal(const void *a, const void *b)
{
    return ((const wifi_red_t *)b)->rssi - ((const wifi_red_t *)a)->rssi;
}

bool wifi_buscar(void)
{
    if (estado == WIFI_APAGADO)
        return false;
    if (buscando)
        return true;
    cyw43_wifi_scan_options_t opc = {0};
    n_encontradas = 0;
    int r = cyw43_wifi_scan(&cyw43_state, &opc, NULL, al_encontrar);
    if (r) {
        printf("wifi: no se pudo buscar (%d)\n", r);
        return false;
    }
    buscando = true;
    return true;
}

bool wifi_buscando(void)
{
    return buscando;
}

int wifi_redes(const wifi_red_t **lista)
{
    *lista = redes;
    return n_redes;
}

static void listar(void)
{
    if (n_redes == 0)
        printf("wifi: no se encontraron redes\n");
    for (int i = 0; i < n_redes; i++)
        printf("  %2d  %-32s %4d dBm  canal %2d%s\n", i + 1, redes[i].ssid,
               redes[i].rssi, redes[i].canal, redes[i].abierta ? "  abierta" : "");
}

// Sin bloquear: la consola del nucleo 1 sigue saliendo mientras se busca.
static void buscar_terminar(void)
{
    if (!buscando || cyw43_wifi_scan_active(&cyw43_state))
        return;
    buscando = false;
    n_redes = n_encontradas;
    memcpy(redes, encontradas, n_redes * sizeof redes[0]);
    qsort(redes, n_redes, sizeof redes[0], por_senal);
    if (listar_al_terminar)
        listar();
    listar_al_terminar = false;
}

// ---- Consola ----

static bool esperando_clave;
static char pedida[WIFI_SSID_MAX + 1];

static void mostrar_estado(void)
{
    if (estado == WIFI_APAGADO) {
        printf("wifi: apagado (el CYW43 no contesto al arrancar)\n");
        return;
    }
    printf("wifi: red guardada: %s\n", guardada.ssid[0] ? guardada.ssid : "ninguna");
    if (estado == WIFI_CONECTADO)
        printf("wifi: conectado a \"%s\", IP %s, %d dBm\n",
               intento.ssid, wifi_ip(), wifi_senal());
    else if (estado == WIFI_CONECTANDO)
        printf("wifi: conectando a \"%s\"\n", intento.ssid);
    else
        printf("wifi: %s\n", texto_link(link()));
}

bool wifi_pide_clave(void)
{
    return esperando_clave;
}

bool wifi_comando(const char *linea)
{
    if (esperando_clave) {
        esperando_clave = false;
        size_t n = strlen(linea);
        if (n > 0 && n < WIFI_CLAVE_MIN)
            printf("wifi: la clave tiene que tener al menos %d caracteres\n", WIFI_CLAVE_MIN);
        else if (n > WIFI_CLAVE_MAX)
            printf("wifi: la clave es demasiado larga\n");
        else
            wifi_conectar(pedida, linea);
        return true;
    }

    if (strcmp(linea, "wifi") == 0) {
        mostrar_estado();
        return true;
    }
    if (estado == WIFI_APAGADO && (strcmp(linea, "buscar") == 0
            || strncmp(linea, "conectar", 8) == 0 || strcmp(linea, "olvidar") == 0)) {
        printf("wifi: apagado\n");
        return true;
    }
    if (strcmp(linea, "buscar") == 0) {
        if (wifi_buscar()) {
            listar_al_terminar = true;
            printf("wifi: buscando redes...\n");
        }
        return true;
    }
    if (strcmp(linea, "olvidar") == 0) {
        wifi_olvidar();
        return true;
    }
    if (strncmp(linea, "conectar", 8) == 0 && (linea[8] == ' ' || linea[8] == 0)) {
        const char *arg = linea + 8;
        while (*arg == ' ')
            arg++;
        // Un numero elige de la ultima busqueda; si no, es el nombre de la red.
        char *fin;
        long i = strtol(arg, &fin, 10);
        if (*arg && *fin == 0 && i >= 1 && i <= n_redes)
            strcpy(pedida, redes[i - 1].ssid);
        else if (*arg && strlen(arg) <= WIFI_SSID_MAX)
            strcpy(pedida, arg);
        else {
            printf("uso: conectar <numero de \"buscar\" o nombre de la red>\n");
            return true;
        }
        printf("clave de \"%s\" (vacia si es abierta): ", pedida);
        esperando_clave = true;
        return true;
    }
    return false;
}
