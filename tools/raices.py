#!/usr/bin/env python3
"""Arma firmware/src/raices.c: los certificados raiz con que la Pico
reconoce a GitHub (https.c, actualizar.c).

    python tools/raices.py [--pem cacert.pem]

Los saca, por nombre, de la lista de certifi (la del paquete de Python, o
la que se pase con --pem). Quedan en DER, en la flash: mbedTLS los lee de
ahi sin copiarlos.

Hoy api.github.com y github.com terminan en USERTrust ECC (con Sectigo E46
de intermedio), y release-assets.githubusercontent.com, de donde se bajan
los archivos de los releases, en ISRG Root X1. Las demas son las que GitHub
uso antes o las que le siguen a esas, por si cambia.
"""
import argparse
import base64
import os
import re
import sys

RAICES = [
    "USERTrust ECC Certification Authority",
    "Sectigo Public Server Authentication Root E46",
    "USERTrust RSA Certification Authority",
    "Sectigo Public Server Authentication Root R46",
    "ISRG Root X1",
    "ISRG Root X2",
    "DigiCert Global Root G2",
]

AQUI = os.path.dirname(os.path.abspath(__file__))
SALIDA = os.path.join(AQUI, "..", "firmware", "src", "raices.c")


def leer_pem(ruta):
    """{nombre: DER} de un archivo como el de certifi ('# Label: "..."')."""
    texto = open(ruta, encoding="utf-8").read()
    certs = {}
    patron = r'# Label: "([^"]+)".*?-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----'
    for nombre, b64 in re.findall(patron, texto, re.S):
        certs[nombre] = base64.b64decode("".join(b64.split()))
    return certs


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pem", help="lista de certificados (si no, la de certifi)")
    a = ap.parse_args()
    ruta = a.pem
    if not ruta:
        try:
            import certifi
        except ImportError:
            sys.exit("falta certifi (pip install certifi) o --pem")
        ruta = certifi.where()
    certs = leer_pem(ruta)

    c = [
        "// Generado por tools/raices.py: no editar a mano.",
        "// Certificados raiz para reconocer a GitHub (raices.h), en DER.",
        "",
        "#include <stddef.h>",
        '#include "raices.h"',
        "",
        "// En la flash: con copy_to_ram, lo demas se copia a la RAM",
        '#define FLASH __attribute__((section(".flashdata.raices")))',
        "",
    ]
    nombres = []
    for i, nombre in enumerate(RAICES):
        if nombre not in certs:
            sys.exit(f"no esta en {ruta}: {nombre}")
        der = certs[nombre]
        c.append(f"// {nombre}")
        c.append(f"static const unsigned char raiz{i}[{len(der)}] FLASH = {{")
        for j in range(0, len(der), 16):
            c.append("    " + " ".join(f"0x{b:02x}," for b in der[j:j + 16]))
        c.append("};")
        c.append("")
        nombres.append((f"raiz{i}", len(der)))
    c.append("const raiz_t raices[] = {")
    for v, n in nombres:
        c.append(f"    {{ {v}, {n} }},")
    c.append("};")
    c.append(f"const int n_raices = {len(nombres)};")
    c.append("")
    open(SALIDA, "w", encoding="utf-8", newline="\n").write("\n".join(c))
    total = sum(n for _, n in nombres)
    print(f"{os.path.normpath(SALIDA)}: {len(nombres)} raices, {total} bytes")


if __name__ == "__main__":
    main()
