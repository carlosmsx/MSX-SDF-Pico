// El lado del nucleo 1 del cliente MQTT (mqtt.h): los comandos del MSX, los
// anillos y la conversion de los acentos. Va en la RAM; el cliente de lwIP,
// en mqtt_red.c.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "ajustes.h"
#include "mqtt.h"
#include "mqtt_comun.h"

// ---- La URL ----
//
// mqtt://[usuario[:clave]@]broker[:puerto][/cliente], sin el mqtt://
// tambien. Sin cliente, el identificador es msx- y el numero de la Pico.

bool url_partir(const char *url, url_t *u)
{
    memset(u, 0, sizeof *u);
    const char *p = url;
    if (strncmp(p, "mqtt://", 7) == 0)
        p += 7;
    else if (strstr(p, "://"))
        return false;           // mqtts:// u otro protocolo
    const char *fin = p + strcspn(p, "/");
    const char *arroba = NULL;
    for (const char *q = p; q < fin; q++)
        if (*q == '@')
            arroba = q;         // la ultima: la clave puede tener @
    if (arroba) {
        const char *dos = memchr(p, ':', arroba - p);
        const char *fin_u = dos ? dos : arroba;
        if (fin_u == p || fin_u - p > USUARIO_MAX)
            return false;
        memcpy(u->usuario, p, fin_u - p);
        if (dos) {
            if (arroba - dos - 1 > CLAVE_MAX)
                return false;
            memcpy(u->clave, dos + 1, arroba - dos - 1);
        }
        p = arroba + 1;
    }
    const char *dos = memchr(p, ':', fin - p);
    const char *fin_host = dos ? dos : fin;
    if (fin_host == p || fin_host - p > HOST_MAX)
        return false;
    memcpy(u->host, p, fin_host - p);
    u->puerto = PUERTO_MQTT;
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
    if (*fin == '/') {
        if (strlen(fin + 1) > CLIENTE_MAX)
            return false;
        strcpy(u->cliente, fin + 1);
    }
    return true;
}

// La clave va con asteriscos
int url_sin_clave(const char *url, char *out)
{
    url_t u;
    int n = 0;
    const char *p = url;
    if (!url[0] || !url_partir(url, &u) || !u.clave[0]) {
        strcpy(out, url);
        return strlen(url) + 1;
    }
    const char *arroba = strrchr(url, '@');
    const char *dos = strchr(strstr(url, "://") ? strstr(url, "://") + 3 : url, ':');
    for (; *p; p++) {
        if (p > dos && p < arroba) {
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

static bool topico_valido(const char *t, int n, bool comodines)
{
    if (n < 1 || n > MQ_TOPICO_MAX || memchr(t, 0, n))
        return false;
    return comodines || (!memchr(t, '+', n) && !memchr(t, '#', n));
}

// ---- El juego de caracteres del MSX ----
//
// De 80h a AFh es el de la pagina 437 de IBM; lo demas son graficos. Lo que
// no esta en el MSX sale como '?', salvo unas letras sin acento.

static const uint16_t msx_alto[48] = {
    0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
    0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xA2, 0xA3, 0xA5, 0x20A7, 0x192,
    0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0x2310, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB,
};

static uint8_t a_msx_1(uint32_t c)
{
    if (c == '\t' || c == '\n' || c == '\r')
        return ' ';
    if (c < 32 || c == 127)
        return '.';
    if (c < 128)
        return (uint8_t)c;
    for (int i = 0; i < 48; i++)
        if (msx_alto[i] == c)
            return 0x80 + i;
    switch (c) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: return 'A';
    case 0xC8: case 0xCA: case 0xCB:            return 'E';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'I';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: return 'O';
    case 0xD9: case 0xDA: case 0xDB:            return 'U';
    case 0xE3:                                  return 'a';
    case 0xF5:                                  return 'o';
    case 0xA0:                                  return ' ';
    case 0xB0:                                  return 0xA7;    // grados: la o volada
    case 0xB7:                                  return '.';
    case 0x2018: case 0x2019:                   return '\'';
    case 0x201C: case 0x201D:                   return '"';
    case 0x2013: case 0x2014:                   return '-';
    case 0x2026:                                return '.';
    default:                                    return '?';
    }
}

// UTF-8 a MSX. Nunca alarga: puede ser en el mismo lugar.
static int a_msx(const uint8_t *in, int n, uint8_t *out)
{
    int m = 0;
    for (int i = 0; i < n;) {
        uint8_t b = in[i];
        int largo = b < 0x80 ? 1 : b >= 0xC2 && b <= 0xDF ? 2
                  : b >= 0xE0 && b <= 0xEF ? 3 : b >= 0xF0 && b <= 0xF4 ? 4 : 0;
        uint32_t c = largo == 1 ? b : largo == 2 ? b & 0x1F : largo == 3 ? b & 0x0F : b & 0x07;
        bool bien = largo > 0 && i + largo <= n;
        for (int k = 1; bien && k < largo; k++) {
            if ((in[i + k] & 0xC0) != 0x80)
                bien = false;
            c = (c << 6) | (in[i + k] & 0x3F);
        }
        if (!bien) {
            out[m++] = '?';     // no es UTF-8 (Latin-1, o cortado)
            i++;
            continue;
        }
        out[m++] = a_msx_1(c);
        i += largo;
    }
    return m;
}

// MSX a UTF-8: hasta tres bytes por caracter. Devuelve cuantos escribio.
static int a_utf8(uint8_t b, uint8_t *out)
{
    uint32_t c = b < 0x80 ? b : b <= 0xAF ? msx_alto[b - 0x80] : '?';
    if (c < 0x80) {
        out[0] = c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = 0xC0 | (c >> 6);
        out[1] = 0x80 | (c & 0x3F);
        return 2;
    }
    out[0] = 0xE0 | (c >> 12);
    out[1] = 0x80 | ((c >> 6) & 0x3F);
    out[2] = 0x80 | (c & 0x3F);
    return 3;
}

// ---- Pedidos, del nucleo 1 (y de la consola) al 0 ----
//
// Cada pedido es tipo, largo (L, H) y datos. Los ponen el nucleo 1 y la
// consola (nucleo 0): el candado los separa. Los saca solo mqtt_tarea.

uint8_t mq_ped_buf[PED_TAM];
volatile uint32_t mq_ped_cab, mq_ped_cola;
static spin_lock_t *candado;

// a y b van seguidos; con utf8, b pasa del juego del MSX a UTF-8. Nada de
// bufers grandes en la pila: la del nucleo 1 es de 2 KB.
static bool ped_poner(uint8_t tipo, const uint8_t *a, int na, const uint8_t *b, int nb,
                      bool utf8)
{
    uint8_t u[3];
    int n = na;
    for (int i = 0; i < nb; i++)
        n += utf8 ? a_utf8(b[i], u) : 1;

    uint32_t s = spin_lock_blocking(candado);
    uint32_t cab = mq_ped_cab;
    bool cabe = PED_TAM - (cab - mq_ped_cola) >= (uint32_t)(3 + n);
    if (cabe) {
        mq_ped_buf[cab++ & (PED_TAM - 1)] = tipo;
        mq_ped_buf[cab++ & (PED_TAM - 1)] = n & 0xFF;
        mq_ped_buf[cab++ & (PED_TAM - 1)] = n >> 8;
        for (int i = 0; i < na; i++)
            mq_ped_buf[cab++ & (PED_TAM - 1)] = a[i];
        for (int i = 0; i < nb; i++) {
            int m = utf8 ? a_utf8(b[i], u) : (u[0] = b[i], 1);
            for (int k = 0; k < m; k++)
                mq_ped_buf[cab++ & (PED_TAM - 1)] = u[k];
        }
        if (tipo == P_CONECTAR || tipo == P_CERRAR) {
            mq_con_quiere = tipo == P_CONECTAR;
            mq_con_pedidos = mq_con_pedidos + 1;
        }
        __mem_fence_release();
        mq_ped_cab = cab;
    }
    spin_unlock(candado, s);
    return cabe;
}

// ---- Mensajes recibidos, del nucleo 0 al 1 ----
//
// Cada mensaje es flags, largo, topico, largo, texto. Si no entra, se
// descarta y se cuenta.

uint8_t mq_rx_buf[RX_TAM];
volatile uint32_t mq_rx_cab, mq_rx_cola, mq_rx_inicio, mq_perdidos;
volatile uint32_t mq_con_pedidos, mq_con_atendidos;
volatile bool mq_con_quiere;
static uint32_t perdidos_vistos;        // nucleo 1

volatile uint8_t mq_estado, mq_motivo, mq_en_vuelo;
char mq_url_guardada[MQ_URL_MAX + 1];
mq_cont_t mq_cont;

// ---- Nucleo 1 ----

uint8_t mqtt_conectar(const char *url, int n)
{
    if (!url)
        return ped_poner(P_CERRAR, NULL, 0, NULL, 0, false) ? MQ_PEDIDO_OK : MQ_LLENO;
    char u[MQ_URL_MAX + 1];
    url_t partes;
    if (n > MQ_URL_MAX)
        return MQ_MAL;
    memcpy(u, url, n);
    u[n] = 0;
    bool bien = n == 0 || url_partir(u, &partes);
    memset(&partes, 0, sizeof partes);
    if (!bien)
        return MQ_MAL;
    bool ok = ped_poner(P_CONECTAR, (const uint8_t *)u, n, NULL, 0, false);
    memset(u, 0, sizeof u);
    return ok ? MQ_PEDIDO_OK : MQ_LLENO;
}

uint8_t mqtt_suscribir(uint8_t modo, const char *topico, int n)
{
    if (!topico_valido(topico, n, true))
        return MQ_MAL;
    return ped_poner(P_SUB, &modo, 1, (const uint8_t *)topico, n, false) ? MQ_PEDIDO_OK : MQ_LLENO;
}

uint8_t mqtt_publicar(uint8_t flags, const char *topico, int nt,
                      const uint8_t *texto, int nx)
{
    if (!topico_valido(topico, nt, false) || nx > MQ_TEXTO_MAX)
        return MQ_MAL;
    if (!(mq_estado & MQ_CONECTADO))
        return MQ_SIN_CONEXION;
    uint8_t d[2 + MQ_TOPICO_MAX];
    d[0] = flags;
    d[1] = nt;
    memcpy(d + 2, topico, nt);
    return ped_poner(P_PUB, d, 2 + nt, texto, nx, flags & MQ_TEXTO) ? MQ_PEDIDO_OK : MQ_LLENO;
}

uint8_t mqtt_guardar_url(const char *url, int n)
{
    char u[MQ_URL_MAX + 1];
    url_t partes;
    if (n > MQ_URL_MAX)
        return MQ_MAL;
    memcpy(u, url, n);
    u[n] = 0;
    bool bien = n == 0 || url_partir(u, &partes);
    memset(&partes, 0, sizeof partes);
    if (!bien)
        return MQ_MAL;
    bool ok = ped_poner(P_URL, (const uint8_t *)u, n, NULL, 0, false);
    memset(u, 0, sizeof u);
    return ok ? MQ_PEDIDO_OK : MQ_LLENO;
}

int mqtt_url_visible(char *out)
{
    char u[MQ_URL_MAX + 1];
    memcpy(u, mq_url_guardada, sizeof u);
    u[MQ_URL_MAX] = 0;
    int n = url_sin_clave(u, out);
    memset(u, 0, sizeof u);
    return n;
}

static uint8_t rx_byte(uint32_t i)
{
    return mq_rx_buf[i & (RX_TAM - 1)];
}

int mqtt_leer(uint8_t modo, uint8_t *out)
{
    if ((int32_t)(mq_rx_inicio - mq_rx_cola) > 0)
        mq_rx_cola = mq_rx_inicio;    // lo que quedo de una conexion anterior

    // Con un CONN o un CLOSE sin atender, el estado y los mensajes que hay
    // son de la sesion anterior: no se muestran.
    bool pendiente = mq_con_pedidos != mq_con_atendidos;
    __mem_fence_acquire();
    uint8_t e = pendiente ? (mq_con_quiere ? MQ_CONECTANDO : 0) : mq_estado;
    if (mq_en_vuelo)
        e |= MQ_ENVIANDO;
    uint32_t p = mq_perdidos;
    if (p != perdidos_vistos) {
        e |= MQ_PERDIDOS;
        perdidos_vistos = p;
    }
    out[1] = pendiente ? MQ_OK : mq_motivo;
    int n = 2;

    __mem_fence_acquire();
    uint32_t c = mq_rx_cola;
    if (!pendiente && c != mq_rx_cab) {
        e |= MQ_MENSAJE;
        out[n++] = rx_byte(c++);                // flags
        for (int parte = 0; parte < 2; parte++) {
            uint8_t largo = rx_byte(c++);
            uint8_t *d = out + n + 1;
            for (int i = 0; i < largo; i++)
                d[i] = rx_byte(c++);
            if (modo & MQ_TEXTO)
                largo = a_msx(d, largo, d);
            out[n] = largo;
            n += 1 + largo;
        }
        __mem_fence_release();
        mq_rx_cola = c;
    }
    out[0] = e;
    return n;
}

// ---- Nucleo 0, al arrancar (lo demas esta en mqtt_red.c) ----

void mqtt_iniciar(void)
{
    candado = spin_lock_init(spin_lock_claim_unused(true));
    strncpy(mq_url_guardada, ajustes_mqtt(), MQ_URL_MAX);
}
