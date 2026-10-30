// Cliente MQTT para MQTT.COM (CMD_MQTTCONN a CMD_MQTTVER, docs/MQTT.md).
//
// El cliente es el de lwIP (pico_lwip_mqtt) y corre en el nucleo 0. El
// nucleo 1 le pasa los pedidos (conectar, suscribir, publicar) por un anillo
// y saca los mensajes recibidos de otro, como red.c: nunca toca lwIP.
//
// Las suscripciones quedan anotadas: si la conexion se cae, el nucleo 0
// reconecta solo y se vuelve a suscribir. Los mensajes que el MSX no alcanza
// a leer se descartan (los nuevos) y CMD_MQTTPOLL lo avisa.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MQ_URL_MAX      200     // mqtt://usuario:clave@broker:puerto/cliente
#define MQ_TOPICO_MAX   127
#define MQ_TEXTO_MAX    255     // lo que entrega CMD_MQTTPOLL; lo demas se corta
#define MQ_SUBS         8       // suscripciones a la vez

// Respuesta de los pedidos (CMD_MQTTCONN, SUB, PUB, URL)
#define MQ_PEDIDO_OK    0
#define MQ_SIN_CONEXION 1       // CMD_MQTTPUB sin estar conectado
#define MQ_LLENO        2       // el anillo de pedidos esta lleno: probar de nuevo
#define MQ_MAL          3       // URL o topico no valido (o demasiado largo)

// Primer byte de CMD_MQTTPOLL: el estado
#define MQ_CONECTADO    0x01
#define MQ_CONECTANDO   0x02    // resolviendo el nombre, conectando o esperando para reintentar
#define MQ_CORTADA      0x04    // se corto o no pudo conectar: el motivo, en el segundo byte
#define MQ_MENSAJE      0x08    // sigue un mensaje
#define MQ_PERDIDOS     0x10    // se descartaron mensajes desde la consulta anterior
#define MQ_ENVIANDO     0x20    // hay publicaciones sin confirmar

// Segundo byte: el motivo del ultimo corte. Del 1 al 5, lo que contesto el
// broker (MQTT 3.1.1).
#define MQ_OK           0
#define MQ_VERSION      1
#define MQ_CLIENTE      2       // el broker no acepto el identificador
#define MQ_SERVIDOR     3       // el broker no esta disponible
#define MQ_USUARIO      4       // usuario o clave
#define MQ_NO_AUTORIZADO 5
#define MQ_SIN_WIFI     16
#define MQ_NOMBRE       17      // el nombre del broker no se resolvio
#define MQ_CONEXION     18      // no conecto, o se corto
#define MQ_TIEMPO       19      // el broker no contesto
#define MQ_SIN_URL      20      // no hay broker guardado
#define MQ_SUSCRIPCION  21      // el broker rechazo una suscripcion (sigue conectado)

// Bits del modo de CMD_MQTTSUB
#define MQ_QOS1         0x01
#define MQ_CANCELAR     0x80

// Bits de CMD_MQTTPUB
//      MQ_QOS1         0x01
#define MQ_RETENER      0x02
#define MQ_TEXTO        0x04    // el texto viene en el juego del MSX: pasarlo a UTF-8

// CMD_MQTTPOLL: con MQ_TEXTO el mensaje sale en el juego del MSX (UTF-8 a
// MSX, sin caracteres de control). Bits del byte de flags del mensaje:
#define MQ_CORTADO      0x01    // el topico o el texto no entraban

// ---- Nucleo 1 ----

// Conecta con la URL (n = 0: la guardada) o, con url NULL, desconecta y
// olvida las suscripciones.
uint8_t mqtt_conectar(const char *url, int n);
uint8_t mqtt_suscribir(uint8_t modo, const char *topico, int n);
uint8_t mqtt_publicar(uint8_t flags, const char *topico, int nt,
                      const uint8_t *texto, int nx);

// Guarda la URL en la flash (la graba el nucleo 0). n = 0 la olvida.
uint8_t mqtt_guardar_url(const char *url, int n);

// La URL guardada, con la clave cambiada por asteriscos (ASCIIZ). Devuelve
// cuantos bytes escribio, con el 0.
int mqtt_url_visible(char *out);

// Estado, motivo y, si hay, el mensaje mas viejo: flags, largo, topico,
// largo, texto. Devuelve cuantos bytes escribio (hasta 5 + MQ_TOPICO_MAX +
// MQ_TEXTO_MAX).
int mqtt_leer(uint8_t modo, uint8_t *out);

// ---- Nucleo 0 ----

void mqtt_iniciar(void);
// Desconecta y olvida la URL guardada (borrar todo).
void mqtt_olvidar(void);
void mqtt_tarea(void);

// Comandos de la consola USB. true si la linea era para MQTT.
bool mqtt_comando(const char *linea);
