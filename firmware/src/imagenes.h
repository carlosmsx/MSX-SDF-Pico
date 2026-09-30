// Imagenes .DSK de la tarjeta SD, montadas en A: y B:.
//
// Mismo comportamiento que el firmware del SDF-1: imagenes de 720 o 360 KB en
// la raiz de la SD, por su nombre corto 8.3; CALL SDFMOUNT/SDFUMOUNT las
// cambian y CALL SDFNEW crea una. Lo montado y la cantidad de drives se
// guardan en la SD (SDFPICO.CFG), en lugar de la EEPROM del ATmega.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SECTOR          512
#define NEW_DRIVE       2       // la imagen recien creada por CALL SDFNEW
#define MEDIA_720K      0xF9
#define MEDIA_360K      0xF8

// Un intento de inicializar la SD y montar la FAT. Si anda, recupera la
// configuracion. Hay que reintentar mientras img_sd_ok() sea false.
bool img_iniciar(void);
bool img_sd_ok(void);
const char *img_sd_texto(void);     // para CALL SDFTEST: "ok", "no contesta"...

uint8_t img_drives(void);           // 1 o 2, lo que contesta DRIVES
uint8_t img_poner_drives(uint8_t n);    // 0 o el error de BASIC

// DSKIO. Devuelven 0 o el codigo de error de DSKIO.
uint8_t img_leer(uint8_t drive, uint32_t sector, uint32_t n, uint8_t *buf);
uint8_t img_validar_escritura(uint8_t drive, uint32_t sector, uint32_t n, uint8_t media);
uint8_t img_escribir(uint8_t drive, uint32_t sector, uint32_t n, const uint8_t *buf);

uint8_t img_cambio(uint8_t drive);  // DSKCHG: 0xFF si cambio (y lo olvida), 1 si no
uint8_t img_media(uint8_t drive);   // DSKFMT: F9, F8 o 0 si no hay imagen

// CALL SDF...: devuelven 0 o el numero de error de BASIC.
uint8_t img_montar(uint8_t drive, const char *nombre, uint8_t largo);
uint8_t img_desmontar(uint8_t drive);
// avance se llama cada tanto mientras se llena la imagen de ceros.
uint8_t img_nueva(uint8_t media, const char *nombre, uint8_t largo,
                  uint8_t *ceros, uint32_t n_ceros, void (*avance)(void));

// CALL SDFFILES: los .DSK de la raiz como ASCIIZ seguidos, y un nombre vacio
// al final. Devuelve cuantos bytes escribio.
uint32_t img_listar(uint8_t *buf, uint32_t max);
