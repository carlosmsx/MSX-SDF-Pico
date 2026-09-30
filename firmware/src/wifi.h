// Wi-Fi del CYW43 de la Pico 2 W, en el nucleo 0.
//
// La red (el driver y lwIP) corre en segundo plano, por interrupciones del
// nucleo 0: el nucleo 1 y el bus no se enteran. Las credenciales se guardan
// en el ultimo sector de la flash de la Pico, solo despues de conectar bien,
// y nunca se muestran: la consola informa la red, no la clave.

#pragma once
#include <stdbool.h>

// Arranca el chip y se conecta a la red guardada, si hay. false si el chip no
// contesta (el resto del firmware sigue igual).
bool wifi_init(void);

// Llamar seguido desde el bucle del nucleo 0: sigue la conexion, reintenta,
// graba las credenciales y avisa los cambios por la consola.
void wifi_tarea(void);

// Comandos de la consola USB. true si la linea era para el Wi-Fi.
bool wifi_comando(const char *linea);

// true mientras la consola espera la clave: la linea va sin eco.
bool wifi_pide_clave(void);
