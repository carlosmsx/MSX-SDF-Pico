#!/usr/bin/env python3
"""Deja WebMSX listo para probar programas de MSX-DOS desde el navegador de
Claude Code (o cualquier navegador sin ventana visible).

    python tools/prepara_webmsx.py

Baja el index.html de webmsx.org (un solo archivo, con el emulador y las ROM)
a out/web/webmsx/ y le agrega al principio un script que cambia
requestAnimationFrame por setTimeout: en una pagina oculta el navegador no
dibuja cuadros y el emulador no arrancaria. Despues:

    python tools/servir_web.py        sirve out/web con CORS en el puerto 8765
    http://127.0.0.1:8765/webmsx/index_local.html?MACHINE=MSX1A&DISKA_URL=/dos.dsk&DISKB_URL=/prueba.dsk&SCREEN_VSYNC_MODE=-1

Los discos van con ruta relativa (las que empiezan con http: pasan por un
proxy de webmsx.org que no ve localhost). tools/webmsx_arnes.js agrega scr(),
run(), typ() y start(): con la pagina oculta el emulador avanza solo cuando se
llama a run().
"""
import os
import urllib.request

DESTINO = os.path.join(os.path.dirname(__file__), "..", "out", "web", "webmsx")
os.makedirs(DESTINO, exist_ok=True)
html = urllib.request.urlopen("https://webmsx.org/").read().decode("utf-8")
inyeccion = ("<script>(function(){window.requestAnimationFrame=function(f){return setTimeout(function(){f(performance.now())},16)};"
             "window.cancelAnimationFrame=function(i){clearTimeout(i)};"
             "Object.defineProperty(document,'hidden',{get:function(){return false}});"
             "Object.defineProperty(document,'visibilityState',{get:function(){return 'visible'}});})();</script>")
marca = '<head lang="en">'
i = html.index(marca) + len(marca)
open(os.path.join(DESTINO, "index_local.html"), "w", encoding="utf-8").write(html[:i] + inyeccion + html[i:])
print("listo:", os.path.join(DESTINO, "index_local.html"))
