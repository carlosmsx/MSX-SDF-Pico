# Conexión TCP

El SDF-Pico puede hacer de módem Wi-Fi: el MSX abre una conexión TCP a un
servidor y le pasa bytes en los dos sentidos. Sirve para portar terminales
(RetroTerm, ver abajo) sin un módem ni un puerto serie. El firmware está en
`firmware/src/red.c`.

> **Estado: sin probar con hardware.** El firmware compila sin avisos y la
> variante de RetroTerm ensambla; falta probar todo en la DPC-200.

## Cómo se conecta

**Como a un módem Wi-Fi.** Mientras no hay conexión, lo que el MSX envía por
`CMD_NETIO` se interpreta como comandos AT (como con Zimodem), y las
respuestas vuelven por el mismo canal:

| Comando | Qué hace |
|---|---|
| `atd host:puerto` | Resuelve el nombre y conecta; también `atdt` y `atd"host:puerto"`. Sin puerto, el 23 |
| `at`, `ate0`, `ate1` | Prueba y eco (encendido por omisión) |
| `atb57600` | Velocidad de entrega al MSX |
| otros (`atf0`, `ath`...) | Se aceptan y no hacen nada |

Responde `OK`, `ERROR`, `CONNECT 57600` al conectar y `NO CARRIER` cuando el
servidor corta, después de los datos que quedaban. Con la conexión abierta
los bytes pasan tal cual.

**Desde la consola USB**, con la Pico ya conectada al Wi-Fi:

| Comando | Qué hace |
|---|---|
| `tcp host puerto` | Abre la conexión, sin CONNECT |
| `tcp cerrar` | La corta |
| `tcp baudios N` | Velocidad de entrega al MSX (57600 por omisión) |
| `tcp` | Estado y contadores |

`CMD_NETCONN` hace lo mismo desde el MSX; RetroTerm lo usa al arrancar para
cortar la conexión de una sesión anterior.

## Comandos del protocolo

Los dos comandos son nuevos (rango `0xBx`) y se agregan sin cambiar los del
protocolo 8.

### `CMD_NETCONN` (0B1h)

```
OUT (01h), CMD_NETCONN
OUT (00h), puerto (L), puerto (H), largo, host x largo
IN  (00h)               0 = pedido aceptado, 2 = nombre o puerto inválido
```

Con el host vacío, corta la conexión. El resultado de la conexión no llega
acá: se ve en el estado de `CMD_NETIO`.

### `CMD_NETIO` (0B2h)

```
OUT (01h), CMD_NETIO
OUT (00h), max, n, n bytes      max: lo más que acepta recibir; n: bytes a enviar
esperar OCUPADO = 0
IN  (00h)               estado
IN  (00h)               m, cantidad de bytes que siguen
IN  (00h) x m           los bytes recibidos
```

Una sola llamada envía y recibe. Límites por llamada: 60 bytes enviados
(`RED_MAX_TX`) y 255 recibidos (`RED_MAX_RX`). No cambia de modo ni toca el
disco, así que se puede intercalar con cualquier otro comando, por ejemplo las
llamadas al DOS de una descarga.

| Bit del estado | Nombre | Significado |
|---|---|---|
| 0 | `CONECTADO` | Hay un socket abierto |
| 1 | `CONECTANDO` | Se resuelve el nombre o se espera al servidor (hasta 15 s) |
| 2 | `CORTADA` | El servidor cerró o la conexión falló: `NO CARRIER` |

Después de `CORTADA` se pueden seguir leyendo los bytes que quedaban.

## Dosificación

Los servidores Turbo56K mandan el audio a la velocidad de línea del módem, y
el MSX lo reproduce a medida que llega. Si la Pico entregara todo de golpe,
el audio sonaría acelerado. Por eso, en las conexiones abiertas con `atd`,
`CMD_NETIO` entrega como máximo lo que habría entregado una línea a
`baudios` (balde de 120 bytes), no más. Las conexiones de `CMD_NETCONN` o
de la consola (las de `GET.COM`) entregan a toda velocidad.

## Memoria

Un anillo de 16 KB para lo recibido y uno de 2 KB para lo enviado. La ventana
de TCP es menor que el anillo, y solo se devuelve cuando el MSX consume, así
que el servidor se frena solo si el MSX no lee. No se pierden bytes.

## RetroTerm

La variante `sdf` de RetroTerm (`make msxsdf`, `IFACE=38` y `SDFP=1`) usa estos
comandos en lugar del puerto serie. Las teclas de marcado del terminal envían
`atd host:puerto` y el firmware conecta.

- `ReadByte` lee de un búfer de 120 bytes y, cuando se vacía, pide más con una
  sola llamada a `CMD_NETIO`. Con la Pico no hay RTS: el control de flujo es el
  búfer y la ventana de TCP.
- El audio por PCM (`TurboRX`) lee del búfer con el juego alterno de
  registros. Cada intercambio termina con un 0 de centinela, que no puede
  confundirse con el 0 que cierra el flujo porque el audio no lo contiene.
- Tiene que haber un solo intercambio a la vez: si la interrupción llama a
  `ReadByte` mientras el programa principal está en `CMD_NETIO`, la llamada
  devuelve "sin datos" (`SdfBusy`).

> **Sin probar con hardware.** La variante ensambla, y las otras cuatro
> producen exactamente los mismos `.com` que antes. Lo más delicado para
> afinar en la DPC-200 es la velocidad del audio PCM y el chasquido en cada
> relleno del búfer.

## PING.COM

`PING host` (MSX-DOS 1) manda cuatro ecos ICMP, uno por segundo, y muestra el
tiempo de cada respuesta y un resumen, como el ping de Windows. CTRL+C lo corta.
El fuente es `comandos/ping.asm`; se ensambla con PASMO (`pasmo ping.asm ping.com`).

El nombre lo resuelve y el eco lo manda la Pico (`firmware/src/ping.c`, con un
socket raw ICMP de lwIP). La ROM no espera con `OCUPADO`: pide y consulta.

### `CMD_PINGSEND` (0B3h)

```
OUT (01h), CMD_PINGSEND
OUT (00h), largo, host x largo      hasta 60 caracteres
IN  (00h)               0 = aceptado, 1 = hay un eco en curso, 2 = nombre no valido
```

### `CMD_PINGPOLL` (0B4h)

```
OUT (01h), CMD_PINGPOLL
IN  (00h) x 9           estado, resultado, IP (4), tiempo en ms (L, H), TTL
```

| Estado | Significado |
|---|---|
| 0 | No se pidió nada |
| 1 | En curso: lo demás no vale |
| 2 | Listo |

| Resultado (con estado 2) | Significado |
|---|---|
| 0 | Respuesta |
| 1 | Sin respuesta en 3 s |
| 2 | No se resolvió el nombre |
| 3 | La Pico no está conectada al Wi-Fi |

La IP es la del que contestó, con el primer octeto primero. El tiempo se
redondea al milisegundo: en una red local suele salir 0, y PING lo muestra como
`<1 ms`.

## GET.COM

`GET http://host[:puerto]/ruta [archivo]` (MSX-DOS 1) baja un archivo por
HTTP/1.0 y lo graba en el disco. El fuente es `comandos/get.asm`; se ensambla
con PASMO (`pasmo get.asm get.com`).

```
B>GET http://192.168.0.10:8000/JUEGO.ROM
Conectando a 192.168.0.10...
32768 bytes de 32768
Grabado en JUEGO.ROM, en 2,1 s
```

- Sin archivo, el nombre sale del final de la ruta (hasta el `?`), pasado a
  8.3 y a mayúsculas; `INDEX.HTM` si la ruta termina en `/`. Con solo una
  unidad (`GET url B:`), graba en esa unidad con ese nombre.
- Si el archivo ya existe, pregunta antes de reemplazarlo.
- Si el servidor manda `Content-Length` y el archivo no entra en el disco
  (contando el lugar que libera el que se reemplaza), avisa y no toca nada.
- Una respuesta que no es 200 se muestra tal cual, con el destino si es una
  redirección (no la sigue).
- `ESC` corta la descarga. Si se corta, si el disco se llena o si llegan
  menos bytes que los anunciados, se borra el archivo a medias.
- Sin HTTPS: la Pico no tiene TLS.

Para pasar archivos desde la PC alcanza con un servidor web en la carpeta,
por ejemplo `python -m http.server 8000`.

Usa `CMD_NETCONN` y `CMD_NETIO`, sin comandos nuevos: el pedido sale de a
60 bytes y la respuesta llega de a 255. El cuerpo se junta en un búfer de
8 KB y se graba con la escritura al azar en bloque del DOS (función 26h).

> **Estado:** probado en la DPC-200 bajando los programas de `out/msx/`.
> Para probar cambios sin hardware, `tools/webmsx_pico_red.js` simula la Pico
> en WebMSX y lee el disco emulado para comparar lo grabado.
