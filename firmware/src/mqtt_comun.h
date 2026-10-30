// Lo que comparten mqtt.c (el nucleo 1, en la RAM) y mqtt_red.c (el
// nucleo 0, en la flash). Solo para esos dos.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mqtt.h"

#define HOST_MAX        63
#define USUARIO_MAX     63
#define CLAVE_MAX       63
#define CLIENTE_MAX     32
#define PUERTO_MQTT     1883

#define PED_TAM         1024    // potencia de 2: pedidos del MSX al nucleo 0
#define RX_TAM          2048    // potencia de 2: mensajes recibidos
#define PED_DATOS_MAX   (2 + MQ_TOPICO_MAX + 3 * MQ_TEXTO_MAX)  // CMD_MQTTPUB, ya en UTF-8

// mqtt://[usuario[:clave]@]broker[:puerto][/cliente], sin el mqtt://
// tambien. Sin cliente, el identificador es msx- y el numero de la Pico.
typedef struct {
    char host[HOST_MAX + 1];
    uint16_t puerto;
    char usuario[USUARIO_MAX + 1];
    char clave[CLAVE_MAX + 1];
    char cliente[CLIENTE_MAX + 1];
} url_t;

bool url_partir(const char *url, url_t *u);
int url_sin_clave(const char *url, char *out);

// Pedidos, del nucleo 1 (y de la consola) al 0: tipo, largo (L, H) y datos.
// Los ponen mqtt.c con un candado; los saca solo mqtt_tarea.
enum { P_CONECTAR, P_CERRAR, P_SUB, P_PUB, P_URL };
extern uint8_t mq_ped_buf[PED_TAM];
extern volatile uint32_t mq_ped_cab, mq_ped_cola;

// Mensajes recibidos, del nucleo 0 al 1: flags, largo, topico, largo, texto.
// Si no entra, se descarta y se cuenta en mq_perdidos.
extern uint8_t mq_rx_buf[RX_TAM];
extern volatile uint32_t mq_rx_cab;     // nucleo 0
extern volatile uint32_t mq_rx_cola;    // nucleo 1
extern volatile uint32_t mq_rx_inicio;  // nucleo 0: lo anterior es de otra conexion
extern volatile uint32_t mq_perdidos;   // nucleo 0

// Pedidos de CONN y CLOSE: mientras falte atender alguno, CMD_MQTTPOLL no
// muestra el estado ni los mensajes de la sesion anterior.
extern volatile uint32_t mq_con_pedidos;    // los pone ped_poner
extern volatile uint32_t mq_con_atendidos;  // nucleo 0
extern volatile bool mq_con_quiere;         // el ultimo fue CONN

// Los escribe el nucleo 0
extern volatile uint8_t mq_estado, mq_motivo;
extern volatile uint8_t mq_en_vuelo;    // publicaciones sin confirmar
extern char mq_url_guardada[MQ_URL_MAX + 1];

typedef struct {
    uint32_t recibidos, publicados, conexiones;
} mq_cont_t;
extern mq_cont_t mq_cont;
