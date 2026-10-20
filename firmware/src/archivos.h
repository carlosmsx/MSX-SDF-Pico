// Archivos de la SD desde el MSX: directorio actual, listados, leer y grabar
// archivos, borrar, crear directorios y renombrar (docs/ARCHIVOS.md).
//
// Es la base de SD.COM y de un explorador: el estado (el directorio actual,
// el ultimo listado y un archivo abierto) queda en el firmware entre comando
// y comando, asi que cualquier programa lo retoma donde quedo. Las rutas son
// de FatFs, con / o \, relativas al directorio actual o absolutas, y con
// nombres largos o cortos. Las que devuelve son de nombres cortos, con \.
//
// Las funciones devuelven 0 o un error de protocolo.h (ERR_...).

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FS_RUTA_MAX     128     // la ruta absoluta, con el 0
#define FS_ENTRADAS     512     // lo mas que guarda un listado
#define FS_ENTRADA      64      // bytes por entrada en FSLIST y FSSTAT
#define FS_LISTA_MAX    8       // entradas por FSLIST
#define FS_BLOQUE       512     // lo mas que mueve un FSREAD o un FSWRITE

// Opciones de FSDIR
#define FS_OCULTOS      0x01    // tambien los ocultos y los de sistema
#define FS_PADRE        0x02    // una entrada ".." primero, fuera de la raiz

// Modos de FSOPEN
#define FS_LEER         0
#define FS_CREAR        1       // archivo nuevo: error si ya existe
#define FS_REEMPLAZAR   2       // nuevo, o vacia el que haya

// Una entrada de FSLIST y FSSTAT, FS_ENTRADA bytes:
//   0      atributos de FAT (10h directorio, 01h solo lectura, 02h oculto...)
//   1-4    largo (L primero)
//   5-6    fecha de FAT (L primero): dia, mes y anio-1980, como MSX-DOS
//   7-8    hora de FAT (L primero)
//   9-21   nombre corto 8.3, ASCIIZ
//   22-63  nombre para mostrar (el largo, o el corto), ASCIIZ, cortado a 41

// El MSX se reinicio (INIHRD): cierra el archivo, olvida el listado y
// vuelve a la raiz.
void fs_reiniciar(void);

uint8_t fs_cd(const char *ruta, uint8_t largo);
// La ruta actual, "\" en la raiz. Devuelve los bytes, con el 0.
int fs_pwd(char *txt);

// Lee el directorio (la ruta, o el actual si viene vacia) y lo ordena:
// primero los directorios, despues los archivos, por nombre. Si el ultimo
// tramo tiene * o ?, es un filtro sobre el nombre corto o el largo.
uint8_t fs_dir(uint8_t opciones, const char *ruta, uint8_t largo,
               uint16_t *cantidad, bool *cortado);
// Copia hasta max entradas del listado desde la entrada desde. Devuelve
// cuantas copio (0 al final).
int fs_listar(uint16_t desde, int max, uint8_t *dst);

uint8_t fs_abrir(uint8_t modo, const char *ruta, uint8_t largo, uint32_t *tam);
uint8_t fs_leer(uint8_t *dst, uint32_t max, uint32_t *n);
uint8_t fs_escribir(const uint8_t *src, uint32_t n);
uint8_t fs_cerrar(void);

uint8_t fs_borrar(const char *ruta, uint8_t largo);
uint8_t fs_mkdir(const char *ruta, uint8_t largo);
uint8_t fs_renombrar(const char *ruta, uint8_t largo, const char *nuevo, uint8_t largo_nuevo);
uint8_t fs_stat(const char *ruta, uint8_t largo, uint8_t *entrada);
uint8_t fs_libre(uint32_t *libres_kb, uint32_t *total_kb);
