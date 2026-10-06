#include <stddef.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "ajustes.h"

// Anteultimo sector de la flash (4 KB), lejos del programa, en dos paginas:
// la red (el formato del firmware 0.4) y la zona horaria. Cada una con su
// marca y su suma: una pagina borrada o vieja vale lo de fabrica.
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

static red_t red;
static zona_t zona;

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

    memset(&red, 0, sizeof red);
    if (r->marca == MARCA_RED && r->suma == suma_de(r, offsetof(red_t, suma))
        && r->ssid[AJ_SSID_MAX] == 0 && r->clave[AJ_CLAVE_MAX] == 0)
        red = *r;

    zona.minutos = AJ_ZONA_INICIAL;
    if (z->marca == MARCA_ZONA && z->suma == suma_de(z, offsetof(zona_t, suma))
        && z->minutos >= -12 * 60 && z->minutos <= 14 * 60)
        zona.minutos = z->minutos;
}

static void grabar(void)
{
    static uint8_t paginas[2 * FLASH_PAGE_SIZE];
    memset(paginas, 0xFF, sizeof paginas);
    if (red.ssid[0]) {
        red.marca = MARCA_RED;
        red.suma = suma_de(&red, offsetof(red_t, suma));
        memcpy(paginas, &red, sizeof red);
    }
    zona.marca = MARCA_ZONA;
    zona.suma = suma_de(&zona, offsetof(zona_t, suma));
    memcpy(paginas + FLASH_PAGE_SIZE, &zona, sizeof zona);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OFFSET, paginas, sizeof paginas);
    restore_interrupts(ints);

    memset(paginas, 0, sizeof paginas);     // que la clave no quede en la RAM
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
