// Ajustes que sobreviven al apagado, en el anteultimo sector de la flash de
// la Pico: la red Wi-Fi y la zona horaria. Solo desde el nucleo 0.
//
// Grabar un .uf2 no toca ese sector (el ultimo si, ver ajustes.c): los
// ajustes sobreviven a las actualizaciones del firmware. La clave no se
// muestra nunca.

#pragma once
#include <stdbool.h>

#define AJ_SSID_MAX     32
#define AJ_CLAVE_MAX    63
#define AJ_ZONA_INICIAL (-180)      // Argentina, UTC-3

void ajustes_leer(void);

const char *ajustes_ssid(void);     // "" si no hay red guardada
const char *ajustes_clave(void);
int ajustes_zona(void);             // minutos respecto de UTC

// Graban el sector solo si algo cambio. ssid NULL o "" olvida la red.
void ajustes_poner_red(const char *ssid, const char *clave);
void ajustes_poner_zona(int minutos);
