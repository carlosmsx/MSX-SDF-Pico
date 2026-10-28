// Actualizacion del firmware desde los releases de GitHub, para SDF UPDATE
// (CMD_UPDATE y CMD_UPDPOLL, docs/ACTUALIZAR.md).
//
// El nucleo 1 pide y consulta, como en camara.h. El nucleo 0 pregunta a la
// API de GitHub cual es el ultimo release y, si se pide actualizar, baja
// sdf_pico.bin y sdf_pico.bin.sha256 de ese release por HTTPS (https.h).
// El .bin se graba en una zona libre de la flash (ACT_ZONA) y se verifica
// ahi: la suma SHA-256, que sea una imagen de RP2350 y que sea un SDF-Pico.
// Recien entonces se copia, sector por sector, sobre el firmware, y la Pico
// se reinicia con el nuevo.
//
// Si se corta la luz mientras copia, la Pico queda sin firmware: se vuelve a
// grabar el .uf2 por USB (BOOTSEL), que esta en la ROM y siempre anda.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ACT_REPO        "carlosmsx/MSX-SDF-Pico"
#define ACT_ARCHIVO     "sdf_pico.bin"          // y sdf_pico.bin.sha256
#define ACT_ZONA        (2u << 20)              // desde el comienzo de la flash
#define ACT_MAX         (1u << 20)              // el firmware mas grande que se acepta
#define ACT_VERSION_MAX 15                      // "v0.14" en GitHub

// Modo de CMD_UPDATE
#define ACT_CONSULTAR   0       // solo la ultima version de GitHub
#define ACT_ACTUALIZAR  1       // la baja, la verifica, la graba y reinicia
#define ACT_SI          0x5A    // el segundo parametro, para ACT_ACTUALIZAR

// Respuesta de CMD_UPDATE
#define ACT_PEDIDO_OK   0
#define ACT_OCUPADA     1       // hay una en curso
#define ACT_PEDIDO_MAL  2

// Estado de CMD_UPDPOLL, primer byte de la respuesta
#define ACT_LIBRE       0
#define ACT_EN_CURSO    1
#define ACT_LISTA       2

// Segundo byte: la fase, en curso
#define ACT_CONSULTANDO 1
#define ACT_BAJANDO     2
#define ACT_VERIFICANDO 3
#define ACT_GRABANDO    4       // despues, la Pico se reinicia

// Segundo byte: el resultado, lista. Del 1 al 8 son los de https.h.
#define ACT_OK          0
#define ACT_SIN_WIFI    1
#define ACT_NOMBRE      2
#define ACT_CONEXION    3
#define ACT_TLS         4       // el error de mbedTLS va en el detalle
#define ACT_HTTP        5       // el codigo va en el detalle (404: no hay release o archivo)
#define ACT_TIEMPO      6
#define ACT_URL         7
#define ACT_MEMORIA     8
#define ACT_RESPUESTA   9       // GitHub no dijo la version
#define ACT_TAMANO      10      // el .bin es demasiado grande o demasiado chico
#define ACT_SUMA        11      // no coincide con sdf_pico.bin.sha256
#define ACT_IMAGEN      12      // no es un firmware del SDF-Pico para RP2350
#define ACT_FLASH       13      // la zona de la flash no quedo como se grabo

// ---- Nucleo 1 ----

// Pide consultar o actualizar (con si = ACT_SI).
uint8_t act_pedir(uint8_t modo, uint8_t si);

// Escribe estado, fase o resultado, detalle (L, H), KB bajados (L, H), KB
// en total (L, H), si la de GitHub es mas nueva (1) o no (0), la version
// instalada y la de GitHub (ASCIIZ). Devuelve cuantos bytes escribio.
int act_leer_estado(uint8_t *out);

// ---- Nucleo 0 ----

void act_tarea(void);

// Comandos de la consola USB. true si la linea era para la actualizacion.
bool act_comando(const char *linea);
