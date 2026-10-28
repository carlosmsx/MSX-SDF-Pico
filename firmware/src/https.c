#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "https.h"
#include "raices.h"
#include "sc2.h"
#include "wifi.h"

#define HOST_MAX        63
#define REDIRECCIONES   5
#define LINEA_MAX       HTTPS_URL_MAX + 16  // "Location: " y la URL

int https_detalle;

// ---- URL ----
//
// https://host[:puerto]/ruta. La ruta queda dentro de la URL.

typedef struct {
    char host[HOST_MAX + 1];
    uint16_t puerto;
    const char *ruta;
} url_t;

static bool url_partir(const char *url, url_t *u)
{
    if (strncmp(url, "https://", 8) != 0)
        return false;
    const char *p = url + 8;
    const char *fin = p + strcspn(p, "/?");
    const char *dos = memchr(p, ':', fin - p);
    const char *fin_host = dos ? dos : fin;
    if (fin_host == p || fin_host - p > HOST_MAX)
        return false;
    memcpy(u->host, p, fin_host - p);
    u->host[fin_host - p] = 0;
    u->puerto = 443;
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

// ---- TCP ----
//
// Como camara.c: lwIP corre en las interrupciones del nucleo 0 y deja lo
// recibido en cola; la ventana se devuelve (tcp_recved) recien cuando
// mbedTLS lo lee.

static struct tcp_pcb *pcb;
static struct pbuf *cola;
static volatile bool conectada, cerrada, resuelto;
static ip_addr_t ip;
static volatile uint32_t t_ultimo;
static bool sin_tiempo;

static uint32_t ahora_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

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
    t_ultimo = ahora_ms();
    return ERR_OK;
}

static void al_error(void *arg, err_t err)
{
    (void)arg; (void)err;
    pcb = NULL;
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

static void tcp_cortar(void)
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

static int tcp_abrir(const url_t *u)
{
    if (wifi_estado() != WIFI_CONECTADO)
        return HTTPS_SIN_WIFI;
    resuelto = conectada = cerrada = false;
    sin_tiempo = false;
    cyw43_arch_lwip_begin();
    err_t r = dns_gethostbyname(u->host, &ip, al_resolver, NULL);
    cyw43_arch_lwip_end();
    if (r == ERR_OK)
        resuelto = true;
    else if (r != ERR_INPROGRESS)
        return HTTPS_NOMBRE;
    if (!esperar(&resuelto, HTTPS_ESPERA_MS) || ip_addr_isany(&ip))
        return HTTPS_NOMBRE;

    cyw43_arch_lwip_begin();
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb) {
        tcp_err(pcb, al_error);
        tcp_recv(pcb, al_recibir);
        if (tcp_connect(pcb, &ip, u->puerto, al_conectar) != ERR_OK) {
            tcp_abort(pcb);
            pcb = NULL;
        }
    }
    cyw43_arch_lwip_end();
    if (!pcb || !esperar(&conectada, HTTPS_ESPERA_MS) || cerrada)
        return HTTPS_CONEXION;
    t_ultimo = ahora_ms();
    return HTTPS_OK;
}

// Lo que manda y recibe mbedTLS. Esperan como tomar() de camara.c.

static int bio_mandar(void *ctx, const unsigned char *buf, size_t n)
{
    (void)ctx;
    absolute_time_t fin = make_timeout_time_ms(HTTPS_ESPERA_MS);
    for (;;) {
        cyw43_arch_lwip_begin();
        int m = -1;
        if (pcb && !cerrada) {
            size_t lugar = tcp_sndbuf(pcb);
            m = 0;
            if (lugar) {
                if (lugar > n)
                    lugar = n;
                if (tcp_write(pcb, buf, lugar, TCP_WRITE_FLAG_COPY) == ERR_OK) {
                    tcp_output(pcb);
                    m = lugar;
                }
            }
        }
        cyw43_arch_lwip_end();
        if (m < 0)
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        if (m > 0)
            return m;
        if (time_reached(fin)) {
            sin_tiempo = true;
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }
        sleep_us(200);
    }
}

static int bio_recibir(void *ctx, unsigned char *buf, size_t n)
{
    (void)ctx;
    for (;;) {
        cyw43_arch_lwip_begin();
        size_t m = 0;
        if (cola) {
            m = cola->tot_len < n ? cola->tot_len : n;
            pbuf_copy_partial(cola, buf, m, 0);
            cola = pbuf_free_header(cola, m);
            if (pcb)
                tcp_recved(pcb, m);
        }
        bool fin = cerrada && !cola;
        cyw43_arch_lwip_end();
        if (m)
            return m;
        if (fin)
            return 0;               // mbedTLS: MBEDTLS_ERR_SSL_CONN_EOF
        if (ahora_ms() - t_ultimo > HTTPS_ESPERA_MS) {
            sin_tiempo = true;
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }
        sleep_us(200);
    }
}

// ---- TLS ----
//
// La RAM libre no alcanza para todo lo que pide mbedTLS: el bufer de
// entrada (16 KB y algo, lo mas grande que pide) va en la banda de la
// conversion de la camara (sc2_prestar), y lo demas, con calloc.

static uint8_t *prestada;
static size_t n_prestada;
static bool prestada_en_uso;

static void *mem_pedir(size_t n, size_t t)
{
    if (t && n > SIZE_MAX / t)
        return NULL;
    size_t b = n * t;
    if (!prestada_en_uso && b > 8192 && b <= n_prestada) {
        prestada_en_uso = true;
        return prestada;            // en cero: sc2_prestar
    }
    return calloc(n, t);
}

static void mem_soltar(void *p)
{
    if (p && p == prestada) {
        memset(prestada, 0, n_prestada);
        prestada_en_uso = false;
    } else {
        free(p);
    }
}

static mbedtls_entropy_context entropia;
static mbedtls_ctr_drbg_context azar;
static mbedtls_x509_crt cas;
static mbedtls_ssl_config conf;
static mbedtls_ssl_context ssl;
static bool tls_activo;

static void tls_cerrar(void)
{
    if (!tls_activo)
        return;
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&cas);
    mbedtls_ctr_drbg_free(&azar);
    mbedtls_entropy_free(&entropia);
    tls_activo = false;
}

static int tls_falla(int err)
{
    if (sin_tiempo)
        return HTTPS_TIEMPO;
    https_detalle = -err;
    if (err == MBEDTLS_ERR_SSL_ALLOC_FAILED || err == MBEDTLS_ERR_X509_ALLOC_FAILED
        || err == MBEDTLS_ERR_MPI_ALLOC_FAILED || err == MBEDTLS_ERR_ECP_ALLOC_FAILED)
        return HTTPS_MEMORIA;
    if (err == MBEDTLS_ERR_SSL_CONN_EOF)
        return HTTPS_CONEXION;
    return HTTPS_TLS;
}

static int tls_abrir(const char *host)
{
    static const char quien[] = "SDF-Pico";
    if (!prestada) {
        prestada = sc2_prestar(&n_prestada);
        mbedtls_platform_set_calloc_free(mem_pedir, mem_soltar);
    }
    mbedtls_entropy_init(&entropia);
    mbedtls_ctr_drbg_init(&azar);
    mbedtls_x509_crt_init(&cas);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    tls_activo = true;

    int r = mbedtls_ctr_drbg_seed(&azar, mbedtls_entropy_func, &entropia,
                                  (const unsigned char *)quien, sizeof quien - 1);
    // Las raices se leen de la flash, sin copiarlas
    for (int i = 0; r == 0 && i < n_raices; i++)
        r = mbedtls_x509_crt_parse_der_nocopy(&cas, raices[i].der, raices[i].largo);
    if (r == 0)
        r = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0)
        return tls_falla(r);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cas, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &azar);
    r = mbedtls_ssl_setup(&ssl, &conf);
    if (r == 0)
        r = mbedtls_ssl_set_hostname(&ssl, host);
    if (r != 0)
        return tls_falla(r);
    mbedtls_ssl_set_bio(&ssl, NULL, bio_mandar, bio_recibir, NULL);

    while ((r = mbedtls_ssl_handshake(&ssl)) != 0)
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE)
            return tls_falla(r);
    return HTTPS_OK;
}

// ---- HTTP ----
//
// Los encabezados se leen de a un byte desde un bufer; lo que sobra del
// bufer es el principio del cuerpo, y lo entrega https_leer.

static uint8_t rb[1024];
static int rb_pos, rb_n;
static int32_t restan;          // del cuerpo, segun Content-Length; -1: hasta que corte

static int leer_tls(uint8_t *buf, int n)
{
    for (;;) {
        int r = mbedtls_ssl_read(&ssl, buf, n);
        if (r > 0)
            return r;
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF)
            return 0;
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE)
            return -tls_falla(r);
    }
}

// Una linea, sin el fin de linea; lo que no entra se pierde. Negativo: el
// error; 0: corto antes.
static int linea(char *txt, int max, bool *larga)
{
    int n = 0;
    *larga = false;
    for (;;) {
        if (rb_pos == rb_n) {
            int r = leer_tls(rb, sizeof rb);
            if (r <= 0)
                return r < 0 ? r : -HTTPS_CONEXION;
            rb_pos = 0;
            rb_n = r;
        }
        char c = rb[rb_pos++];
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        if (n < max - 1)
            txt[n++] = c;
        else
            *larga = true;
    }
    txt[n] = 0;
    return 1;
}

static bool empieza(const char *txt, const char *con)
{
    return strncasecmp(txt, con, strlen(con)) == 0;
}

static char destino[HTTPS_URL_MAX + 1];     // la URL pedida, y adonde redirige

// Una conexion: pide, lee el estado y los encabezados. El codigo HTTP queda
// en *codigo; si redirige (Location), la URL nueva queda en destino.
static int pedir(int *codigo)
{
    static char txt[LINEA_MAX];
    url_t u;
    if (!url_partir(destino, &u))
        return HTTPS_URL;
    int r = tcp_abrir(&u);
    if (r == HTTPS_OK)
        r = tls_abrir(u.host);
    if (r != HTTPS_OK)
        return r;

    int n = snprintf(txt, sizeof txt, "GET %s%s HTTP/1.0\r\nHost: %s\r\n"
                     "User-Agent: SDF-Pico\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                     u.ruta[0] == '/' ? "" : "/", u.ruta, u.host);
    if (n >= (int)sizeof txt)
        return HTTPS_URL;
    for (int hecho = 0; hecho < n;) {
        int m = mbedtls_ssl_write(&ssl, (const unsigned char *)txt + hecho, n - hecho);
        if (m > 0)
            hecho += m;
        else if (m != MBEDTLS_ERR_SSL_WANT_READ && m != MBEDTLS_ERR_SSL_WANT_WRITE)
            return tls_falla(m);
    }

    // "HTTP/1.x 200 ..." y los encabezados hasta la linea vacia
    bool larga;
    rb_pos = rb_n = 0;
    if ((r = linea(txt, sizeof txt, &larga)) <= 0)
        return -r;
    const char *esp = strchr(txt, ' ');
    *codigo = esp ? atoi(esp + 1) : 0;
    if (strncmp(txt, "HTTP/", 5) != 0) {
        https_detalle = 0;
        return HTTPS_HTTP;
    }
    restan = -1;
    bool hay_destino = false;
    for (;;) {
        if ((r = linea(txt, sizeof txt, &larga)) <= 0)
            return -r;
        if (!txt[0])
            break;
        if (empieza(txt, "Content-Length:"))
            restan = atol(txt + 15);
        else if (empieza(txt, "Location:")) {
            const char *v = txt + 9;
            while (*v == ' ')
                v++;
            // u.ruta apunta a destino, pero ya no hace falta
            if (larga || snprintf(destino, sizeof destino, "%s%s%s", v[0] == '/' ? "https://" : "",
                                  v[0] == '/' ? u.host : "", v) >= (int)sizeof destino)
                return HTTPS_URL;
            hay_destino = true;
        }
    }
    if (!hay_destino && *codigo != 200)
        *codigo = -*codigo;         // redirige sin decir adonde: un error mas
    return HTTPS_OK;
}

// ---- Lo de afuera ----

int https_get(const char *url, int32_t *largo)
{
    https_detalle = 0;
    if (strlen(url) > HTTPS_URL_MAX)
        return HTTPS_URL;
    strcpy(destino, url);
    for (int i = 0; i <= REDIRECCIONES; i++) {
        int codigo = 0;
        int r = pedir(&codigo);
        if (r != HTTPS_OK)
            return r;
        if (codigo == 200) {
            *largo = restan;
            return HTTPS_OK;
        }
        if (codigo < 301 || codigo > 308) {
            https_detalle = codigo < 0 ? -codigo : codigo;
            return HTTPS_HTTP;
        }
        https_cerrar();             // y a la URL nueva, en destino
    }
    return HTTPS_URL;
}

int https_leer(uint8_t *buf, int n)
{
    if (restan == 0)
        return 0;
    if (restan > 0 && n > restan)
        n = restan;
    int m;
    if (rb_pos < rb_n) {
        m = rb_n - rb_pos < n ? rb_n - rb_pos : n;
        memcpy(buf, rb + rb_pos, m);
        rb_pos += m;
    } else {
        m = leer_tls(buf, n);
        if (m < 0)
            return m;
        if (m == 0)
            return restan > 0 ? -HTTPS_CONEXION : 0;    // corto antes del largo
    }
    if (restan > 0)
        restan -= m;
    return m;
}

void https_cerrar(void)
{
    if (tls_activo && pcb && !cerrada)
        mbedtls_ssl_close_notify(&ssl);
    tls_cerrar();
    tcp_cortar();
    rb_pos = rb_n = 0;
}
