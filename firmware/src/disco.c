#include <string.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "disco.h"

#define BLOQUE          FLASH_SECTOR_SIZE           // 4096
#define SEC_POR_BLOQUE  (BLOQUE / SECTOR)
#define NINGUNO         0xFFFFFFFFu

static uint8_t cache[BLOQUE];
static uint32_t cache_bloque = NINGUNO;
static bool sucio;

static const uint8_t *xip(uint32_t offset)
{
    return (const uint8_t *)(XIP_BASE + DISCO_OFFSET + offset);
}

uint8_t disco_media(void)
{
    uint8_t m = *xip(SECTOR);   // primer byte de la primera FAT
    return (m == 0xF9 || m == 0xF8) ? m : 0;
}

uint32_t disco_sectores(void)
{
    return disco_media() == 0xF8 ? 720 : 1440;
}

const uint8_t *disco_ptr(uint32_t sector)
{
    return xip(sector * SECTOR);
}

// Corre con el otro nucleo detenido y sin interrupciones (flash_safe_execute).
static void grabar_bloque(void *arg)
{
    (void)arg;
    uint32_t off = DISCO_OFFSET + cache_bloque * BLOQUE;
    flash_range_erase(off, BLOQUE);
    flash_range_program(off, cache, BLOQUE);
}

bool disco_vaciar(void)
{
    if (!sucio)
        return true;
    sucio = false;
    return flash_safe_execute(grabar_bloque, NULL, 1000) == PICO_OK;
}

bool disco_escribir(uint32_t sector, const uint8_t *datos)
{
    uint32_t bloque = sector / SEC_POR_BLOQUE;
    bool ok = true;
    if (bloque != cache_bloque) {
        ok = disco_vaciar();
        memcpy(cache, xip(bloque * BLOQUE), BLOQUE);
        cache_bloque = bloque;
    }
    memcpy(cache + (sector % SEC_POR_BLOQUE) * SECTOR, datos, SECTOR);
    sucio = true;
    return ok;
}
