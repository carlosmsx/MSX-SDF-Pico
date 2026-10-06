// Wi-Fi para WIFI.COM (CMD_WIFISTAT, CMD_WIFISCAN, CMD_WIFIREDES y
// CMD_WIFIJOIN, docs/WIFI.md).
//
// Lo mismo que CALL WIFI, pero el programa de DOS arma la pantalla: el
// firmware solo da el estado, la lista de redes y conecta. Como el Wi-Fi vive
// en el nucleo 0, el nucleo 1 pasa el pedido por un buzon y deja OCUPADO
// hasta la respuesta, igual que menu_wifi.c.

#pragma once
#include <stdint.h>

// Resultado de la ultima conexion pedida, en CMD_WIFISTAT
#define WDOS_EN_CURSO   0
#define WDOS_CONECTO    1
#define WDOS_FALLO      2

// Respuesta de CMD_WIFISCAN y CMD_WIFIJOIN
#define WDOS_OK         0
#define WDOS_APAGADO    1   // el CYW43 no contesto al arrancar
#define WDOS_MAL        2   // nombre o clave fuera de medida

// Marcas de cada red en CMD_WIFIREDES
#define WDOS_ABIERTA    0x01
#define WDOS_GUARDADA   0x02

// Nucleo 1: pasa un pedido (el comando y sus parametros). Nucleo 1: la
// respuesta, o -1 si todavia no esta.
void wdos_pedir(uint8_t cmd, const uint8_t *par, int n);
int wdos_respuesta(const uint8_t **datos);

// Nucleo 0: atiende el pedido pendiente, si hay.
void wdos_tarea(void);
