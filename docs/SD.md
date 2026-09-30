# Tarjeta SD

Los discos del SDF-Pico son imágenes `.DSK` guardadas en una tarjeta SD, igual
que en el SDF-1. El firmware monta una imagen en A: y otra en B:, y el MSX las
ve como disqueteras.

## La tarjeta

- Formateada en **FAT16 o FAT32**. Las tarjetas de 64 GB o más vienen en exFAT,
  que no se admite: hay que reformatearlas en FAT32.
- Las imágenes van en la **raíz**, con nombre corto 8.3 y extensión `.DSK`.
- Cada imagen tiene que medir exactamente 737 280 bytes (720 KB) o 368 640
  bytes (360 KB).

El módulo se conecta por SPI a los pines 16, 17, 19 y 20 de la Pico (ver
`hardware/CABLEADO.md`). La tarjeta se puede poner y sacar con el MSX
encendido: el firmware reintenta cada medio segundo hasta que contesta.

## Comandos desde BASIC

| Comando | Qué hace |
|---|---|
| `CALL SDFTEST` | Versión del firmware y estado de la SD |
| `CALL SDFFILES` | Lista las imágenes `.DSK` de la raíz |
| `CALL SDFMOUNT(drive, "NOMBRE")` | Monta una imagen; `drive` es 0 para A: y 1 para B:. Sin extensión se agrega `.DSK` |
| `CALL SDFUMOUNT(drive)` | Deja el drive sin imagen |
| `CALL SDFNEW("NOMBRE"[, tamaño])` | Crea una imagen vacía y formateada, de 720 o 360 KB; no la monta |
| `CALL SDFDRIVES(n)` | Cantidad de drives que ve el DOS, 1 o 2, desde el próximo arranque |
| `CALL SDFDEBUG` | Cuentas de lecturas, escrituras y checksums, con el tiempo y la velocidad desde el `CALL SDFDEBUG` anterior |

`CALL SDFTEST` muestra en la segunda línea el estado de la tarjeta:

| Texto | Significado |
|---|---|
| `SD ok` | Tarjeta inicializada y FAT montada |
| `SD iniciando` | Todavía no hubo un intento |
| `SD no contesta` | Sin tarjeta, módulo desconectado o cableado |
| `SD no inicia` | La tarjeta contesta pero no termina de inicializar |
| `SD no es FAT` | La tarjeta funciona, pero está en exFAT o sin formatear |
| `SD error de E/S` | Falló una lectura o una escritura |

Un drive sin imagen contesta "Disk offline".

## Lo que se recuerda

Las imágenes montadas y la cantidad de drives se guardan en la propia tarjeta,
en el archivo `SDFPICO.CFG` de la raíz, y se recuperan al encender. Es un
archivo de texto:

```
A=JUEGOS.DSK
B=
DRIVES=2
```

Si una imagen ya no está en la tarjeta, ese drive arranca vacío.

## Cómo se transfiere

Un `DSKIO` de lectura trae todos sus sectores de la SD a la memoria de la Pico
de una vez y los entrega por DMA. Uno de escritura junta todos los sectores en
memoria y los graba juntos con el último; si la tarjeta falla, el firmware lo
indica en el bit `ERROR` del registro de estado y la DiskROM devuelve "write
fault" al DOS.

Con `CALL SDFNEW`, la imagen se reserva contigua si hay lugar y se llena de
ceros. Mientras tanto, el firmware le entrega a la DiskROM un "todavía
trabajando" por segundo, para que no dé la espera por agotada.
