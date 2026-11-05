# Manual de BASIC

Los comandos que la DiskROM del SDF-Pico le agrega al MSX-BASIC. Son nueve
`CALL` y un dispositivo, `RTC:`. Todos funcionan en modo directo y dentro de
un programa, y como cualquier `CALL` se pueden abreviar con `_`:
`_SDFFILES` es lo mismo que `CALL SDFFILES`.

| Comando | Qué hace |
|---|---|
| [`CALL SDFTEST`](#call-sdftest) | Versión del firmware, protocolo y estado de la SD |
| [`CALL SDFFILES`](#call-sdffiles) | Lista las imágenes `.DSK` de la raíz de la SD |
| [`CALL SDFMOUNT(drive, "NOMBRE")`](#call-sdfmount) | Monta una imagen en A: o B: |
| [`CALL SDFUMOUNT(drive)`](#call-sdfumount) | Deja un drive sin imagen |
| [`CALL SDFNEW("NOMBRE"[, tamaño])`](#call-sdfnew) | Crea una imagen vacía y formateada |
| [`CALL SDFDRIVES(n)`](#call-sdfdrives) | Cantidad de drives, desde el próximo arranque |
| [`CALL RTC`](#call-rtc) | Muestra la fecha y la hora |
| [`CALL RTC("fecha hora")`](#call-rtc) | Pone el reloj |
| [`CALL WIFI`](#call-wifi) | Menú de la red Wi-Fi y de la zona horaria |
| [`CALL SDFDEBUG`](#call-sdfdebug) | Contadores y velocidad de transferencia |
| [`OPEN "RTC:"`](#el-dispositivo-rtc) | La fecha y la hora desde un programa |

En los comandos con argumentos, `drive` y `n` son expresiones numéricas de 0
a 255 y `"NOMBRE"` es cualquier expresión de texto: una constante o una
variable (`N$`, `"JUEGO"+STR$(I)`). Un número fuera de 0-255 da
"Illegal function call"; un número donde va texto, "Type mismatch"; un
paréntesis o una coma de menos, "Syntax error".

## La SD y las imágenes

### `CALL SDFTEST`

Diagnóstico: verifica que el firmware conteste y que hable el mismo protocolo
que la ROM. No tiene argumentos.

```
CALL SDFTEST
Firmware SDF-Pico 0.17
SD ok
Protocolo 08
Ok
```

La segunda línea es el estado de la tarjeta:

| Texto | Significado |
|---|---|
| `SD ok` | Tarjeta inicializada y FAT montada |
| `SD iniciando` | Todavía no hubo un intento |
| `SD no contesta` | Sin tarjeta, módulo desconectado o cableado |
| `SD no inicia` | La tarjeta contesta pero no termina de inicializar |
| `SD no es FAT` | La tarjeta funciona, pero está en exFAT o sin formatear |
| `SD error de E/S` | Falló una lectura o una escritura |

Otros mensajes posibles:

| Mensaje | Significado |
|---|---|
| `El firmware no responde (sin SD?)` | Nadie contesta en el puerto: la Pico no arrancó o no está conectada |
| `Firmware anterior a SDFTEST` | Un firmware muy viejo, que no conoce el comando |
| `Distinto al de la ROM!` | El protocolo del firmware no es el de la ROM: hay que actualizar uno de los dos |

### `CALL SDFFILES`

Lista las imágenes `.DSK` de la raíz de la SD, en columnas de 13 caracteres,
con los nombres cortos (8.3). No muestra directorios ni archivos ocultos o de
sistema. `CTRL+STOP` corta la lista.

```
CALL SDFFILES
SISTEMA.DSK  JUEGOS.DSK   DATOS.DSK
Ok
```

Sin tarjeta, la lista sale vacía.

### `CALL SDFMOUNT`

```
CALL SDFMOUNT(drive, "NOMBRE")
```

Monta una imagen de la raíz de la SD. `drive` es 0 para A: y 1 para B:. Sin
extensión se agrega `.DSK`. La imagen tiene que medir exactamente 720 KB o
360 KB; no se revisa su contenido, así que también montan los discos de juegos
con cargador propio.

```basic
CALL SDFMOUNT(1, "JUEGOS")
CALL SDFMOUNT(0, "SISTEMA.DSK")
10 N$="DATOS": CALL SDFMOUNT(1, N$)
```

El DOS se entera solo del cambio: en el siguiente acceso al drive relee la
FAT. La imagen montada queda anotada en `SDFPICO.CFG`, en la tarjeta, y vuelve
a montarse al encender.

| Error | Causa |
|---|---|
| Bad drive name | `drive` no es 0 ni 1 |
| Disk offline | No hay SD, o todavía no inicializó |
| Bad file name | Nombre vacío o demasiado largo |
| File not found | No existe, o es un directorio |
| Bad file mode | No mide 720 ni 360 KB |

### `CALL SDFUMOUNT`

```
CALL SDFUMOUNT(drive)
```

Deja el drive sin imagen, también después de apagar. Desde ese momento el DOS
contesta "Disk offline" en ese drive, hasta el próximo `CALL SDFMOUNT`.
Desmontar un drive que ya estaba vacío no es error.

| Error | Causa |
|---|---|
| Bad drive name | `drive` no es 0 ni 1 |
| Disk offline | No hay SD |

### `CALL SDFNEW`

```
CALL SDFNEW("NOMBRE"[, tamaño])
```

Crea en la raíz de la SD una imagen vacía y formateada, lista para usar.
`tamaño` es 720 (el valor si se omite) o 360, en KB. Si el nombre no tiene
extensión se agrega `.DSK`. La imagen **no** se monta: para usarla hay que
montarla después.

```basic
CALL SDFNEW("DATOS")
CALL SDFNEW("CHICO", 360)
CALL SDFMOUNT(1, "DATOS")
```

El nombre tiene que ser un nombre corto válido: hasta 8 caracteres, y hasta 3
de extensión, sin espacios ni `" * + , / : ; < = > ? [ \ ] |`. Se guarda en
mayúsculas.

Crear una imagen de 720 KB lleva unos segundos: se reserva contigua si hay
lugar y se llena de ceros antes de formatearla. Si se arranca desde una
imagen recién creada, el MSX sigue a BASIC: tiene sector de arranque pero no
tiene el sistema. Para eso hay que copiarle `MSXDOS.SYS` y `COMMAND.COM`.

| Error | Causa |
|---|---|
| Illegal function call | `tamaño` no es 720 ni 360 |
| Disk offline | No hay SD |
| Bad file name | El nombre no es un nombre corto válido |
| File already exists | Ya hay un archivo con ese nombre |
| Disk full | No queda lugar en la SD |
| Disk I/O error | La raíz está llena, o la SD no deja escribir |

### `CALL SDFDRIVES`

```
CALL SDFDRIVES(n)
```

Cuántos drives ve el DOS, 1 o 2. Rige **desde el próximo arranque**. Con uno
solo queda más memoria libre para BASIC y los programas; las imágenes
montadas no cambian. Queda anotado en `SDFPICO.CFG`.

| Error | Causa |
|---|---|
| Illegal function call | `n` no es 1 ni 2 |

## El reloj

### `CALL RTC`

```
CALL RTC
CALL RTC("fecha hora")
```

Sin argumento muestra la fecha y la hora locales:

```
CALL RTC
2026-11-05 21:05:07
Ok
```

Con un texto pone el reloj. Vale cualquier separador que no sea un dígito,
y el año, el mes, el día, la hora, los minutos y los segundos van en ese
orden:

```basic
CALL RTC("2026-11-05 21:05:00")
CALL RTC("2026,11,5,21,5,0")
```

La hora puesta a mano vale hasta la próxima sincronización por Internet, que
se hace al conectar el Wi-Fi y después cada hora. Con el MSX apagado la hora
se pierde y vuelve por Internet al encender; sin red arranca en
1980-01-01 00:00.

| Error | Causa |
|---|---|
| Type mismatch | El argumento no es texto |
| Illegal function call | La fecha o la hora no son válidas |

### El dispositivo `RTC:`

Para leer la hora desde un programa se abre `RTC:` como un archivo. Cada
`LINE INPUT` o `INPUT` trae la hora de ese momento: la lectura no se termina
nunca. El formato va después de los dos puntos o pegado al nombre (`RTCN:`):

| Nombre | Devuelve |
|---|---|
| `RTC:` | `2026-11-05 21:05:07` |
| `RTC:D` | `2026-11-05` |
| `RTC:T` | `21:05:07` |
| `RTC:N` | `2026,11,5,21,5,7,5` (el último es el día de la semana, 1 = domingo) |
| `RTC:W` | `JUE` |

```basic
10 OPEN "RTC:N" FOR INPUT AS #1
20 INPUT #1,A,MS,D,H,MI,S,W
30 PRINT D;"/";MS;"/";A;H;":";MI
40 CLOSE #1
```

`EOF(n)` da -1 si la hora no es confiable, porque nunca se puso.

Abierto para escribir, `PRINT #` pone el reloj con el mismo formato que
`CALL RTC("...")`:

```basic
10 OPEN "RTC:" FOR OUTPUT AS #1
20 PRINT #1,"2026-11-05 21:05:00"
30 CLOSE #1
```

`RTC:` no admite acceso aleatorio (`FOR RANDOM` da "Bad file mode") ni `LOC`,
`LOF` o `FPOS` ("Illegal function call").

## La red

### `CALL WIFI`

Menú en la pantalla del MSX para elegir la red Wi-Fi, poner la clave y ajustar
la zona horaria. No tiene argumentos.

```
SDF-Pico: red Wi-Fi
Conectado a MiCasa
IP 192.168.1.47
Hora 2026-11-05 21:05:07

> MiCasa                         -52*
  Vecino                         -71
  Invitados                      -80

  Buscar redes
  Olvidar la red guardada
  Zona horaria: UTC-3
  Salir

Cursor y RETURN elige, ESC sale
```

- Al entrar busca redes. La lista va ordenada por señal y el `*` marca la red
  guardada.
- Al elegir una red pide la clave, que se ve como asteriscos. Una red abierta
  conecta directamente.
- Si conecta, la red queda guardada en la flash de la Pico. Si no, muestra el
  motivo y sigue la anterior.
- En "Zona horaria", las flechas izquierda y derecha la cambian de a una hora.
  Se guarda al salir.
- `ESC` en el menú, o `CTRL+STOP` en cualquier momento, sale y limpia la
  pantalla.

Más detalles en [WIFI.md](WIFI.md).

## Pruebas

### `CALL SDFDEBUG`

Cuentas del bus desde el `CALL SDFDEBUG` anterior (o desde el arranque), y
las pone en cero. Sirve para medir la velocidad y detectar problemas de bus.

```
CALL SDFDEBUG
R 112/896 W 4/32 CHK 896/0
12.4 s 464 KB 37.4 KB/s
Ok
```

| Campo | Significado |
|---|---|
| `R a/b` | Lecturas: `a` comandos, `b` sectores |
| `W a/b` | Escrituras: `a` comandos, `b` sectores |
| `CHK a/b` | Checksums de sector: `a` coincidieron, `b` no |
| `s`, `KB`, `KB/s` | Tiempo, datos transferidos y velocidad promedio |

Un checksum que no coincide indica un problema en el bus; el firmware además
lo informa por la consola USB.

## Lo que trae el MSX-DOS

Además de estos, la DiskROM trae los comandos de disco del Disk BASIC del kit
de ASCII: `FILES`, `KILL`, `NAME`, `COPY`, `LOAD`, `SAVE`, `BLOAD`, `BSAVE`,
`DSKF`, `CALL FORMAT` y `CALL SYSTEM` (sin argumentos: vuelve a MSX-DOS si el
disco de A: tiene el sistema).

## Desde MSX-DOS

Los mismos comandos de la SD existen en MSX-DOS 1 como `SDF.COM`
(`SDF DIR`, `SDF MOUNT`, ...), y `CALL WIFI` como `WIFI.COM`. Ver
[SD.md](SD.md) y [WIFI.md](WIFI.md).
