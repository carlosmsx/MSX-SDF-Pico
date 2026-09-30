// Consola por USB, sin bloquear al que escribe.
//
// El nucleo 1 atiende el bus y no puede esperar a la USB: con_printf deja el
// texto en un anillo y el nucleo 0 lo saca por stdio (consola_tarea). Si el
// anillo se llena, se descarta y se avisa cuantos bytes se perdieron.
// Un solo escritor: con_printf se llama solo desde el nucleo 1.

#pragma once
#include <stdbool.h>

void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void consola_tarea(void);

// Entrada (nucleo 0): arma una linea sin bloquear. Devuelve la linea
// terminada, sin el fin de linea, o NULL si todavia no hay. Con oculta el eco
// es '*' (claves).
const char *consola_linea(bool oculta);
