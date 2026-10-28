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

## Desde MSX-DOS: `SDF.COM`

Los mismos comandos, desde el DOS (MSX-DOS 1):

| Comando | Qué hace |
|---|---|
| `SDF` | Versión del firmware, estado de la SD e imágenes de A: y B: |
| `SDF DIR` | Lista las imágenes `.DSK` de la raíz, en columnas |
| `SDF MOUNT d: X` | Monta `X.DSK` en `d:` (`A:` o `B:`) |
| `SDF UMOUNT d:` | Deja `d:` sin imagen |
| `SDF NEW X [360]` | Crea `X.DSK` vacía y formateada, de 720 KB o de 360 KB; no la monta |
| `SDF DRIVES n` | Cantidad de unidades, 1 o 2, desde el próximo arranque |
| `SDF BORRAR` | Borra los datos personales de la Pico; antes pide confirmar |
| `SDF UPDATE [/F]` | Baja el último firmware de GitHub y lo graba; antes pide confirmar (`ACTUALIZAR.md`) |

```
B>SDF
SDF-Pico 0.9
SD ok
A: SISTEMA.DSK
B: JUEGOS.DSK
B>SDF MOUNT B: DATOS
B: DATOS.DSK
```

Después de `MOUNT` o `UMOUNT` el DOS se entera solo del cambio: el firmware
contesta "cambiado" en el siguiente `DSKCHG` y el DOS relee la FAT.

- Cambiar la imagen de A: funciona, pero MSX-DOS 1 recarga `COMMAND.COM`
  cuando un programa grande pisa la memoria donde vive. Si la imagen nueva de
  A: no lo tiene, puede pedir el disco del sistema.
- `SDF NEW` formatea con un sector de arranque propio, de disco de datos:
  el sector de arranque de `CALL SDFNEW` es del MSX-DOS kit de ASCII y no se
  puede copiar en el programa. Si se arranca desde esa imagen, el MSX sigue
  a BASIC.

### Borrar los datos personales

`SDF BORRAR` deja la Pico como de fábrica antes de prestarla o venderla. Pide
confirmar con `S` y después:

- desconecta el Wi-Fi y borra la red guardada y su clave;
- cierra la conexión TCP, si hay una abierta;
- borra la URL de la cámara, con su usuario y su clave, y la última foto;
- devuelve la zona horaria a UTC-3.

Los ajustes viven en un sector de la flash de la Pico, que queda borrado
entero (en FFh). Lo que está en la SD (las imágenes y `SDFPICO.CFG`) no se
toca. Desde la consola USB hace lo mismo `borrar todo`. Necesita el firmware
0.13.

El fuente es `comandos/sdf.asm`; se ensambla con `python tools/comandos.py`.

Para recorrer la SD entera y copiar archivos sueltos entre la SD y el disco,
está `SD.COM` (`ARCHIVOS.md`).
Para saber qué hay montado usa un comando que la DiskROM no necesita:

```
OUT (01h), CMD_SDFINFO          0E6h
IN  (00h)               unidades que ve el DOS, 1 o 2
IN  (00h) ... 0         imagen de A:, vacía si no hay
IN  (00h) ... 0         imagen de B:, vacía si no hay
```

Y para `SDF BORRAR`, con un parámetro fijo para que un byte suelto no alcance:

```
OUT (01h), CMD_SDFBORRAR        0E7h
OUT (00h), 5Ah
IN  (00h)               0, o 5 si el parámetro no es 5Ah
```

> **Estado:** probado en la DPC-200. Para probar cambios sin hardware,
> `tools/webmsx_pico_sd.js` simula la Pico en WebMSX y guarda los sectores del
> formateo para revisarlos.

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
