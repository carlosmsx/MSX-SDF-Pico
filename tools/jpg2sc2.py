#!/usr/bin/env python3
"""Convierte una foto (JPEG, PNG...) a SCREEN 2 del MSX1.

    python tools/jpg2sc2.py FOTO.JPG [-o SALIDA] [--dither fs|no] [--fuerza F]
                                     [--encuadre recortar|bandas|estirar]
                                     [--saturacion S] [--contraste C]

Es el prototipo de lo que haria la Pico con la imagen de una camara IP:
llevarla a 256x192 y elegir, para cada tramo de 8x1 pixeles, el par de
colores del TMS9918 que mejor la representa, con difusion de error
(Floyd-Steinberg). Antes se sube la saturacion y el contraste: los
colores del TMS9918 son muy vivos y una foto apagada termina en ruido de
tonos mezclados. firmware/src/sc2.c hace lo mismo en la Pico. El algoritmo esta escrito para pasarlo a C tal cual:
un tramo por vez, los 105 pares probados contra los 8 pixeles.

Deja tres archivos:
  SALIDA.SC2      imagen de VRAM 0000h-37FFh en formato BSAVE
                  (BLOAD"SALIDA.SC2",S desde SCREEN 2)
  SALIDA.png      lo que se veria en el MSX, ampliado x3
  SALIDA_cmp.png  la foto en 256x192 y el resultado, lado a lado
"""
import argparse
import itertools
import os
import sys
import time

import numpy as np
from PIL import Image, ImageEnhance

# Paleta del TMS9918 (valores habituales de los emuladores); el 0 es
# transparente y no se usa.
PALETA = np.array([
    (0, 0, 0),         # 0  transparente
    (0, 0, 0),         # 1  negro
    (33, 200, 66),     # 2  verde medio
    (94, 220, 120),    # 3  verde claro
    (84, 85, 237),     # 4  azul oscuro
    (125, 118, 252),   # 5  azul claro
    (212, 82, 77),     # 6  rojo oscuro
    (66, 235, 245),    # 7  celeste
    (252, 85, 84),     # 8  rojo medio
    (255, 121, 120),   # 9  rojo claro
    (212, 193, 84),    # 10 amarillo oscuro
    (230, 206, 128),   # 11 amarillo claro
    (33, 176, 59),     # 12 verde oscuro
    (201, 91, 186),    # 13 magenta
    (204, 204, 204),   # 14 gris
    (255, 255, 255),   # 15 blanco
], dtype=np.float32)

# Peso de cada canal en la distancia (el ojo nota mas el verde)
PESOS = np.array([0.30, 0.59, 0.11], dtype=np.float32) * 3

PARES = np.array(list(itertools.combinations(range(1, 16), 2)))  # 105 pares
COL_A = PALETA[PARES[:, 0]]   # (105, 3)
COL_B = PALETA[PARES[:, 1]]

ANCHO, ALTO = 256, 192


def encuadrar(img, modo):
    """Lleva la imagen a 256x192 (4:3)."""
    img = img.convert("RGB")
    w, h = img.size
    if modo == "recortar":
        if w * 3 > h * 4:            # mas ancha que 4:3: recorta los costados
            nw = h * 4 // 3
            img = img.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
        else:                        # mas alta: recorta arriba y abajo
            nh = w * 3 // 4
            img = img.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
        return img.resize((ANCHO, ALTO), Image.LANCZOS)
    if modo == "bandas":
        esc = min(ANCHO / w, ALTO / h)
        nw, nh = round(w * esc), round(h * esc)
        lienzo = Image.new("RGB", (ANCHO, ALTO))
        lienzo.paste(img.resize((nw, nh), Image.LANCZOS),
                     ((ANCHO - nw) // 2, (ALTO - nh) // 2))
        return lienzo
    return img.resize((ANCHO, ALTO), Image.LANCZOS)


def mejor_par(px, fuerza):
    """Prueba los 105 pares sobre un tramo de 8 pixeles.

    El error pasa al pixel siguiente del tramo (la parte de 7/16 de
    Floyd-Steinberg), para que el costo de cada par tenga en cuenta el
    tramado. Devuelve el indice del par de menor error total."""
    err = np.zeros((len(PARES), 3), dtype=np.float32)
    costo = np.zeros(len(PARES), dtype=np.float32)
    for i in range(8):
        v = np.clip(px[i] + err, 0, 255)
        da = (((v - COL_A) ** 2) * PESOS).sum(1)
        db = (((v - COL_B) ** 2) * PESOS).sum(1)
        usa_a = da < db
        costo += np.where(usa_a, da, db)
        q = np.where(usa_a[:, None], COL_A, COL_B)
        err = (v - q) * (7 / 16 * fuerza)
    return int(costo.argmin())


def convertir(rgb, fuerza):
    """rgb: (192, 256, 3) float. Devuelve (patrones, colores), 6144 bytes c/u,
    en el orden de la VRAM de SCREEN 2 (tabla de nombres 0-255 por tercio)."""
    img = rgb.astype(np.float32).copy()
    patrones = bytearray(6144)
    colores = bytearray(6144)
    for y in range(ALTO):
        fila = img[y]
        abajo = img[y + 1] if y + 1 < ALTO else np.zeros_like(fila)
        for t in range(ANCHO // 8):
            x0 = t * 8
            p = mejor_par(fila[x0:x0 + 8], fuerza)
            a, b = PARES[p]
            ca, cb = PALETA[a], PALETA[b]
            bits = 0
            for i in range(8):
                x = x0 + i
                v = np.clip(fila[x], 0, 255)
                da = (((v - ca) ** 2) * PESOS).sum()
                db = (((v - cb) ** 2) * PESOS).sum()
                if da < db:
                    bits |= 0x80 >> i
                    q = ca
                else:
                    q = cb
                e = (v - q) * fuerza
                if x + 1 < ANCHO:
                    fila[x + 1] += e * 7 / 16
                if x > 0:
                    abajo[x - 1] += e * 3 / 16
                abajo[x] += e * 5 / 16
                if x + 1 < ANCHO:
                    abajo[x + 1] += e * 1 / 16
            # Direccion en VRAM: tercio, caracter (columna + 32*fila), linea
            dir_ = (y >> 6) * 2048 + (((y >> 3) & 7) * 32 + t) * 8 + (y & 7)
            patrones[dir_] = bits
            colores[dir_] = (a << 4) | b    # bit en 1 = color a (frente)
    return patrones, colores


def vram_sc2(patrones, colores):
    """Imagen de VRAM 0000h-37FFh de SCREEN 2."""
    vram = bytearray(0x3800)
    vram[0x0000:0x1800] = patrones
    vram[0x1800:0x1B00] = bytes(i & 0xFF for i in range(768))
    vram[0x1B00] = 0xD0                 # sin sprites
    vram[0x2000:0x3800] = colores
    return vram


def a_rgb(patrones, colores):
    """Lo que dibujaria el VDP, para la vista previa."""
    out = np.zeros((ALTO, ANCHO, 3), dtype=np.uint8)
    for y in range(ALTO):
        for t in range(32):
            d = (y >> 6) * 2048 + (((y >> 3) & 7) * 32 + t) * 8 + (y & 7)
            bits, c = patrones[d], colores[d]
            for i in range(8):
                out[y, t * 8 + i] = PALETA[c >> 4 if bits & (0x80 >> i) else c & 15]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("foto")
    ap.add_argument("-o", "--salida", help="nombre base de salida (por defecto, el de la foto)")
    ap.add_argument("--dither", choices=("fs", "no"), default="fs")
    ap.add_argument("--fuerza", type=float, default=0.8,
                    help="fraccion del error que se difunde (0-1, por defecto 0.8)")
    ap.add_argument("--encuadre", choices=("recortar", "bandas", "estirar"),
                    default="recortar",
                    help="estirar sirve para camaras de 704x576 (4:3 con pixeles no cuadrados)")
    ap.add_argument("--saturacion", type=float, default=1.8)
    ap.add_argument("--contraste", type=float, default=1.2)
    args = ap.parse_args()

    base = args.salida or os.path.splitext(args.foto)[0]
    fuerza = 0.0 if args.dither == "no" else args.fuerza

    foto = encuadrar(Image.open(args.foto), args.encuadre)
    foto = ImageEnhance.Color(foto).enhance(args.saturacion)
    # Contraste alrededor de 128, como firmware/src/sc2.c (ImageEnhance usa
    # el promedio de la imagen, que la Pico no conoce hasta el final)
    foto = foto.point(lambda v: 128 + (v - 128) * args.contraste)
    t0 = time.time()
    patrones, colores = convertir(np.asarray(foto), fuerza)
    print(f"convertida en {time.time() - t0:.1f} s")

    vram = vram_sc2(patrones, colores)
    with open(base + ".SC2", "wb") as f:
        f.write(bytes([0xFE, 0x00, 0x00, 0xFF, 0x37, 0x00, 0x00]) + vram)

    msx = Image.fromarray(a_rgb(patrones, colores))
    msx.resize((ANCHO * 3, ALTO * 3), Image.NEAREST).save(base + ".png")
    cmp_ = Image.new("RGB", (ANCHO * 4 + 8, ALTO * 2))
    cmp_.paste(foto.resize((ANCHO * 2, ALTO * 2), Image.NEAREST), (0, 0))
    cmp_.paste(msx.resize((ANCHO * 2, ALTO * 2), Image.NEAREST), (ANCHO * 2 + 8, 0))
    cmp_.save(base + "_cmp.png")
    print(f"{base}.SC2, {base}.png, {base}_cmp.png")


if __name__ == "__main__":
    sys.exit(main())
