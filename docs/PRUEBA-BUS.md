# Prueba del bus

Primer firmware del SDF-Pico. No usa la SD ni la DiskROM: comprueba que la
Pico contesta los puertos 00h y 01h sin `/WAIT` y que el DMA alimenta las
lecturas.

## Preparación

1. Compilar el firmware (`firmware\build.ps1`) y copiar `sdf_pico.uf2` a la
   Pico en modo BOOTSEL.
2. Conectar el USB de la Pico a la computadora y abrir la consola serie
   (cualquier terminal; la velocidad no importa en USB). Cada acceso del MSX a
   los puertos aparece ahí.
3. Insertar el cartucho y encender el MSX. La EEPROM puede quedar montada: la
   prueba no depende de ella.

## Qué hace el firmware

| Puerto | Acceso | Respuesta |
|---|---|---|
| 01h | `INP(1)` | Registro de estado |
| 01h | `OUT 1,n` | Comando |
| 00h | `OUT 0,n` | Dato: en modo eco, queda para la lectura siguiente |
| 00h | `INP(0)` | El siguiente byte del FIFO, o FFh si no hay ninguno |

Registro de estado:

| Bit | Significado |
|---|---|
| 6 | Siempre 1: el firmware está corriendo |
| 3 | DMA en curso |
| 0-2 | Bytes esperando en el FIFO de lectura (0 a 4) |

Comandos:

| Código | Nombre | Efecto |
|---|---|---|
| 01h | ECO | Cada byte escrito en 00h se devuelve en la lectura siguiente |
| 02h | CONTADOR | El DMA entrega 00h, 01h, ..., FFh en lecturas sucesivas de 00h |
| 03h | NADA | Cancela el eco y el DMA |

## Secuencia

```basic
10 PRINT HEX$(INP(1))
20 OUT 1,1: OUT 0,65: PRINT HEX$(INP(1)), INP(0)
30 OUT 1,2: FOR I=0 TO 9: PRINT INP(0);: NEXT: PRINT
40 PRINT HEX$(INP(1))
```

Resultado esperado:

| Línea | Muestra | Qué comprueba |
|---|---|---|
| 10 | `40` | Lectura del estado. `FF` indica que la Pico no contesta |
| 20 | `41   65` | Escritura de comando y de dato, y lectura del FIFO |
| 30 | `0 1 2 3 4 5 6 7 8 9` | El DMA repone el FIFO entre lectura y lectura |
| 40 | `4C` | Quedan 4 bytes en el FIFO y el DMA sigue en curso |

La consola debe mostrar `OUT 01h = 01`, `OUT 00h = 41` y `OUT 01h = 02`.

## Qué medir con el analizador

- De la bajada de `/IOSEL` al dato estable en D0-D7 (lado B del `'245`): se
  espera alrededor de 70 ns.
- Que D0-D7 del lado de la Pico queden en alta impedancia después de la
  subida de `/RD`, y no antes.
- En una escritura, que el byte informado por la consola coincida con el dato
  del bus durante todo el ciclo.
