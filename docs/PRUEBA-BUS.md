# Prueba del bus

Primer firmware del SDF-Pico. No usa la SD ni la DiskROM: comprueba que la
Pico contesta los puertos 00h y 01h sin `/WAIT` y que el DMA alimenta las
lecturas.

## Preparación

1. **Verificar el cableado** con el checklist de `hardware/cableado.pdf`, con
   el cartucho fuera del MSX.
2. **Retirar la EEPROM (U1).** La DiskROM del SDF-1 espera `/WAIT` y habla el
   protocolo del ATmega: al arrancar consultaría puertos que este firmware no
   contesta como ella espera. Sin la EEPROM, el MSX arranca directo en BASIC.
3. **Grabar el firmware.** Con el cartucho fuera del MSX, mantener apretado el
   botón BOOTSEL de la Pico y conectar el USB. Aparece una unidad `RP2350`:
   copiar ahí `firmware/build/sdf_pico.uf2`. La Pico se reinicia sola.
4. **Abrir la consola.** La Pico aparece como un puerto COM nuevo (Administrador
   de dispositivos, "Puertos (COM y LPT)"). Abrirlo con PuTTY o el monitor
   serie de VS Code; la velocidad no importa. La consola muestra solo los
   accesos del MSX, así que al principio no aparece nada.
5. **Encender el MSX** con el cartucho insertado. El USB puede quedar
   conectado: el diodo separa las dos fuentes. El MSX tiene que arrancar
   normalmente en BASIC; si no arranca, apagar y revisar el cableado antes de
   seguir.

## Qué hace el firmware

| Puerto | Acceso | Respuesta |
|---|---|---|
| 01h | `INP(1)` | Registro de estado |
| 01h | `OUT 1,n` | Comando |
| 00h | `OUT 0,n` | Dato: en modo eco, queda para la lectura siguiente |
| 00h | `INP(0)` | El siguiente byte del FIFO, o FFh si no hay ninguno |

Registro de estado: `40h` con el firmware listo, y el bit 7 (`OCUPADO`) en 1
mientras prepara una respuesta (ver `PROTOCOLO.md`). Con la Pico sin atender
el bus se lee `FFh`.

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
| 20 | `40   65` | Escritura de comando y de dato, y lectura del FIFO |
| 30 | `0 1 2 3 4 5 6 7 8 9` | El DMA repone el FIFO entre lectura y lectura |
| 40 | `40` | El estado sigue listo |

Con el firmware normal la consola no muestra cada acceso; para verlos, usar
`sdf_pico_diag.uf2`. Las variantes de prueba no se compilan por defecto: hay
que configurar con `-DSDF_VARIANTES=ON` (por ejemplo,
`cmake -S . -B build -DSDF_VARIANTES=ON` y después `build.ps1`).

## Prueba de las ocho líneas de datos

```basic
50 OUT 1,1: FOR I=0 TO 7: OUT 0,2^I: PRINT INP(0);: NEXT: PRINT
```

Debe mostrar `1 2 4 8 16 32 64 128`: cada línea de datos, ida y vuelta.

## Si algo falla

| Síntoma | Causa probable |
|---|---|
| El MSX no arranca | Corto en el bus de datos, o la Pico maneja el bus fuera de tiempo. Retirar la Pico del circuito (desoldar o cortar `/IOSEL`) y probar |
| `INP(1)` da `FF` y la consola no muestra nada con `OUT 1,1` | La Pico no ve los accesos: revisar `/IOSEL` (pin 11) y `EN` (pin 15; U5 pin 6 tiene que estar en ~3,3 V) |
| La consola muestra los `OUT` pero `INP(1)` da `FF` | La Pico no llega a manejar el bus: revisar `/RD` (pin 14) y D0-D7 |
| Un bit siempre en 0 o en 1, o dos bits intercambiados | Línea de datos cortada, en corto o cruzada: la línea 50 dice cuál |
| La línea 30 da `0 1 2 3` y después `255` | El DMA no repone el FIFO |


## Qué medir con el analizador

- De la bajada de `/IOSEL` al dato estable en D0-D7 (lado B del `'245`): se
  espera alrededor de 70 ns.
- Que D0-D7 del lado de la Pico queden en alta impedancia después de la
  subida de `/RD`, y no antes.
- En una escritura, que el byte informado por la consola coincida con el dato
  del bus durante todo el ciclo.
