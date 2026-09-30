# MSX-SDF-Pico

Lector de tarjetas SD para MSX basado en una **Raspberry Pi Pico 2** (RP2350),
montada sobre la placa del [MSX-SDF-1](../MSX-SDF-1) en lugar del ATmega328P.

De la placa original se conservan el decodificador de puertos (dos `'138`), el
búfer de datos (`'245`) y la EEPROM con la DiskROM. La Pico atiende el bus del
MSX con sus bloques PIO y alimenta las transferencias por DMA, lo que permite
responder a cada acceso sin detener al Z80.

> **Estado:** en desarrollo. El bus funciona sin `/WAIT` en una Daewoo DPC-200,
> y el firmware atiende el protocolo del SDF-1 con un disco en la flash de la
> Pico (`docs/DISCO-FLASH.md`). Faltan la DiskROM propia y la tarjeta SD.

## Diferencias con el SDF-1

| | SDF-1 | SDF-Pico |
|---|---|---|
| Microcontrolador | ATmega328P | RP2350 (Pico 2) |
| Señal `/WAIT` | Detiene al Z80 en cada acceso | **No se usa**; la línea está cortada |
| Sincronización | El `/WAIT` cubre el tiempo de respuesta | El Z80 consulta el registro de estado |
| Transferencia de un byte | Rutina de interrupción | Programa PIO, sin intervención del núcleo |
| Sectores | Byte a byte desde el firmware | DMA entre la memoria y los FIFO de la PIO |
| DiskROM | La del SDF-1 | Derivada de la del SDF-1, con consulta de estado |

La decisión de fondo es prescindir de `/WAIT`. La PIO contesta un `IN` en
alrededor de 100 ns, contra una ventana de unos 550 ns, de modo que el dato
siempre llega a tiempo cuando está disponible. Cuando no lo está (por ejemplo,
mientras se lee un sector de la tarjeta), el registro de estado lo indica y la
DiskROM espera antes de leer.

## Estructura

| Carpeta | Contenido |
|---|---|
| `hardware/` | Modificaciones a la placa del SDF-1 y cableado de la Pico |
| `firmware/` | Firmware de la Pico, en C con el pico-sdk |
| `docs/` | Protocolo entre la DiskROM y el firmware, y procedimientos de prueba |
| `tools/` | `dsk2uf2.py`: carga una imagen `.DSK` en la flash de la Pico |

## Compilación del firmware

Se requiere la extensión *Raspberry Pi Pico* de VS Code, que instala el SDK y
las herramientas en `~/.pico-sdk`. Con eso:

```
cd firmware
.\build.ps1
```

El resultado es `firmware/build/sdf_pico.uf2`, que se copia a la Pico
conectada en modo BOOTSEL.

## Licencia

MIT. Ver [LICENSE](LICENSE).
