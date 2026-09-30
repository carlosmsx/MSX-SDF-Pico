#!/usr/bin/env python3
"""Arma el .uf2 que carga una imagen de disco en la flash de la Pico.

El firmware del SDF-Pico lee el disco A: de la flash, a partir de 1 MB
(DISCO_OFFSET en firmware/src/disco.h). Este programa convierte una imagen
.DSK en un .uf2 para esa zona, que se copia a la Pico en modo BOOTSEL igual
que el firmware. El firmware no se toca.

    python tools/dsk2uf2.py JUEGO.DSK             -> JUEGO.uf2
    python tools/dsk2uf2.py --vacio 720           -> vacio720.uf2, formateado
    python tools/dsk2uf2.py --borrar              -> borrar.uf2, zona en blanco
    python tools/dsk2uf2.py JUEGO.DSK -o disco.uf2

Una zona en blanco se puede formatear desde el MSX con CALL FORMAT.
"""

import argparse
import struct
import sys
from pathlib import Path

FLASH_BASE = 0x10000000
DISCO_OFFSET = 1 << 20
FAMILIA_ABSOLUTA = 0xE48BFF57   # RP2350: escribir en la direccion indicada

TAMANOS = {737280: "720 KB", 368640: "360 KB"}

# Sector de arranque: salto, nombre OEM y BPB, igual que FMT720/FMT360 de
# PAGE2.MAC en el SDF-1. Sin codigo de arranque: sirve como disco de datos.
BPB = {
    720: dict(media=0xF9, total=1440, por_fat=3, caras=2),
    360: dict(media=0xF8, total=720, por_fat=2, caras=1),
}
SECTORES_DIR = 7    # 112 entradas de 32 bytes


def imagen_vacia(kb):
    f = BPB[kb]
    img = bytearray(kb * 1024)
    arranque = bytearray(512)
    arranque[0:3] = b"\xEB\xFE\x90"
    arranque[3:11] = b"SDF-PICO"
    arranque[11:30] = struct.pack(
        "<HBHBHHBHHHH",
        512,            # bytes por sector
        2,              # sectores por cluster
        1,              # sectores reservados
        2,              # FATs
        112,            # entradas de directorio
        f["total"],     # sectores en total
        f["media"],
        f["por_fat"],
        9,              # sectores por pista
        f["caras"],
        0,              # sectores ocultos
    )
    # El DOS llama a C01Eh (el byte 1Eh del sector) si el sector empieza con
    # EBh: tiene que haber codigo. Un RET vuelve y deja arrancar la Disk BASIC.
    arranque[0x1E] = 0xC9
    img[0:512] = arranque
    for copia in range(2):
        inicio = 512 * (1 + copia * f["por_fat"])
        img[inicio:inicio + 3] = bytes([f["media"], 0xFF, 0xFF])
    return bytes(img)


def uf2(datos, direccion):
    """Bloques UF2 de 256 bytes de datos cada uno."""
    n = (len(datos) + 255) // 256
    salida = bytearray()
    for i in range(n):
        trozo = datos[i * 256:(i + 1) * 256].ljust(256, b"\xFF")
        cabecera = struct.pack(
            "<IIIIIIII",
            0x0A324655, 0x9E5D5157,     # magia
            0x00002000,                 # familia presente
            direccion + i * 256,
            256, i, n,
            FAMILIA_ABSOLUTA,
        )
        bloque = cabecera + trozo + bytes(476 - 256) + struct.pack("<I", 0x0AB16F30)
        salida += bloque
    return bytes(salida)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("imagen", nargs="?", help="imagen .DSK de 720 o 360 KB")
    ap.add_argument("--vacio", type=int, choices=(720, 360),
                    help="en lugar de una imagen, un disco formateado vacio")
    ap.add_argument("--borrar", action="store_true",
                    help="dejar la zona del disco en blanco")
    ap.add_argument("-o", "--salida", help="archivo .uf2 de salida")
    a = ap.parse_args()

    if sum(bool(x) for x in (a.imagen, a.vacio, a.borrar)) != 1:
        ap.error("indicar una imagen, --vacio o --borrar")

    if a.imagen:
        datos = Path(a.imagen).read_bytes()
        if len(datos) not in TAMANOS:
            sys.exit(f"{a.imagen}: {len(datos)} bytes; tiene que ser de 720 o 360 KB")
        nombre = Path(a.imagen).with_suffix(".uf2").name
        desc = f"{a.imagen} ({TAMANOS[len(datos)]})"
    elif a.vacio:
        datos = imagen_vacia(a.vacio)
        nombre = f"vacio{a.vacio}.uf2"
        desc = f"disco vacio de {a.vacio} KB"
    else:
        datos = b"\xFF" * 737280
        nombre = "borrar.uf2"
        desc = "zona del disco en blanco"

    salida = Path(a.salida or nombre)
    salida.write_bytes(uf2(datos, FLASH_BASE + DISCO_OFFSET))
    print(f"{salida}: {desc}, en 0x{FLASH_BASE + DISCO_OFFSET:08X}")


if __name__ == "__main__":
    main()
