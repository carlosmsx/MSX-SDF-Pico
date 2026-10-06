#!/usr/bin/env python3
"""Servidor TCP de prueba para la conexion del SDF-Pico (docs/RED.md).

    python tools/servidor_prueba.py [puerto]        (6400 por omision)

Desde el MSX o la consola USB de la Pico: atd <ip-de-la-PC>:6400

Al conectar manda un saludo y repite (eco) todo lo que recibe. Lo que se
escribe en esta consola se envia al cliente, con \\r\\n al final. Ordenes:

    /bytes N      manda N bytes de 1 a 255 en ciclo (sin ceros): el cliente
                  puede comprobar que no se pierde ni se repite ninguno
    /pcm [seg]    manda audio PCM de 4 bits por Turbo56K (CMDON, $83, nibbles,
                  $00): un tono de 440 Hz a la velocidad de 57600 bps
    /cortar       cierra la conexion (el MSX ve NO CARRIER)
    /ayuda        esta lista
"""
import math
import socket
import sys
import threading
import time

PUERTO = int(sys.argv[1]) if len(sys.argv) > 1 else 6400
BAUDIOS = 57600
SALUDO = (b"Servidor de prueba del SDF-Pico\r\n"
          b"Escribe algo: se repite. En el servidor: /ayuda\r\n")

cliente = None
candado = threading.Lock()


def enviar(datos):
    with candado:
        c = cliente
    if c is None:
        print("(no hay cliente)")
        return False
    try:
        c.sendall(datos)
        return True
    except OSError as e:
        print("error al enviar:", e)
        return False


def tono_pcm(segundos):
    """Nibbles de 4 bits de un seno de 440 Hz. La ROM reproduce primero el
    nibble bajo y despues el alto; un byte 0 cierra el flujo, asi que el
    nibble 0 se evita (rango 1-15)."""
    muestras_por_seg = BAUDIOS // 10 * 2
    n = int(segundos * muestras_por_seg)
    nib = [1 + round(14 * (0.5 + 0.5 * math.sin(2 * math.pi * 440 * i / muestras_por_seg)))
           for i in range(n)]
    if len(nib) % 2:
        nib.append(8)
    return bytes((nib[i + 1] << 4) | nib[i] for i in range(0, len(nib), 2))


def pcm(segundos):
    audio = tono_pcm(segundos)
    print("PCM: %d bytes (%.1f s)" % (len(audio), segundos))
    if not enviar(b"\xff\x83"):          # CMDON, comando 131
        return
    ritmo = BAUDIOS / 10
    t0 = time.time()
    enviados = 0
    while enviados < len(audio):
        trozo = audio[enviados:enviados + 120]
        if not enviar(trozo):
            return
        enviados += len(trozo)
        espera = t0 + enviados / ritmo - time.time()
        if espera > 0:
            time.sleep(espera)
    enviar(b"\x00")
    print("PCM: fin")


def orden(linea):
    global cliente
    p = linea.split()
    if p[0] == "/ayuda":
        print(__doc__)
    elif p[0] == "/bytes":
        n = int(p[1]) if len(p) > 1 else 1000
        enviar(bytes(1 + i % 255 for i in range(n)))
        print("enviados", n)
    elif p[0] == "/pcm":
        pcm(float(p[1]) if len(p) > 1 else 5)
    elif p[0] == "/cortar":
        with candado:
            c, cliente = cliente, None
        if c:
            c.close()
            print("cortado")
    else:
        print("orden desconocida; /ayuda")


def atender(c, dir_):
    global cliente
    print("cliente:", dir_)
    c.sendall(SALUDO)
    try:
        while True:
            d = c.recv(4096)
            if not d:
                break
            print("recibido %3d: %r" % (len(d), d))
            c.sendall(d)
    except OSError:
        pass
    print("cliente cerrado")
    with candado:
        if cliente is c:
            cliente = None
    c.close()


def consola():
    global cliente
    for linea in sys.stdin:
        linea = linea.rstrip("\r\n")
        if not linea:
            continue
        if linea.startswith("/"):
            try:
                orden(linea)
            except Exception as e:
                print("error:", e)
        else:
            enviar(linea.encode("latin-1", "replace") + b"\r\n")


def main():
    global cliente
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", PUERTO))
    s.listen(1)
    ips = {i[4][0] for i in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)}
    print("escuchando en el puerto %d; IP de la PC: %s" % (PUERTO, ", ".join(sorted(ips))))
    threading.Thread(target=consola, daemon=True).start()
    while True:
        c, dir_ = s.accept()
        c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        with candado:
            cliente = c
        threading.Thread(target=atender, args=(c, dir_), daemon=True).start()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
