// Conexion TCP del MSX: el SDF-Pico hace de modem Wi-Fi (CMD_NETCONN y
// CMD_NETIO, docs/RED.md).
//
// lwIP corre en el nucleo 0; el bus, en el nucleo 1. Los dos se pasan los
// datos por anillos sin bloqueo (un escritor y un lector cada uno) y los
// pedidos de conexion por un contador, como menu_wifi.c: el nucleo 1 nunca
// toca lwIP.

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Bits del estado que devuelve CMD_NETIO
#define RED_CONECTADO   0x01    // hay un socket abierto
#define RED_CONECTANDO  0x02    // resolviendo el nombre o esperando al servidor
#define RED_CORTADA     0x04    // el servidor cerro (o fallo): NO CARRIER

// Resultados de CMD_NETCONN
#define RED_OK          0
#define RED_NOMBRE_MAL  2

// CMD_NETIO entrega hasta RED_MAX_RX bytes por llamada y recibe hasta
// RED_MAX_TX (lo que entra en los parametros del protocolo).
#define RED_MAX_RX      120
#define RED_MAX_TX      60

// ---- Nucleo 1 ----

// Pide conectar a host:puerto, o cortar si el host esta vacio. Devuelve
// RED_OK si el pedido se acepto (el resultado llega por el estado).
uint8_t red_conectar(const char *host, uint16_t puerto);

// Entrega hasta max bytes de lo recibido (dosificado a la velocidad de la
// linea, ver red_baudios) y recibe n bytes para enviar. Devuelve cuantos
// bytes dejo en out; el estado queda en *estado.
uint32_t red_io(const uint8_t *tx, uint32_t n, uint8_t *out, uint32_t max,
                uint8_t *estado);

// ---- Nucleo 0 ----

void red_tarea(void);

// Comandos de la consola USB. true si la linea era para la red.
bool red_comando(const char *linea);
