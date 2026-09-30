# Protocolo entre la DiskROM y el firmware

> **Estado:** implementado en los dos lados: la DiskROM (`diskrom/`) y el
> firmware (`firmware/src/protocolo.c`), con el disco en la flash de la Pico
> (`DISCO-FLASH.md`). Es el **protocolo 8**: el 7 del SDF-1 con el registro de
> estado. `CALL SDFTEST` avisa si la ROM y el firmware no coinciden.

## Punto de partida

El SDF-Pico conserva el protocolo del SDF-1 (versión 7, `diskrom/PROTOCOL.INC`
de ese repo): los mismos puertos, los mismos códigos de comando y el mismo
orden de parámetros y respuestas. Cambia una sola cosa: **cómo sabe el Z80 que
puede leer**.

En el SDF-1, `/WAIT` detiene al Z80 hasta que el firmware tiene la respuesta.
En el SDF-Pico no hay `/WAIT`, así que un `IN` que llega antes de tiempo lee
FFh (el FIFO vacío) y sigue. La DiskROM tiene que consultar el registro de
estado antes de leer.

## Puertos

| Puerto | Lectura | Escritura |
|---|---|---|
| 00h | Dato | Dato |
| 01h | **Registro de estado** | Comando |

En el SDF-1 la lectura de 01h devolvía siempre 0. Acá pasa a ser el mecanismo
de sincronización.

## Registro de estado

| Bit | Nombre | Significado |
|---|---|---|
| 7 | `OCUPADO` | El firmware está procesando: no leer ni escribir 00h |
| 6 | — | Siempre 1 |
| 5 | — | Siempre 0 |
| 1 | `CAMBIO` | La imagen montada cambió desde la última consulta (para `DSKCHG`) |
| 0 | `ERROR` | El último comando terminó con error |

Los bits 6 y 5 fijos permiten distinguir la respuesta del firmware del bus al
aire, que se lee como FFh.

## Primitivas de la DiskROM

| Rutina | Qué hace |
|---|---|
| `WriteByte` | `OUT (00h),A`, sin retardo: la PIO captura cada escritura en ~100 ns |
| `WriteCommand` | `OUT (01h),A` y ~60 µs de espera, para que el firmware levante `OCUPADO` antes de cualquier consulta |
| `WaitReady` | Consulta el estado hasta ver `OCUPADO` en 0, con un límite de ~2,6 s |
| `ReadByte` | `WaitReady` y después `IN A,(00h)`; si se agota el tiempo devuelve `FFh`, como el bus al aire |
| `ReadBlock` / `WriteBlock` | 256 bytes con `INI`/`OUTI`, sin espera por byte |

`ReadByte` espera antes de cada byte, no solo del primero. Como el firmware
baja `OCUPADO` recién con la respuesta completa encolada, la espera de los
bytes siguientes es inmediata, y así ningún comando necesita saber cuál es
el primer byte de su respuesta.

## Regla general

**Antes de cada bloque de lectura de 00h, la DiskROM espera a que `OCUPADO`
esté en 0.** Un bloque es cualquier grupo de bytes que el firmware entrega de
una vez: la respuesta de un comando o un sector de 512 bytes.

Del lado del firmware, `OCUPADO` en 0 es una garantía: el bloque completo está
en memoria y el DMA lo entrega al FIFO a medida que el Z80 lo lee. El DMA
repone un byte en nanosegundos y el Z80 tarda microsegundos entre lectura y
lectura, así que la DiskROM puede leer el bloque entero con `INIR`, sin volver
a consultar el estado.

Las escrituras del Z80 no necesitan espera: el núcleo vacía el FIFO de
escritura mucho más rápido de lo que el Z80 lo llena. Después de escribir un
sector, la DiskROM espera `OCUPADO` en 0 antes del comando siguiente, lo que
cubre el tiempo de escritura en la tarjeta.

## Lectura de sectores (`CMD_READ`)

```
OUT (01h), CMD_READ
OUT (00h) x 7                 parametros, igual que en el SDF-1
por cada sector:
    esperar OCUPADO = 0       el firmware lee el sector de la SD
    si ERROR: salir con error
    INIR x 512
```

El firmware lee el sector siguiente mientras el Z80 lee el actual (dos
búferes), así que a partir del segundo sector la espera suele ser nula.

## Escritura de sectores (`CMD_WRITE`)

```
OUT (01h), CMD_WRITE
OUT (00h) x 7
por cada sector:
    OTIR x 512
    esperar OCUPADO = 0       el firmware escribe el sector en la SD
    si ERROR: salir con error
```

## `CMD_DEBUG`, la excepción

La ROM manda `CMD_DEBUG` y el checksum XOR de cada sector en medio de un
`CMD_READ`. Es el único comando que no corta la transacción en curso: no
levanta `OCUPADO` ni descarta los sectores que quedan por leer. El firmware
compara el checksum con el del sector enviado y lleva la cuenta, que muestra
`CALL SDFDEBUG`.

## Condición de carrera al enviar un comando

`OCUPADO` tiene que estar en 1 antes de que el Z80 pueda consultar el estado
después de un comando. Entre un `OUT (01h),A` y el `IN A,(01h)` siguiente pasan
como mínimo unos 3 µs. El firmware atiende el bus desde un núcleo dedicado, en
un bucle cerrado, y pone `OCUPADO` en 1 en cuanto recibe el byte de comando,
antes que cualquier otra cosa. El núcleo 1 no tiene interrupciones y el
programa corre desde la RAM, así que la demora es del orden de un
microsegundo.

## Velocidad esperada

`INIR` y `OTIR` tardan 21 estados T por byte: con el Z80 a 3,58 MHz, unos
170 KB/s como techo teórico. La DiskROM del SDF-1 intercala retardos en cada
acceso y queda en unos 45 KB/s.

## Pendiente

- Definir cómo se descartan bytes sobrantes del FIFO de lectura si la DiskROM
  y el firmware pierden la sincronización (por ejemplo, un comando de
  reinicio que espere `OCUPADO` en 0).
- Revisar cada comando del SDF-1 que usa respuestas de espera en el puerto de
  datos (`SDFNEW_BUSY`, `DEV_BUSY`): con el registro de estado dejan de hacer
  falta.
- La DiskROM se compila contra el MSX-DOS kit de ASCII, igual que la del
  SDF-1: la imagen resultante no se publica.
