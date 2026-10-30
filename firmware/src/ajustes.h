// Ajustes que sobreviven al apagado, en el anteultimo sector de la flash de
// la Pico: la red Wi-Fi, la zona horaria, la URL de la camara (camara.h) y
// la del broker MQTT (mqtt.h).
// Solo desde el nucleo 0.
//
// Grabar un .uf2 no toca ese sector (el ultimo si, ver ajustes.c): los
// ajustes sobreviven a las actualizaciones del firmware. Las claves (la de
// la red y las de las URL) no se muestran nunca.

#pragma once
#include <stdbool.h>

#define AJ_SSID_MAX     32
#define AJ_CLAVE_MAX    63
#define AJ_ZONA_INICIAL (-180)      // Argentina, UTC-3
#define AJ_URL_MAX      200         // CAM_URL_MAX
#define AJ_MQTT_MAX     200         // MQ_URL_MAX

void ajustes_leer(void);

const char *ajustes_ssid(void);     // "" si no hay red guardada
const char *ajustes_clave(void);
int ajustes_zona(void);             // minutos respecto de UTC
const char *ajustes_camara(void);   // "" si no hay URL guardada
const char *ajustes_mqtt(void);     // "" si no hay broker guardado

// Graban el sector solo si algo cambio. ssid NULL o "" olvida la red.
void ajustes_poner_red(const char *ssid, const char *clave);
void ajustes_poner_zona(int minutos);
void ajustes_poner_camara(const char *url);
void ajustes_poner_mqtt(const char *url);

// Vuelve a lo de fabrica: borra el sector entero (red, clave, URL de la
// camara y del broker) y la zona queda en AJ_ZONA_INICIAL.
void ajustes_borrar(void);
