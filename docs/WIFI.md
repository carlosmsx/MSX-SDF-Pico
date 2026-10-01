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

La red y la clave se guardan en el **último sector de la flash** (4 KB, en
`PICO_FLASH_SIZE_BYTES - 4096`), con una suma de verificación. Grabar un
`.uf2` no toca ese sector: las credenciales sobreviven a las actualizaciones.
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

## Consola USB

| Comando | Acción |
|---|---|
| `wifi` | Red guardada, estado, IP y señal |
| `buscar` | Lista las redes a la vista, ordenadas por señal |
| `conectar <n>` | Conecta a la red `n` de la última búsqueda; pide la clave |
| `conectar <nombre>` | Lo mismo, por nombre (sirve para redes ocultas) |
| `olvidar` | Desconecta y borra la red guardada |
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
