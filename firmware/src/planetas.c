// PLANETAS (planetas.h): los 8 planetas en sus orbitas reales, acelerados.
//
// Cada planeta sale de sus elementos orbitales en la fecha: con la anomalia
// media se resuelve la ecuacion de Kepler (M = E - e sen E) y se pasa del
// plano de la orbita a la ecliptica. La vista gira alrededor del eje de la
// ecliptica, se inclina y tiene perspectiva; la escala es la real, con zoom.
//
// La pantalla es SCREEN 2. Las orbitas y el Sol se dibujan en el bitmap solo
// cuando cambia la vista; los textos, en cada cuadro. De la imagen se mandan
// las celdas de 8x8 que difieren de la VRAM, hasta el presupuesto del
// cuadro: si no entran todas, siguen en el proximo. Los planetas son sprites
// de 8x8, del mas cercano al mas lejano.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "planetas.h"
#include "reloj.h"

#define VRAM_PAT  0x0000    // SCREEN 2
#define VRAM_NOM  0x1800
#define VRAM_SAT  0x1B00
#define VRAM_COL  0x2000
#define VRAM_SPR  0x3800
#define TABLA     6144
#define Y_FIN     0xD0

#define CX        128       // el Sol, en el centro
#define CY        96
#define Y_MIN     8         // las filas de caracteres 0 y 23 son de los textos
#define Y_MAX     (192 - 8)

#define PRESUPUESTO 1500    // bytes por cuadro: ~43.500 ciclos del Z80 a 29 por byte
#define PUNTOS    180       // por orbita
#define PI        3.14159265f
#define RAD       (PI / 180)

// Colores: frente << 4 | fondo; el fondo es siempre negro
#define C_VACIO   0xF1
#define C_TEXTO   0xF1
#define C_AYUDA   0xE1
#define SOL       11

// La fecha va en minutos enteros desde J2000 (el 1/1/2000 a las 12 UTC) y
// los calculos en float, que la Pico 2 hace por hardware: las rutinas de
// double del SDK estan en la RAM (copy_to_ram) y ocupan unos 3 KB.
#define DIA       1440
#define J2000_UNIX 10957    // dias del 1/1/1970 al 1/1/2000
#define MIN_MAX   (365000 * DIA)  // ~mil anios para cada lado

// ---- Los planetas ----
//
// Elementos de JPL ("Approximate Positions of the Planets", E. M. Standish,
// tabla 1, de 1800 a 2050): semieje (UA), excentricidad, inclinacion,
// longitud media, longitud del perihelio y longitud del nodo ascendente
// (grados), y lo que cambia cada uno por siglo. Para la Tierra, el
// baricentro Tierra-Luna. Fuera de esos anios el error crece de a poco: en
// mil anios sigue siendo mucho menos que un pixel.

static const struct {
    float el[6], var[6];
    uint8_t color;
    const char *dibujo[8];
} planeta[8] = {
    {   // Mercurio
        { 0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593 },
        { 0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081 },
        14, { "", "", "...#", "..###", "...#" } },
    {   // Venus
        { 0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255 },
        { 0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418 },
        11, { "", "..###", ".#####", ".#####", ".#####", "..###" } },
    {   // Tierra
        { 1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0 },
        { 0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0 },
        5, { "", "..###", ".#####", ".#####", ".#####", "..###" } },
    {   // Marte
        { 1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891 },
        { 0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343 },
        8, { "", "", "..###", "..###", "..###" } },
    {   // Jupiter
        { 5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909 },
        { -0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106 },
        9, { "..###", ".#####", "#######", "#######", "#######", ".#####", "..###" } },
    {   // Saturno, con el anillo
        { 9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448 },
        { -0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794 },
        10, { "", "", "..###", "#######", "..###" } },
    {   // Urano
        { 19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503 },
        { -0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589 },
        7, { "", "..###", ".#####", ".#####", ".#####", "..###" } },
    {   // Neptuno
        { 30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574 },
        { 0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664 },
        4, { "", "..###", ".#####", ".#####", ".#####", "..###" } },
};

// ---- Fuente de 5x7, por columnas (bit 0 arriba) ----

static const uint8_t fuente['Z' - ' ' + 1][5] = {
    ['-' - ' '] = { 0x08, 0x08, 0x08, 0x08, 0x08 },
    ['.' - ' '] = { 0x00, 0x60, 0x60, 0x00, 0x00 },
    ['/' - ' '] = { 0x20, 0x10, 0x08, 0x04, 0x02 },
    ['0' - ' '] = { 0x3E, 0x51, 0x49, 0x45, 0x3E },
    ['1' - ' '] = { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    ['2' - ' '] = { 0x42, 0x61, 0x51, 0x49, 0x46 },
    ['3' - ' '] = { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    ['4' - ' '] = { 0x18, 0x14, 0x12, 0x7F, 0x10 },
    ['5' - ' '] = { 0x27, 0x45, 0x45, 0x45, 0x39 },
    ['6' - ' '] = { 0x3C, 0x4A, 0x49, 0x49, 0x30 },
    ['7' - ' '] = { 0x01, 0x71, 0x09, 0x05, 0x03 },
    ['8' - ' '] = { 0x36, 0x49, 0x49, 0x49, 0x36 },
    ['9' - ' '] = { 0x06, 0x49, 0x49, 0x29, 0x1E },
    [':' - ' '] = { 0x00, 0x36, 0x36, 0x00, 0x00 },
    ['A' - ' '] = { 0x7E, 0x11, 0x11, 0x11, 0x7E },
    ['B' - ' '] = { 0x7F, 0x49, 0x49, 0x49, 0x36 },
    ['C' - ' '] = { 0x3E, 0x41, 0x41, 0x41, 0x22 },
    ['D' - ' '] = { 0x7F, 0x41, 0x41, 0x22, 0x1C },
    ['E' - ' '] = { 0x7F, 0x49, 0x49, 0x49, 0x41 },
    ['F' - ' '] = { 0x7F, 0x09, 0x09, 0x09, 0x01 },
    ['G' - ' '] = { 0x3E, 0x41, 0x49, 0x49, 0x7A },
    ['H' - ' '] = { 0x7F, 0x08, 0x08, 0x08, 0x7F },
    ['I' - ' '] = { 0x00, 0x41, 0x7F, 0x41, 0x00 },
    ['J' - ' '] = { 0x20, 0x40, 0x41, 0x3F, 0x01 },
    ['K' - ' '] = { 0x7F, 0x08, 0x14, 0x22, 0x41 },
    ['L' - ' '] = { 0x7F, 0x40, 0x40, 0x40, 0x40 },
    ['M' - ' '] = { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
    ['N' - ' '] = { 0x7F, 0x04, 0x08, 0x10, 0x7F },
    ['O' - ' '] = { 0x3E, 0x41, 0x41, 0x41, 0x3E },
    ['P' - ' '] = { 0x7F, 0x09, 0x09, 0x09, 0x06 },
    ['Q' - ' '] = { 0x3E, 0x41, 0x51, 0x21, 0x5E },
    ['R' - ' '] = { 0x7F, 0x09, 0x19, 0x29, 0x46 },
    ['S' - ' '] = { 0x46, 0x49, 0x49, 0x49, 0x31 },
    ['T' - ' '] = { 0x01, 0x01, 0x7F, 0x01, 0x01 },
    ['U' - ' '] = { 0x3F, 0x40, 0x40, 0x40, 0x3F },
    ['V' - ' '] = { 0x1F, 0x20, 0x40, 0x20, 0x1F },
    ['W' - ' '] = { 0x3F, 0x40, 0x38, 0x40, 0x3F },
    ['X' - ' '] = { 0x63, 0x14, 0x08, 0x14, 0x63 },
    ['Y' - ' '] = { 0x07, 0x08, 0x70, 0x08, 0x07 },
    ['Z' - ' '] = { 0x61, 0x51, 0x49, 0x45, 0x43 },
};

// ---- Estado ----

// Minutos por cuadro de cada velocidad: de una hora a 100 dias
static const int32_t pasos[] = { 60, 6 * 60, DIA, 2 * DIA, 5 * DIA, 10 * DIA, 20 * DIA, 50 * DIA, 100 * DIA };
#define NPASOS    (int)(sizeof pasos / sizeof pasos[0])
#define PASO_INICIAL 3      // 1 dia por cuadro

static struct {
    int32_t minutos;        // desde J2000
    int paso;               // 1..NPASOS para adelante, -1..-NPASOS para atras
    float incl, azim;       // grados: 0 = desde arriba, 90 = de canto, < 0 = desde abajo; giro
    float escala;           // pixeles por UA
    uint8_t orbitas, pausa, redibujar;
    uint16_t antes;         // las teclas de un toque, apretadas en el cuadro anterior
    uint32_t cuadro;
    int32_t minutos_orbitas; // la fecha de las orbitas dibujadas
    int fila;               // por donde sigue el envio de celdas
} pl;

#define ESCALA_MIN 2.6f     // Neptuno entero
#define ESCALA_MAX 160.0f   // Mercurio llena la pantalla
#define ESCALA_INI 40.0f    // hasta Marte
#define INCL_MAX  90.0f     // de -90 a 90: de canto por los dos lados

typedef struct {
    uint8_t pat[TABLA], col[TABLA];
} imagen_t;

// ---- Fechas ----

// Dias desde el 1/1/1970 y al reves, con el calendario gregoriano para
// todos los anios (H. Hinnant, "chrono-Compatible Low-Level Date Algorithms")
static int32_t dias_de(int anio, int mes, int dia)
{
    anio -= mes <= 2;
    int32_t era = (anio >= 0 ? anio : anio - 399) / 400;
    int32_t ade = anio - era * 400;
    int32_t dda = (153 * (mes > 2 ? mes - 3 : mes + 9) + 2) / 5 + dia - 1;
    int32_t dde = ade * 365 + ade / 4 - ade / 100 + dda;
    return era * 146097 + dde - 719468;
}

static void fecha_de(int32_t dias, int *anio, int *mes, int *dia)
{
    dias += 719468;
    int32_t era = (dias >= 0 ? dias : dias - 146096) / 146097;
    int32_t dde = dias - era * 146097;
    int32_t ade = (dde - dde / 1460 + dde / 36524 - dde / 146096) / 365;
    int32_t dda = dde - (365 * ade + ade / 4 - ade / 100);
    int32_t mp = (5 * dda + 2) / 153;
    *dia = (int)(dda - (153 * mp + 2) / 5 + 1);
    *mes = (int)(mp < 10 ? mp + 3 : mp - 9);
    *anio = (int)(ade + era * 400 + (*mes <= 2));
}

// La fecha del reloj (UTC), o J2000 si nadie lo puso
static int32_t hoy(void)
{
    if (reloj_origen() == RELOJ_SIN_PONER)
        return 0;
    fecha_t f;
    reloj_leer(&f);
    int32_t d = dias_de(f.anio, f.mes, f.dia) - J2000_UNIX;
    return d * DIA + f.hora * 60 + f.min - reloj_zona() - DIA / 2;
}

// ---- Orbitas ----

// La orbita en una fecha: lo que lleva del plano de la orbita (x a lo largo
// del eje mayor) a la ecliptica, y la anomalia media.
typedef struct {
    float a, e, b, m;
    float xx, xy, yx, yy, zx, zy;
} orbita_t;

static void orbita(int k, int32_t minutos, orbita_t *o)
{
    float t = (float)minutos / (36525.0f * DIA), el[6];
    for (int i = 0; i < 6; i++)
        el[i] = planeta[k].el[i] + planeta[k].var[i] * t;
    float m = el[3] - el[4];
    m -= 360 * (float)(int32_t)(m / 360);
    if (m > 180) m -= 360;
    if (m < -180) m += 360;
    o->a = el[0];
    o->e = el[1];
    o->b = el[0] * sqrtf(1 - el[1] * el[1]);
    o->m = m * RAD;
    float w = (el[4] - el[5]) * RAD, n = el[5] * RAD, i = el[2] * RAD;
    float cw = cosf(w), sw = sinf(w), cn = cosf(n), sn = sinf(n), ci = cosf(i), si = sinf(i);
    o->xx = cw * cn - sw * sn * ci;
    o->xy = -sw * cn - cw * sn * ci;
    o->yx = cw * sn + sw * cn * ci;
    o->yy = -sw * sn + cw * cn * ci;
    o->zx = sw * si;
    o->zy = cw * si;
}

// El punto de la orbita con anomalia excentrica ex, en UA
static void punto(const orbita_t *o, float ex, float p[3])
{
    float x = o->a * (cosf(ex) - o->e), y = o->b * sinf(ex);
    p[0] = o->xx * x + o->xy * y;
    p[1] = o->yx * x + o->yy * y;
    p[2] = o->zx * x + o->zy * y;
}

// M = E - e sen E, por Newton
static float kepler(float m, float e)
{
    float ex = m + e * sinf(m);
    for (int i = 0; i < 5; i++)
        ex -= (ex - e * sinf(ex) - m) / (1 - e * cosf(ex));
    return ex;
}

// ---- La vista ----

static struct {
    float ca, sa, ci, si;
    float dist;             // de la camara al Sol, en UA
} vista;

static void preparar_vista(void)
{
    vista.ca = cosf(pl.azim * RAD);
    vista.sa = sinf(pl.azim * RAD);
    vista.ci = cosf(pl.incl * RAD);
    vista.si = sinf(pl.incl * RAD);
    vista.dist = 4 * CX / pl.escala;    // perspectiva suave: 4 medias pantallas
}

// De la ecliptica a la pantalla. La camara esta del lado de -y, arriba de
// la ecliptica; cerca > 0 es hacia ella. 0 si queda detras o muy lejos.
static int proyectar(const float p[3], int *x, int *y, float *cerca)
{
    float x1 = p[0] * vista.ca - p[1] * vista.sa;
    float y1 = p[0] * vista.sa + p[1] * vista.ca;
    float arriba = y1 * vista.ci + p[2] * vista.si;
    float c = -y1 * vista.si + p[2] * vista.ci;
    float den = vista.dist - c;
    if (den < vista.dist / 4)
        return 0;
    float k = pl.escala * vista.dist / den;
    float fx = CX + x1 * k, fy = CY - arriba * k;
    if (fabsf(fx - CX) > 4000 || fabsf(fy - CY) > 4000)
        return 0;
    *x = (int)(fx + 4096.5f) - 4096;    // redondeo, tambien para negativos
    *y = (int)(fy + 4096.5f) - 4096;
    if (cerca) *cerca = c;
    return 1;
}

// ---- El bitmap ----

static void pixel(imagen_t *im, int x, int y, uint8_t color)
{
    if ((unsigned)x >= 256 || y < Y_MIN || y >= Y_MAX)
        return;
    int i = (y >> 3) * 256 + (x & ~7) + (y & 7);
    im->pat[i] |= 0x80 >> (x & 7);
    im->col[i] = (uint8_t)(color << 4 | 1);
}

static void linea(imagen_t *im, int x0, int y0, int x1, int y1, uint8_t color)
{
    if ((x0 < 0 && x1 < 0) || (x0 >= 256 && x1 >= 256) ||
        (y0 < Y_MIN && y1 < Y_MIN) || (y0 >= Y_MAX && y1 >= Y_MAX))
        return;
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        pixel(im, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Las orbitas (de afuera hacia adentro: donde se cruzan gana el color de la
// de adentro) y el Sol encima
static void dibujar(imagen_t *im)
{
    memset(im->pat, 0, TABLA);
    memset(im->col, C_VACIO, TABLA);
    if (pl.orbitas)
        for (int k = 7; k >= 0; k--) {
            orbita_t o;
            orbita(k, pl.minutos, &o);
            int x0 = 0, y0 = 0, antes = 0;
            for (int n = 0; n <= PUNTOS; n++) {
                float p[3];
                int x, y;
                punto(&o, 2 * PI * n / PUNTOS, p);
                int ok = proyectar(p, &x, &y, NULL);
                if (ok && antes)
                    linea(im, x0, y0, x, y, planeta[k].color);
                x0 = x;
                y0 = y;
                antes = ok;
            }
        }
    int r = 2 + (int)(pl.escala / 40);
    if (r > 5) r = 5;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                pixel(im, CX + x, CY + y, SOL);
    pl.minutos_orbitas = pl.minutos;
}

static void texto(imagen_t *im, int fila, int col, const char *s, uint8_t color)
{
    for (; *s && col < 32; s++, col++) {
        int i = fila * 256 + col * 8;
        const uint8_t *g = *s >= ' ' && *s <= 'Z' ? fuente[*s - ' '] : fuente[0];
        for (int r = 0; r < 8; r++) {
            uint8_t b = 0;
            for (int c = 0; c < 5; c++)
                if (g[c] >> r & 1)
                    b |= 0x40 >> c;
            im->pat[i + r] = b;
            im->col[i + r] = b ? color : C_VACIO;
        }
    }
}

static void textos(imagen_t *im)
{
    char s[33];
    int anio, mes, dia, hora, min;

    texto(im, 0, 0, "A/Z ZOOM R/L VEL O ORB H HOY    ", C_AYUDA);
    int32_t m = pl.minutos + J2000_UNIX * DIA + DIA / 2;
    int32_t d = m >= 0 ? m / DIA : -((-m + DIA - 1) / DIA);
    m -= d * DIA;
    fecha_de(d, &anio, &mes, &dia);
    hora = (int)(m / 60);
    min = (int)(m % 60);
    int n = snprintf(s, sizeof s, "%04d-%02d-%02d", anio, mes, dia);
    int p = pl.paso < 0 ? -pl.paso : pl.paso;
    if (p <= 2)
        n += snprintf(s + n, sizeof s - n, " %02d:%02d", hora, min);
    char v[24];
    if (pl.pausa)
        strcpy(v, "PAUSA");
    else if (p <= 2)
        snprintf(v, sizeof v, "%s%d HORA%s/CUADRO", pl.paso < 0 ? "-" : "",
                 p == 1 ? 1 : 6, p == 1 ? "" : "S");
    else
        snprintf(v, sizeof v, "%s%d DIA%s/CUADRO", pl.paso < 0 ? "-" : "",
                 (int)(pasos[p - 1] / DIA), p == PASO_INICIAL ? "" : "S");
    while (n < 32 - (int)strlen(v))
        s[n++] = ' ';
    strcpy(s + n, v);
    texto(im, 23, 0, s, C_TEXTO);
}

// ---- La entrada ----

// e[0]: el registro 14 del PSG; e[1 + f]: la fila f del teclado
#define TECLA(f, b)   (!(e[1 + (f)] & (1 << (b))))
#define JOY(b)        (!(e[0] & (1 << (b))))

enum { T_ORBITAS = 1, T_HOY = 2, T_PAUSA = 4, T_RAPIDO = 8, T_LENTO = 16 };

static void entrada(const uint8_t *e)
{
    float incl = pl.incl, azim = pl.azim, escala = pl.escala;

    if (JOY(0) || TECLA(8, 5)) incl -= 1;
    if (JOY(1) || TECLA(8, 6)) incl += 1;
    if (JOY(2) || TECLA(8, 4)) azim -= 2;
    if (JOY(3) || TECLA(8, 7)) azim += 2;
    if (TECLA(2, 6)) escala *= 1.03f;       // A
    if (TECLA(5, 7)) escala /= 1.03f;       // Z
    if (incl < -INCL_MAX) incl = -INCL_MAX;
    if (incl > INCL_MAX) incl = INCL_MAX;
    if (azim < 0) azim += 360;
    if (azim >= 360) azim -= 360;
    if (escala < ESCALA_MIN) escala = ESCALA_MIN;
    if (escala > ESCALA_MAX) escala = ESCALA_MAX;
    if (incl != pl.incl || azim != pl.azim || escala != pl.escala)
        pl.redibujar = 1;
    pl.incl = incl;
    pl.azim = azim;
    pl.escala = escala;

    // Las de un toque: cuentan al apretarlas
    uint16_t t = 0;
    if (TECLA(4, 4)) t |= T_ORBITAS;
    if (TECLA(3, 5)) t |= T_HOY;
    if (JOY(4) || TECLA(8, 0)) t |= T_PAUSA;
    if (TECLA(4, 7)) t |= T_RAPIDO;
    if (TECLA(4, 1)) t |= T_LENTO;
    uint16_t nuevas = t & ~pl.antes;
    pl.antes = t;
    if (nuevas & T_ORBITAS) {
        pl.orbitas ^= 1;
        pl.redibujar = 1;
    }
    if (nuevas & T_HOY) {
        pl.minutos = hoy();
        pl.redibujar = 1;
    }
    if (nuevas & T_PAUSA)
        pl.pausa ^= 1;
    if (nuevas & T_RAPIDO && pl.paso < NPASOS)
        pl.paso = pl.paso == -1 ? 1 : pl.paso + 1;
    if (nuevas & T_LENTO && pl.paso > -NPASOS)
        pl.paso = pl.paso == 1 ? -1 : pl.paso - 1;
}

// ---- Lo que va al MSX ----

static uint8_t *encabezado(uint8_t *p, uint16_t dir, uint16_t n)
{
    *p++ = dir >> 8;
    *p++ = dir & 0xFF;
    *p++ = n & 0xFF;
    *p++ = n >> 8;
    return p;
}

static uint8_t *tramo(uint8_t *p, uint16_t dir, const uint8_t *datos, uint16_t n)
{
    p = encabezado(p, dir, n);
    memcpy(p, datos, n);
    return p + n;
}

static void silencio(uint8_t *r)
{
    memset(r, 0, 11);
    r[7] = 0xBF;            // sin tonos ni ruido; el puerto A del PSG, entrada
}

// Los planetas, del mas cercano (plano 0, adelante) al mas lejano. Si en
// alguna linea hay mas de 4, el orden rota cada cuadro y parpadean.
static uint8_t *sprites(uint8_t *p)
{
    struct { int x, y; float cerca; uint8_t k; } v[8];
    int n = 0;

    for (int k = 0; k < 8; k++) {
        orbita_t o;
        float q[3];
        orbita(k, pl.minutos, &o);
        punto(&o, kepler(o.m, o.e), q);
        int x, y;
        float c;
        if (!proyectar(q, &x, &y, &c))
            continue;
        x -= 3;             // el centro del dibujo es (3, 3)
        y -= 3;
        if (x <= -8 || x >= 256 || y <= -8 || y >= 192)
            continue;
        int i = n++;
        while (i > 0 && v[i - 1].cerca < c) {
            v[i] = v[i - 1];
            i--;
        }
        v[i].x = x;
        v[i].y = y;
        v[i].cerca = c;
        v[i].k = (uint8_t)k;
    }

    int rotar = 0;
    for (int y = 0; y < 192 && !rotar; y++) {
        int c = 0;
        for (int i = 0; i < n; i++)
            c += y >= v[i].y && y < v[i].y + 8;
        rotar = c > 4;
    }

    uint8_t sat[8 * 4 + 1], *a = sat;
    for (int j = 0; j < n; j++) {
        int i = rotar ? (j + (int)pl.cuadro) % n : j;
        int x = v[i].x, color = planeta[v[i].k].color;
        if (x < 0) {
            x += 32;
            color |= 0x80;  // EC: 32 pixeles mas a la izquierda
        }
        *a++ = (uint8_t)(v[i].y - 1);   // el VDP lo dibuja una linea mas abajo
        *a++ = (uint8_t)x;
        *a++ = v[i].k;
        *a++ = (uint8_t)color;
    }
    *a++ = Y_FIN;
    return tramo(p, VRAM_SAT, sat, (uint16_t)(a - sat));
}

static int igual(const imagen_t *a, const imagen_t *b, int celda)
{
    int i = celda * 8;
    return memcmp(a->pat + i, b->pat + i, 8) == 0 && memcmp(a->col + i, b->col + i, 8) == 0;
}

// Las celdas que cambiaron, de a tiras seguidas en una fila, mientras
// entren antes de tope. La fila donde se corto es la primera del proximo.
static uint8_t *celdas(const imagen_t *nueva, imagen_t *vram, uint8_t *p, const uint8_t *tope)
{
    for (int f = 0; f < 24; f++) {
        int fila = (pl.fila + f) % 24;
        for (int c = 0; c < 32;) {
            if (igual(nueva, vram, fila * 32 + c)) {
                c++;
                continue;
            }
            int c0 = c;
            while (c < 32 && !igual(nueva, vram, fila * 32 + c))
                c++;
            int n = c - c0, caben = (int)(tope - p - 9) / 16;
            if (n > caben) n = caben;
            if (n > 0) {
                uint16_t off = (uint16_t)((fila * 32 + c0) * 8);
                p = tramo(p, VRAM_PAT + off, nueva->pat + off, (uint16_t)(n * 8));
                p = tramo(p, VRAM_COL + off, nueva->col + off, (uint16_t)(n * 8));
                memcpy(vram->pat + off, nueva->pat + off, n * 8);
                memcpy(vram->col + off, nueva->col + off, n * 8);
            }
            if (n < c - c0) {
                pl.fila = fila;
                return p;
            }
        }
    }
    return p;
}

uint32_t planetas_cuadro(const uint8_t *e, uint8_t *buf)
{
    imagen_t *nueva = (imagen_t *)(buf + PLANETAS_RESPUESTA), *vram = nueva + 1;

    entrada(e);
    if (!pl.pausa) {
        int p = pl.paso < 0 ? -pl.paso : pl.paso;
        pl.minutos += pl.paso < 0 ? -pasos[p - 1] : pasos[p - 1];
        if (pl.minutos > MIN_MAX || pl.minutos < -MIN_MAX) {
            pl.minutos = pl.minutos > 0 ? MIN_MAX : -MIN_MAX;
            pl.pausa = 1;
        }
    }
    // Las orbitas tambien cambian, despacio: se redibujan cada 10 anios
    if (labs((long)(pl.minutos - pl.minutos_orbitas)) > 3652L * DIA)
        pl.redibujar = 1;
    preparar_vista();
    if (pl.redibujar) {
        dibujar(nueva);
        pl.redibujar = 0;
    }
    textos(nueva);
    pl.cuadro++;

    silencio(buf);
    uint8_t *p = sprites(buf + 11);
    p = celdas(nueva, vram, p, buf + PRESUPUESTO);
    *p++ = 0xFF;
    return (uint32_t)(p - buf);
}

uint32_t planetas_iniciar(uint8_t *buf)
{
    imagen_t *nueva = (imagen_t *)(buf + PLANETAS_RESPUESTA), *vram = nueva + 1;

    memset(&pl, 0, sizeof pl);
    pl.minutos = hoy();
    pl.paso = PASO_INICIAL;
    pl.incl = 30;
    pl.escala = ESCALA_INI;
    pl.orbitas = 1;
    pl.redibujar = 1;
    pl.antes = 0xFFFF;      // lo que se aprieta antes de empezar no cuenta
    memset(vram->pat, 0, TABLA);
    memset(vram->col, C_VACIO, TABLA);
    memcpy(nueva, vram, sizeof *nueva);

    // SCREEN 2 con la pantalla apagada mientras se llenan las tablas
    static const uint8_t regs[8] = { 0x02, 0xA0, 0x06, 0xFF, 0x03, 0x36, 0x07, 0x01 };
    silencio(buf);
    uint8_t *p = buf + 11;
    for (int r = 0; r < 8; r++) {
        *p++ = 0xFE;
        *p++ = (uint8_t)r;
        *p++ = regs[r];
    }
    p = encabezado(p, VRAM_NOM, 768);
    for (int i = 0; i < 768; i++)
        *p++ = (uint8_t)i;
    p = tramo(p, VRAM_PAT, vram->pat, TABLA);
    p = tramo(p, VRAM_COL, vram->col, TABLA);
    p = encabezado(p, VRAM_SPR, 8 * 8);
    for (int k = 0; k < 8; k++)
        for (int f = 0; f < 8; f++) {
            const char *s = planeta[k].dibujo[f];
            uint8_t b = 0;
            for (int c = 0; s && s[c]; c++)
                if (s[c] == '#')
                    b |= 0x80 >> c;
            *p++ = b;
        }
    uint8_t fin = Y_FIN;
    p = tramo(p, VRAM_SAT, &fin, 1);
    *p++ = 0xFE;            // encendida, sprites de 8x8
    *p++ = 1;
    *p++ = 0xE0;
    *p++ = 0xFF;
    return (uint32_t)(p - buf);
}
