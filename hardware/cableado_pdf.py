# Hoja imprimible de cableado del SDF-Pico (A4, una pagina).
#
#   python hardware/cableado_pdf.py            -> hardware/cableado.pdf
#   python hardware/cableado_pdf.py otro.pdf
#
# Requiere reportlab. Los datos salen de hardware/CABLEADO.md y de
# firmware/src/pins.h: si cambia un pin, hay que cambiarlo en los tres.
import os
import sys
from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor, black, white

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "cableado.pdf")
W, H = A4
M = 36  # margen

GRIS = HexColor("#666666")
CLARO = HexColor("#eeeeee")
TIT = HexColor("#222222")
ETAPA = {1: HexColor("#fde2c4"), 2: HexColor("#d6e8fa"), 3: HexColor("#dff2d8")}

# (pin Pico, GPIO, pin U3, senal, etapa)
FILAS = [
    (1, [("39", "VSYS", "7", "+5 V  (con diodo FR107)"),
         ("3", "GND", "8", "GND"),
         ("8", "GND", "22", "GND")]),
    (2, [("1", "GP0", "2", "D0"),
         ("2", "GP1", "3", "D1"),
         ("4", "GP2", "4", "D2"),
         ("5", "GP3", "5", "D3"),
         ("6", "GP4", "6", "D4"),
         ("7", "GP5", "11", "D5"),
         ("9", "GP6", "12", "D6"),
         ("10", "GP7", "13", "D7"),
         ("11", "GP8", "23", "/IOSEL"),
         ("12", "GP9", "24", "A0"),
         ("14", "GP10", "25", "/RD"),
         ("15", "GP11", "26", "EN")]),
    (3, [("16", "GP12", "18", "MISO de la SD"),
         ("17", "GP13", "16", "CS de la SD"),
         ("19", "GP14", "19", "SCK de la SD"),
         ("20", "GP15", "17", "MOSI de la SD")]),
]
TITULO_ETAPA = {1: "1 - Alimentación", 2: "2 - Bus del MSX", 3: "3 - SD (para después)"}

c = canvas.Canvas(OUT, pagesize=A4)
c.setTitle("SDF-Pico - Cableado")

# --- Titulo ---------------------------------------------------------------
y = H - M
c.setFillColor(TIT)
c.setFont("Helvetica-Bold", 17)
c.drawString(M, y - 14, "SDF-Pico  -  Cableado de U3 a la Pico 2 W")
c.setFont("Helvetica", 9)
c.setFillColor(GRIS)
c.drawString(M, y - 28, "Placa SDF-1 rev1 con la línea /WAIT cortada (CON1-7).  "
             "Sin ATmega, sin X1, C1 ni C2.")
y -= 44

# --- Tabla ----------------------------------------------------------------
tx = M
cols = [("Pico", 34), ("GPIO", 40), ("U3", 30), ("Señal", 124), ("OK", 22)]
tw = sum(w for _, w in cols)
rh = 15.5

def celdas(yy, valores, fuente="Helvetica", tam=9.5, color=black):
    x = tx
    c.setFont(fuente, tam)
    c.setFillColor(color)
    for (nombre, w), v in zip(cols, valores):
        if nombre == "OK":
            c.setStrokeColor(black)
            c.setLineWidth(0.8)
            c.rect(x + w / 2 - 5, yy + 4, 10, 10, stroke=1, fill=0)
        elif nombre == "Señal":
            c.drawString(x + 5, yy + 5, v)
        else:
            c.drawCentredString(x + w / 2, yy + 5, v)
        x += w

# encabezado
y -= rh
c.setFillColor(TIT)
c.rect(tx, y, tw, rh, stroke=0, fill=1)
x = tx
c.setFont("Helvetica-Bold", 9.5)
c.setFillColor(white)
for nombre, w in [("Pin Pico", 34), ("GPIO", 40), ("Pin U3", 30), ("Señal", 124), ("", 22)]:
    pass
hdr = ["Pico", "GPIO", "U3", "Señal", "OK"]
for (nombre, w), h in zip(cols, hdr):
    if nombre == "Señal":
        c.drawString(x + 5, y + 5, "Señal")
    else:
        c.drawCentredString(x + w / 2, y + 5, h)
    x += w

for etapa, filas in FILAS:
    y -= rh
    c.setFillColor(ETAPA[etapa])
    c.rect(tx, y, tw, rh, stroke=0, fill=1)
    c.setFillColor(TIT)
    c.setFont("Helvetica-Bold", 9.5)
    c.drawString(tx + 5, y + 5, TITULO_ETAPA[etapa])
    for i, f in enumerate(filas):
        y -= rh
        if i % 2:
            c.setFillColor(CLARO)
            c.rect(tx, y, tw, rh, stroke=0, fill=1)
        celdas(y, f, fuente="Helvetica-Bold" if f[1] in ("VSYS",) else "Helvetica")
tabla_fondo = y
c.setStrokeColor(GRIS)
c.setLineWidth(0.5)
c.rect(tx, tabla_fondo, tw, (H - M - 44) - tabla_fondo, stroke=1, fill=0)

# Notas debajo de la tabla
ny = tabla_fondo - 16
c.setFillColor(black)
c.setFont("Helvetica-Bold", 9.5)
c.drawString(tx, ny, "No se conectan:")
c.setFont("Helvetica", 9.5)
c.drawString(tx + 78, ny, "U3 pines 1, 9, 10, 20 y 21.")
ny -= 13
c.drawString(tx, ny, "Cables de menos de 10 cm; los del bus, los más cortos.")
ny -= 13
c.drawString(tx, ny, "EN no puede quedar sin conectar: sin él, la Pico")
ny -= 13
c.drawString(tx, ny, "no ve los accesos del MSX.")

# --- Pico: fila izquierda -------------------------------------------------
px = tx + tw + 40
top = H - M - 44 - 4
c.setFillColor(TIT)
c.setFont("Helvetica-Bold", 10)
c.drawString(px, top - 10, "Pico 2 W, vista desde arriba")
bx, bw = px + 6, 62
pin_h = 12
by_top = top - 34
# USB
c.setFillColor(HexColor("#bbbbbb"))
c.rect(bx + bw / 2 - 12, by_top - 2, 24, 10, stroke=0, fill=1)
c.setFillColor(GRIS)
c.setFont("Helvetica", 7)
c.drawCentredString(bx + bw / 2, by_top + 10, "USB")
bh = 20 * pin_h + 10
c.setStrokeColor(black)
c.setLineWidth(1)
c.setFillColor(HexColor("#e7f3e7"))
c.rect(bx, by_top - bh, bw, bh, stroke=1, fill=1)
GP = {1: "GP0", 2: "GP1", 3: "GND", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5",
      8: "GND", 9: "GP6", 10: "GP7", 11: "GP8", 12: "GP9", 13: "GND",
      14: "GP10", 15: "GP11", 16: "GP12", 17: "GP13", 18: "GND", 19: "GP14",
      20: "GP15"}
SEN = {1: "D0", 2: "D1", 3: "U3-8", 4: "D2", 5: "D3", 6: "D4", 7: "D5",
       8: "U3-22", 9: "D6", 10: "D7", 11: "/IOSEL", 12: "A0", 13: "",
       14: "/RD", 15: "EN", 16: "MISO", 17: "CS", 18: "", 19: "SCK", 20: "MOSI"}
for n in range(1, 21):
    yy = by_top - 5 - (n - 0.5) * pin_h
    gnd = GP[n] == "GND"
    c.setFillColor(HexColor("#999999") if gnd else HexColor("#d4a017"))
    c.circle(bx, yy, 3.2, stroke=0, fill=1)
    c.setFillColor(black)
    c.setFont("Helvetica-Bold", 7.5)
    c.drawString(bx + 6, yy - 2.6, str(n))
    c.setFont("Helvetica", 7.5)
    c.drawString(bx + 20, yy - 2.6, GP[n])
    c.setFont("Helvetica-Bold" if not gnd else "Helvetica", 7.5)
    c.setFillColor(GRIS if gnd or not SEN[n] else black)
    c.drawRightString(bx - 7, yy - 2.6, SEN[n])
# pin 39 del otro lado
yy39 = by_top - 5 - 1.5 * pin_h
c.setFillColor(HexColor("#d4a017"))
c.circle(bx + bw, yy39, 3.2, stroke=0, fill=1)
c.setFillColor(black)
c.setFont("Helvetica-Bold", 7.5)
c.drawRightString(bx + bw - 6, yy39 - 2.6, "39")
c.setFont("Helvetica", 7.5)
c.drawString(bx + bw + 6, yy39 - 2.6, "VSYS")
c.drawString(bx + bw + 6, yy39 - 12, "(diodo)")
pico_fondo = by_top - bh

# --- U3: DIP-28 -----------------------------------------------------------
dx = px + 6
dtop = pico_fondo - 34
c.setFillColor(TIT)
c.setFont("Helvetica-Bold", 10)
c.drawString(px, dtop + 12, "U3 (pads del ATmega), vista desde arriba")
dw, dph = 34, 11.5
dh = 14 * dph + 8
dcx = dx + 58
c.setStrokeColor(black)
c.setFillColor(HexColor("#f4f4f4"))
c.rect(dcx, dtop - dh, dw, dh, stroke=1, fill=1)
c.setFillColor(white)
c.circle(dcx + dw / 2, dtop, 5, stroke=1, fill=1)  # muesca
U3 = {1: "", 2: "D0", 3: "D1", 4: "D2", 5: "D3", 6: "D4", 7: "+5V", 8: "GND",
      9: "", 10: "", 11: "D5", 12: "D6", 13: "D7", 14: "(TX)",
      15: "(RX)", 16: "CS", 17: "MOSI", 18: "MISO", 19: "SCK", 20: "", 21: "",
      22: "GND", 23: "/IOSEL", 24: "A0", 25: "/RD", 26: "EN", 27: "(SDA)",
      28: "(SCL)"}
for i in range(14):
    yy = dtop - 4 - (i + 0.5) * dph
    for n, xp, izq in ((i + 1, dcx, True), (28 - i, dcx + dw, False)):
        sq = n == 1
        c.setFillColor(HexColor("#d4a017"))
        if sq:
            c.rect(xp - 3.2, yy - 3.2, 6.4, 6.4, stroke=0, fill=1)
        else:
            c.circle(xp, yy, 3.2, stroke=0, fill=1)
        c.setFillColor(black)
        c.setFont("Helvetica-Bold", 7.5)
        lab = U3[n]
        usado = lab and not lab.startswith("(")
        if izq:
            c.drawString(xp + 6, yy - 2.6, str(n))
            c.setFont("Helvetica-Bold" if usado else "Helvetica", 7.5)
            c.setFillColor(black if usado else GRIS)
            c.drawRightString(xp - 7, yy - 2.6, lab or "-")
        else:
            c.drawRightString(xp - 6, yy - 2.6, str(n))
            c.setFont("Helvetica-Bold" if usado else "Helvetica", 7.5)
            c.setFillColor(black if usado else GRIS)
            c.drawString(xp + 7, yy - 2.6, lab or "-")
c.setFillColor(GRIS)
c.setFont("Helvetica", 7.5)
c.drawString(px, dtop - dh - 12, "Pin 1: pad cuadrado. Entre paréntesis: para más adelante.")
dip_fondo = dtop - dh - 12

# --- Diodo ----------------------------------------------------------------
fy = min(ny, dip_fondo) - 26
c.setStrokeColor(GRIS)
c.setLineWidth(0.5)
c.line(M, fy + 20, W - M, fy + 20)
c.setFillColor(TIT)
c.setFont("Helvetica-Bold", 11)
c.drawString(M, fy, "Diodo de alimentación: FR107")

cy = fy - 30
x0 = M + 10
c.setFont("Helvetica-Bold", 9.5)
c.setFillColor(black)
c.drawString(x0, cy - 3, "U3 pin 7  (+5 V)")
l0 = x0 + 92
c.setStrokeColor(black)
c.setLineWidth(1.2)
c.line(l0, cy, l0 + 40, cy)
# cuerpo
bx0 = l0 + 40
c.setFillColor(HexColor("#333333"))
c.rect(bx0, cy - 8, 60, 16, stroke=0, fill=1)
c.setFillColor(HexColor("#dddddd"))
c.rect(bx0 + 46, cy - 8, 8, 16, stroke=0, fill=1)  # banda
c.line(bx0 + 60, cy, bx0 + 100, cy)
c.setFillColor(black)
c.drawString(bx0 + 106, cy - 3, "Pico pin 39  (VSYS)")
c.setFont("Helvetica", 8.5)
c.drawCentredString(bx0 + 50, cy - 20, "banda = cátodo, hacia la Pico")
# simbolo
sx = bx0 + 250
c.setLineWidth(1)
p = c.beginPath()
p.moveTo(sx, cy - 8); p.lineTo(sx, cy + 8); p.lineTo(sx + 14, cy); p.close()
c.drawPath(p, stroke=1, fill=1)
c.line(sx + 14, cy - 8, sx + 14, cy + 8)
c.line(sx - 20, cy, sx, cy)
c.line(sx + 14, cy, sx + 34, cy)
c.setFont("Helvetica", 8.5)
c.drawCentredString(sx - 20, cy + 6, "+5 V")
c.drawCentredString(sx + 34, cy + 6, "VSYS")

c.setFont("Helvetica", 9)
ly = cy - 36
for t in [
    "Con la Pico consumiendo unos 50 mA cae ~0,8 V: VSYS queda en ~4,1 V (la Pico acepta desde 1,8 V).",
    "Nunca llevar +5 V al pin 36 (3V3) de la Pico: es la salida de su regulador.",
    "El 1N4148 sirve solo para la prueba del bus (sin WiFi).  El 1N4729A es un zener de 3,6 V: no usar.",
]:
    c.drawString(M + 10, ly, "-  " + t)
    ly -= 13

# --- Checklist --------------------------------------------------------------
ly -= 4
c.setFillColor(TIT)
c.setFont("Helvetica-Bold", 11)
c.drawString(M, ly, "Antes de enchufar (téster en continuidad, sin USB)")
ly -= 16
c.setFillColor(black)
c.setFont("Helvetica", 9)
for t in [
    "CON1-7 sin continuidad con U5 pin 15 (corte de /WAIT)",
    "Pico pines 1, 2, 4, 5, 6, 7, 9 y 10 con continuidad a U2 pines 18 a 11, en ese orden",
    "Pico pin 11 con U5 pin 15 y U2 pin 19;  pin 14 con U2 pin 1;  pin 15 con U5 pin 6",
    "Diodo con la banda del lado de la Pico;  sin continuidad entre U3-7 y el pin 36 de la Pico",
    "Sin continuidad entre VCC y GND;  pads de la Pico aislados entre sí",
]:
    c.rect(M + 10, ly - 1, 8, 8, stroke=1, fill=0)
    c.drawString(M + 26, ly, t)
    ly -= 14

c.setFillColor(GRIS)
c.setFont("Helvetica", 7.5)
c.drawRightString(W - M, 18, "MSX-SDF-Pico  -  hardware/CABLEADO.md")
c.save()
print("ok", ly)
