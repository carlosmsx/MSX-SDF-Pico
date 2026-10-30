// El cliente MQTT del nucleo 0, con lwIP (mqtt.h). Va entero en la flash
// (ld/section_copy_to_ram_rodata.incl): nada de aca lo llama el nucleo 1.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "hardware/sync.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/mqtt_priv.h"
#include "ajustes.h"
#include "mqtt.h"
#include "mqtt_comun.h"
#include "wifi.h"

#define KEEPALIVE_S     60
#define CONECTAR_MS     15000
#define REINTENTO_MS    5000

// Todo lo que sigue corre con el candado de lwIP: en los callbacks y en
// mqtt_tarea, que lo toma.

enum { F_LIBRE, F_DNS, F_TCP, F_CONECTADO, F_ESPERA };

static mqtt_client_t cli;
static int fase = F_LIBRE;
static bool quiere;                     // el MSX pidio estar conectado: reintentar
static url_t actual;                    // con la clave: hace falta para reconectar
static uint32_t generacion;             // descarta las respuestas de DNS viejas
static absolute_time_t limite;

static struct {
    char topico[MQ_TOPICO_MAX + 1];
    uint8_t qos;
    bool usada, enviada;
} subs[MQ_SUBS];

// Traza para la consola (mqtt traza): los mensajes recibidos, como texto
#define TRAZA_TAM   512         // potencia de 2
static bool traza;
static char tr_buf[TRAZA_TAM];
static volatile uint32_t tr_cab, tr_cola;

static void tr_texto(const uint8_t *t, int n)
{
    for (int i = 0; i < n && tr_cab - tr_cola < TRAZA_TAM; i++)
        tr_buf[tr_cab++ & (TRAZA_TAM - 1)] = t[i] >= 32 && t[i] < 127 ? t[i] : '.';
}

// El mensaje que esta llegando: lwIP entrega el topico y despues el texto
// de a pedazos.
static uint8_t in_topico[MQ_TOPICO_MAX];
static uint8_t in_texto[MQ_TEXTO_MAX];
static int in_lt, in_lx;
static bool in_cortado;

static void al_publicacion(void *arg, const char *topico, u32_t total)
{
    (void)arg; (void)total;
    size_t n = strlen(topico);
    in_cortado = n > MQ_TOPICO_MAX;
    if (in_cortado)
        n = MQ_TOPICO_MAX;
    memcpy(in_topico, topico, n);
    in_lt = n;
    in_lx = 0;
}

static void encolar(void)
{
    uint32_t hace_falta = 3 + in_lt + in_lx;
    uint32_t cab = mq_rx_cab;
    if (RX_TAM - (cab - mq_rx_cola) < hace_falta) {
        mq_perdidos = mq_perdidos + 1;
        return;
    }
    mq_rx_buf[cab++ & (RX_TAM - 1)] = in_cortado ? MQ_CORTADO : 0;
    mq_rx_buf[cab++ & (RX_TAM - 1)] = in_lt;
    for (int i = 0; i < in_lt; i++)
        mq_rx_buf[cab++ & (RX_TAM - 1)] = in_topico[i];
    mq_rx_buf[cab++ & (RX_TAM - 1)] = in_lx;
    for (int i = 0; i < in_lx; i++)
        mq_rx_buf[cab++ & (RX_TAM - 1)] = in_texto[i];
    __mem_fence_release();
    mq_rx_cab = cab;
    mq_cont.recibidos++;
    if (traza) {
        tr_texto(in_topico, in_lt);
        tr_texto((const uint8_t *)": ", 2);
        tr_texto(in_texto, in_lx);
        if (tr_cab - tr_cola < TRAZA_TAM)
            tr_buf[tr_cab++ & (TRAZA_TAM - 1)] = '\n';
    }
}

static void al_dato(void *arg, const u8_t *d, u16_t n, u8_t flags)
{
    (void)arg;
    for (int i = 0; i < n; i++) {
        if (in_lx < MQ_TEXTO_MAX)
            in_texto[in_lx++] = d[i];
        else
            in_cortado = true;
    }
    if (flags & MQTT_DATA_FLAG_LAST)
        encolar();
}

static void olvidar_subs(void)
{
    memset(subs, 0, sizeof subs);
}

// Se corto o no pudo conectar. Si el broker no rechazo la conexion (y el MSX
// la sigue queriendo), se reintenta.
static void perder(uint8_t m)
{
    mq_motivo = m;
    mq_en_vuelo = 0;               // lwIP descarta los pedidos sin avisar
    bool reintentar = quiere && m != MQ_VERSION && m != MQ_CLIENTE
                      && m != MQ_USUARIO && m != MQ_NO_AUTORIZADO && m != MQ_SIN_URL;
    if (reintentar) {
        fase = F_ESPERA;
        limite = make_timeout_time_ms(REINTENTO_MS);
        mq_estado = MQ_CONECTANDO | MQ_CORTADA;
    } else {
        quiere = false;
        fase = F_LIBRE;
        mq_estado = MQ_CORTADA;
    }
}

static void al_conectar(mqtt_client_t *c, void *arg, mqtt_connection_status_t st);

static void al_publicado(void *arg, err_t err)
{
    (void)arg; (void)err;
    if (mq_en_vuelo)
        mq_en_vuelo = mq_en_vuelo - 1;
}

static void al_suscrito(void *arg, err_t err)
{
    (void)arg;
    if (err != ERR_OK)
        mq_motivo = MQ_SUSCRIPCION;
}

static void empezar(const ip_addr_t *ip)
{
    static char id[CLIENTE_MAX + 1];
    if (actual.cliente[0]) {
        strcpy(id, actual.cliente);
    } else {
        char numero[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(numero, sizeof numero);
        snprintf(id, sizeof id, "msx-%s", numero + strlen(numero) - 8);
    }
    struct mqtt_connect_client_info_t info = {
        .client_id = id,
        .client_user = actual.usuario[0] ? actual.usuario : NULL,
        .client_pass = actual.clave[0] ? actual.clave : NULL,
        .keep_alive = KEEPALIVE_S,
    };
    if (mqtt_client_connect(&cli, ip, actual.puerto, al_conectar, NULL, &info) != ERR_OK) {
        perder(MQ_CONEXION);
        return;
    }
    // mqtt_client_connect borra el cliente: los callbacks van despues
    mqtt_set_inpub_callback(&cli, al_publicacion, al_dato, NULL);
    fase = F_TCP;
}

static void al_resolver(const char *nombre, const ip_addr_t *ip, void *arg)
{
    (void)nombre;
    if (fase != F_DNS || (uint32_t)(uintptr_t)arg != generacion)
        return;                 // el pedido ya se cancelo
    if (!ip) {
        perder(MQ_NOMBRE);
        return;
    }
    empezar(ip);
}

static void al_conectar(mqtt_client_t *c, void *arg, mqtt_connection_status_t st)
{
    (void)c; (void)arg;
    if (st == MQTT_CONNECT_ACCEPTED) {
        fase = F_CONECTADO;
        mq_estado = MQ_CONECTADO;
        mq_motivo = MQ_OK;
        mq_cont.conexiones++;
        for (int i = 0; i < MQ_SUBS; i++)
            subs[i].enviada = false;
        return;
    }
    perder(st <= MQ_NO_AUTORIZADO ? (uint8_t)st
           : st == MQTT_CONNECT_TIMEOUT ? MQ_TIEMPO : MQ_CONEXION);
}

static void arrancar(void)
{
    if (wifi_estado() != WIFI_CONECTADO) {
        perder(MQ_SIN_WIFI);
        return;
    }
    mq_estado = MQ_CONECTANDO | (mq_estado & MQ_CORTADA);
    fase = F_DNS;
    limite = make_timeout_time_ms(CONECTAR_MS);
    generacion++;
    ip_addr_t ip;
    err_t r = dns_gethostbyname(actual.host, &ip, al_resolver,
                                (void *)(uintptr_t)generacion);
    if (r == ERR_OK)
        empezar(&ip);
    else if (r != ERR_INPROGRESS)
        perder(MQ_NOMBRE);
}

static void cortar(void)
{
    if (fase == F_TCP || fase == F_CONECTADO)
        mqtt_disconnect(&cli);  // sin callback
    fase = F_LIBRE;
    mq_en_vuelo = 0;
    generacion++;
}

static void conectar(const char *url)
{
    cortar();
    olvidar_subs();
    mq_rx_inicio = mq_rx_cab;
    mq_estado = 0;
    mq_motivo = MQ_OK;
    quiere = true;
    if (!url[0])
        url = mq_url_guardada;
    if (!url[0] || !url_partir(url, &actual)) {
        perder(MQ_SIN_URL);
        return;
    }
    arrancar();
}

static void cerrar(void)
{
    cortar();
    olvidar_subs();
    mq_rx_inicio = mq_rx_cab;
    memset(&actual, 0, sizeof actual);
    quiere = false;
    mq_estado = 0;
    mq_motivo = MQ_OK;
}

static int buscar_sub(const char *t)
{
    for (int i = 0; i < MQ_SUBS; i++)
        if (subs[i].usada && strcmp(subs[i].topico, t) == 0)
            return i;
    return -1;
}

// false: lwIP no tiene lugar ahora, hay que reintentar el pedido
static bool sub(uint8_t modo, const char *t)
{
    int i = buscar_sub(t);
    if (modo & MQ_CANCELAR) {
        if (i < 0)
            return true;
        if (fase == F_CONECTADO && subs[i].enviada
            && mqtt_unsubscribe(&cli, t, NULL, NULL) == ERR_MEM)
            return false;
        subs[i].usada = false;
        return true;
    }
    if (i < 0) {
        for (i = 0; i < MQ_SUBS && subs[i].usada; i++)
            ;
        if (i == MQ_SUBS) {
            mq_motivo = MQ_SUSCRIPCION;    // no hay lugar
            return true;
        }
        strcpy(subs[i].topico, t);
        subs[i].usada = true;
    }
    subs[i].qos = modo & MQ_QOS1;
    subs[i].enviada = false;
    return true;
}

static bool pub(const uint8_t *d, int n)
{
    if (fase != F_CONECTADO)
        return true;            // se perdio la conexion: se descarta
    char topico[MQ_TOPICO_MAX + 1];
    uint8_t flags = d[0], nt = d[1];
    memcpy(topico, d + 2, nt);
    topico[nt] = 0;
    err_t r = mqtt_publish(&cli, topico, d + 2 + nt, n - 2 - nt, flags & MQ_QOS1,
                           (flags & MQ_RETENER) != 0, al_publicado, NULL);
    if (r == ERR_MEM)
        return false;
    if (r == ERR_OK) {
        mq_en_vuelo = mq_en_vuelo + 1;
        mq_cont.publicados++;
    }
    return true;
}

// Atiende el pedido mas viejo. false si hay que dejarlo para despues.
static bool atender(void)
{
    static uint8_t d[PED_DATOS_MAX + 1];
    uint32_t c = mq_ped_cola;
    uint8_t tipo = mq_ped_buf[c++ & (PED_TAM - 1)];
    int n = mq_ped_buf[c++ & (PED_TAM - 1)];
    n |= mq_ped_buf[c++ & (PED_TAM - 1)] << 8;
    for (int i = 0; i < n; i++)
        d[i] = mq_ped_buf[(c + i) & (PED_TAM - 1)];
    d[n] = 0;

    bool hecho = true;
    switch (tipo) {
    case P_CONECTAR: conectar((char *)d); break;
    case P_CERRAR:   cerrar(); break;
    case P_SUB:      hecho = sub(d[0], (char *)d + 1); break;
    case P_PUB:      hecho = pub(d, n); break;
    case P_URL:
        ajustes_poner_mqtt((char *)d);
        strncpy(mq_url_guardada, ajustes_mqtt(), MQ_URL_MAX);
        break;
    }
    if (hecho) {
        for (int i = 0; i < n; i++)     // las claves no quedan en la RAM
            mq_ped_buf[(c + i) & (PED_TAM - 1)] = 0;
        mq_ped_cola = c + n;
        if (tipo == P_CONECTAR || tipo == P_CERRAR) {
            __mem_fence_release();      // el estado nuevo, antes
            mq_con_atendidos = mq_con_atendidos + 1;
        }
    }
    memset(d, 0, n);
    return hecho;
}

static void suscribir_pendientes(void)
{
    for (int i = 0; i < MQ_SUBS; i++) {
        if (!subs[i].usada || subs[i].enviada)
            continue;
        if (mqtt_subscribe(&cli, subs[i].topico, subs[i].qos, al_suscrito, NULL) != ERR_OK)
            return;             // sin lugar en lwIP: en la proxima vuelta
        subs[i].enviada = true;
    }
}

static const char *texto_motivo(uint8_t m)
{
    switch (m) {
    case MQ_OK:            return "";
    case MQ_VERSION:       return "el broker no acepta MQTT 3.1.1";
    case MQ_CLIENTE:       return "el broker no acepto el identificador";
    case MQ_SERVIDOR:      return "el broker no esta disponible";
    case MQ_USUARIO:       return "usuario o clave";
    case MQ_NO_AUTORIZADO: return "no autorizado";
    case MQ_SIN_WIFI:      return "sin Wi-Fi";
    case MQ_NOMBRE:        return "el nombre no se resolvio";
    case MQ_CONEXION:      return "fallo la conexion";
    case MQ_TIEMPO:        return "el broker no contesto";
    case MQ_SIN_URL:       return "no hay broker (o la URL no es valida)";
    case MQ_SUSCRIPCION:   return "suscripcion rechazada";
    default:               return "?";
    }
}

static void mostrar_estado(uint8_t e, uint8_t m)
{
    printf("mqtt: %s", e & MQ_CONECTADO ? "conectado"
                       : e & MQ_CONECTANDO ? "conectando" : "sin conexion");
    if (m)
        printf(" (%s)", texto_motivo(m));
    printf("\n");
}

void mqtt_olvidar(void)
{
    cyw43_arch_lwip_begin();
    cerrar();
    cyw43_arch_lwip_end();
    memset(mq_url_guardada, 0, sizeof mq_url_guardada);
}

void mqtt_tarea(void)
{
    // Los cambios de estado, a la consola
    static uint8_t e_visto, m_visto;
    uint8_t e = mq_estado & (MQ_CONECTADO | MQ_CONECTANDO), m = mq_motivo;
    if (e != e_visto || m != m_visto) {
        mostrar_estado(e, m);
        e_visto = e;
        m_visto = m;
    }
    if (traza) {
        while (tr_cola != tr_cab)
            putchar(tr_buf[tr_cola++ & (TRAZA_TAM - 1)]);
    }

    if (mq_ped_cola == mq_ped_cab && fase == F_LIBRE)
        return;

    cyw43_arch_lwip_begin();
    while (mq_ped_cola != mq_ped_cab) {
        __mem_fence_acquire();
        if (!atender())
            break;
    }
    if ((fase == F_DNS || fase == F_TCP) && time_reached(limite)) {
        if (fase == F_TCP)
            mqtt_disconnect(&cli);
        perder(MQ_TIEMPO);
    }
    if (fase == F_ESPERA && time_reached(limite))
        arrancar();
    if (fase == F_CONECTADO)
        suscribir_pendientes();
    cyw43_arch_lwip_end();
}

// ---- Consola ----

bool mqtt_comando(const char *linea)
{
    if (strcmp(linea, "mqtt") == 0) {
        char u[MQ_URL_MAX + 8];
        mqtt_url_visible(u);
        printf("mqtt: broker guardado %s\n", u[0] ? u : "(ninguno: MQTT /U desde MSX-DOS)");
        mostrar_estado(mq_estado, mq_motivo);
        if (actual.host[0])
            printf("mqtt: %s:%u\n", actual.host, actual.puerto);
        for (int i = 0; i < MQ_SUBS; i++)
            if (subs[i].usada)
                printf("mqtt: suscripto a %s (QoS %u)%s\n", subs[i].topico, subs[i].qos,
                       subs[i].enviada ? "" : ", pendiente");
        printf("mqtt: %lu conexiones, %lu recibidos, %lu perdidos, %lu publicados\n",
               (unsigned long)mq_cont.conexiones, (unsigned long)mq_cont.recibidos,
               (unsigned long)mq_perdidos, (unsigned long)mq_cont.publicados);
        return true;
    }
    if (strncmp(linea, "mqtt ", 5) != 0)
        return false;
    const char *a = linea + 5;
    uint8_t r = MQ_PEDIDO_OK;
    if (strcmp(a, "traza") == 0) {
        traza = !traza;
        printf("mqtt: traza %s (los mensajes recibidos)\n", traza ? "activada" : "apagada");
        return true;
    } else if (strncmp(a, "url ", 4) == 0) {
        const char *u = a + 4;
        int n = strcmp(u, "-") == 0 ? 0 : (int)strlen(u);
        r = mqtt_guardar_url(u, n);
        if (r == MQ_PEDIDO_OK)
            printf("mqtt: %s\n", n ? "broker guardado" : "broker olvidado");
    } else if (strcmp(a, "conectar") == 0) {
        r = mqtt_conectar("", 0);
    } else if (strncmp(a, "conectar ", 9) == 0) {
        r = mqtt_conectar(a + 9, strlen(a + 9));
    } else if (strcmp(a, "cerrar") == 0) {
        r = mqtt_conectar(NULL, 0);
    } else if (strncmp(a, "sub ", 4) == 0) {
        r = mqtt_suscribir(0, a + 4, strlen(a + 4));
    } else if (strncmp(a, "unsub ", 6) == 0) {
        r = mqtt_suscribir(MQ_CANCELAR, a + 6, strlen(a + 6));
    } else if (strncmp(a, "pub ", 4) == 0) {
        const char *sp = strchr(a + 4, ' ');
        const char *texto = sp ? sp + 1 : "";
        int nt = sp ? sp - (a + 4) : (int)strlen(a + 4);
        r = mqtt_publicar(0, a + 4, nt, (const uint8_t *)texto, strlen(texto));
    } else {
        printf("uso: mqtt | mqtt url <URL> | mqtt url - | mqtt conectar [URL] | mqtt cerrar\n"
               "     mqtt sub <topico> | mqtt unsub <topico> | mqtt pub <topico> <texto>\n"
               "     mqtt traza\n");
        return true;
    }
    if (r != MQ_PEDIDO_OK)
        printf("mqtt: %s\n", r == MQ_MAL ? "URL o topico no valido"
               : r == MQ_SIN_CONEXION ? "sin conexion" : "ocupado, probar de nuevo");
    return true;
}
