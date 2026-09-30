// Disco en la flash de la Pico: una imagen .DSK de 720 o 360 KB a partir de
// 1 MB. Se carga con tools/dsk2uf2.py; en blanco se puede formatear desde el
// MSX (CALL FORMAT). Es el primer respaldo del driver, antes de la SD.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DISCO_OFFSET        (1u << 20)      // desde el comienzo de la flash
#define DISCO_SECTORES_MAX  1440            // 720 KB
#define SECTOR              512

// Media de la imagen: F9 (720 KB) o F8 (360 KB) segun el primer byte de la
// FAT; 0 si la zona no tiene una imagen.
uint8_t disco_media(void);
uint32_t disco_sectores(void);

// Puntero de lectura (XIP): sirve para el DMA. Antes de leer, disco_vaciar.
const uint8_t *disco_ptr(uint32_t sector);

// Escritura con cache de un bloque de 4 KB de flash (8 sectores). Graba
// cuando cambia de bloque o con disco_vaciar.
bool disco_escribir(uint32_t sector, const uint8_t *datos);
bool disco_vaciar(void);
