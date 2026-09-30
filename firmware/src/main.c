// SDF-Pico: firmware de la Pico 2 W en el cartucho SDF-1 rev1, sin /WAIT.
//
// Nucleo 1: el bus y el protocolo con la DiskROM (protocolo.c). No tiene
// interrupciones: tiene que ver cada comando en menos de un microsegundo para
// levantar OCUPADO antes de que el Z80 consulte el estado. Tambien maneja la
// SD: mientras tanto el estado queda en OCUPADO.
// Nucleo 0: la consola USB (y el diagnostico, si esta compilado).
//
// Todo el programa corre desde la RAM (copy_to_ram), para que el nucleo 1 no
// dependa de la cache de la flash.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "bus.h"
#include "consola.h"
#include "protocolo.h"

int main(void)
{
    stdio_init_all();
    bus_init();
    multicore_launch_core1(protocolo_core1);

    printf("%s, protocolo %d\n", FW_VERSION, PROTOCOL_VERSION);

    for (;;) {
        consola_tarea();
        bus_diagnostico();
    }
}
