# Archivos de la SD

> **Estado:** firmware 0.10 y `SD.COM`, probados en la DPC-200. La lógica del
> firmware también está probada en la PC (FatFs sobre un disco en memoria) y
> `SD.COM` en WebMSX con una Pico simulada (`tools/webmsx_pico_fs.js`).

Además de montar imágenes `.DSK`, el MSX puede recorrer la tarjeta SD
entera: cambiar de directorio, listar, copiar archivos de la SD al disco y del
disco a la SD, borrar, crear directorios y renombrar. Así la SD sirve de
intercambio con la PC sin tener que meter los archivos en una imagen.

Hay dos partes:

- **La API del firmware**: comandos C0h a CCh del protocolo. El estado (el
  directorio actual, el último listado y un archivo abierto) queda en la
  Pico entre un comando y el siguiente.
- **`SD.COM`**, un programa de MSX-DOS 1 con comandos al estilo de un FTP.
  Usa la API a través de `comandos/sdfs.inc`, que también sirve para otros
  programas (un explorador de archivos, por ejemplo).

## SD.COM

```
SD                       consola, hasta EXIT
SD CD [ruta]             cambia el directorio (sin ruta, lo muestra)
SD DIR [ruta] [/A]       lista; /A muestra también ocultos y de sistema
SD GET ruta [d:archivo]  copia de la SD al disco
SD PUT d:archivo [ruta]  copia del disco a la SD
SD DEL ruta              borra un archivo, o un directorio vacío (también RD)
SD MD ruta               crea un directorio
SD REN ruta nombre       renombra; si el nombre trae directorio, lo mueve
```

Cada comando se puede dar suelto (`SD DIR`) o, con `SD` solo, en una consola
que muestra el directorio actual:

```
B>SD
SD.COM, la SD del SDF-Pico.
HELP para los comandos.
SD \> CD JUEGOS
SD \JUEGOS> DIR *.ROM
KNIGHT~1.ROM    32768 19-10-26 Knigh
NEMESIS.ROM    131072 19-10-26
2 archivos, 0 directorios
163840 bytes, 1234567 KB libres
SD \JUEGOS> GET KNIGHTMARE.ROM
32768 de 32768 bytes
Copiado en KNIGHT~1.ROM, en 1,0 s
SD \JUEGOS> EXIT
```

- **Rutas**: con `\` o `/`, relativas al directorio actual o absolutas, con
  `..`. Se puede usar el nombre largo o el corto; las que tienen espacios van
  entre comillas: `CD "Juegos de Konami"`.
- **El directorio actual queda en la Pico**: un `SD CD` vale para el
  `SD DIR` siguiente, también desde otro programa. Vuelve a la raíz al
  cambiar la tarjeta o reiniciar el MSX: con el `INIHRD` de la DiskROM el
  firmware cierra el archivo abierto, olvida el listado y corta la conexión
  TCP, si había una.
- **`DIR`** lista primero los directorios y después los archivos, por nombre.
  Si el último tramo tiene `*` o `?`, es un filtro (`DIR JUEGOS\*.ROM`) sobre
  el nombre corto o el largo. Muestra el nombre largo si entra en el ancho de
  la pantalla, hace una pausa en cada pantalla y ESC corta. Muestra hasta 512
  entradas por directorio.
- **`GET`** graba con el nombre corto de la SD si no se le da otro (o solo la
  unidad: `GET X.ROM A:`). Si el archivo ya está en el disco, pregunta antes
  de reemplazarlo. Si no entra, lo avisa antes de empezar.
- **`PUT`** sin ruta deja el archivo en el directorio actual de la SD, con su
  nombre; con la ruta de un directorio, adentro; con otra ruta, con ese
  nombre. Si ya existe, pregunta.
- ESC corta una copia. Lo que quedó a medias se borra, del disco o de la SD.
- No se puede borrar, renombrar ni reemplazar una imagen montada, ni el
  directorio actual o uno que lo contenga. Una imagen montada sí se puede
  copiar al disco con `GET`.

El fuente es `comandos/sd.asm`; se ensambla con `python tools/comandos.py`.

## Nombres largos

Desde el firmware 0.10, FatFs maneja nombres largos (`FF_USE_LFN`). Las
imágenes se siguen montando y guardando en `SDFPICO.CFG` por su nombre corto
8.3, que es lo que ve el MSX. Los listados traen los dos nombres: el corto
para usar en el MSX y el largo para mostrar. Las rutas que devuelve el
firmware son siempre de nombres cortos.

Con nombres largos, los archivos que crea el firmware llevan la fecha y la
hora del reloj (`RELOJ.md`). Si todavía no se puso, quedan en 1980-01-01.

## La API

Los comandos siguen las reglas de `PROTOCOLO.md`: después del comando, los
parámetros por 00h, y la respuesta se lee de 00h cuando `OCUPADO` baja. Una
ruta va como un byte de largo (hasta 127) y el texto, sin el 0. Los números
de más de un byte van con el byte bajo primero.

| Cmd | Nombre | Parámetros | Respuesta |
|---|---|---|---|
| C0h | `FSCD` | largo, ruta | error |
| C1h | `FSPWD` | — | la ruta actual, ASCIIZ (`\` en la raíz) |
| C2h | `FSDIR` | opciones, largo, ruta o patrón | error, cantidad (2), cortado |
| C3h | `FSLIST` | desde (2), máximo (hasta 8) | n, n entradas |
| C4h | `FSOPEN` | modo, largo, ruta | error, largo del archivo (4) |
| C5h | `FSREAD` | máximo (2, hasta 512) | error, n (2), n bytes |
| C6h | `FSWRITE` | n (2, hasta 512), n bytes | error |
| C7h | `FSCLOSE` | — | error |
| C8h | `FSDEL` | largo, ruta | error |
| C9h | `FSMKDIR` | largo, ruta | error |
| CAh | `FSREN` | largo, ruta, largo, nombre nuevo | error |
| CBh | `FSSTAT` | largo, ruta | error, una entrada |
| CCh | `FSFREE` | — | error, KB libres (4), KB en total (4) |

- **`FSDIR`** lee el directorio entero, lo ordena y lo guarda en la Pico;
  después `FSLIST` lo trae de a pedazos, desde cualquier posición. Un
  explorador puede subir y bajar por la lista sin volver a leer la SD. Una
  ruta vacía es el directorio actual. Opciones: bit 0, también ocultos y de
  sistema; bit 1, una entrada `..` primero (fuera de la raíz). Si el
  directorio tiene más de 512 entradas, `cortado` es 1.
- **`FSOPEN`**: modo 0 lee, 1 crea un archivo nuevo (error si existe), 2 crea
  o vacía el que haya. Hay un solo archivo abierto: abrir otro cierra el
  anterior. Lo grabado queda completo en la SD con `FSCLOSE`.
- **`FSWRITE`**: `OCUPADO` sigue en 1 desde el comando hasta grabar los
  bytes; recién entonces se lee el error.
- **`FSREN`**: si el nombre nuevo no tiene directorio, queda en el mismo; si
  lo tiene, se mueve ahí (relativo al directorio actual).
- **`FSFREE`**: en FAT32 la cuenta sale del sector FSINFO. Si la tarjeta no
  lo tiene al día, FatFs recorre la FAT y en una tarjeta grande puede tardar
  más que la espera del MSX.

### Entradas

`FSLIST` y `FSSTAT` devuelven entradas de 64 bytes:

| Bytes | Contenido |
|---|---|
| 0 | Atributos de FAT: 01h solo lectura, 02h oculto, 04h sistema, 10h directorio, 20h archivo |
| 1-4 | Largo |
| 5-6 | Fecha, como en MSX-DOS: bits 0-4 día, 5-8 mes, 9-15 año - 1980 |
| 7-8 | Hora: bits 0-4 segundos / 2, 5-10 minutos, 11-15 hora |
| 9-21 | Nombre corto 8.3, ASCIIZ |
| 22-63 | Nombre para mostrar (el largo, o el corto), ASCIIZ, hasta 41 letras |

### Errores

Los códigos son los del BASIC de disco donde hay uno que corresponde, más
tres propios:

| Código | Significado |
|---|---|
| 0 | Bien |
| 5 | Pedido no válido (borrar la raíz, mover un directorio dentro de sí mismo) |
| 53 | No existe el archivo |
| 54 | En uso: una imagen montada, el archivo abierto o el directorio actual |
| 56 | Nombre no válido |
| 59 | No hay archivo abierto (o está abierto en el otro sentido) |
| 65 | Ya existe |
| 66 | La SD (o el directorio raíz de FAT16) está lleno |
| 68 | Archivo de solo lectura |
| 69 | Error de la SD |
| 70 | No hay SD |
| 80 | No existe el directorio |
| 81 | El directorio no está vacío |
| 82 | Es un directorio (y se esperaba un archivo) |

## Desde otro programa: `sdfs.inc`

```
	INCLUDE	"sdfs.inc"
```

Una rutina por comando, con las rutas en ASCIIZ: `FsCd`, `FsPwd`, `FsDir`,
`FsList`, `FsStat`, `FsOpen`, `FsRead`, `FsWrite`, `FsClose`, `FsDel`,
`FsMkdir`, `FsRen` y `FsFree`. Devuelven el error en A con el flag Z si es 0,
y cortan las interrupciones solo mientras hablan con la Pico. El programa
tiene que definir `FsNoPico`, adonde se salta si la Pico no contesta. Los
registros de cada rutina están en el comentario de cada una.

Un explorador de archivos tendría lo necesario: `FsDir` con `FS_PADRE` arma
la lista con `..`, `FsList` trae la parte que se ve en pantalla, y
`FsOpen`/`FsRead`/`FsWrite` copian.

## Prueba

- **El firmware en la PC.** `archivos.c` se compila junto con FatFs y un
  disco en memoria con FAT32, y se prueban las rutas (nombres largos, `..`,
  absolutas), los filtros, el orden, leer y grabar, las imágenes montadas,
  el corte en 512 entradas y el cambio de tarjeta.
- **`SD.COM` en WebMSX.** `tools/webmsx_pico_fs.js` simula la API con una SD
  en memoria, con nombres largos, un archivo oculto, uno de solo lectura y
  una imagen montada. Se probaron `GET` de 32 y 128 KB y `PUT` de 60 KB, byte
  por byte; reemplazar con confirmación, la consola, los errores y cortar con
  ESC de los dos lados. En WebMSX copia unos 40 KB/s en los dos sentidos (el
  emulador no simula los tiempos de la Pico ni de la SD).
