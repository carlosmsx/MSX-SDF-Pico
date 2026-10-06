#!/usr/bin/env python3
"""Sirve una carpeta por HTTP con CORS, para cargar imagenes .DSK en WebMSX.

    python tools/servir_web.py [carpeta] [puerto]      (out/web, 8765)
"""
import http.server
import os
import sys

carpeta = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "out", "web")
puerto = int(sys.argv[2]) if len(sys.argv) > 2 else 8765


class Manejador(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Access-Control-Allow-Private-Network", "true")   # una pagina https que lee de localhost
        self.end_headers()

    def log_message(self, fmt, *a):
        if os.environ.get("SERVIR_LOG"):
            sys.stderr.write((fmt % a) + "\n")


os.chdir(carpeta)
http.server.ThreadingHTTPServer(("127.0.0.1", puerto), Manejador).serve_forever()
