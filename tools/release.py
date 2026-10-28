#!/usr/bin/env python3
"""Junta los archivos de un release de GitHub del firmware, para SDF UPDATE.

    python tools/release.py

Despues de compilar (firmware/build.ps1) deja en out/release/vX.Y/:

    sdf_pico.uf2            para grabar por USB (BOOTSEL)
    sdf_pico.bin            lo que baja la Pico con SDF UPDATE
    sdf_pico.bin.sha256     su suma, que la Pico verifica antes de grabar

El release se crea en GitHub con el tag vX.Y (la version de FW_VERSION, en
firmware/src/protocolo.h) y esos tres archivos. La Pico toma el ultimo
release que no sea borrador ni pre-release (docs/ACTUALIZAR.md).
"""
import hashlib
import os
import re
import shutil
import struct
import sys

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BUILD = os.path.join(RAIZ, "firmware", "build")
PROTOCOLO = os.path.join(RAIZ, "firmware", "src", "protocolo.h")
MAX = 1 << 20                   # ACT_MAX en firmware/src/actualizar.h
MARCA = 0xFFFFDED3              # PICOBIN_BLOCK_MARKER_START


def version():
    txt = open(PROTOCOLO, encoding="utf-8").read()
    m = re.search(r'#define\s+FW_VERSION\s+"SDF-Pico ([^"]+)"', txt)
    if not m:
        sys.exit(f"no encontre FW_VERSION en {PROTOCOLO}")
    return m.group(1)


def revisar(binario, ver):
    """Lo mismo que revisa actualizar.c antes de grabar."""
    if not 4096 <= len(binario) <= MAX:
        sys.exit(f"sdf_pico.bin tiene {len(binario)} bytes: la Pico acepta de 4 KB a 1 MB")
    palabras = struct.unpack_from("<1024I", binario.ljust(4096, b"\xff"))
    if MARCA not in palabras:
        sys.exit("sdf_pico.bin no tiene el bloque IMAGE_DEF en el primer sector")
    if f"SDF-Pico {ver}\0".encode() not in binario:
        sys.exit(f"sdf_pico.bin no dice SDF-Pico {ver}: compilar de nuevo")


def main():
    ver = version()
    origen = {n: os.path.join(BUILD, n) for n in ("sdf_pico.uf2", "sdf_pico.bin")}
    for ruta in origen.values():
        if not os.path.exists(ruta):
            sys.exit(f"falta {ruta}: compilar con firmware/build.ps1")
    binario = open(origen["sdf_pico.bin"], "rb").read()
    revisar(binario, ver)

    destino = os.path.join(RAIZ, "out", "release", f"v{ver}")
    os.makedirs(destino, exist_ok=True)
    for nombre, ruta in origen.items():
        shutil.copyfile(ruta, os.path.join(destino, nombre))
    suma = hashlib.sha256(binario).hexdigest()
    with open(os.path.join(destino, "sdf_pico.bin.sha256"), "w", newline="\n") as f:
        f.write(f"{suma}  sdf_pico.bin\n")

    print(f"{os.path.normpath(destino)}:")
    for n in sorted(os.listdir(destino)):
        print(f"  {n:22} {os.path.getsize(os.path.join(destino, n)):8} bytes")
    print(f"\nEn GitHub: release nuevo con el tag v{ver} y estos tres archivos.")


if __name__ == "__main__":
    main()
