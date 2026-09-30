// SDF-Pico: firmware de la Pico 2 W en el cartucho SDF-1 rev1, sin /WAIT.
//
// Nucleo 1: el bus y el protocolo con la DiskROM (protocolo.c). No tiene
// interrupciones: tiene que ver cada comando en menos de un microsegundo para
// levantar OCUPADO antes de que el Z80 consulte el estado. Tambien maneja la
// SD: mientras tanto el estado queda en OCUPADO.
// Nucleo 0: la consola USB, el Wi-Fi (wifi.c), su menu (menu_wifi.c) y el
// diagnostico, si esta compilado. El Wi-Fi arranca despues del nucleo 1: el
// bus no lo espera.
//
// Todo el programa corre desde la RAM (copy_to_ram), para que el nucleo 1 no
// dependa de la cache de la flash.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "bus.h"
#include "consola.h"
#include "protocolo.h"
#include "menu_wifi.h"
#include "wifi.h"

static void comando(const char *linea)
{
    if (linea[0] == 0 || wifi_comando(linea))
        return;
    if (strcmp(linea, "ayuda") != 0)
        printf("comando desconocido: %s\n", linea);
    printf("comandos:\n"
           "  wifi          estado de la conexion\n"
           "  buscar        lista las redes a la vista\n"
           "  conectar <n>  conecta a la red n de la lista (o por nombre);\n"
           "                pide la clave y, si conecta, la guarda\n"
           "  olvidar       borra la red guardada\n");
}

int main(void)
{
    stdio_init_all();
    bus_init();
    multicore_launch_core1(protocolo_core1);

    printf("%s, protocolo %d\n", FW_VERSION, PROTOCOL_VERSION);
    wifi_init();

    for (;;) {
        consola_tarea();
        bus_diagnostico();
        wifi_tarea();
        menu_tarea();
        const char *linea = consola_linea(wifi_pide_clave());
        if (linea)
            comando(linea);
    }
}
