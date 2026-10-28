// De JPEG a SCREEN 2: el algoritmo de tools/jpg2sc2.py, en C y sin guardar
// la imagen entera. No depende de la Pico: se prueba tambien en la PC.
//
// El JPEG se decodifica con TJpgDec a medida que llega, reducido (1/2, 1/4,
// 1/8) lo justo para que siga cubriendo 256x192. Cada fila de bloques que
// sale se guarda en una banda, de ahi se interpolan las filas de 256 pixeles
// que ya se puedan armar, y cada una se convierte enseguida: se sube la
// saturacion y el contraste, y para cada tramo de 8x1 se prueban los 105
// pares de colores del TMS9918, con difusion de error (Floyd-Steinberg).
//
// Encuadre: hasta 1,55:1 (4:3, 5:4, los 704x576 de las camaras D1) la
// imagen se estira a 4:3; mas ancha (16:9) se recortan los costados.

#pragma once
#include <stddef.h>
#include <stdint.h>

#define SC2_TAM         12288   // patrones (6144) y colores (6144)

// Resultados de sc2_convertir
#define SC2_OK          0
#define SC2_JPEG        1       // TJpgDec no pudo: el codigo (JRESULT) queda en *detalle
#define SC2_TAMANO      2       // no entra en la banda, o es menor que 32x24

// Lee hasta n bytes del JPEG en buf; con buf NULL los saltea. Devuelve
// cuantos leyo o salteo (0: se termino o fallo).
typedef size_t (*sc2_entrada_t)(void *ctx, uint8_t *buf, size_t n);

// Arma en pantalla la tabla de patrones (0 a 6143) y la de colores (6144 a
// 12287), en el orden de la VRAM de SCREEN 2 con la tabla de nombres 0-255
// en cada tercio. ancho y alto: los del JPEG.
int sc2_convertir(sc2_entrada_t entrada, void *ctx, uint8_t *pantalla,
                  uint16_t *ancho, uint16_t *alto, uint8_t *detalle);

// La banda, prestada mientras no hay conversion: https.c pone ahi el bufer
// de entrada del TLS. Las dos cosas corren en el bucle del nucleo 0, nunca a
// la vez. Hay que devolverla en cero.
void *sc2_prestar(size_t *n);
