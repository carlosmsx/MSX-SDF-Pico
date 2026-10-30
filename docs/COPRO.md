# La Pico como coprocesador de juegos

Prueba de una idea: que la Pico haga el trabajo de un juego (la lógica, las
colisiones, el orden de los sprites, la música) y que el Z80 solo copie al
VDP lo que le llega por el puerto 00h. `COPRO.COM` mide cuántos bytes por
cuadro se pueden copiar así y mueve 32 sprites calculados en la Pico. El
firmware está en `firmware/src/copro.c`; el programa, en `comandos/copro.asm`.

> **Estado.** Con el firmware 0.17, probado en la DPC-200 (50 Hz): los tres
> casos sin errores, a un 95 % del máximo teórico, y los sprites sin perder
> cuadros (ver [Resultados](#resultados)).

## Por qué el Z80 sigue copiando

El VDP está en la placa del MSX y el slot no tiene `/BUSRQ`: el cartucho no
puede escribir la VRAM. Cada byte que llega a la pantalla lo mueve el Z80,
con un `IN A,(00h)` y un `OUT (98h),A`. La Pico calcula cientos de veces más
rápido que el Z80; el límite es el VDP y el tiempo de copia.

| Copia | Ciclos por byte | Cuándo vale |
|---|---|---|
| `IN` y `OUT` desenrollados | 24 (con el wait de M1) | Pantalla apagada o borde de abajo |
| `IN`, `OUT` y `NOP` | 29 | Siempre: lo mínimo que pide el TMS9918 en SCREEN 2 |
| `INIR` a la RAM | 23 | Sin VDP: solo el bus |

Un cuadro de 60 Hz son unos 59.700 ciclos, y el borde de abajo, unos 16.000;
uno de 50 Hz, unos 71.600, y el borde, unos 27.600. En papel, entonces, unos
2.000 bytes por cuadro a 60 Hz y 2.500 a 50 Hz, con el Z80 dedicado solo a
copiar. En un juego eso alcanza para la tabla de atributos de sprites
(128 bytes) y la tabla de nombres completa (768 bytes) en cada cuadro.

## COPRO.COM

```
COPRO
```

Sin parámetros. Primero mide tres casos, con cuatro pedidos de 16 KB cada uno
(64 KB), y verifica el último pedido de cada caso byte por byte:

1. **A la RAM**, con `INIR`: el bus solo.
2. **Al VDP con la pantalla apagada**, a 24 ciclos por byte.
3. **Al VDP con la pantalla encendida**, a 29 ciclos por byte.

Los bytes por cuadro salen de los 65.536 bytes dividido por los cuadros que
pasaron (`JIFFY`), con las interrupciones del MSX funcionando.

Después muestra los 32 sprites: en cada interrupción el Z80 lee el joystick 1
y la fila 8 del teclado, se los manda a la Pico y copia los 128 bytes de la
tabla de atributos que vuelven, todavía en el borde de abajo. Los cursores o
el joystick mueven la pelota amarilla; las otras rebotan y destellan en
blanco cuando la tocan. El TMS9918 muestra 4 sprites por línea: la Pico rota
el orden en cada cuadro, y los que sobran parpadean en lugar de desaparecer.
ESC termina:

```
A>COPRO
Bytes por cuadro, de la Pico:
A la RAM (INIR):  2978, errores: 0
VDP apagado, 24T: 2849, errores: 0
VDP, 29T:         2340, errores: 0
Sprites: 7151 cuadros, perdidos: 0
```

Los *perdidos* son los cuadros en que el Z80 no llegó a copiar la tabla
antes de la interrupción siguiente.

## Resultados

En la DPC-200, que funciona a 50 Hz (71.591 ciclos por cuadro):

| Caso | Bytes por cuadro | Máximo teórico | Por segundo | Errores |
|---|---|---|---|---|
| A la RAM (`INIR`, 23 ciclos) | 2.978 | 3.113 | 149 KB | 0 de 16.384 |
| VDP apagado (24 ciclos) | 2.849 | 2.983 | 142 KB | 0 de 16.384 |
| VDP encendido (29 ciclos) | 2.340 | 2.469 | 117 KB | 0 de 16.384 |
| Sprites (128 bytes por cuadro) | — | — | — | 0 de 7.151 cuadros perdidos |

- Se llega al 95 % del máximo: el 5 % restante son las interrupciones del
  MSX y el comando de cada pedido. La Pico entrega todo a tiempo sin
  `/WAIT`, también con `INIR`.
- El TMS9918 de la DPC-200 acepta 29 ciclos entre escrituras con la pantalla
  encendida en SCREEN 2.
- Con 2.340 bytes por cuadro entran la tabla de nombres completa y la de
  sprites (896 bytes) y sobran unos 1.400 para patrones. Solo en el borde de
  abajo (unos 27.600 ciclos a 50 Hz) entran unos 1.100 bytes a 24 ciclos.

Un juego completo hecho así está en `CUEVA.md`, y un ejemplo de cálculo pesado (el sistema solar con la ecuación de Kepler) en `PLANETAS.md`.

## Comandos

En el grupo de la prueba del bus (`PRUEBA-BUS.md`):

| Código | Nombre | Parámetros | Respuesta |
|---|---|---|---|
| 04h | COPROBLQ | n (1-64), semilla | n bloques de 256 bytes: el byte k del bloque j es `k xor j xor semilla` |
| 05h | COPROINI | sprites (1-32) | Reinicia la escena; el patrón de la pelota de 16x16 (32 bytes) |
| 06h | COPROCUAD | registro 14 del PSG, fila 8 del teclado | Avanza un cuadro; la tabla de atributos (128 bytes) |

La entrada va tal cual se lee, con las teclas activas en 0. La escena avanza
un paso por cada `COPROCUAD`: si el MSX pierde cuadros, el movimiento se
hace más lento, no salta.
