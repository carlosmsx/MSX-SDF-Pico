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
#include "ajustes.h"
#include "menu_wifi.h"
#include "reloj.h"
#include "wifi.h"

static void mostrar_hora(void)
{
    static const char *const origen[] = {
        [RELOJ_SIN_PONER] = "sin poner", [RELOJ_INTERNET] = "de internet",
        [RELOJ_A_MANO] = "puesta a mano",
    };
    char txt[20], zona[12];
    reloj_texto(txt);
    reloj_texto_zona(zona, reloj_zona());
    printf("reloj: %s %s, %s\n", txt, zona, origen[reloj_origen()]);
}

// "zona -3", "zona +5:30", "zona 0"
static bool leer_zona(const char *txt, int *minutos)
{
    int signo = 1, h = 0, m = 0;
    if (*txt == '-' || *txt == '+')
        signo = *txt++ == '-' ? -1 : 1;
    if (*txt < '0' || *txt > '9')
        return false;
    while (*txt >= '0' && *txt <= '9')
        h = h * 10 + (*txt++ - '0');
    if (*txt == ':') {
        txt++;
        if (txt[0] < '0' || txt[0] > '9' || txt[1] < '0' || txt[1] > '9' || txt[2])
            return false;
        m = (txt[0] - '0') * 10 + (txt[1] - '0');
    } else if (*txt) {
        return false;
    }
    *minutos = signo * (h * 60 + m);
    return m < 60 && *minutos >= -12 * 60 && *minutos <= 14 * 60;
}

static void comando(const char *linea)
{
    if (linea[0] == 0 || wifi_comando(linea))
        return;
    if (strcmp(linea, "hora") == 0) {
        mostrar_hora();
        return;
    }
    if (strncmp(linea, "zona ", 5) == 0) {
        int m;
        if (!leer_zona(linea + 5, &m)) {
            printf("uso: zona -3, zona +5:30 (entre -12 y +14)\n");
            return;
        }
        reloj_poner_zona(m);
        ajustes_poner_zona(m);
        mostrar_hora();
        return;
    }
    if (strcmp(linea, "ayuda") != 0)
        printf("comando desconocido: %s\n", linea);
    printf("comandos:\n"
           "  wifi          estado de la conexion\n"
           "  buscar        lista las redes a la vista\n"
           "  conectar <n>  conecta a la red n de la lista (o por nombre);\n"
           "                pide la clave y, si conecta, la guarda\n"
           "  olvidar       borra la red guardada\n"
           "  hora          fecha, hora y de donde salio\n"
           "  zona <h>      zona horaria: zona -3, zona +5:30\n");
}

int main(void)
{
    stdio_init_all();
    ajustes_leer();
    reloj_init(ajustes_zona());
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
