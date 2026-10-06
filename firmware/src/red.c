#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "red.h"
#include "wifi.h"

#define RX_TAM      16384   // potencia de 2, y mayor que TCP_WND
#define TX_TAM      2048
#define MSG_TAM     512     // potencia de 2: respuestas del modem
#define HOST_MAX    63
#define RAFAGA      120     // lo maximo que se junta sin que el MSX pregunte (el bufer de RetroTerm)
#define CONECTAR_MS 15000

#define BAUDIOS_INI 57600   // la velocidad de linea de los servidores Turbo56K

#define TXT_CONNECT     "\r\nCONNECT 57600\r\n"
#define TXT_NO_CARRIER  "\r\nNO CARRIER\r\n"

// ---- Anillos entre los nucleos ----

static uint8_t rx_buf[RX_TAM];
static volatile uint32_t rx_cab;        // escribe el nucleo 0 (lwIP)
static volatile uint32_t rx_cola;       // escribe el nucleo 1 (el bus)
static uint32_t rx_ackeado;             // nucleo 0: ya devuelto a TCP

static uint8_t tx_buf[TX_TAM];
static volatile uint32_t tx_cab;        // nucleo 1
static volatile uint32_t tx_cola;       // nucleo 0

// Lo que dice el modem (eco, OK, CONNECT): sale antes que los datos del
// socket. Va aparte para no confundir las cuentas de TCP.
static char msg_buf[MSG_TAM];
static volatile uint32_t msg_cab;       // nucleo 0
static volatile uint32_t msg_cola;      // nucleo 1

// NO CARRIER sale despues de los datos que quedaban: un contador de pedidos
// (nucleo 0) y otro de entregados (nucleo 1).
static volatile uint32_t nc_pedidos, nc_entregados;

// ---- Pedido de conexion, del nucleo 1 al 0 ----

static char p_host[HOST_MAX + 1];
static uint16_t p_puerto;
static bool p_conectar;
static volatile uint32_t pedido, atendido;

static volatile uint8_t estado;         // RED_*, lo escribe el nucleo 0
static volatile uint32_t rx_inicio;     // nucleo 0: lo anterior a esto es de un socket viejo
static volatile bool via_at;            // la conexion la pidio el MSX con atd: hay CONNECT y NO CARRIER,
                                        // y lo recibido sale dosificado

static volatile uint32_t baudios = BAUDIOS_INI;

// Traza para la consola (tcp traza): los bytes que van y vienen por el
// socket. Los anota el nucleo 0 y los imprime red_tarea.
#define TRAZA_TAM   4096        // potencia de 2
static bool traza;
static uint16_t tr_buf[TRAZA_TAM];  // direccion en el byte alto, dato en el bajo
static volatile uint32_t tr_cab, tr_cola;

static void tr_anotar(char dir, uint8_t b)
{
    if (!traza || tr_cab - tr_cola >= TRAZA_TAM)
        return;
    tr_buf[tr_cab & (TRAZA_TAM - 1)] = (uint16_t)(dir << 8) | b;
    tr_cab++;
}

static void tr_volcar(void)
{
    static char dir;
    static uint8_t n, dato[24];
    for (;;) {
        bool hay = tr_cola != tr_cab;
        uint16_t e = hay ? tr_buf[tr_cola & (TRAZA_TAM - 1)] : 0;
        if (n && (!hay || (char)(e >> 8) != dir || n == sizeof dato)) {
            printf("%c %2u:", dir, n);
            for (int i = 0; i < n; i++)
                printf(" %02X", dato[i]);
            printf("  ");
            for (int i = 0; i < n; i++)
                putchar(dato[i] >= 32 && dato[i] < 127 ? dato[i] : '.');
            putchar('\n');
            n = 0;
        }
        if (!hay)
            return;
        dir = (char)(e >> 8);
        dato[n++] = (uint8_t)e;
        tr_cola++;
    }
}

static struct {
    uint32_t recibidos, enviados, perdidos;
} cont;

// ---- Nucleo 1 ----

uint8_t red_conectar(const char *host, uint16_t puerto)
{
    size_t n = strlen(host);
    if (n > HOST_MAX || (n && puerto == 0))
        return RED_NOMBRE_MAL;
    memcpy(p_host, host, n + 1);
    p_puerto = puerto;
    p_conectar = n > 0;
    __mem_fence_release();
    pedido = pedido + 1;
    return RED_OK;
}

// Bytes que la linea habria entregado desde la ultima vez, como un balde:
// hasta RAFAGA acumulados. Los servidores Turbo56K mandan el audio a la
// velocidad del modem y la ROM lo reproduce a medida que llega. Solo con
// las conexiones de atd (RetroTerm): las de CMD_NETCONN (GET.COM) van a
// toda velocidad.
static uint32_t balde, fraccion;

static uint32_t fichas(void)
{
    static uint32_t ultimo;
    if (!via_at)
        return RED_MAX_RX;
    uint32_t ahora = time_us_32();
    uint32_t dt = ahora - ultimo;
    ultimo = ahora;
    if (dt > 100000)
        dt = 100000;
    fraccion += dt * (baudios / 10);
    balde += fraccion / 1000000;
    fraccion %= 1000000;
    if (balde > RAFAGA) {
        balde = RAFAGA;
        fraccion = 0;
    }
    return balde;
}

uint32_t red_io(const uint8_t *tx, uint32_t n, uint8_t *out, uint32_t max,
                uint8_t *est)
{
    if ((int32_t)(rx_inicio - rx_cola) > 0)
        rx_cola = rx_inicio;    // descarta lo que quedo de un socket anterior

    uint32_t cab = tx_cab;
    for (uint32_t i = 0; i < n; i++) {
        if (cab - tx_cola >= TX_TAM) {
            cont.perdidos += n - i;
            break;
        }
        tx_buf[cab++ & (TX_TAM - 1)] = tx[i];
    }
    __mem_fence_release();
    tx_cab = cab;

    uint32_t tope = fichas();
    if (tope > max)
        tope = max;
    uint32_t m = 0;
    __mem_fence_acquire();

    // 1. el modem: eco, OK, CONNECT
    uint32_t mc = msg_cola;
    while (m < tope && mc != msg_cab)
        out[m++] = msg_buf[mc++ & (MSG_TAM - 1)];
    __mem_fence_release();
    msg_cola = mc;

    // 2. los datos del socket
    uint32_t c = rx_cola;
    while (m < tope && c != rx_cab)
        out[m++] = rx_buf[c++ & (RX_TAM - 1)];
    __mem_fence_release();
    rx_cola = c;

    // 3. NO CARRIER, cuando ya no queda nada del socket
    static uint32_t nc_pos;
    while (m < tope && c == rx_cab && nc_entregados != nc_pedidos) {
        out[m++] = TXT_NO_CARRIER[nc_pos++];
        if (nc_pos == sizeof TXT_NO_CARRIER - 1) {
            nc_pos = 0;
            nc_entregados++;
        }
    }
    balde = m < balde ? balde - m : 0;

    uint8_t e = estado;
    if (pedido != atendido)
        e = p_conectar ? RED_CONECTANDO : 0;
    *est = e;
    return m;
}

// ---- Nucleo 0: lwIP ----
//
// Todo lo que sigue corre con el candado de lwIP: en las llamadas de lwIP
// (los callbacks) y en red_tarea (que lo toma).

static struct tcp_pcb *pcb;
static absolute_time_t limite_conexion;

static void msg_texto(const char *t)
{
    uint32_t cab = msg_cab;
    for (; *t; t++) {
        if (cab - msg_cola >= MSG_TAM)
            break;
        msg_buf[cab++ & (MSG_TAM - 1)] = *t;
    }
    __mem_fence_release();
    msg_cab = cab;
}

static void conectada(void)
{
    estado = RED_CONECTADO;
    if (via_at)
        msg_texto(TXT_CONNECT);
}

static void cortada(void)
{
    estado = RED_CORTADA;
    if (via_at) {
        via_at = false;
        nc_pedidos = nc_pedidos + 1;
    }
}

static void cortar_pcb(void)
{
    if (!pcb)
        return;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_sent(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK)
        tcp_abort(pcb);
    pcb = NULL;
}

static void al_error(void *arg, err_t err)
{
    (void)arg; (void)err;
    pcb = NULL;                 // lwIP ya lo libero
    cortada();
}

static err_t al_recibir(void *arg, struct tcp_pcb *p, struct pbuf *q, err_t err)
{
    (void)arg; (void)err;
    if (!q) {                   // el servidor cerro
        cortar_pcb();
        cortada();
        return ERR_OK;
    }
    uint32_t cab = rx_cab;
    for (struct pbuf *b = q; b; b = b->next) {
        const uint8_t *d = b->payload;
        for (uint16_t i = 0; i < b->len; i++) {
            if (cab - rx_cola >= RX_TAM) {      // no deberia: TCP_WND < RX_TAM
                cont.perdidos++;
                continue;
            }
            tr_anotar('R', d[i]);
            rx_buf[cab++ & (RX_TAM - 1)] = d[i];
        }
    }
    cont.recibidos += q->tot_len;
    __mem_fence_release();
    rx_cab = cab;
    pbuf_free(q);
    (void)p;
    return ERR_OK;              // tcp_recved se hace cuando el MSX consume
}

static err_t al_conectar(void *arg, struct tcp_pcb *p, err_t err)
{
    (void)arg; (void)p;
    if (err != ERR_OK) {
        cortar_pcb();
        cortada();
        return err;
    }
    conectada();
    return ERR_OK;
}

static void empezar_tcp(const ip_addr_t *ip)
{
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        cortada();
        return;
    }
    tcp_nagle_disable(pcb);
    tcp_err(pcb, al_error);
    tcp_recv(pcb, al_recibir);
    if (tcp_connect(pcb, ip, p_puerto, al_conectar) != ERR_OK) {
        cortar_pcb();
        cortada();
    }
}

static void al_resolver(const char *nombre, const ip_addr_t *ip, void *arg)
{
    (void)nombre; (void)arg;
    if (estado != RED_CONECTANDO || pcb)
        return;                 // el pedido ya se cancelo
    if (!ip) {
        cortada();
        return;
    }
    empezar_tcp(ip);
}

// Corta lo que hubiera y abre p_host:p_puerto, o solo corta si p_conectar es
// false. modem: lo pidio el MSX con atd (con CONNECT y NO CARRIER).
static void abrir(bool modem)
{
    cortar_pcb();
    via_at = false;
    if (!modem) {
        rx_inicio = rx_cab;     // lo que quede del socket anterior se descarta
        rx_ackeado = rx_inicio;
    } else {
        rx_ackeado = rx_cab;
    }
    estado = 0;
    if (!p_conectar)
        return;
    via_at = modem;
    if (wifi_estado() != WIFI_CONECTADO) {
        cortada();
        return;
    }
    estado = RED_CONECTANDO;
    limite_conexion = make_timeout_time_ms(CONECTAR_MS);
    ip_addr_t ip;
    err_t r = dns_gethostbyname(p_host, &ip, al_resolver, NULL);
    if (r == ERR_OK)
        empezar_tcp(&ip);
    else if (r != ERR_INPROGRESS)
        cortada();
}

// ---- El modem: comandos AT mientras no hay conexion ----
//
// RetroTerm no marca por su cuenta: manda "atd host:puerto" como a un modem
// Wi-Fi con Zimodem. Con una conexion abierta los bytes van al socket tal
// cual; sin ella, se arma una linea y se interpreta.

static char at_linea[96];
static int at_n;
static bool at_eco = true;

static void at_dial(const char *p)
{
    while (*p == 't' || *p == 'T' || *p == 'p' || *p == 'P' || *p == ' ')
        p++;
    bool comillas = *p == '"';
    if (comillas)
        p++;
    size_t n = 0;
    while (p[n] && p[n] != ':' && p[n] != '"' && p[n] != ' ')
        n++;
    long puerto = 23;
    const char *q = p + n;
    if (*q == '"' && q[1] == ':')
        q++;
    if (*q == ':') {
        puerto = strtol(q + 1, NULL, 10);
    }
    (void)comillas;
    if (n == 0 || n > HOST_MAX || puerto < 1 || puerto > 65535) {
        msg_texto("\r\nERROR\r\n");
        return;
    }
    memcpy(p_host, p, n);
    p_host[n] = 0;
    p_puerto = (uint16_t)puerto;
    p_conectar = true;
    abrir(true);
}

static void at_ejecutar(void)
{
    at_linea[at_n] = 0;
    const char *p = at_linea;
    if (at_n == 0)
        return;
    if (tolower((unsigned char)p[0]) != 'a' || tolower((unsigned char)p[1]) != 't') {
        msg_texto("\r\nERROR\r\n");
        return;
    }
    p += 2;
    while (*p) {
        char c = tolower((unsigned char)*p++);
        if (c == ' ')
            continue;
        if (c == 'd') {
            at_dial(p);
            return;
        }
        if (c == '&' && *p)
            p++;
        long n = 0;
        bool hay = false;
        while (isdigit((unsigned char)*p)) {
            n = n * 10 + (*p++ - '0');
            hay = true;
        }
        switch (c) {
        case 'e': at_eco = n != 0; break;
        case 'z': at_eco = true; break;
        case 'b': if (hay && n >= 1200 && n <= 1000000) baudios = n; break;
        default:  break;        // h, f, v, x, s... : se aceptan y no hacen nada
        }
    }
    msg_texto("\r\nOK\r\n");
}

static void at_byte(uint8_t b)
{
    if (b == '\r' || b == '\n') {
        if (at_eco)
            msg_texto("\r");
        at_ejecutar();
        at_n = 0;
        return;
    }
    if ((b == 8 || b == 127) && at_n > 0) {
        at_n--;
        if (at_eco)
            msg_texto("\b \b");
        return;
    }
    if (b >= 32 && b < 127 && at_n < (int)sizeof at_linea - 1) {
        at_linea[at_n++] = (char)b;
        if (at_eco) {
            char e[2] = {(char)b, 0};
            msg_texto(e);
        }
    }
}

void red_tarea(void)
{
    if (traza)
        tr_volcar();
    uint32_t p = pedido;
    bool nuevo = p != atendido;
    bool modem = !(estado & (RED_CONECTADO | RED_CONECTANDO)) && tx_cola != tx_cab;
    if (!nuevo && !modem && !pcb && estado != RED_CONECTANDO)
        return;

    cyw43_arch_lwip_begin();
    if (nuevo) {
        __mem_fence_acquire();
        abrir(false);
        tx_cola = tx_cab;
        atendido = p;
    }

    if (modem && !nuevo) {
        __mem_fence_acquire();
        uint32_t c = tx_cola, cab = tx_cab;
        while (c != cab && !(estado & (RED_CONECTADO | RED_CONECTANDO)))
            at_byte(tx_buf[c++ & (TX_TAM - 1)]);
        tx_cola = c;
    }

    if (estado == RED_CONECTANDO && time_reached(limite_conexion)) {
        cortar_pcb();
        cortada();
    }

    if (pcb) {
        // Lo que el MSX ya leyo se devuelve a la ventana de TCP
        int32_t resta = (int32_t)(rx_cola - rx_ackeado);    // <= 0 hasta que el MSX consuma
        if (resta > 0) {
            if (resta > 0xFFFF)
                resta = 0xFFFF;
            tcp_recved(pcb, resta);
            rx_ackeado += resta;
        }

        // Lo que escribio el MSX, al socket
        if (estado == RED_CONECTADO) {
            uint32_t c = tx_cola, cab = tx_cab;
            while (c != cab) {
                uint32_t sigue = TX_TAM - (c & (TX_TAM - 1));
                uint32_t n = cab - c;
                if (n > sigue)
                    n = sigue;
                uint32_t libre = tcp_sndbuf(pcb);
                if (n > libre)
                    n = libre;
                if (n == 0 || tcp_write(pcb, &tx_buf[c & (TX_TAM - 1)], n,
                                        TCP_WRITE_FLAG_COPY) != ERR_OK)
                    break;
                for (uint32_t i = 0; i < n; i++)
                    tr_anotar('T', tx_buf[(c + i) & (TX_TAM - 1)]);
                c += n;
                cont.enviados += n;
            }
            tx_cola = c;
            tcp_output(pcb);
        }
    }
    cyw43_arch_lwip_end();
}

// ---- Consola ----

bool red_comando(const char *linea)
{
    if (strcmp(linea, "tcp") == 0) {
        static const char *const nombre[] = {"sin conexion", "conectado", "conectando"};
        uint8_t e = estado;
        printf("tcp: %s%s, recibidos %lu, enviados %lu, perdidos %lu, %lu baudios\n",
               nombre[e & 3], e & RED_CORTADA ? " (cortada)" : "",
               (unsigned long)cont.recibidos, (unsigned long)cont.enviados,
               (unsigned long)cont.perdidos, (unsigned long)baudios);
        return true;
    }
    if (strncmp(linea, "tcp ", 4) != 0)
        return false;
    const char *a = linea + 4;
    if (strcmp(a, "traza") == 0) {
        traza = !traza;
        printf("tcp: traza %s (R = del servidor, T = del MSX)\n", traza ? "activada" : "apagada");
        return true;
    }
    if (strcmp(a, "cerrar") == 0) {
        red_conectar("", 0);
        return true;
    }
    if (strncmp(a, "baudios ", 8) == 0) {
        long b = strtol(a + 8, NULL, 10);
        if (b < 1200 || b > 1000000) {
            printf("uso: tcp baudios 57600 (entre 1200 y 1000000)\n");
            return true;
        }
        baudios = b;
        return true;
    }
    char host[HOST_MAX + 2];
    const char *sp = strchr(a, ' ');
    long puerto = sp ? strtol(sp + 1, NULL, 10) : 0;
    size_t n = sp ? (size_t)(sp - a) : 0;
    if (!sp || n == 0 || n > HOST_MAX || puerto < 1 || puerto > 65535) {
        printf("uso: tcp host puerto | tcp cerrar | tcp baudios N | tcp\n");
        return true;
    }
    memcpy(host, a, n);
    host[n] = 0;
    red_conectar(host, (uint16_t)puerto);
    return true;
}
