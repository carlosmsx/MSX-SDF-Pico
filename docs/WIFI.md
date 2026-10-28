# Wi-Fi

> **Estado:** probado en una Daewoo DPC-200: la conexión desde la consola USB
> (firmware 0.4) y el menú `CALL WIFI` (firmware 0.5, con la DiskROM que lo
> incluye).

La Pico 2 W trae un CYW43439. El firmware lo usa en modo estación: se conecta
a una red existente y obtiene la dirección por DHCP.

## Reparto del trabajo

| | Núcleo 0 | Núcleo 1 |
|---|---|---|
| Antes | Consola USB | Bus y protocolo |
| Ahora | Consola USB, driver del CYW43 y lwIP | Bus y protocolo, sin cambios |

El driver y lwIP corren en segundo plano, en las interrupciones del núcleo 0
(`pico_cyw43_arch_lwip_threadsafe_background`). El núcleo 1 no tiene
interrupciones y no se entera de la red.

Recursos que toma el Wi-Fi, fuera de los del bus:

- GPIO 23, 24, 25 y 29 (internos del módulo).
- Una SM de PIO con su programa. PIO0 está lleno con el bus, así que el
  driver toma la primera libre de PIO1 o PIO2.
- Dos canales de DMA, pedidos con `dma_claim_unused_channel` después de los
  del bus.

## Memoria

El programa corre entero desde la RAM (`copy_to_ram`). El firmware del CYW43
ocupa unos 226 KB y no entraría junto al búfer de la SD, así que queda en la
flash: `sdf_config.h` lo pone en una sección `.flashdata`, que el linker no
copia. Se lee una sola vez, al arrancar el Wi-Fi.

Con el Wi-Fi, la RAM usada pasa de ~222 KB a ~355 KB de 512 KB.

## Credenciales

La red y la clave se guardan en el **anteúltimo sector de la flash** (4 KB,
en `PICO_FLASH_SIZE_BYTES - 8192`), con una suma de verificación. Grabar un
`.uf2` no toca ese sector: las credenciales sobreviven a las actualizaciones
desde el firmware 0.9.

Hasta el 0.8 estaban en el último sector, y cada grabación las borraba: todo
`.uf2` para RP2350 empieza con un bloque dirigido a 0x10FFFF00 (la solución
del pico-sdk a la errata RP2350-E10), el final de una flash de 16 MB. En los
4 MB de la Pico 2 W las direcciones se repiten cada 4 MB, así que ese bloque
cae en el último sector y el bootrom lo borra para escribirlo.
La segunda página del sector guarda la zona horaria (`RELOJ.md`); las dos
están en `firmware/src/ajustes.c`.

- Se graban solo cuando la conexión funciona.
- La clave no se muestra nunca: la consola informa la red, no la clave. Al
  escribirla, el eco es `*`.
- No están en la SD ni en la EEPROM.

El núcleo 0 graba el sector sin detener al núcleo 1. Es seguro porque el
núcleo 1 corre desde la RAM y nunca lee la flash; lo único que queda en la
flash es el firmware del CYW43, que ya se leyó.

## Comportamiento

- Al encender, si hay una red guardada, se conecta a ella.
- Si la conexión falla o se pierde, reintenta con la red guardada cada 30 s.
- Un intento que no termina en 30 s cuenta como fallo.
- Si falla una red nueva, no se guarda y se vuelve a la anterior.

## `CALL WIFI`

Menú en la pantalla del MSX, desde BASIC:

```
SDF-Pico: red Wi-Fi
Conectado a MiCasa
IP 192.168.1.47
Hora 2026-09-30 21:05:07

> MiCasa                         -52*
  Vecino                         -71
  Invitados                      -80

  Buscar redes
  Olvidar la red guardada
  Zona horaria: UTC-3
  Salir

Cursor y RETURN elige, ESC sale
```

- Al entrar busca redes. La lista va ordenada por señal y el `*` marca la
  red guardada.
- Al elegir una red pide la clave, que se ve como asteriscos. Una red
  abierta conecta directamente.
- Mientras conecta muestra el avance. `ESC` vuelve al menú y la conexión
  sigue.
- Si conecta, la red queda guardada. Si no, muestra el motivo (clave
  incorrecta, red no encontrada) y sigue la anterior.
- En "Zona horaria", las flechas izquierda y derecha la cambian de a una
  hora. Se guarda al salir del menú.
- `ESC` en el menú, o `CTRL+STOP` en cualquier momento, sale y limpia la
  pantalla.

El menú lo arma el firmware y la ROM solo lo muestra (ver `CMD_WIFIMENU` en
`PROTOCOLO.md`). Cambiar el menú es cambiar el firmware, sin regrabar la
EEPROM.

## `WIFI.COM`

Lo mismo que `CALL WIFI`, desde MSX-DOS 1 y en modo texto. Busca las redes,
las lista numeradas y pregunta a cuál conectarse:

```
B>WIFI
Sin conexion a Casa
Buscando redes...
 1  -48 dBm  Casa (guardada)
 2  -71 dBm  Vecino
 3  -80 dBm  Bar (abierta)
Red (1-3, RETURN sale): 2
Clave (ESC vuelve): *********
Conectando a Vecino...
Conectado. IP 192.168.0.37
La red quedo guardada.
```

- `WIFI /S` solo muestra el estado: la red, la IP y la señal.
- `RETURN` sin número sale sin cambiar nada.
- Una red abierta conecta sin pedir clave. En la clave, `BS` borra y `ESC`
  vuelve a la pregunta de la red.
- Si conecta, la red queda guardada, igual que con `CALL WIFI`. Si no,
  muestra el motivo y la Pico sigue con la red anterior.
- Las letras que no son ASCII del nombre de una red (UTF-8) se ven como `?`.

El fuente es `comandos/wifi.asm` y se ensambla con PASMO
(`pasmo wifi.asm wifi.com`). El programa arma la pantalla y la Pico hace el
resto con cuatro comandos (`firmware/src/wifi_dos.c`). Como `CMD_WIFIMENU`,
los atiende el núcleo 0: el núcleo 1 deja `OCUPADO` hasta la respuesta.

> **Estado:** probado en la DPC-200 con una red WPA2 (estado, lista, clave
> incorrecta y conexión). Falta probar una red abierta. Para probar cambios
> sin hardware, `tools/webmsx_pico_wifi.js` simula la Pico en WebMSX.

### `CMD_WIFISTAT` (0B5h)

```
OUT (01h), CMD_WIFISTAT
IN  (00h)               estado: 0 apagado, 1 sin red, 2 conectando, 3 conectado, 4 en espera
IN  (00h)               ultima conexion pedida: 0 en curso, 1 conecto, 2 fallo
IN  (00h)               senal en dBm, con signo (0 si no esta conectado)
IN  (00h) ... 0         red: la que se usa o, si no hay, la guardada
IN  (00h) ... 0         IP, en texto (vacia si no esta conectado)
IN  (00h) ... 0         motivo del fallo (vacio si no fallo)
```

### `CMD_WIFISCAN` (0B6h)

```
OUT (01h), CMD_WIFISCAN
IN  (00h)               0 = buscando, 1 = el Wi-Fi no responde, 2 = no se pudo empezar
```

### `CMD_WIFIREDES` (0B7h)

```
OUT (01h), CMD_WIFIREDES
IN  (00h)               1 = todavia buscando (n = 0), 0 = lista
IN  (00h)               n, cantidad de redes (hasta 16), ordenadas por senal
por cada red:
IN  (00h)               senal en dBm, con signo
IN  (00h)               marcas: bit 0 = abierta, bit 1 = es la guardada
IN  (00h) ... 0         nombre
```

### `CMD_WIFIJOIN` (0B8h)

```
OUT (01h), CMD_WIFIJOIN
OUT (00h), largo, nombre x largo        1 a 32
OUT (00h), largo, clave x largo         0 (red abierta) u 8 a 63
IN  (00h)               0 = conectando, 1 = el Wi-Fi no responde, 2 = nombre o clave no validos
```

El resultado se consulta con `CMD_WIFISTAT` hasta que el segundo byte deja de
ser 0. La Pico da la conexión por fallida a los 30 s. Firmware y programa
borran la clave de la memoria después de usarla.

## Consola USB

| Comando | Acción |
|---|---|
| `wifi` | Red guardada, estado, IP y señal |
| `buscar` | Lista las redes a la vista, ordenadas por señal |
| `conectar <n>` | Conecta a la red `n` de la última búsqueda; pide la clave |
| `conectar <nombre>` | Lo mismo, por nombre (sirve para redes ocultas) |
| `olvidar` | Desconecta y borra la red guardada |
| `borrar todo` | Borra también la URL de la cámara y la última foto, y vuelve la zona a UTC-3 (`SDF BORRAR` en `SD.md`) |
| `actualizar` | Pregunta a GitHub cuál es el último firmware (`ACTUALIZAR.md`) |
| `actualizar si` | Lo baja, lo verifica, lo graba y reinicia (`SDF UPDATE` en MSX-DOS) |
| `ayuda` | Lista los comandos |

Una clave vacía conecta a una red abierta. Las demás se conectan con
WPA2-AES.

## Prueba

1. Grabar `sdf_pico.uf2` y abrir la consola USB.
2. `buscar`, `conectar <n>` y la clave. Tiene que llegar a
   `wifi: conectado a "...", IP ...` y `wifi: red guardada`.
3. Desconectar la USB y volver a conectarla: al arrancar, se conecta sola.
4. Con el MSX encendido y el Wi-Fi conectado, repetir la prueba de lectura y
   escritura (`SDFDEBUG`, CHK) para confirmar que el bus no se ve afectado.
5. Medir el +5 V del slot mientras conecta: el CYW43 tiene picos de ~300 mA
   al transmitir.
