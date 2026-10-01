// Reloj del MSX, sin chip: la hora se cuenta con el timer del RP2350 y se
// pone desde Internet (SNTP, por el Wi-Fi) o a mano (DOS, CALL RTC, OPEN
// "RTC:"). Con el MSX apagado la Pico no tiene alimentacion y la hora se
// pierde: al volver, SNTP la pone de nuevo en cuanto hay red.
//
// Lo leen y lo ponen los dos nucleos: el estado son palabras de 32 bits,
// que se escriben de una vez.

#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t anio;          // 1980-2079, lo que maneja el MSX
    uint8_t mes, dia;
    uint8_t hora, min, seg;
    uint8_t dia_semana;     // 1 = domingo ... 7 = sabado, como el DS1307 del SDF-1
} fecha_t;

enum { RELOJ_SIN_PONER, RELOJ_INTERNET, RELOJ_A_MANO };

void reloj_init(int zona_min);

// Hora local
void reloj_leer(fecha_t *f);
bool reloj_poner(const fecha_t *f);         // false si la fecha no es valida
int reloj_origen(void);

// Zona horaria, en minutos respecto de UTC (Argentina: -180). Cambiarla
// mueve la hora local, no la UTC.
int reloj_zona(void);
void reloj_poner_zona(int minutos);

// SNTP: se llama desde lwIP (interrupcion del nucleo 0) con la hora UTC.
void reloj_internet(uint32_t utc);
uint32_t reloj_sincronizaciones(void);      // cuantas veces la puso SNTP

// "2026-09-30 21:05:07", 20 caracteres con el 0
void reloj_texto(char *txt);
// "UTC-3", "UTC+5:30"
void reloj_texto_zona(char *txt, int minutos);
