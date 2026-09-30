#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "consola.h"

#define ANILLO 8192     // potencia de 2

static char anillo[ANILLO];
static volatile uint32_t cabeza, cola;  // cabeza: escribe el nucleo 1
static volatile uint32_t perdidos;

void con_printf(const char *fmt, ...)
{
    char linea[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(linea, sizeof linea, fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if (n >= (int)sizeof linea)
        n = sizeof linea - 1;

    uint32_t c = cabeza;
    if (ANILLO - (c - cola) < (uint32_t)n) {
        perdidos += n;
        return;
    }
    for (int i = 0; i < n; i++)
        anillo[(c + i) & (ANILLO - 1)] = linea[i];
    __mem_fence_release();
    cabeza = c + n;
}

void consola_tarea(void)
{
    uint32_t c = cabeza;
    __mem_fence_acquire();
    while (cola != c) {
        putchar(anillo[cola & (ANILLO - 1)]);
        cola++;
    }
    if (perdidos) {
        printf("[consola: %lu bytes perdidos]\n", (unsigned long)perdidos);
        perdidos = 0;
    }
}

const char *consola_linea(bool oculta)
{
    static char linea[128];
    static int n;
    static bool listo;
    static char previo;

    if (listo) {                // la anterior ya se entrego: se borra (claves)
        listo = false;
        memset(linea, 0, sizeof linea);
        n = 0;
    }
    for (;;) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT)
            return NULL;
        char p = previo;
        previo = (char)c;
        if (c == '\n' && p == '\r')
            continue;           // CR LF: un solo fin de linea
        if (c == '\r' || c == '\n') {
            putchar('\n');
            linea[n] = 0;
            listo = true;
            return linea;
        }
        if ((c == 8 || c == 127) && n > 0) {
            n--;
            fputs("\b \b", stdout);
        } else if (c >= 32 && c < 127 && n < (int)sizeof linea - 1) {
            linea[n++] = (char)c;
            putchar(oculta ? '*' : c);
        }
    }
}
