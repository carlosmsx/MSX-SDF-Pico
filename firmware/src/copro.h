// Coprocesador de juegos, prueba (COPRO.COM, docs/COPRO.md): la Pico calcula
// y el Z80 solo copia al VDP lo que le llega por 00h. Corre en el nucleo 1,
// dentro del comando: cada respuesta tarda microsegundos.

#pragma once
#include <stdint.h>

#define COPRO_BLOQUES  64   // CMD_COPROBLQ: hasta 16 KB, la VRAM entera
#define COPRO_SPRITES  32

// CMD_COPROBLQ: n bloques de 256 bytes; el byte k del bloque j es
// k ^ j ^ semilla, para que el MSX verifique lo que llego. Devuelve el largo.
uint32_t copro_bloques(uint8_t n, uint8_t semilla, uint8_t *buf);

// CMD_COPROINI: reinicia la escena con n sprites y deja en buf el patron de
// 16x16 (32 bytes, como lo espera el VDP). Devuelve el largo.
uint32_t copro_iniciar(uint8_t n, uint8_t *buf);

// CMD_COPROCUAD: avanza un cuadro con la entrada del MSX (registro 14 del
// PSG y fila 8 del teclado, tal cual, activos en 0) y deja en buf la tabla
// de atributos de sprites (128 bytes). Devuelve el largo.
uint32_t copro_cuadro(uint8_t joy, uint8_t fila8, uint8_t *buf);
