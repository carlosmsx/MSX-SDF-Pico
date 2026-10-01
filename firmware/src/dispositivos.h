// Dispositivos de BASIC (OPEN "RTC:"), en el nucleo 1.
//
// La ROM es un tubo: pregunta si un nombre es nuestro (CMD_DEVNAME) y despues
// pasa bytes. Todo lo que distingue a un dispositivo esta aca, asi que
// agregar uno es firmware y no obliga a regrabar la EEPROM. Por ahora hay
// uno, el reloj, con los formatos del SDF-1:
//
//   RTC:    2026-09-20 14:35:07        RTC:N   2026,9,20,14,35,7,1
//   RTC:D   2026-09-20                 RTC:W   DOM
//   RTC:T   14:35:07
//
// El formato va despues de los ":" o pegado al nombre ("RTCN:"). La lectura
// es un chorro infinito de lineas: cada LINE INPUT trae la hora de ese
// momento. EOF da -1 si la hora no es confiable (nunca se puso). PRINT #
// pone el reloj con cualquier separador: "2026-09-20 14:35:07".

#pragma once
#include <stdint.h>

uint8_t dev_nombre(const char *nombre);     // ID 0-3, o DEV_NONE
uint8_t dev_abrir(uint8_t id, uint8_t modo, const char *texto, uint8_t largo);
void dev_cerrar(uint8_t id, uint8_t modo);
uint8_t dev_leer(void);                     // un caracter, 0 = fin de datos
int dev_escribir(uint8_t c);                // -1 sin respuesta; con el CR, 0 o el error
uint8_t dev_eof(void);
