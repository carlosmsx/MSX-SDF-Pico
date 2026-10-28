#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "ajustes.h"
#include "camara.h"
#include "sc2.h"
#include "wifi.h"

#define HOST_MAX    63
#define PEDIDO_MAX  (CAM_URL_MAX + 400)     // la ruta, el host y la clave en base64

// ---- URL ----
//
// http://[usuario:clave@]host[:puerto]/ruta, sin el http:// tambien. Se
// revisa en el nucleo 1 (al pedir) y se usa en el 0.

typedef struct {
    char host[HOST_MAX + 1];
    uint16_t puerto;
    const char *ruta;           // dentro de la URL; "" es "/"
    const char *usuario;        // dentro de la URL, hasta la @; NULL sin usuario
    int n_usuario;
} url_t;

static bool url_partir(const char *url, url_t *u)
{
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;
    else if (strstr(p, "://"))
        return false;           // https:// u otro
    const char *fin = p + strcspn(p, "/");
    const char *arroba = NULL;
    for (const char *q = p; q < fin; q++)
        if (*q == '@')
            arroba = q;
    u->usuario = NULL;
    u->n_usuario = 0;
    if (arroba) {
        u->usuario = p;
        u->n_usuario = arroba - p;
        p = arroba + 1;
    }
    const char *dos = memchr(p, ':', fin - p);
    const char *fin_host = dos ? dos : fin;
    if (fin_host == p || fin_host - p > HOST_MAX)
        return false;
    memcpy(u->host, p, fin_host - p);
    u->host[fin_host - p] = 0;
    u->puerto = 80;
    if (dos) {
        long n = 0;
        for (const char *q = dos + 1; q < fin; q++) {
            if (*q < '0' || *q > '9')
                return false;
            n = n * 10 + (*q - '0');
            if (n > 65535)
                return false;
        }
        if (n == 0)
            return false;
        u->puerto = (uint16_t)n;
    }
    u->ruta = fin;
    return true;
}

static void base64(const char *in, int n, char *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *p = (const uint8_t *)in;
    for (; n > 0; n -= 3, p += 3) {
        uint32_t v = p[0] << 16 | (n > 1 ? p[1] << 8 : 0) | (n > 2 ? p[2] : 0);
        *out++ = t[v >> 18];
        *out++ = t[(v >> 12) & 63];
        *out++ = n > 1 ? t[(v >> 6) & 63] : '=';
        *out++ = n > 2 ? t[v & 63] : '=';
    }
    *out = 0;
}

// ---- Del nucleo 1 al 0 ----
//
// Como ping.c: estado lo escribe el nucleo 1 al pedir (de LIBRE o LISTA a
// EN_CURSO) y el 0 al terminar.

static char p_url[CAM_URL_MAX + 1];         // vacia: la guardada
static volatile uint32_t pedido, atendido;
static volatile uint8_t estado;

static char g_url[CAM_URL_MAX + 1];         // la que se guarda en la flash
static volatile uint32_t pedido_url, atendido_url;

// La URL guardada, la escribe el nucleo 0 (al arrancar y al guardar)
static char url_guardada[CAM_URL_MAX + 1];

// Resultado: lo escribe el nucleo 0 antes de pasar a LISTA
static uint8_t res;
static uint16_t detalle, ancho, alto, ms;
static uint8_t pantalla[CAM_PANTALLA];

static bool copiar_url(char *dst, const char *url, int n)
{
    url_t u;
    if (n > CAM_URL_MAX)
        return false;
    memcpy(dst, url, n);
    dst[n] = 0;
    return n == 0 || url_partir(dst, &u);
}

uint8_t cam_pedir(const char *url, int n)
{
    if (estado == CAM_EN_CURSO)
        return CAM_OCUPADA;
    if (!copiar_url(p_url, url, n))
        return CAM_URL_MAL;
    estado = CAM_EN_CURSO;
    __mem_fence_release();
    pedido = pedido + 1;
    return CAM_PEDIDO_OK;
}

uint8_t cam_guardar_url(const char *url, int n)
{
    if (pedido_url != atendido_url)
        return CAM_OCUPADA;
    if (!copiar_url(g_url, url, n))
        return CAM_URL_MAL;
    __mem_fence_release();
    pedido_url = pedido_url + 1;
    return CAM_PEDIDO_OK;
}

// La clave (lo que hay entre los : y la @ del usuario) va con asteriscos
static int url_sin_clave(const char *url, char *out)
{
    url_t u;
    int n = 0;
    const char *dos = NULL;
    if (url[0] && url_partir(url, &u) && u.usuario)
        dos = memchr(u.usuario, ':', u.n_usuario);
    for (const char *p = url; *p; p++) {
        if (dos && p > dos && p < u.usuario + u.n_usuario) {
            if (p == dos + 1)
                for (int i = 0; i < 4; i++)
                    out[n++] = '*';
            continue;
        }
        out[n++] = *p;
    }
    out[n++] = 0;
    return n;
}

int cam_url_visible(char *out)
{
    char u[CAM_URL_MAX + 1];
    memcpy(u, url_guardada, sizeof u);
    u[CAM_URL_MAX] = 0;
    return url_sin_clave(u, out);
}

int cam_leer_estado(uint8_t *out)
{
    uint8_t e = estado;
    __mem_fence_acquire();
    memset(out, 0, 10);
    out[0] = e;
    if (e == CAM_LISTA) {
        out[1] = res;
        out[2] = detalle & 0xFF;
        out[3] = detalle >> 8;
        out[4] = ancho & 0xFF;
        out[5] = ancho >> 8;
        out[6] = alto & 0xFF;
        out[7] = alto >> 8;
        out[8] = ms & 0xFF;
        out[9] = ms >> 8;
    }
    return 10;
}

const uint8_t *cam_bloque(int i)
{
    if (i < 0 || i >= CAM_BLOQUES)
        return NULL;
    return pantalla + i * CAM_BLOQUE;
}

// ---- Nucleo 0: la conexion ----
//
// lwIP corre en las interrupciones del nucleo 0; la foto se arma en el bucle
// principal, que espera lo que llega sin el candado de lwIP. Lo recibido se
// queda en los pbuf de lwIP (en cola) hasta que el decodificador lo pide, y
// recien entonces se devuelve la ventana con tcp_recved: la camara no manda
// mas de lo que entra.

static struct tcp_pcb *pcb;
static struct pbuf *cola;               // lo recibido sin leer
static volatile bool conectada, cerrada, resuelto;
static ip_addr_t ip;
static volatile uint32_t t_ultimo;      // ms de lo ultimo recibido
static bool sin_tiempo;                 // se espero CAM_ESPERA_MS sin recibir

static err_t al_recibir(void *arg, struct tcp_pcb *p, struct pbuf *q, err_t err)
{
    (void)arg; (void)p; (void)err;
    if (!q) {
        cerrada = true;
        return ERR_OK;
    }
    if (cola)
        pbuf_cat(cola, q);
    else
        cola = q;
    t_ultimo = to_ms_since_boot(get_absolute_time());
    return ERR_OK;
}

static void al_error(void *arg, err_t err)
{
    (void)arg; (void)err;
    pcb = NULL;             // lwIP ya lo libero
    cerrada = true;
}

static err_t al_conectar(void *arg, struct tcp_pcb *p, err_t err)
{
    (void)arg; (void)p;
    if (err != ERR_OK)
        cerrada = true;
    else
        conectada = true;
    return ERR_OK;
}

static void al_resolver(const char *nombre, const ip_addr_t *dir, void *arg)
{
    (void)nombre; (void)arg;
    if (dir)
        ip = *dir;
    else
        ip_addr_set_zero(&ip);
    resuelto = true;
}

static void cortar(void)
{
    cyw43_arch_lwip_begin();
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        if (tcp_close(pcb) != ERR_OK)
            tcp_abort(pcb);
        pcb = NULL;
    }
    if (cola) {
        pbuf_free(cola);
        cola = NULL;
    }
    cyw43_arch_lwip_end();
}

// Espera a que listo() o se pase el tiempo. false: se paso.
static bool esperar(volatile bool *listo, uint32_t ms_max)
{
    absolute_time_t fin = make_timeout_time_ms(ms_max);
    while (!*listo) {
        if (time_reached(fin))
            return false;
        sleep_us(200);
    }
    return true;
}

// Hasta n bytes de lo recibido (al menos 1, salvo que se termine). Con buf
// NULL los saltea.
static size_t tomar(void *ctx, uint8_t *buf, size_t n)
{
    (void)ctx;
    for (;;) {
        cyw43_arch_lwip_begin();
        size_t m = 0;
        if (cola) {
            m = cola->tot_len < n ? cola->tot_len : n;
            if (buf)
                pbuf_copy_partial(cola, buf, m, 0);
            cola = pbuf_free_header(cola, m);
            if (pcb)
                tcp_recved(pcb, m);
        }
        bool fin = cerrada && !cola;
        cyw43_arch_lwip_end();
        if (m || fin)
            return m;
        if (to_ms_since_boot(get_absolute_time()) - t_ultimo > CAM_ESPERA_MS) {
            sin_tiempo = true;
            return 0;
        }
        sleep_us(200);
    }
}

// Una linea de los encabezados, sin el fin de linea. false: se corto.
static bool linea(char *txt, int max)
{
    int n = 0;
    for (;;) {
        uint8_t c;
        if (tomar(NULL, &c, 1) == 0)
            return false;
        if (c == '\n')
            break;
        if (c != '\r' && n < max - 1)
            txt[n++] = c;
    }
    txt[n] = 0;
    return true;
}

static uint32_t ahora_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static uint32_t t_inicio, t_respuesta;  // al pedir; ms hasta la respuesta

static uint8_t capturar(const char *url_txt)
{
    url_t u;
    char host[HOST_MAX + 1];
    static char req[PEDIDO_MAX];

    if (!url_txt[0])
        return CAM_SIN_URL;
    if (!url_partir(url_txt, &u))
        return CAM_SIN_URL;
    if (wifi_estado() != WIFI_CONECTADO)
        return CAM_SIN_WIFI;
    strcpy(host, u.host);

    // El pedido
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\n",
                     u.ruta[0] ? u.ruta : "/", host);
    if (u.usuario && n < (int)sizeof req - 300) {
        char b64[(CAM_URL_MAX + 2) / 3 * 4 + 1];
        base64(u.usuario, u.n_usuario, b64);
        n += snprintf(req + n, sizeof req - n, "Authorization: Basic %s\r\n", b64);
        memset(b64, 0, sizeof b64);
    }
    n += snprintf(req + n, sizeof req - n,
                  "User-Agent: SDF-Pico\r\nConnection: close\r\n\r\n");
    if (n >= (int)sizeof req)
        return CAM_SIN_URL;

    // El nombre
    resuelto = conectada = cerrada = false;
    sin_tiempo = false;
    cyw43_arch_lwip_begin();
    err_t r = dns_gethostbyname(host, &ip, al_resolver, NULL);
    cyw43_arch_lwip_end();
    if (r == ERR_OK)
        resuelto = true;
    else if (r != ERR_INPROGRESS)
        return CAM_NOMBRE;
    if (!esperar(&resuelto, CAM_ESPERA_MS) || ip_addr_isany(&ip))
        return CAM_NOMBRE;

    // La conexion
    cyw43_arch_lwip_begin();
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb) {
        tcp_err(pcb, al_error);
        tcp_recv(pcb, al_recibir);
        if (tcp_connect(pcb, &ip, u.puerto, al_conectar) != ERR_OK) {
            tcp_abort(pcb);
            pcb = NULL;
        }
    }
    cyw43_arch_lwip_end();
    if (!pcb)
        return CAM_CONEXION;
    if (!esperar(&conectada, CAM_ESPERA_MS) || cerrada)
        return CAM_CONEXION;

    cyw43_arch_lwip_begin();
    r = pcb ? tcp_write(pcb, req, n, TCP_WRITE_FLAG_COPY) : ERR_CONN;
    if (r == ERR_OK)
        tcp_output(pcb);
    cyw43_arch_lwip_end();
    memset(req, 0, sizeof req);         // la clave en base64
    if (r != ERR_OK)
        return CAM_CONEXION;
    t_ultimo = ahora_ms();

    // La respuesta: "HTTP/1.x 200 ..." y los encabezados hasta la linea vacia
    char txt[80];
    if (!linea(txt, sizeof txt))
        return sin_tiempo ? CAM_TIEMPO : CAM_CONEXION;
    t_respuesta = ahora_ms() - t_inicio;
    const char *esp = strchr(txt, ' ');
    detalle = esp ? (uint16_t)atoi(esp + 1) : 0;
    if (strncmp(txt, "HTTP/", 5) != 0 || detalle != 200)
        return CAM_HTTP;
    do {
        if (!linea(txt, sizeof txt))
            return sin_tiempo ? CAM_TIEMPO : CAM_CONEXION;
    } while (txt[0]);

    uint8_t jres;
    int s = sc2_convertir(tomar, NULL, pantalla, &ancho, &alto, &jres);
    detalle = jres;
    if (s == SC2_TAMANO)
        return CAM_TAMANO;
    if (s != SC2_OK)
        return sin_tiempo ? CAM_TIEMPO : jres == 2 /* JDR_INP */ ? CAM_CONEXION : CAM_JPEG;
    return CAM_OK;
}

static void foto(void)
{
    char url[CAM_URL_MAX + 1];
    strcpy(url, p_url[0] ? p_url : url_guardada);
    t_inicio = ahora_ms();
    t_respuesta = 0;
    detalle = ancho = alto = 0;
    uint8_t r = capturar(url);
    cortar();
    memset(url, 0, sizeof url);
    uint32_t t = ahora_ms() - t_inicio;
    ms = t > 0xFFFF ? 0xFFFF : t;
    res = r;
    __mem_fence_release();
    estado = CAM_LISTA;
}

// ---- Nucleo 0 ----

void cam_iniciar(void)
{
    strncpy(url_guardada, ajustes_camara(), CAM_URL_MAX);
}

void cam_olvidar(void)
{
    // La foto corre en este mismo nucleo (cam_tarea): ahora no hay ninguna a
    // medias. Un pedido pendiente sale con CAM_SIN_URL.
    atendido_url = pedido_url;
    memset(g_url, 0, sizeof g_url);
    memset(p_url, 0, sizeof p_url);
    memset(url_guardada, 0, sizeof url_guardada);
    memset(pantalla, 0, sizeof pantalla);
    detalle = ancho = alto = ms = 0;
    res = CAM_SIN_URL;
}

static bool consola_espera;     // la consola pidio la foto: mostrar el resultado

static void informar(void)
{
    static const char *const que[] = {
        [CAM_OK] = "lista", [CAM_SIN_WIFI] = "sin Wi-Fi",
        [CAM_NOMBRE] = "el nombre no se resolvio", [CAM_CONEXION] = "fallo la conexion",
        [CAM_HTTP] = "la camara contesto", [CAM_JPEG] = "JPEG que no se entiende, error",
        [CAM_TAMANO] = "tamano no soportado", [CAM_TIEMPO] = "la camara dejo de mandar",
        [CAM_SIN_URL] = "no hay URL (o no es valida)",
    };
    printf("camara: %s", res < sizeof que / sizeof que[0] ? que[res] : "?");
    if (res == CAM_HTTP || res == CAM_JPEG)
        printf(" %u", detalle);
    if (ancho)
        printf(", %ux%u", ancho, alto);
    printf(", %u ms", ms);
    if (res == CAM_OK)
        printf(" (%lu hasta la respuesta)", (unsigned long)t_respuesta);
    printf("\n");
}

void cam_tarea(void)
{
    if (pedido_url != atendido_url) {
        __mem_fence_acquire();
        ajustes_poner_camara(g_url);
        strncpy(url_guardada, ajustes_camara(), CAM_URL_MAX);
        memset(g_url, 0, sizeof g_url);
        atendido_url = pedido_url;
    }
    uint32_t p = pedido;
    if (p != atendido) {
        __mem_fence_acquire();
        atendido = p;
        foto();
        if (consola_espera) {
            consola_espera = false;
            informar();
        }
    }
}

bool cam_comando(const char *linea_txt)
{
    if (strcmp(linea_txt, "camara") == 0) {
        char u[CAM_URL_MAX + 8];
        cam_url_visible(u);
        printf("camara: %s\n", u[0] ? u : "(sin URL: CAM /U desde MSX-DOS)");
        if (estado == CAM_LISTA)
            informar();
        return true;
    }
    if (strcmp(linea_txt, "camara foto") == 0) {
        // Desde el nucleo 0: si justo pide el MSX, uno de los dos se pierde
        consola_espera = true;
        if (cam_pedir("", 0) != CAM_PEDIDO_OK) {
            consola_espera = false;
            printf("camara: hay una foto en curso\n");
        }
        return true;
    }
    return false;
}
