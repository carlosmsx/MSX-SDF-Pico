// Tarjeta SD por SPI1 (GP12 MISO, GP13 CS, GP14 SCK, GP15 MOSI).
// FatFs la usa a traves de disk_read/disk_write, definidas en sd_spi.c.

#pragma once
#include <stdbool.h>

enum {
    SD_OK,
    SD_SIN_PROBAR,      // todavia no hubo un intento
    SD_NO_CONTESTA,     // sin tarjeta, modulo desenchufado o cableado
    SD_ERROR_INICIO,    // contesta pero no termina de inicializar
    SD_ERROR_ES,        // fallo una lectura o una escritura
};

bool sd_iniciar(void);
void sd_apagar(void);       // pines en alta impedancia: el modulo quedo sin alimentacion
int sd_error(void);
