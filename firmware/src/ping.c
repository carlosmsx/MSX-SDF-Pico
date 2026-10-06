#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/raw.h"
#include "ping.h"
#include "wifi.h"

#define HOST_MAX    63
#define ID          0xAF02
#define CARGA       32      // bytes de datos del eco, como el ping de Windows

// ---- Del nucleo 1 al 0 ----
//
// estado lo escribe el nucleo 1 al pedir (de LIBRE o LISTO a EN_CURSO) y el
// nucleo 0 al terminar (de EN_CURSO a LISTO): nunca a la vez, porque el
// nucleo 1 no pide con un eco en curso.

static char p_host[HOST_MAX + 1];
static volatile uint32_t pedido, atendido;
static volatile uint8_t estado;

// Resultado: lo escribe el nucleo 0 antes de pasar a LISTO
static uint8_t res;
static ip_addr_t ip;
static uint16_t rtt_ms;
static uint8_t ttl;

uint8_t ping_pedir(const char *host)
{
    size_t n = strlen(host);
    if (n == 0 || n > HOST_MAX)
        return PING_NOMBRE_MAL;
    if (estado == PING_EN_CURSO)
        return PING_OCUPADO;
    memcpy(p_host, host, n + 1);
    estado = PING_EN_CURSO;
    __mem_fence_release();
    pedido = pedido + 1;
    return PING_OK;
}

int ping_leer(uint8_t *out)
{
    uint8_t e = estado;
    __mem_fence_acquire();
    out[0] = e;
    out[1] = res;
    uint32_t a = ip4_addr_get_u32(ip_2_ip4(&ip));    // en orden de red: el primer octeto es el byte bajo
    out[2] = a & 0xFF;
    out[3] = (a >> 8) & 0xFF;
    out[4] = (a >> 16) & 0xFF;
    out[5] = a >> 24;
    out[6] = rtt_ms & 0xFF;
    out[7] = rtt_ms >> 8;
    out[8] = ttl;
    if (e != PING_LISTO) {
        // lo viejo no se muestra mientras hay uno en curso
        memset(out + 1, 0, 8);
    }
    return 9;
}

// ---- Nucleo 0 ----

static struct raw_pcb *pcb;
static uint16_t seq;
static absolute_time_t enviado, limite;

static void terminar(uint8_t r)
{
    res = r;
    __mem_fence_release();
    estado = PING_LISTO;
}

static u8_t al_recibir(void *arg, struct raw_pcb *p_, struct pbuf *p, const ip_addr_t *origen)
{
    (void)arg; (void)p_;
    if (estado != PING_EN_CURSO || p->len < sizeof(struct ip_hdr))
        return 0;
    const struct ip_hdr *iph = p->payload;
    u16_t hlen = IPH_HL_BYTES(iph);
    if (p->len < hlen + sizeof(struct icmp_echo_hdr))
        return 0;
    const struct icmp_echo_hdr *eco = (const struct icmp_echo_hdr *)((const u8_t *)p->payload + hlen);
    if (ICMPH_TYPE(eco) != ICMP_ER || eco->id != ID || eco->seqno != lwip_htons(seq)
        || !ip_addr_eq(origen, &ip))
        return 0;                           // no es la nuestra: que siga su camino
    int64_t us = absolute_time_diff_us(enviado, get_absolute_time());
    rtt_ms = us < 0 ? 0 : (us + 500) / 1000 > 0xFFFF ? 0xFFFF : (us + 500) / 1000;
    ttl = IPH_TTL(iph);
    pbuf_free(p);
    terminar(PING_RESPUESTA);
    return 1;
}

static void mandar(void)
{
    if (!pcb) {
        pcb = raw_new(IP_PROTO_ICMP);
        if (!pcb) {
            terminar(PING_TIEMPO);
            return;
        }
        raw_recv(pcb, al_recibir, NULL);
        raw_bind(pcb, IP_ADDR_ANY);
    }
    size_t tam = sizeof(struct icmp_echo_hdr) + CARGA;
    struct pbuf *p = pbuf_alloc(PBUF_IP, tam, PBUF_RAM);
    if (!p) {
        terminar(PING_TIEMPO);
        return;
    }
    struct icmp_echo_hdr *eco = p->payload;
    ICMPH_TYPE_SET(eco, ICMP_ECHO);
    ICMPH_CODE_SET(eco, 0);
    eco->chksum = 0;
    eco->id = ID;
    eco->seqno = lwip_htons(++seq);
    for (size_t i = 0; i < CARGA; i++)
        ((u8_t *)eco)[sizeof *eco + i] = 'a' + i % 23;
    eco->chksum = inet_chksum(eco, tam);
    enviado = get_absolute_time();
    raw_sendto(pcb, p, &ip);
    pbuf_free(p);
}

static void al_resolver(const char *nombre, const ip_addr_t *dir, void *arg)
{
    (void)nombre; (void)arg;
    if (estado != PING_EN_CURSO)
        return;
    if (!dir) {
        terminar(PING_NOMBRE);
        return;
    }
    ip = *dir;
    mandar();
}

static void empezar(void)
{
    ip_addr_set_zero(&ip);
    rtt_ms = 0;
    ttl = 0;
    limite = make_timeout_time_ms(PING_ESPERA_MS);
    if (wifi_estado() != WIFI_CONECTADO) {
        terminar(PING_SIN_WIFI);
        return;
    }
    ip_addr_t dir;
    err_t r = dns_gethostbyname(p_host, &dir, al_resolver, NULL);
    if (r == ERR_OK) {
        ip = dir;
        mandar();
    } else if (r != ERR_INPROGRESS) {
        terminar(PING_NOMBRE);
    }
}

void ping_tarea(void)
{
    uint32_t p = pedido;
    bool nuevo = p != atendido;
    if (!nuevo && estado != PING_EN_CURSO)
        return;

    cyw43_arch_lwip_begin();
    if (nuevo) {
        __mem_fence_acquire();
        empezar();
        atendido = p;
    } else if (estado == PING_EN_CURSO && time_reached(limite)) {
        terminar(PING_TIEMPO);
    }
    cyw43_arch_lwip_end();
}
