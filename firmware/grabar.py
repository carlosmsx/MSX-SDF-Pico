#!/usr/bin/env python3
"""Graba el firmware en la Pico por USB, sin el boton BOOTSEL.

    python grabar.py                      graba build/sdf_pico.uf2
    python grabar.py sdf_pico_diag.uf2    otra variante (de build/, o una ruta)
    python grabar.py -p COM5              si hay mas de una Pico conectada

Abre el puerto de la consola a 1200 baudios: el pico-sdk lo toma como pedido
de reinicio en BOOTSEL (PICO_ENABLE_USB_RESET_VIA_BAUD_RATE, activo por
omision). Espera la unidad RP2350, copia el .uf2 y la Pico arranca sola con el
firmware nuevo. Si la Pico ya esta en BOOTSEL, copia directamente. Si el
firmware esta colgado no contesta al pedido: hace falta el boton.

Necesita pyserial (pip install pyserial).
"""
import argparse
import os
import shutil
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("Falta pyserial: pip install pyserial")

VID_RPI = 0x2E8A
ETIQUETA = "RP2350"
AQUI = os.path.dirname(os.path.abspath(__file__))


def unidad_bootsel():
    """La raiz de la unidad RP2350, o None."""
    if os.name == "nt":
        import ctypes
        k32 = ctypes.windll.kernel32
        k32.SetErrorMode(1)     # sin cartelitos por lectoras vacias
        mascara = k32.GetLogicalDrives()
        for i in range(26):
            if not mascara & (1 << i):
                continue
            raiz = chr(65 + i) + ":\\"
            if k32.GetDriveTypeW(raiz) != 2:        # removible
                continue
            nombre = ctypes.create_unicode_buffer(64)
            if k32.GetVolumeInformationW(raiz, nombre, 64, None, None, None, None, 0) \
                    and nombre.value == ETIQUETA:
                return raiz
        return None
    usuario = os.environ.get("USER", "")
    for raiz in (f"/media/{usuario}/{ETIQUETA}", f"/run/media/{usuario}/{ETIQUETA}",
                 f"/Volumes/{ETIQUETA}"):
        if os.path.isdir(raiz):
            return raiz
    return None


def puertos_pico():
    return [p.device for p in list_ports.comports() if p.vid == VID_RPI]


def esperar(condicion, segundos):
    limite = time.monotonic() + segundos
    while time.monotonic() < limite:
        r = condicion()
        if r:
            return r
        time.sleep(0.25)
    return None


def main():
    ap = argparse.ArgumentParser(description="Graba el firmware en la Pico por USB.")
    ap.add_argument("archivo", nargs="?", default="sdf_pico.uf2",
                    help="el .uf2 (de build/ si no es una ruta existente)")
    ap.add_argument("-p", "--puerto", help="puerto COM de la Pico")
    a = ap.parse_args()

    archivo = a.archivo
    if not os.path.exists(archivo):
        archivo = os.path.join(AQUI, "build", a.archivo)
    if not os.path.exists(archivo):
        sys.exit(f"No encontrado: {archivo} (compilar primero)")

    unidad = unidad_bootsel()
    if not unidad:
        puerto = a.puerto
        if not puerto:
            puertos = puertos_pico()
            if not puertos:
                sys.exit("No se ve la Pico por USB (ni su puerto COM ni la unidad RP2350)")
            if len(puertos) > 1:
                sys.exit(f"Hay varias Pico ({', '.join(puertos)}): elegir una con -p")
            puerto = puertos[0]

        print(f"Reiniciando la Pico de {puerto} en BOOTSEL...")
        try:
            serial.Serial(puerto, 1200).close()
        except serial.SerialException:
            pass    # el dispositivo puede desaparecer antes de cerrar: no es un error

        unidad = esperar(unidad_bootsel, 15)
        if not unidad:
            sys.exit("La unidad RP2350 no aparecio: el firmware no atendio el pedido "
                     "(usar el boton BOOTSEL)")

    print(f"Copiando {os.path.basename(archivo)} a {unidad}...")
    shutil.copyfile(archivo, os.path.join(unidad, os.path.basename(archivo)))

    # Al terminar la copia la Pico se reinicia con el firmware nuevo
    if not esperar(lambda: not unidad_bootsel(), 15):
        sys.exit("La unidad RP2350 sigue ahi: la copia no se completo?")
    nuevos = esperar(puertos_pico, 15)
    if nuevos:
        print(f"Listo: la Pico arranco con el firmware nuevo, consola en {nuevos[0]}.")
    else:
        print("Grabado. La consola USB todavia no aparecio.")


if __name__ == "__main__":
    main()
