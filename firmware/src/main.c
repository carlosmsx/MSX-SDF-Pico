// SDF-Pico: firmware de la Pico 2 W en el cartucho SDF-1 rev1, sin /WAIT.
//
// Nucleo 1: el bus y el protocolo con la DiskROM (protocolo.c). No tiene
// interrupciones: tiene que ver cada comando en menos de un microsegundo para
// levantar OCUPADO antes de que el Z80 consulte el estado.
// Nucleo 0: la consola USB (y el diagnostico, si esta compilado). Cuando el
// nucleo 1 graba la flash, lo detiene con flash_safe_execute.
//
// Todo el programa corre desde la RAM (copy_to_ram): la flash queda para el
// disco (disco.c), y grabarla no frena al que atiende el bus.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "bus.h"
#include "consola.h"
#include "disco.h"
#include "protocolo.h"

int main(void)
{
    stdio_init_all();
    flash_safe_execute_core_init();     // el nucleo 1 puede detener a este
    bus_init();
    multicore_launch_core1(protocolo_core1);

    printf("%s, protocolo %d\n", FW_VERSION, PROTOCOL_VERSION);
    uint8_t m = disco_media();
    printf("disco en flash: %s\n", m == 0xF9 ? "720 KB" : m == 0xF8 ? "360 KB" : "en blanco");

    for (;;) {
        consola_tarea();
        bus_diagnostico();
    }
}
