// Menu de la red Wi-Fi para CALL WIFI (CMD_WIFIMENU).
//
// El menu lo arma entero el firmware: la ROM es una terminal que manda cada
// tecla y muestra lo que vuelve (docs/PROTOCOLO.md). Corre en el nucleo 0,
// donde esta el Wi-Fi; el nucleo 1 solo pasa el pedido y espera la
// respuesta, con OCUPADO en el estado.

#pragma once
#include <stdint.h>

// Teclas especiales de CMD_WIFIMENU (las demas son las del CHGET)
#define MENU_NADA       0x00    // sin tecla: la ROM consulta por cambios
#define MENU_CORTAR     0xFE    // CTRL+STOP
#define MENU_EMPEZAR    0xFF

// Nucleo 1: pasa un pedido. Nucleo 1: la respuesta, o -1 si todavia no esta.
void menu_pedir(uint8_t tecla, uint8_t ancho, uint8_t filas);
int menu_respuesta(const uint8_t **datos);

// Nucleo 0: atiende el pedido pendiente, si hay.
void menu_tarea(void);
