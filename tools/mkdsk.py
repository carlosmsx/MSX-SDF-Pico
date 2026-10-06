#!/usr/bin/env python3
"""Arma una imagen .DSK de 720 KB (FAT12, 3.5" doble cara) con los archivos dados.

    python tools/mkdsk.py salida.dsk archivo1 [archivo2 ...]

Los nombres se pasan a mayusculas y 8.3. La imagen no lleva sistema: sirve de
disco de datos (B:) o, junto con un disco que arranque, para llevar programas
al MSX. Se copia a la tarjeta SD y se monta con CALL SDFMOUNT.
"""
import os
import struct
import sys
import time

BPS, SPC, RSV, NFAT, ROOT, TOTAL, MEDIA, SPF, SPT, CARAS = 512, 2, 1, 2, 112, 1440, 0xF9, 3, 9, 2
RAIZ_SEC = RSV + NFAT * SPF
DATOS_SEC = RAIZ_SEC + ROOT * 32 // BPS
CLUSTER = SPC * BPS


def nombre_83(ruta):
    base, ext = os.path.splitext(os.path.basename(ruta))
    base, ext = base.upper(), ext[1:].upper()
    if not base or len(base) > 8 or len(ext) > 3:
        sys.exit("nombre no valido para 8.3: " + ruta)
    return base.ljust(8).encode("ascii"), ext.ljust(3).encode("ascii")


def fecha_dos(t):
    lt = time.localtime(t)
    fecha = ((lt.tm_year - 1980) << 9) | (lt.tm_mon << 5) | lt.tm_mday
    hora = (lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec // 2)
    return fecha, hora


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    salida, archivos = sys.argv[1], sys.argv[2:]

    img = bytearray(TOTAL * BPS)
    boot = struct.pack("<3s8sHBHBHHBHHHII", b"\xeb\xfe\x90", b"SDFPICO ", BPS, SPC, RSV, NFAT,
                       ROOT, TOTAL, MEDIA, SPF, SPT, CARAS, 0, 0)
    img[:len(boot)] = boot
    img[510:512] = b"\x55\xaa"

    fat = bytearray(SPF * BPS)
    fat[0:3] = bytes([MEDIA, 0xFF, 0xFF])

    def poner(n, v):
        o = n * 3 // 2
        if n % 2 == 0:
            fat[o] = v & 0xFF
            fat[o + 1] = (fat[o + 1] & 0xF0) | (v >> 8)
        else:
            fat[o] = (fat[o] & 0x0F) | ((v & 0x0F) << 4)
            fat[o + 1] = v >> 4

    siguiente = 2
    raiz = bytearray(ROOT * 32)
    for i, ruta in enumerate(archivos):
        datos = open(ruta, "rb").read()
        n = max(1, -(-len(datos) // CLUSTER))
        if siguiente - 2 + n > (TOTAL - DATOS_SEC) // SPC or i >= ROOT:
            sys.exit("no entra: " + ruta)
        for k in range(n):
            poner(siguiente + k, 0xFFF if k == n - 1 else siguiente + k + 1)
        o = (DATOS_SEC + (siguiente - 2) * SPC) * BPS
        img[o:o + len(datos)] = datos
        base, ext = nombre_83(ruta)
        fecha, hora = fecha_dos(os.path.getmtime(ruta))
        raiz[i * 32:(i + 1) * 32] = base + ext + struct.pack("<B10xHHHI", 0, hora, fecha, siguiente, len(datos))
        print("%s.%s  %6d bytes  clusters %d-%d" % (base.decode().strip(), ext.decode().strip(),
                                                   len(datos), siguiente, siguiente + n - 1))
        siguiente += n

    for f in range(NFAT):
        o = (RSV + f * SPF) * BPS
        img[o:o + len(fat)] = fat
    img[RAIZ_SEC * BPS:RAIZ_SEC * BPS + len(raiz)] = raiz
    open(salida, "wb").write(img)
    print("%s: %d bytes" % (salida, len(img)))


if __name__ == "__main__":
    main()
