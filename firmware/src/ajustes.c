#include <stddef.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "ajustes.h"

// Anteultimo sector de la flash (4 KB), lejos del programa, en cuatro
// paginas: la red (el formato del firmware 0.4), la zona horaria, la URL de
// la camara (desde el 0.11) y la del broker MQTT (desde el 0.16). Cada una con su marca y su suma: una pagina
// borrada o vieja vale lo de fabrica.
//
// No el ultimo: todo .uf2 para RP2350 empieza con un bloque en 0x10FFFF00
// (la solucion del SDK a la errata RP2350-E10), el final de una flash de
// 16 MB. En los 4 MB de la Pico 2 W las direcciones se repiten, ese bloque
// cae en el ultimo sector y el bootrom lo borra en cada grabacion.
//
// Grabar sin detener al nucleo 1 es seguro porque todo el programa corre de
// la RAM (copy_to_ram) y el nucleo 1 nunca lee la flash. Lo unico que queda
// en la flash es el firmware del CYW43, y solo se lee al arrancar el Wi-Fi.

#define OFFSET          (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)
#define MARCA_RED       0x49464957u     // "WIFI"
#define MARCA_ZONA      0x414E4F5Au     // "ZONA"
#define MARCA_CAM       0x524D4143u     // "CAMR"
#define MARCA_MQTT      0x5454514Du     // "MQTT"

typedef struct {
    uint32_t marca;
    char ssid[AJ_SSID_MAX + 1];
    char clave[AJ_CLAVE_MAX + 1];
    uint32_t suma;
} red_t;

typedef struct {
    uint32_t marca;
    int32_t minutos;
    uint32_t suma;
} zona_t;

typedef struct {
    uint32_t marca;
    char url[AJ_URL_MAX + 1];
    uint32_t suma;
} camara_t;

typedef struct {
    uint32_t marca;
    char url[AJ_MQTT_MAX + 1];
    uint32_t suma;
} mqtt_t;

static red_t red;
static zona_t zona;
static camara_t camara;
static mqtt_t mqtt;

static uint32_t suma_de(const void *datos, size_t n)
{
    const uint8_t *p = datos;
    uint32_t s = 0x5DF;
    for (size_t i = 0; i < n; i++)
        s = (s << 5) + s + p[i];
    return s;
}

void ajustes_leer(void)
{
    const red_t *r = (const red_t *)(XIP_BASE + OFFSET);
    const zona_t *z = (const zona_t *)(XIP_BASE + OFFSET + FLASH_PAGE_SIZE);
    const camara_t *c = (const camara_t *)(XIP_BASE + OFFSET + 2 * FLASH_PAGE_SIZE);
    const mqtt_t *m = (const mqtt_t *)(XIP_BASE + OFFSET + 3 * FLASH_PAGE_SIZE);

    memset(&red, 0, sizeof red);
    if (r->marca == MARCA_RED && r->suma == suma_de(r, offsetof(red_t, suma))
        && r->ssid[AJ_SSID_MAX] == 0 && r->clave[AJ_CLAVE_MAX] == 0)
        red = *r;

    zona.minutos = AJ_ZONA_INICIAL;
    if (z->marca == MARCA_ZONA && z->suma == suma_de(z, offsetof(zona_t, suma))
        && z->minutos >= -12 * 60 && z->minutos <= 14 * 60)
        zona.minutos = z->minutos;

    memset(&camara, 0, sizeof camara);
    if (c->marca == MARCA_CAM && c->suma == suma_de(c, offsetof(camara_t, suma))
        && c->url[AJ_URL_MAX] == 0)
        camara = *c;

    memset(&mqtt, 0, sizeof mqtt);
    if (m->marca == MARCA_MQTT && m->suma == suma_de(m, offsetof(mqtt_t, suma))
        && m->url[AJ_MQTT_MAX] == 0)
        mqtt = *m;
}

static void grabar(void)
{
    static uint8_t paginas[4 * FLASH_PAGE_SIZE];
    memset(paginas, 0xFF, sizeof paginas);
    if (red.ssid[0]) {
        red.marca = MARCA_RED;
        red.suma = suma_de(&red, offsetof(red_t, suma));
        memcpy(paginas, &red, sizeof red);
    }
    zona.marca = MARCA_ZONA;
    zona.suma = suma_de(&zona, offsetof(zona_t, suma));
    memcpy(paginas + FLASH_PAGE_SIZE, &zona, sizeof zona);
    if (camara.url[0]) {
        camara.marca = MARCA_CAM;
        camara.suma = suma_de(&camara, offsetof(camara_t, suma));
        memcpy(paginas + 2 * FLASH_PAGE_SIZE, &camara, sizeof camara);
    }
    if (mqtt.url[0]) {
        mqtt.marca = MARCA_MQTT;
        mqtt.suma = suma_de(&mqtt, offsetof(mqtt_t, suma));
        memcpy(paginas + 3 * FLASH_PAGE_SIZE, &mqtt, sizeof mqtt);
    }

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OFFSET, paginas, sizeof paginas);
    restore_interrupts(ints);

    memset(paginas, 0, sizeof paginas);     // que las claves no queden en la RAM
}

const char *ajustes_ssid(void)
{
    return red.ssid;
}

const char *ajustes_clave(void)
{
    return red.clave;
}

int ajustes_zona(void)
{
    return zona.minutos;
}

const char *ajustes_camara(void)
{
    return camara.url;
}

const char *ajustes_mqtt(void)
{
    return mqtt.url;
}

void ajustes_poner_red(const char *ssid, const char *clave)
{
    red_t nueva;
    memset(&nueva, 0, sizeof nueva);
    if (ssid && ssid[0]) {
        strncpy(nueva.ssid, ssid, AJ_SSID_MAX);
        strncpy(nueva.clave, clave, AJ_CLAVE_MAX);
    }
    if (strcmp(nueva.ssid, red.ssid) == 0 && strcmp(nueva.clave, red.clave) == 0)
        return;
    red = nueva;
    memset(&nueva, 0, sizeof nueva);
    grabar();
}

void ajustes_poner_zona(int minutos)
{
    if (minutos == zona.minutos)
        return;
    zona.minutos = minutos;
    grabar();
}

void ajustes_poner_camara(const char *url)
{
    if (strcmp(url, camara.url) == 0)
        return;
    memset(&camara, 0, sizeof camara);
    strncpy(camara.url, url, AJ_URL_MAX);
    grabar();
}

void ajustes_poner_mqtt(const char *url)
{
    if (strcmp(url, mqtt.url) == 0)
        return;
    memset(&mqtt, 0, sizeof mqtt);
    strncpy(mqtt.url, url, AJ_MQTT_MAX);
    grabar();
}

void ajustes_borrar(void)
{
    memset(&red, 0, sizeof red);
    memset(&camara, 0, sizeof camara);
    memset(&mqtt, 0, sizeof mqtt);
    zona.minutos = AJ_ZONA_INICIAL;

    // Sin grabar nada despues: el sector queda en FFh, como una Pico nueva
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}
