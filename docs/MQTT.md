# MQTT

El SDF-Pico puede conectarse a un broker MQTT: el MSX recibe los mensajes de
los tópicos a los que se suscribe y publica los suyos. Sirve para la
domótica (Home Assistant, Zigbee2MQTT, Tasmota: ver sensores y prender
luces desde el MSX) o para chatear entre varios MSX suscritos al mismo
tópico. El cliente es el de lwIP y corre en la Pico (`firmware/src/mqtt.c` y
`firmware/src/mqtt_red.c`); el programa de MSX-DOS está en `comandos/mqtt.asm`.

> **Estado: sin probar con hardware.** El firmware 0.16 compila sin avisos y
> `MQTT.COM` se probó en WebMSX con una Pico simulada
> (`tools/webmsx_pico_mqtt.js`, con un broker local), en MSX1 y MSX2. Falta
> probarlo en la Pico con un broker de verdad.

## MQTT.COM

```
MQTT [broker] [tópico]
MQTT /P tópico texto
MQTT /U [broker | -]
```

| Uso | Qué hace |
|---|---|
| `MQTT` | Abre la sesión con el broker guardado, sin suscribirse a nada |
| `MQTT tópico` | Lo mismo, suscrito a ese tópico: lo que se escribe se publica ahí |
| `MQTT broker tópico` | Con ese broker (no lo guarda) |
| `MQTT mqtt://broker` | Con ese broker y sin tópico. Con una sola palabra, es el tópico salvo que lleve `://` |
| `MQTT /P tópico texto` | Conecta con el broker guardado, publica el texto y sale: sirve en un `.BAT` |
| `MQTT /U broker` | Guarda el broker en la flash de la Pico |
| `MQTT /U` | Muestra el broker guardado, con la clave cambiada por `****` |
| `MQTT /U -` | Olvida el broker guardado |

El broker es `mqtt://usuario:clave@servidor:puerto/cliente`. Se pueden omitir
el `mqtt://`, el usuario y la clave, el puerto (1883) y el identificador del
cliente (sin él es `msx-` y el final del número de la Pico). No hay TLS
todavía: `mqtts://` (8883) no se puede. Ejemplo, con un broker público de
prueba:

```
B>MQTT /U test.mosquitto.org
Broker guardado.

B>MQTT msx/chat
```

### La pantalla

`MQTT.COM` pasa a 40 columnas. Arriba van los mensajes, que suben a medida
que llegan; debajo, una línea con el estado de la conexión y el tópico
elegido; abajo, dos líneas para escribir. Al salir vuelve el modo de texto
de antes.

```
MQTT.COM - /? comandos, ESC sale
* suscripto a msx/chat
* conectado
hola desde el MSX
casa/temp: 21.5 ºC
-> casa/luz/cocina: ON
── conectado ── msx/chat ───────────────
> _
```

Los mensajes del tópico elegido se muestran solos; los de otros tópicos,
con el tópico adelante. Los avisos empiezan con `*`. Un mensaje se muestra
hasta 255 caracteres (y un tópico hasta 127): si es más largo, se corta y
termina en `...`.

Lo que se escribe (hasta 200 caracteres) se publica con Enter en el tópico
elegido. Si se está suscrito a ese tópico, el broker lo devuelve y aparece
en la lista: es la confirmación. Las líneas que empiezan con `/` son
comandos:

| Comando | Qué hace |
|---|---|
| `/s tópico` | Se suscribe. Valen los comodines `+` (un nivel) y `#` (el resto) |
| `/u tópico` | Cancela la suscripción |
| `/t tópico` | Elige el tópico donde se publica lo que se escribe (`/t` solo: ninguno). No se suscribe |
| `/p tópico texto` | Publica en otro tópico, y lo muestra con `->` |
| `/r tópico texto` | Lo mismo, retenido: el broker se lo da a los que se suscriban después |
| `/q` o ESC | Sale |
| `/?` | Lista los comandos |

Se publica con QoS 0 y las suscripciones son con QoS 0. Hasta 8
suscripciones a la vez.

### Acentos

La Pico convierte los textos: lo que llega en UTF-8 se muestra con el juego
de caracteres del MSX (á, é, ñ, ¿, ¡, ü... los de la página 437 de IBM, de
80h a AFh) y lo que se escribe en el MSX sale en UTF-8. Las mayúsculas con
acento que el MSX no tiene salen sin acento, `°` sale como `º`, las comillas
tipográficas como `"` y `'`, y lo demás como `?`. Los saltos de línea y los
tabuladores de un mensaje se muestran como espacios.

### Si se corta

Si se cae el Wi-Fi o el broker, la Pico reintenta cada 5 segundos y, al
reconectar, vuelve a suscribirse a todo: `MQTT.COM` muestra el corte con
`(reintenta)` y después `* conectado`. No reintenta si el broker rechaza la
conexión (usuario o clave, identificador, no autorizado).

La Pico guarda hasta 2 KB de mensajes que el MSX todavía no leyó (5 de los
más largos, o decenas de cortos). Si se llena, descarta los nuevos y `MQTT.COM`
avisa `* se perdieron mensajes`.

Al salir de `MQTT.COM` (y en cada reset del MSX), la Pico se desconecta del
broker.

El broker guardado sobrevive al apagado y a las actualizaciones del
firmware, junto a la red Wi-Fi. Como la clave del Wi-Fi, la clave del broker
se guarda **sin cifrar** en la flash de la Pico, y viaja sin cifrar: conviene
un usuario solo para el MSX. `SDF BORRAR` (o `borrar todo` en la consola) lo
borra junto con lo demás.

## Consola USB

| Comando | Qué hace |
|---|---|
| `mqtt` | El broker guardado, el estado, las suscripciones y los contadores |
| `mqtt url URL` | Guarda el broker, como `MQTT /U` (`mqtt url -` lo olvida) |
| `mqtt conectar [URL]` | Conecta, con el broker guardado o con esa URL |
| `mqtt sub tópico`, `mqtt unsub tópico` | Se suscribe o cancela |
| `mqtt pub tópico texto` | Publica |
| `mqtt traza` | Muestra en la consola los mensajes que llegan (activa o apaga) |
| `mqtt cerrar` | Se desconecta |

Los cambios de estado (conectado, cortes con su motivo) salen siempre en la
consola. Lo que se recibe va a la misma cola que lee el MSX: probando desde
la consola sin `MQTT.COM`, la cola se llena y se cuentan perdidos.

## Comandos del protocolo

Son nuevos (0A8h a 0AEh) y se agregan sin cambiar los del protocolo 8. Como
en `PING.COM`, los pedidos se aceptan enseguida y el resultado se consulta
con `CMD_MQTTPOLL`. Con un firmware anterior, `IN (00h)` da FFh.

### `CMD_MQTTCONN` (0A8h) y `CMD_MQTTCLOSE` (0A9h)

```
OUT (01h), CMD_MQTTCONN
OUT (00h), largo, URL x largo       largo 0: el broker guardado
IN  (00h)               0 = pedido aceptado, 2 = la Pico está ocupada, 3 = URL inválida

OUT (01h), CMD_MQTTCLOSE
IN  (00h)               0
```

Conectar corta la conexión anterior y olvida sus suscripciones y sus
mensajes sin leer. `CMD_MQTTCLOSE` se desconecta. La DiskROM, al arrancar,
también desconecta (`CMD_INIHRD`).

### `CMD_MQTTSUB` (0AAh)

```
OUT (01h), CMD_MQTTSUB
OUT (00h), modo, largo, tópico x largo
IN  (00h)               0 = pedido aceptado, 2 = ocupada, 3 = tópico inválido
```

Modo: bit 0 = QoS 1, bit 7 = cancelar la suscripción. El tópico tiene hasta
127 caracteres. Se puede pedir antes de estar conectado: la suscripción se
hace al conectar (y al reconectar).

### `CMD_MQTTPUB` (0ABh)

```
OUT (01h), CMD_MQTTPUB
OUT (00h), flags, largo, tópico x largo, largo, texto x largo
IN  (00h)               0 = pedido aceptado, 1 = sin conexión, 2 = ocupada,
                        3 = tópico inválido (vacío, largo, o con + o #)
```

Flags: bit 0 = QoS 1, bit 1 = retenido, bit 2 = el texto está en el juego
del MSX (la Pico lo pasa a UTF-8). El texto tiene hasta 255 bytes.

### `CMD_MQTTPOLL` (0ACh)

```
OUT (01h), CMD_MQTTPOLL
OUT (00h), modo         bit 2: el mensaje en el juego del MSX
IN  (00h) x 2           estado, motivo
si el estado tiene el bit 3:
IN  (00h)               flags: bit 0 = el tópico o el texto se cortaron
IN  (00h), tópico       largo y el tópico (hasta 127)
IN  (00h), texto        largo y el texto (hasta 255; puede ser 0)
```

Cada consulta entrega a lo sumo un mensaje, el más viejo. Bits del estado:

| Bit | Significado |
|---|---|
| 0 | Conectado |
| 1 | Conectando: resolviendo el nombre, esperando al broker o por reintentar |
| 2 | Se cortó o no pudo conectar: el motivo está en el segundo byte |
| 3 | Sigue un mensaje |
| 4 | Se descartaron mensajes desde la consulta anterior |
| 5 | Hay publicaciones que el broker todavía no recibió |

| Motivo | Significado |
|---|---|
| 0 | — |
| 1 a 5 | Lo que contestó el broker: versión de MQTT, identificador rechazado, broker no disponible, usuario o clave, no autorizado |
| 16 | La Pico no está conectada al Wi-Fi |
| 17 | El nombre del broker no se resolvió |
| 18 | No conectó, o se cortó |
| 19 | El broker no contestó (15 s para conectar; sin respuesta a 1,5 veces el keepalive de 60 s, ya conectado) |
| 20 | No hay broker guardado |
| 21 | El broker rechazó una suscripción (la conexión sigue) |

Mientras la Pico no atendió un `CMD_MQTTCONN` o `CMD_MQTTCLOSE`, el estado es
"conectando" (o 0) y no hay mensajes: así no se ve el estado de la sesión
anterior.

### `CMD_MQTTURL` (0ADh) y `CMD_MQTTVER` (0AEh)

```
OUT (01h), CMD_MQTTURL
OUT (00h), largo, URL x largo       largo 0: lo olvida
IN  (00h)               0 = aceptada, 3 = URL inválida

OUT (01h), CMD_MQTTVER
IN  (00h) ... 0         el broker guardado, con la clave cambiada por ****
```

La URL tiene hasta 200 caracteres; la graba el núcleo 0 en la flash.

## Memoria

El cliente usa unos 10 KB de RAM: 2 KB para los mensajes recibidos, 1 KB
para los pedidos del MSX, 1,5 KB del cliente de lwIP (con 1 KB para lo que
sale: una publicación entera tiene que entrar) y 1 KB de las suscripciones.
El código del núcleo 0 (`mqtt_red.c`) corre desde la flash, como lwIP; el
del núcleo 1 (`mqtt.c`: los comandos y la conversión de los acentos), desde
la RAM.

Para hacerle lugar, el anillo DMA del bus (32 KB, `bus.c`) pasó a los
últimos 32 KB de la RAM (`firmware/ld/section_heap.incl`). Tiene que estar
alineado a su tamaño, y dentro de la `.bss` esa alineación arrastraba toda
la sección: el firmware 0.15 estaba a 212 bytes de perder 32 KB de golpe.
Con el anillo aparte quedan unos 52 KB libres para el HTTPS de la cámara y
de `SDF UPDATE` (piden unos 40 KB).

## Pruebas en WebMSX

`tools/webmsx_pico_mqtt.js` simula la Pico con un broker local: lo que se
publica les llega a los suscritos y `mqtt.llega(tópico, texto)` manda un
mensaje "de afuera". Con `tools/prepara_webmsx.py` y `tools/servir_web.py`
(ver `RED.md`), un disco con `MQTT.COM` en B: y, en la consola del
navegador, `tools/webmsx_arnes.js` y después `tools/webmsx_pico_mqtt.js`:

```
boot(); typ("b:\r", 100); typ("mqtt msx/chat\r", 800)
typ("hola\r", 300)
mqtt.llega("casa/temp", "21.5 °C"); run(100); scr()
mqtt.cortar(); run(600); scr()
```

El broker `noexiste` no resuelve y un broker con `clave@` contesta usuario o
clave.
