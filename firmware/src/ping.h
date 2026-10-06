// Ping para PING.COM (CMD_PINGSEND y CMD_PINGPOLL, docs/RED.md).
//
// Un eco ICMP por pedido. El nucleo 1 pide y consulta; el nucleo 0 resuelve el
// nombre, manda el eco por lwIP y espera la respuesta. La ROM no espera con
// OCUPADO: consulta con CMD_PINGPOLL hasta que el resultado esta listo.

#pragma once
#include <stdint.h>

// Estado de CMD_PINGPOLL, primer byte de la respuesta
#define PING_LIBRE      0   // no se pidio nada
#define PING_EN_CURSO   1
#define PING_LISTO      2

// Segundo byte, cuando esta listo
#define PING_RESPUESTA  0
#define PING_TIEMPO     1   // no contesto en PING_ESPERA_MS
#define PING_NOMBRE     2   // el nombre no se resolvio
#define PING_SIN_WIFI   3

// Resultados de CMD_PINGSEND
#define PING_OK         0
#define PING_OCUPADO    1   // hay un eco en curso
#define PING_NOMBRE_MAL 2   // el nombre es demasiado largo o esta vacio

#define PING_ESPERA_MS  3000

// ---- Nucleo 1 ----

uint8_t ping_pedir(const char *host);

// Escribe 9 bytes: estado, resultado, IP (4), tiempo en ms (L, H), TTL.
// Devuelve 9.
int ping_leer(uint8_t *out);

// ---- Nucleo 0 ----

void ping_tarea(void);
