#include <string.h>
#include "tjpgd.h"
#include "sc2.h"

#define ANCHO           256
#define ALTO            192
#define BANDA_ANCHO     384     // pixeles de la imagen reducida (despues del recorte)
#define BANDA_FILAS     17      // bloques de 16 filas sin reducir, y la ultima de la banda anterior
#define POOL            5000    // memoria de trabajo de TJpgDec (pide unos 3,5 KB)

// Los ajustes de tools/jpg2sc2.py: saturacion 1,8 y contraste 1,2 (alrededor
// de 128), y se difunde el 80% del error.
#define SAT_X10         18
#define CON_X10         12

// Paleta del TMS9918 (la de los emuladores); el 0 es transparente y no se usa
static const uint8_t paleta[16][3] = {
    {0, 0, 0}, {0, 0, 0}, {33, 200, 66}, {94, 220, 120},
    {84, 85, 237}, {125, 118, 252}, {212, 82, 77}, {66, 235, 245},
    {252, 85, 84}, {255, 121, 120}, {212, 193, 84}, {230, 206, 128},
    {33, 176, 59}, {201, 91, 186}, {204, 204, 204}, {255, 255, 255},
};

// Peso de cada canal en la distancia (el ojo nota mas el verde)
static const int32_t peso[3] = {30, 59, 11};

#define PARES   105
static uint8_t par_a[PARES], par_b[PARES];

// ---- Estado de una conversion ----

static uint8_t banda[BANDA_FILAS][BANDA_ANCHO * 3];
static uint16_t col0[ANCHO], col1[ANCHO];  // columnas de la banda para cada x
static uint8_t frac_x[ANCHO];             // y cuanto de col1 (de 256)

static int16_t fila[ANCHO][3];            // la fila en curso, con el error que le llega
static int16_t err_a[ANCHO + 2][3], err_b[ANCHO + 2][3];
static int16_t (*err_esta)[3], (*err_prox)[3];  // desplazados en 1: err[x + 1]

static struct {
    int hs, x0, wc;         // alto reducido; recorte horizontal
    int paso;               // 2: en la banda va una columna de cada dos
    int top, filas;         // la banda: filas top a top + filas - 1 (en banda[1..])
    int y;                  // proxima fila de 256 pixeles
    uint8_t *pantalla;
} g;

static uint32_t pool[POOL / 4];

// ---- Pares de colores ----

static inline int clip(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static inline int32_t dist(const int *v, const uint8_t *c)
{
    int32_t d0 = v[0] - c[0], d1 = v[1] - c[1], d2 = v[2] - c[2];
    return peso[0] * d0 * d0 + peso[1] * d1 * d1 + peso[2] * d2 * d2;
}

// El par que mejor representa los 8 pixeles desde x0, con el error pasando
// de pixel en pixel dentro del tramo (los 7/16 de Floyd-Steinberg) para que
// el costo tenga en cuenta el tramado. Se deja de sumar al pasar al mejor.
static int mejor_par(int x0)
{
    int32_t mejor = INT32_MAX;
    int elegido = 0;
    for (int p = 0; p < PARES; p++) {
        const uint8_t *ca = paleta[par_a[p]], *cb = paleta[par_b[p]];
        int e[3] = {0, 0, 0};
        int32_t costo = 0;
        for (int i = 0; i < 8; i++) {
            int v[3];
            for (int c = 0; c < 3; c++)
                v[c] = clip(fila[x0 + i][c] + e[c]);
            int32_t da = dist(v, ca), db = dist(v, cb);
            const uint8_t *q = da < db ? ca : cb;
            costo += da < db ? da : db;
            if (costo >= mejor)
                break;
            for (int c = 0; c < 3; c++)
                e[c] = (v[c] - q[c]) * 45 >> 7;     // 7/16 del 80%
        }
        if (costo < mejor) {
            mejor = costo;
            elegido = p;
        }
    }
    return elegido;
}

// La fila y (ya en fila[], sin el error) a las tablas de patrones y colores
static void convertir_fila(int y)
{
    for (int x = 0; x < ANCHO; x++)
        for (int c = 0; c < 3; c++)
            fila[x][c] += err_esta[x + 1][c];
    memset(err_prox, 0, sizeof err_a);

    for (int t = 0; t < ANCHO / 8; t++) {
        int x0 = t * 8;
        int p = mejor_par(x0);
        const uint8_t *ca = paleta[par_a[p]], *cb = paleta[par_b[p]];
        uint8_t bits = 0;
        for (int i = 0; i < 8; i++) {
            int x = x0 + i, v[3];
            for (int c = 0; c < 3; c++)
                v[c] = clip(fila[x][c]);
            const uint8_t *q;
            if (dist(v, ca) < dist(v, cb)) {
                bits |= 0x80 >> i;
                q = ca;
            } else {
                q = cb;
            }
            for (int c = 0; c < 3; c++) {
                int e = (v[c] - q[c]) * 4 / 5;
                if (x + 1 < ANCHO)
                    fila[x + 1][c] += e * 7 / 16;
                err_prox[x][c] += e * 3 / 16;       // x - 1, desplazado
                err_prox[x + 1][c] += e * 5 / 16;
                err_prox[x + 2][c] += e / 16;
            }
        }
        // Tercio, caracter (columna + 32 * fila de caracteres) y linea
        int dir = (y >> 6) * 2048 + (((y >> 3) & 7) * 32 + t) * 8 + (y & 7);
        g.pantalla[dir] = bits;
        g.pantalla[SC2_TAM / 2 + dir] = (uint8_t)((par_a[p] << 4) | par_b[p]);  // bit en 1 = a
    }

    int16_t (*e)[3] = err_esta;
    err_esta = err_prox;
    err_prox = e;
}

// ---- De la banda a filas de 256 ----

static const uint8_t *fila_banda(int r)
{
    return banda[r < g.top ? 0 : 1 + r - g.top];
}

// Saturacion y contraste, como ImageEnhance de PIL (pero el contraste,
// alrededor de 128 en lugar del promedio de la imagen).
static void ajustar(int *rgb)
{
    int l = (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000;
    for (int c = 0; c < 3; c++) {
        int v = clip(l + (rgb[c] - l) * SAT_X10 / 10);
        rgb[c] = clip(128 + (v - 128) * CON_X10 / 10);
    }
}

// Arma y convierte las filas de 256 que ya se pueden interpolar con lo que
// hay en la banda. final: no va a llegar nada mas.
static void emitir(int final)
{
    int ultima = g.top + g.filas - 1;
    while (g.y < ALTO) {
        int32_t sy = (int32_t)((((int64_t)(2 * g.y + 1) * g.hs) << 16) / (2 * ALTO)) - 32768;
        if (sy < 0)
            sy = 0;
        int r0 = sy >> 16, r1 = r0 + 1 < g.hs ? r0 + 1 : g.hs - 1;
        int fy = (sy >> 8) & 0xFF;
        if (r1 > ultima) {
            if (!final)
                return;
            r1 = ultima;
            if (r0 > ultima)
                r0 = ultima;
        }
        const uint8_t *a = fila_banda(r0), *b = fila_banda(r1);
        for (int x = 0; x < ANCHO; x++) {
            int c0 = col0[x] * 3, c1 = col1[x] * 3, fx = frac_x[x];
            int rgb[3];
            for (int c = 0; c < 3; c++) {
                int arr = a[c0 + c] * (256 - fx) + a[c1 + c] * fx;
                int aba = b[c0 + c] * (256 - fx) + b[c1 + c] * fx;
                rgb[c] = (arr * (256 - fy) + aba * fy) >> 16;
            }
            ajustar(rgb);
            for (int c = 0; c < 3; c++)
                fila[x][c] = (int16_t)rgb[c];
        }
        convertir_fila(g.y++);
    }
}

// ---- TJpgDec ----

typedef struct {
    sc2_entrada_t f;
    void *ctx;
} dispositivo_t;

// TJpgDec da por roto el JPEG si no recibe todo lo que pide
static size_t al_leer(JDEC *jd, uint8_t *buf, size_t n)
{
    dispositivo_t *d = jd->device;
    size_t total = 0;
    while (total < n) {
        size_t m = d->f(d->ctx, buf ? buf + total : NULL, n - total);
        if (m == 0)
            break;
        total += m;
    }
    return total;
}

// Un bloque decodificado: va a la banda. El primero de una fila de bloques
// cierra la banda anterior.
static int al_dibujar(JDEC *jd, void *bitmap, JRECT *r)
{
    (void)jd;
    if (r->top != g.top) {
        if (g.filas) {
            emitir(0);
            memcpy(banda[0], banda[g.filas], g.wc * 3);
        }
        g.top = r->top;
        g.filas = r->bottom - r->top + 1;
        if (g.filas > BANDA_FILAS - 1)
            return 0;
    }
    int w = r->right - r->left + 1;
    int desde = r->left > g.x0 ? r->left : g.x0;
    int hasta = r->right < g.x0 + g.wc * g.paso - 1 ? r->right : g.x0 + g.wc * g.paso - 1;
    for (int y = r->top; y <= r->bottom; y++) {
        const uint8_t *src = (const uint8_t *)bitmap + (y - r->top) * w * 3;
        uint8_t *dst = banda[1 + y - r->top];
        for (int x = desde; x <= hasta; x++)
            if ((x - g.x0) % g.paso == 0)
                memcpy(dst + (x - g.x0) / g.paso * 3, src + (x - r->left) * 3, 3);
    }
    return 1;
}

int sc2_convertir(sc2_entrada_t entrada, void *ctx, uint8_t *pantalla,
                  uint16_t *ancho, uint16_t *alto, uint8_t *detalle)
{
    if (par_a[0] == 0) {
        int p = 0;
        for (int a = 1; a < 16; a++)
            for (int b = a + 1; b < 16; b++) {
                par_a[p] = a;
                par_b[p++] = b;
            }
    }

    *ancho = *alto = 0;
    *detalle = 0;
    memset(pantalla, 0, SC2_TAM);

    JDEC jd;
    dispositivo_t dev = {entrada, ctx};
    JRESULT r = jd_prepare(&jd, al_leer, pool, sizeof pool, &dev);
    if (r != JDR_OK) {
        *detalle = r;
        return SC2_JPEG;
    }
    *ancho = jd.width;
    *alto = jd.height;

    // La mayor reduccion que todavia cubre 256x192; mas si no entra en la
    // banda, y si ni asi, una columna de cada dos.
    int s = 3;
    while (s > 0 && ((jd.width >> s) < ANCHO || (jd.height >> s) < ALTO))
        s--;
    int ws, hs, wc;
    for (;;) {
        ws = jd.width >> s;
        hs = jd.height >> s;
        wc = ws * 100 > hs * 155 ? hs * 4 / 3 : ws;     // 16:9: se recortan los costados
        if (wc <= BANDA_ANCHO || s == 3)
            break;
        s++;
    }
    g.paso = wc > BANDA_ANCHO ? 2 : 1;
    if (wc > 2 * BANDA_ANCHO || wc < ANCHO / 8 || hs < ALTO / 8)
        return SC2_TAMANO;

    g.hs = hs;
    g.x0 = (ws - wc) / 2;
    wc /= g.paso;
    g.wc = wc;
    g.top = -1;
    g.filas = 0;
    g.y = 0;
    g.pantalla = pantalla;
    for (int x = 0; x < ANCHO; x++) {
        int32_t sx = (int32_t)((((int64_t)(2 * x + 1) * wc) << 16) / (2 * ANCHO)) - 32768;
        if (sx < 0)
            sx = 0;
        col0[x] = sx >> 16;
        col1[x] = col0[x] + 1 < wc ? col0[x] + 1 : wc - 1;
        frac_x[x] = (sx >> 8) & 0xFF;
    }
    memset(err_a, 0, sizeof err_a);
    memset(err_b, 0, sizeof err_b);
    err_esta = err_a;
    err_prox = err_b;

    r = jd_decomp(&jd, al_dibujar, s);
    if (r != JDR_OK) {
        *detalle = r;
        return SC2_JPEG;
    }
    if (g.filas)
        emitir(1);
    return SC2_OK;
}
