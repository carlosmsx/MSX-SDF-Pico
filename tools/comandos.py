#!/usr/bin/env python3
"""Ensambla los programas de comandos/ con PASMO y los deja en out/msx/.

    python tools/comandos.py [--pasmo RUTA]

Cada comandos/X.asm queda como out/msx/X.COM, y out/comandos.dsk lleva
todos (para montar con CALL SDFMOUNT). out/msx/ se sirve en la red para
bajar los programas desde el MSX con GET.COM:

    python -m http.server 8000 --bind 0.0.0.0 --directory out/msx

PASMO se busca en el PATH, en la variable PASMO o junto a RetroTerm
(../retroterm/tools/pasmo.exe).
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SALIDA = os.path.join(RAIZ, "out", "msx")


def buscar_pasmo(dada):
    for c in (dada, os.environ.get("PASMO"), shutil.which("pasmo"),
              os.path.join(RAIZ, "..", "retroterm", "tools", "pasmo.exe")):
        if c and os.path.isfile(c):
            return c
    sys.exit("No se encuentra PASMO (--pasmo RUTA o variable PASMO)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pasmo")
    pasmo = buscar_pasmo(ap.parse_args().pasmo)

    os.makedirs(SALIDA, exist_ok=True)
    hechos = []
    for fuente in sorted(glob.glob(os.path.join(RAIZ, "comandos", "*.asm"))):
        nombre = os.path.splitext(os.path.basename(fuente))[0].upper() + ".COM"
        destino = os.path.join(SALIDA, nombre)
        r = subprocess.run([pasmo, os.path.basename(fuente), os.path.abspath(destino)],
                           cwd=os.path.dirname(fuente), capture_output=True, text=True)
        avisos = [l for l in (r.stdout + r.stderr).splitlines()
                  if l.strip() and "is never used" not in l]
        if r.returncode:
            sys.exit(f"{os.path.basename(fuente)}:\n" + "\n".join(avisos))
        for l in avisos:
            print(f"  {os.path.basename(fuente)}: {l}")
        print(f"{nombre:12} {os.path.getsize(destino):6} bytes")
        hechos.append(destino)

    subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "mkdsk.py"),
                    os.path.join(RAIZ, "out", "comandos.dsk"), *hechos],
                   check=True, stdout=subprocess.DEVNULL)
    print("out/comandos.dsk")


if __name__ == "__main__":
    main()
