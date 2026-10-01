# Placa y cableado

El SDF-Pico usa una placa **SDF-1 rev1** con una sola modificación y una
**Pico 2 W** en lugar del ATmega328P.

El Wi-Fi y el reloj requieren la Pico 2 W. La Pico 2 sin módulo inalámbrico
tiene el mismo pinout y en principio alcanza para el lector de SD, pero con
este firmware, que se compila para la Pico 2 W, no se probó. Los GPIO que la W
reserva para el módulo (GP23 a GP25 y GP29) son internos y no salen a los
pines.

## Modificación de la placa

**Corte de la línea `/WAIT`.** Es el corte 2 de
[CORRECCIONES.md](https://github.com/carlosmsx/MSX-SDF-1/blob/main/hardware/rev1/CORRECCIONES.md) del SDF-1: se
corta la pista que llega al contacto 7 del borde (`CON1-7`). El contacto queda
sin conectar y el cartucho nunca pide espera.

Ninguna otra corrección de la rev1 hace falta:

| Corrección del SDF-1 | En el SDF-Pico |
|---|---|
| 1 · RESET al ICSP | No aplica: la Pico se programa por USB |
| 2 · Separar U5 del enable | No hace falta: el firmware deja `EN` fijo en alto |
| 3 · Pull-down en `PC3` | No hace falta: sin `/WAIT`, `EN` no puede colgar al MSX |
| 4 · U6 para `/WAIT` | No hace falta: `/WAIT` no se usa |
| 5 · HCT en el zócalo | Recomendable por consumo, no por niveles (ver abajo) |

Con `EN` en alto, `/IOSEL` (U5.Y0) es decodificación pura: baja durante los
accesos a los puertos 00h y 01h y sube cuando termina el ciclo. Abre el `'245`
durante todo el ciclo y le indica a la Pico cuándo empieza y cuándo termina.

## Qué no se monta

- **U3** (ATmega328P).
- **X1, C1 y C2** (cristal y capacitores de carga).

R1 y C8 pueden quedar montados; no se conectan a nada.

## Cableado

Hay una versión imprimible de esta sección, con el diodo y el checklist:
[cableado.pdf](cableado.pdf). Se regenera con `python hardware/cableado_pdf.py`.

La Pico se monta del lado de componentes, sobre el borde derecho del área
universal, con la fila de pines 1 a 20 soldada a la placa y la fila 21 a 40
hacia afuera. En los pads de U3 se sueldan tiras de pines, desde donde se
cablea hasta la Pico. Todas las señales del cartucho van
al lado izquierdo de la Pico (pines físicos 1 a 20): el bus del MSX en los
pines 1 a 15 y la SD en los pines 16 a 20. Del lado derecho quedan solo la
alimentación, el I2C y la UART.

| Señal | Origen en la placa | GPIO | Pin Pico |
|---|---|---|---|
| D0 | U3-2 (U2-18) | GP0 | 1 |
| D1 | U3-3 (U2-17) | GP1 | 2 |
| D2 | U3-4 (U2-16) | GP2 | 4 |
| D3 | U3-5 (U2-15) | GP3 | 5 |
| D4 | U3-6 (U2-14) | GP4 | 6 |
| D5 | U3-11 (U2-13) | GP5 | 7 |
| D6 | U3-12 (U2-12) | GP6 | 9 |
| D7 | U3-13 (U2-11) | GP7 | 10 |
| `/IOSEL` | U3-23 (U5-15, U2-19) | GP8 | 11 |
| A0 | U3-24 | GP9 | 12 |
| `/RD` | U3-25 (U2-1) | GP10 | 14 |
| `EN` | U3-26 (U5-6) | GP11 | 15 |
| MISO de la SD | U3-18 / H1-3 | GP12 (SPI1 RX) | 16 |
| CS de la SD | U3-16 / H1-5 | GP13 (SPI1 CSn) | 17 |
| SCK de la SD | U3-19 / H1-2 | GP14 (SPI1 SCK) | 19 |
| MOSI de la SD | U3-17 / H1-4 | GP15 (SPI1 TX) | 20 |
| TX (ex PB0) | U3-14 / H1-7 | GP16 (UART0 TX) | 21 |
| RX (ex PB1) | U3-15 / H1-6 | GP17 (UART0 RX) | 22 |
| SDA | U3-27 / H1-8 | GP20 (I2C0 SDA) | 26 |
| SCL | U3-28 / H1-9 | GP21 (I2C0 SCL) | 27 |
| +5 V | U3-7 | VSYS, con diodo Schottky | 39 |
| GND | U3-8, U3-22 | GND | 3, 8, 13, 18, 38 |

El orden de D0-D7, `/IOSEL`, A0 y `/RD` en GPIO consecutivos no es arbitrario:
el programa PIO lee los once pines de una vez y salta según A0 y `/RD`.
Cambiarlo obliga a cambiar `firmware/src/pins.h` y `firmware/src/bus.pio`.

Las señales de la SD, el I2C y la UART están también en H1: es posible
tomarlas de ahí en lugar de los pads de U3.

Quedan libres GP18, GP19 y GP22 (pines 24, 25 y 29). GP26 a GP28 también están
libres, pero son entradas analógicas y **no admiten 5 V**.

## Alimentación

La Pico se alimenta del +5 V del slot por **VSYS (pin 39)**, con un diodo
Schottky en serie (1N5817 o BAT54): ánodo del lado de U3-7, cátodo del lado de
la Pico. El diodo permite conectar el USB con el cartucho en el MSX sin que las
dos fuentes se enfrenten.

**El +5 V nunca debe llegar al pin 36 (3V3) de la Pico**, que es la salida de
su regulador.

## Niveles de tensión

- **Entradas.** Las GPIO del RP2350 toleran 5 V siempre que la Pico esté
  alimentada y que el pin no sea analógico (GP26 a GP28). Las dos condiciones
  se cumplen con el cartucho en un MSX encendido.
- **Salidas.** Los 3,3 V de la Pico superan el umbral de 2,0 V de las entradas
  LS y HCT. No sirven para lógica HC, cuyo umbral a 5 V es de 3,5 V.
- **SD e I2C.** El módulo de SD habitual trae regulador y adaptador de niveles
  propios. Los pull-ups de I2C a 5 V están dentro de lo que toleran los pines.

Los RP2350 del stepping A2 tienen una errata (E9) que afecta a las entradas con
pull-down interno. El firmware desactiva los pulls del bus, así que no la
afecta, pero conviene preferir el stepping A4 (marca `RP2350A0A4` en el chip).

## Verificación, antes de enchufar

Con la Pico sin conectar al USB y el téster en continuidad:

- [ ] `CON1-7` sin continuidad con U5 pin 15
- [ ] Pin 11 de la Pico con continuidad a U5 pin 15 y a U2 pin 19
- [ ] Pin 15 de la Pico con continuidad a U5 pin 6
- [ ] Pin 14 de la Pico con continuidad a U2 pin 1
- [ ] Pines 1, 2, 4, 5, 6, 7, 9 y 10 de la Pico con continuidad a U2 pines 18 a 11, en ese orden
- [ ] Diodo entre U3-7 y el pin 39 de la Pico, con el cátodo del lado de la Pico
- [ ] Sin continuidad entre U3-7 y el pin 36 de la Pico
- [ ] Sin continuidad entre VCC y GND
