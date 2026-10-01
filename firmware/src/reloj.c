#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "reloj.h"

// La hora es base + los segundos desde que arranco la Pico. base es la hora
// UTC (segundos Unix) que corresponde a time_us_64() = 0: ponerla es escribir
// una sola palabra, asi que el otro nucleo nunca ve una hora a medias.

#define UNIX_1980   315532800u      // 1980-01-01 00:00:00

static volatile uint32_t base;
static volatile int32_t zona_s;
static volatile int origen = RELOJ_SIN_PONER;
static volatile uint32_t sincronizaciones;

static uint32_t desde_arranque(void)
{
    return (uint32_t)(time_us_64() / 1000000u);
}

// Dias desde 1970-01-01 y vuelta (H. Hinnant, "chrono-compatible low-level
// date algorithms"), para fechas desde 1970.
static int32_t dias_de(int y, int m, int d)
{
    y -= m <= 2;
    int32_t era = y / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void fecha_de(int32_t z, int *y, int *m, int *d)
{
    z += 719468;
    int32_t era = z / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

static int dias_del_mes(int y, int m)
{
    static const uint8_t dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool bisiesto = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && bisiesto ? 29 : dm[m - 1];
}

void reloj_init(int zona_min)
{
    zona_s = zona_min * 60;
    // Sin poner: arranca en 1980-01-01 00:00, hora local.
    base = UNIX_1980 - zona_s - desde_arranque();
}

void reloj_leer(fecha_t *f)
{
    uint32_t local = base + desde_arranque() + zona_s;
    int32_t dias = local / 86400;
    uint32_t s = local % 86400;
    int y, m, d;
    fecha_de(dias, &y, &m, &d);
    f->anio = y;
    f->mes = m;
    f->dia = d;
    f->hora = s / 3600;
    f->min = s / 60 % 60;
    f->seg = s % 60;
    f->dia_semana = (dias + 4) % 7 + 1;     // 1970-01-01 fue jueves
}

bool reloj_poner(const fecha_t *f)
{
    if (f->anio < 1980 || f->anio > 2079 || f->mes < 1 || f->mes > 12
        || f->dia < 1 || f->dia > dias_del_mes(f->anio, f->mes)
        || f->hora > 23 || f->min > 59 || f->seg > 59)
        return false;
    uint32_t local = (uint32_t)dias_de(f->anio, f->mes, f->dia) * 86400u
                     + f->hora * 3600u + f->min * 60u + f->seg;
    base = local - zona_s - desde_arranque();
    origen = RELOJ_A_MANO;
    return true;
}

int reloj_origen(void)
{
    return origen;
}

int reloj_zona(void)
{
    return zona_s / 60;
}

void reloj_poner_zona(int minutos)
{
    zona_s = minutos * 60;
}

void reloj_internet(uint32_t utc)
{
    base = utc - desde_arranque();
    origen = RELOJ_INTERNET;
    sincronizaciones = sincronizaciones + 1;
}

uint32_t reloj_sincronizaciones(void)
{
    return sincronizaciones;
}

void reloj_texto(char *txt)
{
    fecha_t f;
    reloj_leer(&f);
    snprintf(txt, 20, "%04u-%02u-%02u %02u:%02u:%02u",
             f.anio % 10000u, f.mes % 100u, f.dia % 100u,
             f.hora % 100u, f.min % 100u, f.seg % 100u);   // el % acota para el compilador
}

void reloj_texto_zona(char *txt, int minutos)
{
    int a = abs(minutos);
    if (a % 60)
        sprintf(txt, "UTC%c%d:%02d", minutos < 0 ? '-' : '+', a / 60, a % 60);
    else if (a)
        sprintf(txt, "UTC%c%d", minutos < 0 ? '-' : '+', a / 60);
    else
        sprintf(txt, "UTC");
}
