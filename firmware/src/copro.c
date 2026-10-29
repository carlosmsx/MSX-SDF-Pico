// Coprocesador de juegos, prueba (copro.h). La escena: una pelota que se
// mueve con el joystick o los cursores y el resto rebotando. Todo en punto
// fijo 8.8; la pantalla es de 256x192 y los sprites de 16x16.
//
// El TMS9918 muestra 4 sprites por linea: en cada cuadro se rota el orden de
// los planos 1 en adelante, y los que sobran parpadean en lugar de no verse.
// El plano 0 es siempre el jugador.

#include <string.h>
#include "copro.h"

#define X_MAX    ((256 - 16) << 8)
#define Y_MAX    ((192 - 16) << 8)
#define PASO     (2 << 8)       // el jugador, por cuadro
#define TOQUE    12             // distancia de choque, en pixeles
#define DESTELLO 10             // cuadros con el color de choque
#define Y_FIN    0xD0           // en la tabla de atributos: no hay mas sprites

// Colores del TMS9918: sin el 0 (transparente) ni el 1 (negro, el fondo)
#define COLOR_JUGADOR 11
#define COLOR_TOQUE   15
static const uint8_t colores[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14 };

typedef struct {
    int32_t x, y, vx, vy;
    uint8_t color, destello;
} objeto_t;

static objeto_t obj[COPRO_SPRITES];
static int cuantos;
static uint32_t cuadro;
static uint32_t azar = 1;

static uint32_t sorteo(uint32_t n)
{
    azar = azar * 1103515245u + 12345u;
    return (azar >> 16) % n;
}

uint32_t copro_bloques(uint8_t n, uint8_t semilla, uint8_t *buf)
{
    if (n > COPRO_BLOQUES)
        n = COPRO_BLOQUES;
    for (uint32_t j = 0; j < n; j++)
        for (uint32_t k = 0; k < 256; k++)
            buf[j * 256 + k] = (uint8_t)(k ^ j ^ semilla);
    return n * 256u;
}

uint32_t copro_iniciar(uint8_t n, uint8_t *buf)
{
    cuantos = n < 1 ? 1 : n > COPRO_SPRITES ? COPRO_SPRITES : n;
    cuadro = 0;
    azar = 1;
    for (int i = 0; i < cuantos; i++) {
        objeto_t *o = &obj[i];
        o->x = sorteo(X_MAX);
        o->y = sorteo(Y_MAX);
        o->vx = (int32_t)(sorteo(3 << 8) + (1 << 7)) * (sorteo(2) ? 1 : -1);
        o->vy = (int32_t)(sorteo(3 << 8) + (1 << 7)) * (sorteo(2) ? 1 : -1);
        o->color = colores[i % sizeof colores];
        o->destello = 0;
    }
    obj[0].x = X_MAX / 2;
    obj[0].y = Y_MAX / 2;
    obj[0].color = COLOR_JUGADOR;

    // La pelota: un circulo de 16 pixeles. El VDP guarda un sprite de 16x16
    // como cuatro de 8x8: arriba a la izquierda, abajo, y despues la columna
    // de la derecha.
    memset(buf, 0, 32);
    for (int f = 0; f < 16; f++)
        for (int c = 0; c < 16; c++) {
            int dx = 2 * c - 15, dy = 2 * f - 15;
            if (dx * dx + dy * dy <= 240)
                buf[(c >= 8 ? 16 : 0) + f] |= 0x80 >> (c & 7);
        }
    return 32;
}

static void rebotar(int32_t *p, int32_t *v, int32_t max)
{
    *p += *v;
    if (*p < 0) {
        *p = -*p;
        *v = -*v;
    } else if (*p > max) {
        *p = 2 * max - *p;
        *v = -*v;
    }
}

static int32_t limitar(int32_t p, int32_t max)
{
    return p < 0 ? 0 : p > max ? max : p;
}

uint32_t copro_cuadro(uint8_t joy, uint8_t fila8, uint8_t *buf)
{
    if (!cuantos)
        copro_iniciar(COPRO_SPRITES, buf);

    // Joystick: bits 0-3 arriba, abajo, izquierda, derecha. Fila 8: bits
    // 4-7 izquierda, arriba, abajo, derecha. Todo activo en 0.
    objeto_t *j = &obj[0];
    if (!(joy & 0x01) || !(fila8 & 0x20)) j->y -= PASO;
    if (!(joy & 0x02) || !(fila8 & 0x40)) j->y += PASO;
    if (!(joy & 0x04) || !(fila8 & 0x10)) j->x -= PASO;
    if (!(joy & 0x08) || !(fila8 & 0x80)) j->x += PASO;
    j->x = limitar(j->x, X_MAX);
    j->y = limitar(j->y, Y_MAX);

    for (int i = 1; i < cuantos; i++) {
        objeto_t *o = &obj[i];
        rebotar(&o->x, &o->vx, X_MAX);
        rebotar(&o->y, &o->vy, Y_MAX);
        if (o->destello)
            o->destello--;
        // Un choque con el jugador: sale para el otro lado y destella
        int32_t dx = (o->x - j->x) >> 8, dy = (o->y - j->y) >> 8;
        if (dx > -TOQUE && dx < TOQUE && dy > -TOQUE && dy < TOQUE) {
            if ((dx < 0) != (o->vx < 0)) o->vx = -o->vx;
            if ((dy < 0) != (o->vy < 0)) o->vy = -o->vy;
            o->destello = DESTELLO;
        }
    }

    // La tabla de atributos: Y, X, patron, color. El VDP dibuja un sprite
    // una linea mas abajo de su Y.
    memset(buf, 0, 128);
    for (int p = 0; p < COPRO_SPRITES; p++) {
        uint8_t *a = buf + 4 * p;
        if (p >= cuantos) {
            a[0] = Y_FIN;
            continue;
        }
        const objeto_t *o = p ? &obj[1 + (p - 1 + cuadro) % (cuantos - 1)] : j;
        a[0] = (uint8_t)((o->y >> 8) - 1);
        a[1] = (uint8_t)(o->x >> 8);
        a[2] = 0;
        a[3] = o->destello ? COLOR_TOQUE : o->color;
    }
    cuadro++;
    return 128;
}
