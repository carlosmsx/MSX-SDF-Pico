// PLANETAS: el sistema solar con las orbitas reales, acelerado (PLANETAS.COM,
// docs/PLANETAS.md). La Pico resuelve la ecuacion de Kepler de los 8
// planetas, los proyecta con la vista inclinada y arma la pantalla; el MSX
// copia lo que vuelve, como en CUEVA (cueva.h, el mismo formato).
//
// La pantalla es SCREEN 2: las orbitas, el Sol y los textos van en el
// bitmap y los planetas son sprites de 8x8. La Pico guarda la imagen nueva y
// la que tiene la VRAM, y manda solo las celdas que cambiaron.

#pragma once
#include <stdint.h>

// Las dos imagenes (24 KB) no van en variables propias: viven en el buffer
// de la respuesta, despues de PLANETAS_RESPUESTA. Es io_buf de protocolo.c,
// que es de 127 KB para un DSKIO entero y mientras corre PLANETAS.COM no lo
// usa nadie mas. Un comando que no sea de PLANETAS lo pisa: hay que volver a
// empezar con CMD_PLANETASINI.
#define PLANETAS_RESPUESTA  (16 * 1024)
#define PLANETAS_MEMORIA    (4 * 6144)
#define PLANETAS_ENTRADA    10      // joystick y filas 0 a 8 del teclado

// CMD_PLANETASINI: los registros del VDP de SCREEN 2, las tablas enteras y
// los sprites. Arranca en la fecha del reloj (o en J2000 si no esta puesto).
uint32_t planetas_iniciar(uint8_t *buf);

// CMD_PLANETASCUAD: avanza un cuadro con el registro 14 del PSG y las filas
// 0 a 8 del teclado, tal cual (activos en 0); contesta el PSG, la tabla de
// sprites y las celdas del bitmap que cambiaron, hasta ~1.500 bytes.
uint32_t planetas_cuadro(const uint8_t *entrada, uint8_t *buf);
