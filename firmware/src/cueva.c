// CUEVA (cueva.h): una nave en una cueva que avanza sola, con enemigos que
// llegan por la derecha. Todo lo calcula la Pico; el MSX copia en cada
// cuadro los registros del PSG, la tabla de sprites (128 bytes), los
// patrones del borde de la cueva y la tabla de nombres entera (768 bytes).
//
// La pantalla es SCREEN 1: la fuente de la BIOS sirve para los textos. El
// mapa tiene la altura del techo y del piso de cada columna de pixeles, con
// pendientes y puntas. Las celdas de pared llena o vacia usan
// patrones fijos; las del borde se dibujan pixel a pixel en cada cuadro y se
// mandan sin repetir, en uno de dos bancos de 64 caracteres: mientras la
// pantalla muestra uno, se escribe el otro, y no se ve el cambio a medias.
// Las estrellas van a la mitad de velocidad, con un patron por posicion.

#include <stdio.h>
#include <string.h>
#include "cueva.h"

// ---- Pantalla ----

#define COLS      32
#define FILAS     24
#define ARRIBA    1         // la fila 0 es la de los puntos
#define JUEGO     (FILAS - ARRIBA)
#define MAPA      512       // columnas de pixeles guardadas (potencia de 2)

#define VRAM_PAT  0x0000    // SCREEN 1
#define VRAM_NOM  0x1800
#define VRAM_SAT  0x1B00
#define VRAM_COL  0x2000
#define VRAM_SPR  0x3800

#define T_LLENO   0x00      // pared llena
#define T_ESTRELLA 0x08     // 08h-0Fh: un punto en la columna s
#define T_BANCO0  0x80      // 80h-BFh y C0h-FFh: los bordes, un banco por cuadro
#define T_BANCO1  0xC0
#define BANCO     64
#define T_VACIO   ' '
#define Y_FIN     0xD0

// Colores (frente y fondo) de cada grupo de 8 caracteres
#define C_TEXTO   0xF1      // blanco sobre negro
#define C_PARED   0xC1      // verde oscuro
#define C_ESTRELLA 0xE1     // gris

// ---- Sprites de 16x16 ----

enum { S_NAVE, S_ENEMIGO, S_BALA, S_EXPLOSION, S_CANTIDAD };

static const char *const dibujos[S_CANTIDAD][16] = {
    {   "................",
        "................",
        "..##............",
        "..####..........",
        "...######.......",
        "...#########....",
        "..############..",
        ".###############",
        ".###############",
        "..############..",
        "...#########....",
        "...######.......",
        "..####..........",
        "..##............",
        "................",
        "................" },
    {   "................",
        "......####......",
        "....########....",
        "...##########...",
        "..###..##..###..",
        "..###..##..###..",
        ".##############.",
        "################",
        "################",
        ".#.##.####.##.#.",
        "..############..",
        "....##....##....",
        "...##......##...",
        "..##........##..",
        "................",
        "................" },
    {   "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "....########....",
        "....########....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................" },
    {   "#......#.......#",
        ".#.....#......#.",
        "..#...###....#..",
        "...#.#####..#...",
        "....#######.....",
        "...#########....",
        "..###########...",
        "#############..#",
        "..###########...",
        "...#########.#..",
        "....#######...#.",
        "...#.#####.....#",
        "..#...###.......",
        ".#.....#........",
        "#......#........",
        "...............#" },
};

#define COLOR_NAVE     15
#define COLOR_ENEMIGO  13   // magenta
#define COLOR_BALA     11   // amarillo
#define COLOR_EXPLOSION 9   // rojo claro

// ---- Juego ----

enum { E_PRESENTACION, E_JUGANDO, E_MURIENDO, E_FIN };

#define BALAS      3
#define ENEMIGOS   8
#define VIDAS      3
#define INVULNERABLE 100    // cuadros, al volver
#define MUERTE     60       // cuadros entre el choque y volver

typedef struct { int x, y; uint8_t activa; } bala_t;

// El generador del mapa: tramos rectos (8.8) y una punta en curso
typedef struct {
    int t, p, dt, dp, resto;
    int punta, punta_largo, punta_alto;
    uint8_t punta_arriba;
} generador_t;

typedef struct {
    int x, y0;              // x en 8.8; y0 el centro de la onda
    int vx, fase;
    uint8_t estado;         // 0 libre, 1 volando, 2 explotando
    uint8_t t;
} enemigo_t;

static struct {
    uint8_t estado;
    uint32_t desp;          // pixel del mapa en el borde izquierdo
    uint32_t hecha;         // la proxima columna por generar
    uint8_t techo[MAPA], piso[MAPA];    // pixeles de pared arriba y abajo
    generador_t g;
    int x, y;               // la nave
    int invulnerable, t;    // t: cuenta de E_MURIENDO
    int vidas;
    uint32_t puntos, maximo;
    int espera_tiro, espera_enemigo;
    uint8_t disparo_antes;
    bala_t bala[BALAS];
    enemigo_t enemigo[ENEMIGOS];
    uint32_t cuadro;
} j;

static uint32_t azar = 1;

static int sorteo(int n)
{
    azar = azar * 1103515245u + 12345u;
    return (int)((azar >> 16) % (uint32_t)n);
}

// Seno de 32 pasos, amplitud 16
static int seno(int i)
{
    static const int8_t cuarto[9] = { 0, 3, 6, 9, 11, 13, 15, 16, 16 };
    i &= 31;
    if (i < 8)  return cuarto[i];
    if (i < 16) return cuarto[16 - i];
    if (i < 24) return -cuarto[i - 16];
    return -cuarto[32 - i];
}

// ---- El mapa ----

#define ALTO      (JUEGO * 8)

// El hueco minimo entre techo y piso, en pixeles: se achica con la distancia
static int hueco_minimo(void)
{
    int h = 13 - (int)(j.desp / 1500);
    return (h < 7 ? 7 : h) * 8;
}

// Las pendientes posibles (8.8, pixeles por columna): con pocas, los bordes
// se repiten y caben en el banco de patrones de cada cuadro
static const int16_t pendientes[] = { 0, 0, 64, -64, 128, -128, 256, -256 };

static void nuevo_tramo(void)
{
    generador_t *g = &j.g;
    g->resto = 8 * (2 + sorteo(5));
    g->dt = pendientes[sorteo(8)];
    g->dp = pendientes[sorteo(8)];
    // Que no se vaya de 4 a 72 pixeles al final del tramo
    int t = (g->t + g->dt * g->resto) >> 8, p = (g->p + g->dp * g->resto) >> 8;
    if (t < 4 || t > 72) g->dt = -g->dt;
    if (p < 4 || p > 72) g->dp = -g->dp;
    // A veces, una estalactita o una estalagmita: un triangulo de pendiente
    // 1 o 2
    if (!g->punta && sorteo(3) == 0) {
        g->punta = g->punta_largo = 8 * (1 + sorteo(3));
        g->punta_alto = 1 + sorteo(2);
        g->punta_arriba = (uint8_t)sorteo(2);
    }
}

static void generar(uint32_t x)
{
    generador_t *g = &j.g;
    int t, p;
    if (x < 320) {          // el principio, plano
        t = p = 16;
        g->t = t << 8;
        g->p = p << 8;
    } else {
        if (g->resto == 0)
            nuevo_tramo();
        g->t += g->dt;
        g->p += g->dp;
        g->resto--;
        t = g->t >> 8;
        p = g->p >> 8;
        if (g->punta) {
            int k = g->punta_largo - g->punta;
            int h = g->punta_alto * (k < g->punta ? k : g->punta);
            if (g->punta_arriba) t += h; else p += h;
            g->punta--;
        }
    }
    if (t < 2) t = 2;
    if (p < 2) p = 2;
    while (t + p > ALTO - hueco_minimo()) {
        if (t > p) t--; else p--;
    }
    j.techo[x & (MAPA - 1)] = (uint8_t)t;
    j.piso[x & (MAPA - 1)] = (uint8_t)p;
}

// Un pixel del mapa: x desde el principio de la cueva, y desde arriba de la
// pantalla
static int pared(uint32_t x, int y)
{
    if (y < ARRIBA * 8)
        return 0;
    return y < ARRIBA * 8 + j.techo[x & (MAPA - 1)]
        || y >= FILAS * 8 - j.piso[x & (MAPA - 1)];
}

// Un pixel de la pantalla, contra la pared
static int choca(int px, int py)
{
    if (py < 0 || py >= FILAS * 8 || px < 0 || px >= COLS * 8)
        return 0;
    return pared(j.desp + (uint32_t)px, py);
}

// El centro del hueco en la columna de la pantalla x, en pixeles (para un
// sprite de 16, ya restados los 8)
static int centro_hueco(int x)
{
    uint32_t m = (j.desp + (uint32_t)x) & (MAPA - 1);
    return (ARRIBA * 8 + j.techo[m] + FILAS * 8 - j.piso[m]) / 2 - 8;
}

// ---- Partida ----

static void nueva_nave(void)
{
    j.x = 24;
    j.y = centro_hueco(j.x + 8);
    j.invulnerable = INVULNERABLE;
}

static void reiniciar(uint8_t estado)
{
    uint32_t maximo = j.maximo;
    memset(&j, 0, sizeof j);
    j.maximo = maximo;
    j.estado = estado;
    j.vidas = VIDAS;
    j.espera_enemigo = 100;
    azar = 1;
    while (j.hecha < COLS * 8 + 8)
        generar(j.hecha++);
    nueva_nave();
}

static void avanzar_cueva(void)
{
    j.desp++;
    while (j.hecha <= j.desp + COLS * 8 + 8)
        generar(j.hecha++);
}

// ---- Sonido ----
//
// Canal A: la melodia; B: el bajo; C: los efectos (tono para el disparo,
// ruido para las explosiones). Sin la envolvente del PSG: los volumenes los
// baja la Pico cuadro a cuadro.

#define PASO_MUSICA 7       // cuadros por paso

// La melodia (notas MIDI, 0: silencio) y el bajo, uno cada dos pasos
static const uint8_t melodia[32] = {
    69, 0, 72, 0, 76, 74, 72, 0,   71, 0, 72, 74, 0, 71, 67, 0,
    69, 0, 72, 0, 76, 79, 77, 76,  74, 0, 72, 71, 69, 0, 0, 0,
};
static const uint8_t bajo[16] = {
    45, 45, 45, 45,  43, 43, 43, 43,  41, 41, 41, 41,  40, 40, 43, 43,
};

static struct {
    int paso, cuenta;
    int per_a, vol_a, meta_a;
    int per_b, vol_b;
    int tiro, explosion, largo_explosion;
} son;

// Periodo del PSG (reloj de 3,58 MHz / 32) de una nota MIDI
static int periodo(int nota)
{
    static const uint16_t do4[12] = { 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 227 };
    int o = (nota - 60) / 12, n = (nota - 60) % 12;
    if (n < 0) {
        n += 12;
        o--;
    }
    return o >= 0 ? do4[n] >> o : do4[n] << -o;
}

static void sonar_explosion(int largo)
{
    son.explosion = son.largo_explosion = largo;
}

static void sonido(uint8_t *r)
{
    if (son.cuenta == 0) {
        int n = melodia[son.paso];
        if (n) {
            son.per_a = periodo(n);
            son.vol_a = 13;
            son.meta_a = 9;
        } else {
            son.meta_a = 0;
        }
        if (!(son.paso & 1)) {
            son.per_b = periodo(bajo[son.paso >> 1]);
            son.vol_b = 12;
        }
        son.paso = (son.paso + 1) & 31;
    }
    son.cuenta = (son.cuenta + 1) % PASO_MUSICA;
    if (son.vol_a > son.meta_a && (j.cuadro & 1))
        son.vol_a--;
    if (son.vol_b > 0 && j.cuadro % 3 == 0)
        son.vol_b--;

    int per_c = 0, vol_c = 0, ruido = 0, tono_c = 0, ruido_c = 0;
    if (son.explosion) {
        int pasado = son.largo_explosion - son.explosion;
        ruido = 8 + pasado / 2;
        if (ruido > 31) ruido = 31;
        vol_c = 15 * son.explosion / son.largo_explosion;
        ruido_c = 1;
        son.explosion--;
    } else if (son.tiro) {
        per_c = 60 + (8 - son.tiro) * 25;
        vol_c = 6 + son.tiro;
        tono_c = 1;
        son.tiro--;
    }

    r[0] = son.per_a & 0xFF;
    r[1] = son.per_a >> 8;
    r[2] = son.per_b & 0xFF;
    r[3] = son.per_b >> 8;
    r[4] = per_c & 0xFF;
    r[5] = per_c >> 8;
    r[6] = (uint8_t)ruido;
    // Mezclador: bit 7 en 1 y bit 6 en 0, como lo deja la BIOS (el puerto A
    // del PSG es el de los joysticks); tono en A y B; en C, lo que toque.
    r[7] = 0x80 | 0x18 | (tono_c ? 0 : 0x04) | (ruido_c ? 0 : 0x20);
    r[8] = (uint8_t)son.vol_a;
    r[9] = (uint8_t)son.vol_b;
    r[10] = (uint8_t)vol_c;
}

static void silencio(uint8_t *r)
{
    memset(r, 0, 11);
    r[7] = 0xBF;
}

// ---- Un cuadro ----

static void mover(uint8_t joy, uint8_t fila5, uint8_t fila2)
{
    // Teclado: W arriba, Z abajo, A izquierda, S derecha y el punto dispara
    int arriba = !(joy & 0x01) || !(fila5 & 0x10);
    int abajo = !(joy & 0x02) || !(fila5 & 0x80);
    int izquierda = !(joy & 0x04) || !(fila2 & 0x40);
    int derecha = !(joy & 0x08) || !(fila5 & 0x01);
    int disparo = !(joy & 0x10) || !(fila2 & 0x08);
    int nuevo = disparo && !j.disparo_antes;
    j.disparo_antes = (uint8_t)disparo;

    avanzar_cueva();

    if (j.estado == E_PRESENTACION || j.estado == E_FIN) {
        if (nuevo)
            reiniciar(E_JUGANDO);
        return;
    }

    if (j.estado == E_JUGANDO) {
        if (arriba) j.y -= 2;
        if (abajo) j.y += 2;
        if (izquierda) j.x -= 2;
        if (derecha) j.x += 2;
        if (j.x < 0) j.x = 0;
        if (j.x > COLS * 8 - 16) j.x = COLS * 8 - 16;
        if (j.y < ARRIBA * 8) j.y = ARRIBA * 8;
        if (j.y > FILAS * 8 - 16) j.y = FILAS * 8 - 16;
        if (j.invulnerable)
            j.invulnerable--;

        if (j.espera_tiro)
            j.espera_tiro--;
        if (disparo && !j.espera_tiro)
            for (int i = 0; i < BALAS; i++)
                if (!j.bala[i].activa) {
                    j.bala[i] = (bala_t){ j.x + 8, j.y, 1 };
                    j.espera_tiro = 8;
                    son.tiro = 8;
                    break;
                }
    } else if (j.estado == E_MURIENDO && --j.t == 0) {
        if (j.vidas == 0) {
            j.estado = E_FIN;
            memset(j.bala, 0, sizeof j.bala);
            memset(j.enemigo, 0, sizeof j.enemigo);
            if (j.puntos > j.maximo)
                j.maximo = j.puntos;
        } else {
            j.estado = E_JUGANDO;
            nueva_nave();
        }
    }

    // Las balas
    for (int i = 0; i < BALAS; i++) {
        bala_t *b = &j.bala[i];
        if (!b->activa)
            continue;
        b->x += 6;
        if (b->x > COLS * 8 - 8 || choca(b->x + 8, b->y + 8))
            b->activa = 0;
    }

    // Los enemigos: aparecen a la derecha, en el hueco, y van ondulando
    if (--j.espera_enemigo <= 0) {
        int d = 70 - (int)(j.desp / 400);
        j.espera_enemigo = (d < 25 ? 25 : d) + sorteo(30);
        for (int i = 0; i < ENEMIGOS; i++)
            if (!j.enemigo[i].estado) {
                enemigo_t *e = &j.enemigo[i];
                e->x = 255 << 8;
                e->y0 = centro_hueco(255);
                e->vx = 256 + sorteo(192);
                e->fase = sorteo(32);
                e->estado = 1;
                break;
            }
    }
    for (int i = 0; i < ENEMIGOS; i++) {
        enemigo_t *e = &j.enemigo[i];
        if (e->estado == 2) {
            if (--e->t == 0)
                e->estado = 0;
            continue;
        }
        if (!e->estado)
            continue;
        e->x -= e->vx;
        e->fase++;
        if (e->x < -(16 << 8)) {
            e->estado = 0;
            continue;
        }
        int ex = e->x >> 8, ey = e->y0 + seno(e->fase >> 1);
        for (int k = 0; k < BALAS; k++) {
            bala_t *b = &j.bala[k];
            int dx = b->x - ex, dy = b->y - ey;
            if (b->activa && dx > -10 && dx < 10 && dy > -8 && dy < 8) {
                b->activa = 0;
                e->estado = 2;
                e->t = 12;
                j.puntos += 10;
                sonar_explosion(15);
                break;
            }
        }
        if (e->estado == 1 && j.estado == E_JUGANDO && !j.invulnerable) {
            int dx = j.x - ex, dy = j.y - ey;
            if (dx > -12 && dx < 12 && dy > -10 && dy < 10) {
                e->estado = 2;
                e->t = 12;
                j.estado = E_MURIENDO;
            }
        }
    }

    // La nave contra la pared: la punta y los dos extremos de atras
    if (j.estado == E_JUGANDO && !j.invulnerable
        && (choca(j.x + 15, j.y + 8) || choca(j.x + 3, j.y + 3) || choca(j.x + 3, j.y + 12)))
        j.estado = E_MURIENDO;

    if (j.estado == E_MURIENDO && j.t == 0) {
        j.t = MUERTE;
        j.vidas--;
        sonar_explosion(40);
        for (int i = 0; i < BALAS; i++)
            j.bala[i].activa = 0;
    }

    if (j.estado == E_JUGANDO && (j.desp & 31) == 0)
        j.puntos++;
}

// ---- Lo que va al MSX ----

static uint8_t *tramo(uint8_t *p, uint16_t dir, const uint8_t *datos, uint16_t n)
{
    *p++ = dir >> 8;
    *p++ = dir & 0xFF;
    *p++ = n & 0xFF;
    *p++ = n >> 8;
    memcpy(p, datos, n);
    return p + n;
}

static void texto(uint8_t pant[FILAS][COLS], int fila, int col, const char *s)
{
    while (*s && col < COLS)
        pant[fila][col++] = (uint8_t)*s++;
}

static void centrado(uint8_t pant[FILAS][COLS], int fila, const char *s)
{
    texto(pant, fila, (COLS - (int)strlen(s)) / 2, s);
}

// El caracter de una celda de borde: el patron se busca entre los de este
// cuadro y, si no esta, se agrega. Si el banco se llena, la celda queda
// llena o vacia segun la mitad de sus pixeles.
static uint8_t caracter(const uint8_t d[8], uint8_t pat[BANCO][8], int *n, uint8_t primero)
{
    int unos = 0, ceros = 0;
    for (int r = 0; r < 8; r++) {
        unos += d[r] == 0xFF;
        ceros += d[r] == 0;
    }
    if (unos == 8)
        return T_LLENO;
    if (ceros == 8)
        return T_VACIO;
    for (int i = 0; i < *n; i++)
        if (!memcmp(pat[i], d, 8))
            return (uint8_t)(primero + i);
    if (*n < BANCO) {
        memcpy(pat[*n], d, 8);
        return (uint8_t)(primero + (*n)++);
    }
    int bits = 0;
    for (int r = 0; r < 8; r++)
        for (uint8_t b = d[r]; b; b &= b - 1)
            bits++;
    return bits >= 32 ? T_LLENO : T_VACIO;
}

// La tabla de nombres y los patrones de borde de este cuadro (devuelve
// cuantos)
static int armar_pantalla(uint8_t pant[FILAS][COLS], uint8_t pat[BANCO][8], uint8_t primero)
{
    int n = 0;
    for (int c = 0; c < COLS; c++) {
        // En las 8 columnas de la celda: hasta donde la pared es seguro
        // llena y desde donde es seguro vacia
        uint32_t x0 = j.desp + 8 * c;
        int tmin = 255, tmax = 0, pmin = 255, pmax = 0;
        for (int i = 0; i < 8; i++) {
            int t = j.techo[(x0 + i) & (MAPA - 1)], p = j.piso[(x0 + i) & (MAPA - 1)];
            if (t < tmin) tmin = t;
            if (t > tmax) tmax = t;
            if (p < pmin) pmin = p;
            if (p > pmax) pmax = p;
        }
        for (int f = ARRIBA; f < FILAS; f++) {
            int y0 = f * 8;
            uint8_t t;
            if (y0 + 8 <= ARRIBA * 8 + tmin || y0 >= FILAS * 8 - pmin) {
                t = T_LLENO;
            } else if (y0 >= ARRIBA * 8 + tmax && y0 + 8 <= FILAS * 8 - pmax) {
                t = T_VACIO;
            } else {
                uint8_t d[8];
                for (int r = 0; r < 8; r++) {
                    uint8_t b = 0;
                    for (int i = 0; i < 8; i++)
                        if (pared(x0 + i, y0 + r))
                            b |= 0x80 >> i;
                    d[r] = b;
                }
                t = caracter(d, pat, &n, primero);
            }
            pant[f][c] = t;
        }
    }

    // Estrellas, a la mitad de velocidad: una cada tanto en columnas de 8
    // pixeles del fondo, solo en las celdas vacias
    uint32_t fondo = j.desp >> 1;
    for (uint32_t sc = fondo >> 3; sc <= (fondo >> 3) + COLS; sc++) {
        uint32_t h = sc * 2654435761u;
        if ((h >> 28) % 3)
            continue;
        int x = (int)(sc * 8 + 3 - fondo);
        int f = ARRIBA + (int)((h >> 8) % JUEGO);
        if (x >= 0 && x < COLS * 8 && pant[f][x >> 3] == T_VACIO)
            pant[f][x >> 3] = T_ESTRELLA + (x & 7);
    }

    char linea[COLS + 1];
    snprintf(linea, sizeof linea, "PUNTOS %06lu MAX %06lu VIDAS %d",
             (unsigned long)(j.puntos % 1000000), (unsigned long)(j.maximo % 1000000), j.vidas);
    memset(pant[0], T_VACIO, COLS);
    texto(pant, 0, 0, linea);

    if (j.estado == E_PRESENTACION) {
        centrado(pant, 8, " C U E V A ");
        centrado(pant, 11, " PUNTO O DISPARO: EMPEZAR ");
        centrado(pant, 13, " W A S Z O JOYSTICK: MOVER ");
        centrado(pant, 15, " ESC: SALIR ");
    } else if (j.estado == E_FIN) {
        centrado(pant, 10, " FIN DEL JUEGO ");
        centrado(pant, 13, " PUNTO O DISPARO: OTRA VEZ ");
    }
    return n;
}

// Un plano de la tabla de atributos. Los que estan a la izquierda del borde
// usan el bit EC (32 pixeles mas a la izquierda).
static uint8_t *sprite(uint8_t *a, int x, int y, int patron, int color)
{
    if (x < 0) {
        x += 32;
        color |= 0x80;
    }
    if (y < 0) y = 0;
    if (y > FILAS * 8 - 16) y = FILAS * 8 - 16;
    a[0] = (uint8_t)(y - 1);    // el VDP lo dibuja una linea mas abajo
    a[1] = (uint8_t)x;
    a[2] = (uint8_t)(patron * 4);
    a[3] = (uint8_t)color;
    return a + 4;
}

static void armar_sprites(uint8_t *sat)
{
    // La nave va siempre en el plano 0; el resto rota cuadro a cuadro, para
    // que los que pasan de 4 en una linea parpadeen en lugar de no verse.
    uint8_t otros[(BALAS + ENEMIGOS) * 4];
    uint8_t *p = sat, *o = otros;

    if (j.estado == E_JUGANDO && !(j.invulnerable & 4))
        p = sprite(p, j.x, j.y, S_NAVE, COLOR_NAVE);
    else if (j.estado == E_MURIENDO && j.t > MUERTE / 2)
        p = sprite(p, j.x, j.y, S_EXPLOSION, (j.t & 2) ? COLOR_EXPLOSION : COLOR_BALA);

    for (int i = 0; i < BALAS; i++)
        if (j.bala[i].activa)
            o = sprite(o, j.bala[i].x, j.bala[i].y, S_BALA, COLOR_BALA);
    for (int i = 0; i < ENEMIGOS; i++) {
        const enemigo_t *e = &j.enemigo[i];
        if (e->estado == 1)
            o = sprite(o, e->x >> 8, e->y0 + seno(e->fase >> 1), S_ENEMIGO, COLOR_ENEMIGO);
        else if (e->estado == 2)
            o = sprite(o, e->x >> 8, e->y0 + seno(e->fase >> 1), S_EXPLOSION, COLOR_EXPLOSION);
    }

    int n = (int)(o - otros) / 4;
    for (int i = 0; i < n; i++)
        p = (uint8_t *)memcpy(p, otros + 4 * ((i + j.cuadro) % n), 4) + 4;
    while (p < sat + 128) {
        *p = Y_FIN;
        p += 4;
    }
}

uint32_t cueva_cuadro(uint8_t joy, uint8_t fila5, uint8_t fila2, uint8_t *buf)
{
    static uint8_t pant[FILAS][COLS];
    static uint8_t pat[BANCO][8];
    static uint8_t banco;   // no vuelve a 0 con una partida nueva
    uint8_t sat[128];

    if (j.estado == E_PRESENTACION && j.hecha == 0)
        reiniciar(E_PRESENTACION);
    mover(joy, fila5, fila2);
    j.cuadro++;
    sonido(buf);
    armar_sprites(sat);
    banco ^= 1;
    uint8_t primero = banco ? T_BANCO1 : T_BANCO0;
    int n = armar_pantalla(pant, pat, primero);

    // Los patrones antes que la tabla de nombres que los usa
    uint8_t *p = buf + 11;
    p = tramo(p, VRAM_SAT, sat, sizeof sat);
    if (n)
        p = tramo(p, (uint16_t)(VRAM_PAT + primero * 8), &pat[0][0], (uint16_t)(n * 8));
    p = tramo(p, VRAM_NOM, &pant[0][0], sizeof pant);
    *p++ = 0xFF;
    return (uint32_t)(p - buf);
}

uint32_t cueva_iniciar(uint8_t *buf)
{
    uint8_t pat[16 * 8], col[32], spr[S_CANTIDAD * 32], nom[FILAS * COLS];

    reiniciar(E_PRESENTACION);
    memset(&son, 0, sizeof son);

    // La pared llena y las estrellas: un punto en la fila 3, columna s. Los
    // bordes los manda cada cuadro.
    memset(pat, 0, sizeof pat);
    memset(pat, 0xFF, 8);
    for (int s = 0; s < 8; s++)
        pat[8 * (8 + s) + 3] = 0x80 >> s;
    memset(col, C_TEXTO, sizeof col);
    col[T_LLENO / 8] = C_PARED;
    col[T_ESTRELLA / 8] = C_ESTRELLA;
    for (int g = T_BANCO0 / 8; g < 32; g++)   // los bordes, del mismo color
        col[g] = C_PARED;

    memset(spr, 0, sizeof spr);
    for (int k = 0; k < S_CANTIDAD; k++)
        for (int f = 0; f < 16; f++)
            for (int c = 0; c < 16; c++)
                if (dibujos[k][f][c] == '#')
                    spr[32 * k + (c >= 8 ? 16 : 0) + f] |= 0x80 >> (c & 7);
    memset(nom, T_VACIO, sizeof nom);

    silencio(buf);
    uint8_t *p = buf + 11;
    *p++ = 0xFE;            // registro 1: SCREEN 1 encendida, sprites de 16x16
    *p++ = 1;
    *p++ = 0xE2;
    p = tramo(p, VRAM_PAT + T_LLENO * 8, pat, sizeof pat);
    p = tramo(p, VRAM_COL, col, sizeof col);
    p = tramo(p, VRAM_SPR, spr, sizeof spr);
    p = tramo(p, VRAM_NOM, nom, sizeof nom);
    uint8_t fin = Y_FIN;
    p = tramo(p, VRAM_SAT, &fin, 1);
    *p++ = 0xFF;
    return (uint32_t)(p - buf);
}
