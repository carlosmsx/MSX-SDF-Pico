# MSX-SDF-Pico

Un **perfil del [MSX-SDF-1](https://github.com/carlosmsx/MSX-SDF-1)**: la misma
placa de I/O en formato cartucho, con una **Raspberry Pi Pico 2 W** (RP2350) en
lugar del ATmega328P.

El SDF-1 está pensado como un kit, en el que cada proyecto cambia la ROM, el
firmware y lo que se monta en el área de islas perforadas. Este proyecto lleva
esa idea un paso más allá y reemplaza también el microcontrolador. De la placa
original se conservan el decodificador de puertos (dos `'138`), el búfer de
datos (`'245`) y la EEPROM W27C512. La Pico se monta sobre el área universal y
atiende el bus del MSX con sus bloques PIO.

El cambio de microcontrolador permite reunir en un solo cartucho tres funciones:

- **Lector de memorias SD.** El cartucho presenta al MSX hasta dos unidades de
  disco estándar y arranca MSX-DOS y Disk BASIC desde imágenes `.DSK`
  guardadas en la tarjeta, igual que el lector del SDF-1.
- **Wi-Fi.** El módulo inalámbrico de la Pico 2 W se configura desde el propio
  MSX con `CALL WIFI`, un menú que busca las redes, pide la clave y la guarda.
- **Reloj.** La fecha y la hora se obtienen de Internet al encender. MSX-DOS
  arranca con la fecha correcta, los archivos se graban con su fecha, y
  `CALL RTC` y `OPEN "RTC:"` permiten consultarlas desde BASIC, sin chip de
  reloj ni pila.

> **Estado: en desarrollo.** Las tres funciones están probadas en una Daewoo
> DPC-200, la única máquina en la que se ensayó hasta ahora. La placa es una
> SDF-1 rev1 con una sola modificación (ver
> [hardware/CABLEADO.md](hardware/CABLEADO.md)).

## Las decisiones de diseño

**Sin `/WAIT`.** El SDF-1 detiene al Z80 en cada acceso hasta que el ATmega
responde. El SDF-Pico prescinde de esa señal, y la línea queda cortada en la
placa. La PIO contesta un `IN` en alrededor de 100 ns, contra una ventana de
unos 550 ns, de modo que el dato siempre llega a tiempo cuando está
disponible. Cuando no lo está, por ejemplo mientras se lee un sector de la
tarjeta, el registro de estado lo indica y la DiskROM espera antes de leer.

**El registro de estado, finalmente en uso.** En el SDF-1 la lectura del
puerto `0x01` está cableada pero devuelve un valor fijo. Acá es el mecanismo
de sincronización: un bit de *ocupado*, otro de error y otro de cambio de
disco. El protocolo es el del SDF-1 con ese único agregado (ver
[docs/PROTOCOLO.md](docs/PROTOCOLO.md)).

**PIO y DMA en lugar de interrupciones.** Cada acceso del Z80 lo atiende un
programa PIO, sin intervención del procesador. Los sectores pasan por DMA
entre la memoria y los FIFO de la PIO, y la DiskROM los transfiere con `INIR`
y `OTIR`. En la DPC-200 se midieron unos 88 KB/s en lectura y 80 KB/s en
escritura, contra unos 45 KB/s del SDF-1.

**Un núcleo para el bus, otro para todo lo demás.** El núcleo 1 atiende el
protocolo y la tarjeta SD, sin interrupciones. El núcleo 0 se ocupa del
Wi-Fi, del reloj y de la consola USB. Todo el programa corre desde la RAM, de
modo que grabar la flash o esperar a la red nunca demora una respuesta al MSX.

**Los menús los arma el firmware.** `CALL WIFI` ocupa unos 100 bytes de la
EEPROM: es una terminal que envía cada tecla y muestra lo que devuelve la
Pico. El menú completo vive en el firmware, que se actualiza por USB sin
regrabar la EEPROM.

**La clave, fuera de la tarjeta.** La red, la clave y la zona horaria se
guardan en la flash de la Pico, no en la SD, de modo que no viajan con la
tarjeta. La clave no se muestra en ningún momento.

## Diferencias con el SDF-1

| | SDF-1 | SDF-Pico |
|---|---|---|
| Microcontrolador | ATmega328P a 20 MHz | RP2350 (Pico 2 W) a 150 MHz |
| Señal `/WAIT` | Detiene al Z80 en cada acceso | No se usa; la línea está cortada |
| Sincronización | El `/WAIT` cubre el tiempo de respuesta | El Z80 consulta el registro de estado |
| Atención del bus | Rutina de interrupción | Programa PIO, sin intervención del procesador |
| Sectores | Byte a byte desde el firmware | DMA entre la memoria y la PIO |
| Reloj | DS1307 con pila, por I2C | Por Internet, sin chip |
| Imágenes montadas | En la EEPROM del ATmega | En la tarjeta (`SDFPICO.CFG`) |
| Red y zona horaria | — | En la flash de la Pico |
| DiskROM | La del SDF-1 | Derivada de la del SDF-1, con consulta de estado |

## Comandos desde BASIC

| Comando | Función |
|---|---|
| `CALL SDFFILES` | Lista las imágenes `.DSK` de la tarjeta |
| `CALL SDFMOUNT(drive, "NOMBRE")` | Monta una imagen en A: (0) o B: (1) |
| `CALL SDFUMOUNT(drive)` | Deja el drive sin imagen |
| `CALL SDFNEW("NOMBRE"[, tamaño])` | Crea una imagen vacía y formateada, de 720 o 360 KB |
| `CALL SDFDRIVES(n)` | Cantidad de drives que ve el DOS, desde el próximo arranque |
| `CALL WIFI` | Menú de la red Wi-Fi y de la zona horaria |
| `CALL RTC` | Muestra la fecha y la hora; con un texto, las ajusta |
| `CALL SDFTEST` | Versión del firmware, protocolo y estado de la tarjeta |
| `CALL SDFDEBUG` | Contadores y velocidad de transferencia, para pruebas |

Además, `OPEN "RTC:"` permite leer la fecha y la hora desde un programa, en
varios formatos. El detalle de cada función está en la documentación.

## Documentación

| Documento | Contenido |
|---|---|
| [hardware/CABLEADO.md](hardware/CABLEADO.md) | Modificación de la placa y cableado de la Pico |
| [docs/SD.md](docs/SD.md) | La tarjeta, las imágenes, los `CALL SDF*` y `SDF.COM` |
| [docs/ARCHIVOS.md](docs/ARCHIVOS.md) | Los archivos de la SD desde el MSX: `SD.COM` y la API |
| [docs/WIFI.md](docs/WIFI.md) | El Wi-Fi, `CALL WIFI`, `WIFI.COM` y la consola USB |
| [docs/RELOJ.md](docs/RELOJ.md) | El reloj, la zona horaria y `OPEN "RTC:"` |
| [docs/RED.md](docs/RED.md) | Conexión TCP para terminales (en desarrollo) |
| [docs/CAMARA.md](docs/CAMARA.md) | Fotos de una cámara IP en SCREEN 2: `CAM.COM` (en desarrollo) |
| [docs/VI.md](docs/VI.md) | `VI.COM`, un editor al estilo de vi para MSX-DOS 1 |
| [docs/COMANDOS.md](docs/COMANDOS.md) | Utilidades de MSX-DOS 1: `GREP.COM` |
| [docs/PROTOCOLO.md](docs/PROTOCOLO.md) | Protocolo entre la DiskROM y el firmware |
| [docs/PRUEBA-BUS.md](docs/PRUEBA-BUS.md) | Prueba del bus desde BASIC, sin DiskROM, y `RDTEST.COM` para fallas de lectura |

## Estructura

| Carpeta | Contenido |
|---|---|
| `diskrom/` | DiskROM (Z80), derivada de la del SDF-1 |
| `comandos/` | Programas de MSX-DOS: `PING.COM`, `WIFI.COM`, `GET.COM`, `SDF.COM`, `SD.COM`, `CAM.COM` y `RDTEST.COM`, que usan la Pico, y `VI.COM` y `GREP.COM` |
| `hardware/` | Modificación de la placa del SDF-1 y cableado de la Pico |
| `firmware/` | Firmware de la Pico, en C con el pico-sdk |
| `docs/` | Documentación de cada función y del protocolo |
| `tools/` | Armado de la ROM (`mkrom.py`, `patch_rtc_kernel.py`) |

## Herramientas

La compilación se probó en Windows 11 con estas versiones. Ninguna herramienta
se incluye en el repositorio.

| Herramienta | Versión | Para qué | Dónde |
|---|---|---|---|
| Extensión *Raspberry Pi Pico* de VS Code | — | Firmware | [Marketplace](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico) |
| pico-sdk | 2.3.0 | Firmware | Lo instala la extensión |
| GCC para ARM | 14.2 (`14_2_Rel1`) | Firmware | Lo instala la extensión |
| CMake | 3.31.5 | Firmware | Lo instala la extensión |
| Ninja | 1.12.1 | Firmware | Lo instala la extensión |
| picotool y pioasm | 2.3.0 | Firmware | Lo instala la extensión |
| Python | 3 | ROM, programas de DOS, `grabar.py` | [python.org](https://www.python.org/downloads/) |
| pyserial | — | Solo `firmware/grabar.py` | `pip install pyserial` |
| GNU make | 4.4.1 | DiskROM | En Windows, por ejemplo `scoop install make` ([Scoop](https://scoop.sh/)) |
| Nestor80 (N80 y LK80) | N80 1.3.6, LK80 1.1.1 | DiskROM | [Releases de Nestor80](https://github.com/Konamiman/Nestor80/releases) |
| .NET | 6 | Nestor80 | [dotnet.microsoft.com](https://dotnet.microsoft.com/download/dotnet/6.0) |
| PASMO | 0.5.5 | Programas de DOS | [pasmo.speccy.org](https://pasmo.speccy.org/) |
| MSX-DOS kit de ASCII | 1984-85 | DiskROM | No se distribuye (ver abajo) |

`build.ps1` busca cada herramienta del firmware en `~/.pico-sdk` con la versión
exacta de la tabla. Para que la extensión las instale, se importa la carpeta
`firmware/` como proyecto eligiendo el SDK 2.3.0.

Los ejecutables `N80.exe` y `LK80.exe` van en `tools/bin/`. La variante
*framework-dependent* de Nestor80 requiere el runtime de .NET 6. La variante
*self-contained* no lo requiere.

PASMO se busca en el `PATH`, en la variable de entorno `PASMO` o con
`python tools/comandos.py --pasmo RUTA`.

Del MSX-DOS kit, la DiskROM usa `DOSHEAD.REL`, `BIOS.REL`, `MSXDOS.REL`,
`SMLDISP.REL`, `INIT.REL`, `DSKBASIC.REL`, `MSXDATA.REL`, `BASDATA.REL` y
`BOOT.Z80`, que van en `build/`. `make check` verifica que estén Nestor80 y
el kit.

## Compilación del firmware

Se requiere la extensión *Raspberry Pi Pico* de VS Code, que instala el SDK y
las herramientas en `~/.pico-sdk`. Con eso:

```
cd firmware
.\build.ps1
```

El resultado es `firmware/build/sdf_pico.uf2`, que se copia a la Pico
conectada en modo BOOTSEL. `python firmware/grabar.py` lo hace sin el botón:
reinicia la Pico en BOOTSEL abriendo su consola USB a 1200 baudios y copia el
archivo (requiere pyserial). Actualizar el firmware no borra la red ni la zona
horaria guardadas.

Los programas de MSX-DOS de `comandos/` se ensamblan con
`python tools/comandos.py` (PASMO), que los deja en `out/msx/` y arma
`out/comandos.dsk`. Con `out/msx/` servida en la red
(`python -m http.server 8000 --bind 0.0.0.0 --directory out/msx`), se bajan
desde el MSX con `GET.COM`.

## Compilación de la DiskROM

La DiskROM se ensambla con [Nestor80](https://github.com/Konamiman/Nestor80)
(N80 y LK80, en `tools/bin/`) y se linkea contra el MSX-DOS kit de ASCII, que
va en `build/`. Ninguno de los dos se incluye en el repositorio: el kit es
código de terceros. Con ambos en su lugar:

```
make rom
```

El resultado es `out/sdfpico.rom`, la imagen de 64 KB para la W27C512. Por
contener el kernel de ASCII, esa imagen tampoco se publica.

## Licencia

MIT. Ver [LICENSE](LICENSE).
