// CUEVA: un juego chico que corre entero en la Pico (CUEVA.COM, docs/CUEVA.md).
// El MSX es una terminal: manda la entrada de cada cuadro y copia lo que
// vuelve, sin saber nada del juego.
//
// Respuesta de los dos comandos:
//   11 bytes: registros 0 a 10 del PSG
//   tramos:   dir (H, L), largo (L, H) y los bytes, a la VRAM desde dir
//             FEh, registro, valor: un registro del VDP
//             FFh: fin

#pragma once
#include <stdint.h>

// CMD_CUEVAINI: arranca en la pantalla de presentacion; contesta los
// patrones, los colores, los sprites y la pantalla en blanco (SCREEN 1).
uint32_t cueva_iniciar(uint8_t *buf);

// CMD_CUEVACUAD: avanza un cuadro con el registro 14 del PSG (joystick 1) y
// las filas 5 (W, S y Z) y 2 (A y el punto, para disparar) del teclado, tal
// cual (activos en 0); contesta el PSG, la tabla de sprites, los patrones
// del borde de la cueva y la tabla de nombres.
uint32_t cueva_cuadro(uint8_t joy, uint8_t fila5, uint8_t fila2, uint8_t *buf);
