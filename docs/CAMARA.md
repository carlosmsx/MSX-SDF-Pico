# Cámara IP

El SDF-Pico puede sacar una foto de una cámara IP de la red y mostrarla en
SCREEN 2. La Pico baja la imagen JPEG por HTTP, la convierte a SCREEN 2 y el
MSX solo copia el resultado al VDP. El firmware está en
`firmware/src/camara.c` (la conexión) y `firmware/src/sc2.c` (la conversión);
el programa de MSX-DOS, en `comandos/cam.asm`.

> **Estado: sin probar con hardware.** El firmware 0.11 compila sin avisos.
> La conversión se probó en la PC con una foto real de la cámara
> (`sc2.c` compilado con MSVC), y `CAM.COM` en WebMSX con una Pico simulada
> (`tools/webmsx_pico_cam.js`). Falta probar la conexión con la cámara desde
> la Pico.

## Qué cámaras sirven

La cámara tiene que entregar una foto JPEG por HTTP: lo que los fabricantes
llaman *snapshot*. La mayoría de las cámaras con ONVIF lo tienen. Por ejemplo:

| Marca | Ruta de la foto |
|---|---|
| Hikvision | `/ISAPI/Streaming/channels/101/picture` (102: la del flujo secundario) |
| Dahua | `/cgi-bin/snapshot.cgi` |
| Otras | `/snapshot.jpg`, `/image.jpg`, `/cgi-bin/snapshot.cgi`... |

Condiciones:

- **HTTP sin cifrar.** No hay HTTPS: las cámaras que lo usan suelen aceptar
  también HTTP en el puerto 80.
- **Autenticación Basic** (`usuario:clave@` en la URL). Las cámaras que solo
  aceptan Digest no sirven todavía.
- **JPEG *baseline*.** El progresivo no está soportado; las fotos de las
  cámaras no lo usan.
- Hasta unos 3000 píxeles de ancho (2560x1440 y 3840x2160 entran).

Las cámaras que solo transmiten video por RTSP (H.264 o H.265) o que dependen
de la nube del fabricante no sirven directamente. En ese caso se puede poner
en el medio un programa que entregue una foto por HTTP, por ejemplo
[go2rtc](https://github.com/AlexxIT/go2rtc) (`/api/frame.jpeg?src=...`).

Para probar la URL desde la PC, la foto tiene que abrirse en el navegador o con:

```
curl -u usuario:clave -o foto.jpg http://192.168.1.3/ISAPI/Streaming/channels/102/picture
```

Probado con una Hikvision de 704x576: contesta en 0,15 a 0,2 s con un JPEG
de unos 60 KB.

## CAM.COM

```
CAM [/C] [/F archivo] [URL]
CAM /U [URL | -]
```

| Uso | Qué hace |
|---|---|
| `CAM` | Saca una foto con la URL guardada y la muestra en SCREEN 2 hasta que se aprieta una tecla |
| `CAM URL` | Lo mismo, con esa URL (no la guarda) |
| `CAM /C` | Sin parar: muestra una foto tras otra hasta una tecla, y al final dice cuántas fueron y en cuánto tiempo |
| `CAM /F archivo` | Graba la foto en un archivo en lugar de mostrarla. Sin extensión, `.SC2` |
| `CAM /U URL` | Guarda la URL en la flash de la Pico |
| `CAM /U` | Muestra la URL guardada, con la clave cambiada por `****` |
| `CAM /U -` | Olvida la URL guardada |

La URL es `http://usuario:clave@host:puerto/ruta`. El `http://`, el usuario
y el puerto (80) se pueden omitir. Ejemplo:

```
B>CAM /U http://claude:clave@192.168.1.3/ISAPI/Streaming/channels/102/picture
URL guardada.

B>CAM
Sacando la foto...
Foto de 704x576 en 1,2 s.

B>CAM /F B:PATIO
Sacando la foto...
Grabada en B:PATIO.SC2:
704x576 en 1,2 s.
```

El archivo tiene el formato de `BSAVE` (VRAM de 0000h a 37FFh: patrones,
nombres, sprites apagados y colores). Desde BASIC:

```
SCREEN 2:BLOAD"PATIO.SC2",S:A$=INPUT$(1)
```

Un archivo existente se reemplaza sin preguntar. Mientras la Pico saca la
foto, una tecla la cancela (en `/C`, termina después de la foto en curso).

La URL guardada sobrevive al apagado y a las actualizaciones del firmware,
junto a la red Wi-Fi. Como la clave del Wi-Fi, se guarda **sin cifrar** en la
flash de la Pico: conviene crear en la cámara un usuario solo para ver.

## La conversión

La Pico decodifica el JPEG a medida que llega, con
[TJpgDec](http://elm-chan.org/fsw/tjpgd/) de ChaN (`firmware/lib/tjpgd`), sin
guardar la imagen entera:

1. TJpgDec reduce la imagen (1/2, 1/4 u 1/8) lo justo para que siga cubriendo
   256x192. Una de 704x576 sale a 352x288.
2. Hasta 1,55:1 (4:3, 5:4, los 704x576 de las cámaras D1) la imagen se estira
   a 4:3. Más ancha (16:9) se recortan los costados.
3. Cada fila de bloques decodificados va a una banda, y de ahí se interpolan
   las filas de 256 píxeles.
4. Se sube la saturación (x1,8) y el contraste (x1,2): los colores del
   TMS9918 son muy vivos y una foto apagada termina en ruido de tonos
   mezclados.
5. Para cada tramo de 8x1 píxeles se prueban los 105 pares de colores contra
   los 8 píxeles, con difusión de error (Floyd-Steinberg, el 80% del error).

El resultado son las tablas de patrones y de colores (6 KB cada una). La tabla
de nombres es la de siempre en SCREEN 2 (0 a 255 en cada tercio).

`tools/jpg2sc2.py` hace lo mismo en la PC, para probar fotos y ajustes:

```
python tools/jpg2sc2.py FOTO.JPG [--encuadre recortar|bandas|estirar]
                       [--saturacion 1.8] [--contraste 1.2] [--fuerza 0.8]
```

Deja `FOTO.SC2` (para `BLOAD`), `FOTO.png` (lo que se vería) y `FOTO_cmp.png`
(la foto y el resultado, lado a lado).

## Consola USB

| Comando | Qué hace |
|---|---|
| `camara` | La URL guardada (sin la clave) y el resultado de la última foto |
| `camara foto` | Saca una foto con la URL guardada y dice cuánto tardó: en total y hasta la respuesta de la cámara |

## Comandos del protocolo

Son nuevos (0B9h a 0BDh) y se agregan sin cambiar los del protocolo 8. Como en
`PING.COM`, la foto se pide y después se consulta: puede tardar más que la
espera de la ROM.

### `CMD_CAMSHOT` (0B9h)

```
OUT (01h), CMD_CAMSHOT
OUT (00h), largo, URL x largo       largo 0: la URL guardada
IN  (00h)               0 = pedido aceptado, 1 = hay una foto en curso, 2 = URL inválida
```

### `CMD_CAMPOLL` (0BAh)

```
OUT (01h), CMD_CAMPOLL
IN  (00h) x 10          estado, resultado, detalle (L, H), ancho (L, H),
                        alto (L, H), tiempo en ms (L, H)
```

Estado: 0 = no se pidió nada, 1 = en curso, 2 = lista. Con el estado en 2:

| Resultado | Significado | Detalle |
|---|---|---|
| 0 | La foto está lista | — |
| 1 | La Pico no está conectada al Wi-Fi | — |
| 2 | El nombre no se resolvió | — |
| 3 | No conectó, o se cortó antes de terminar | — |
| 4 | La cámara no contestó 200 | El código HTTP (401: usuario o clave) |
| 5 | TJpgDec no entiende la imagen | El error de TJpgDec (8: JPEG progresivo) |
| 6 | Tamaño no soportado | — |
| 7 | La cámara dejó de mandar (8 s sin datos) | — |
| 8 | No hay URL guardada | — |

Ancho y alto son los del JPEG; el tiempo, desde el pedido hasta el final.

### `CMD_CAMREAD` (0BBh)

```
OUT (01h), CMD_CAMREAD
OUT (00h), bloque       0 a 47
IN  (00h) x 256         el bloque
```

Los bloques 0 a 23 son la tabla de patrones (VRAM 0000h a 17FFh) y los 24 a
47, la de colores (2000h a 37FFh). `CAM.COM` los pasa directo al VDP: entre
un `OUT (98h)` y el siguiente hay 35 ciclos, más que los 29 que pide el
TMS9918 en SCREEN 2 con la pantalla encendida.

### `CMD_CAMURL` (0BCh) y `CMD_CAMVER` (0BDh)

```
OUT (01h), CMD_CAMURL
OUT (00h), largo, URL x largo       largo 0: la olvida
IN  (00h)               0 = aceptada, 2 = URL inválida

OUT (01h), CMD_CAMVER
IN  (00h) ... 0         la URL guardada, con la clave cambiada por ****
```

La URL tiene hasta 200 caracteres; la graba el núcleo 0 en la flash.

## Memoria y tiempos

La foto se arma en el núcleo 0, en el bucle principal: mientras tanto la
consola y el Wi-Fi esperan, pero el bus (núcleo 1) sigue atendiendo al MSX.
Lo recibido se queda en los búferes de lwIP hasta que el decodificador lo
pide, así que no hace falta un búfer para el JPEG. La conversión usa unos
40 KB: la banda (20 KB), la pantalla (12 KB), la memoria de TJpgDec (5 KB) y
el error de la difusión (3 KB). Con el firmware 0.11 quedan unos 6 KB de RAM
libres.

En la PC, la conversión de una foto de 704x576 tarda unos 60 ms. En la Pico
falta medirlo (`camara foto` en la consola).
