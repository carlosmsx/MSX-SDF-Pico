# Reloj

> **Estado:** firmware 0.6, probado en una Daewoo DPC-200. La DiskROM no
> cambia: ya traía todo lo del reloj desde el SDF-1.

El SDF-Pico le da al MSX un reloj sin chip de reloj. La hora se cuenta con el
timer del RP2350 y se pone desde Internet, por el Wi-Fi.

## Qué lo usa

La DiskROM ya tiene los tres caminos, heredados del SDF-1. Hasta el firmware
0.5 contestaban "no hay reloj".

| Desde el MSX | Comandos | Qué hace |
|---|---|---|
| El kernel de Disk BASIC y MSX-DOS | `CMD_RTCCHK`, `CMD_RTCGET`, `CMD_RTCSETDA`, `CMD_RTCSETTI` | Fecha y hora de los archivos, `DATE` y `TIME` de DOS |
| `OPEN "RTC:"` | `CMD_DEV*` | Leer la hora en BASIC y ponerla con `PRINT #` |
| `CALL RTC` y `CALL RTC("...")` | `CMD_DEV*` | Mostrar la hora y ponerla desde el prompt |

El kernel llama a `CHKCLK` al arrancar y el firmware contesta siempre que hay
reloj. Si todavía no se puso, la hora arranca en 1980-01-01 00:00 y se
corrige sola en cuanto llega la de Internet.

## De dónde sale la hora

- **Internet:** SNTP contra `pool.ntp.org`, desde la primera conexión del
  Wi-Fi y después cada hora. Llega unos segundos después de encender, lo que
  tarda en conectar.
- **A mano:** `DATE` y `TIME` de DOS, `CALL RTC("2026-09-30 21:05:00")` o
  `PRINT #` sobre `RTC:`. Vale hasta la próxima sincronización.

Con el MSX apagado la Pico no tiene alimentación y la hora se pierde. Al
encender vuelve por SNTP.

La hora se guarda como la hora UTC que corresponde al arranque de la Pico.
Leerla o ponerla es una palabra de 32 bits, así que los dos núcleos la usan
sin bloqueos: el núcleo 1 la lee para el MSX y el 0 la pone desde SNTP.

## Zona horaria

SNTP da la hora UTC. La zona se guarda en la flash junto a la red Wi-Fi y de
fábrica es UTC-3 (Argentina, sin horario de verano). Se cambia desde:

- `CALL WIFI`, en "Zona horaria", con las flechas izquierda y derecha (de a
  una hora).
- La consola USB: `zona -3`, `zona +5:30`.

## `OPEN "RTC:"`

Los mismos formatos que en el SDF-1:

| Nombre | Devuelve |
|---|---|
| `RTC:` | `2026-09-30 21:05:07` |
| `RTC:D` | `2026-09-30` |
| `RTC:T` | `21:05:07` |
| `RTC:N` | `2026,9,30,21,5,7,4` |
| `RTC:W` | `MIE` |

El formato puede ir pegado al nombre (`RTCN:`). La lectura es un chorro
infinito de líneas: cada `LINE INPUT` trae la hora de ese momento. `EOF` da
-1 si la hora no es confiable (nunca se puso).

```basic
10 OPEN "RTC:N" FOR INPUT AS #1
20 INPUT #1,A,MS,D,H,MI,S,W
30 PRINT D;"/";MS;"/";A;H;":";MI
```

`PRINT #` pone el reloj con cualquier separador que no sea un dígito:
`"2026-09-30 21:05:07"` o `"2026,9,30,21,5,7"`.

## Consola USB

| Comando | Acción |
|---|---|
| `hora` | Fecha, hora, zona y de dónde salió (Internet, a mano o sin poner) |
| `zona <h>` | Cambia la zona horaria y la guarda |

## Prueba

1. Con el Wi-Fi configurado, encender el MSX. En la consola tiene que
   aparecer `reloj: hora de internet, ...`.
2. `CALL RTC` en BASIC: la hora local correcta.
3. `SAVE "PRUEBA.BAS"` y después `DIR` en MSX-DOS: el archivo con la fecha
   de hoy.
4. `CALL WIFI`, cambiar la zona y salir. `CALL RTC` muestra la hora
   corrida.
5. `CALL RTC("2026-01-01 00:00:00")` y `CALL RTC`: la hora puesta a mano.
