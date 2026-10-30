# CUEVA: un juego que corre en la Pico

Ejemplo de la Pico como coprocesador de juegos (`COPRO.md`): un juego
completo en el que la Pico hace todo (el mapa, el movimiento, las
colisiones, los puntos y la música) y el MSX solo copia lo que recibe.
`CUEVA.COM` no sabe nada del juego: en cada cuadro manda la entrada y copia
a la VRAM y al PSG lo que vuelve. El juego está en `firmware/src/cueva.c`; el
programa, en `comandos/cueva.asm`, con el motor que comparte con
`PLANETAS.COM` en `comandos/juego.inc`.

> **Estado.** Firmware 0.17. La lógica del juego y `CUEVA.COM` se probaron
> fuera de la placa; falta probar en la Pico y en un MSX.

## El juego

Una nave avanza por una cueva que se desplaza sola hacia la izquierda, de a
un píxel por cuadro, con pendientes, estalactitas y estalagmitas. Por la derecha llegan enemigos que ondulan; cada uno
derribado suma 10 puntos, y la distancia suma uno cada 32 píxeles. Chocar
con un enemigo o con la pared cuesta una vida; con las tres perdidas termina
la partida. La cueva se angosta a medida que se avanza y los enemigos llegan
más seguido.

```
CUEVA
```

| Tecla | Acción |
|---|---|
| W, A, S y Z, o joystick 1 | Mover la nave: arriba, izquierda, derecha y abajo |
| Punto (`.`) o disparo | Disparar; empezar una partida |
| ESC | Salir a MSX-DOS |

Al salir, `CUEVA.COM` muestra cuántos cuadros atendió y cuántos se perdieron
(cuadros en que el Z80 no terminó de copiar antes de la interrupción
siguiente).

## Cómo se reparte el trabajo

En cada interrupción, el Z80:

1. Lee el joystick 1 (registro 14 del PSG) y las filas 5 (W, S y Z) y 2
   (A y el punto) del teclado.
2. Los manda con `CMD_CUEVACUAD` y espera `OCUPADO` en 0.
3. Copia los 11 registros del PSG y después cada tramo de VRAM, hasta el
   final.

La Pico, entre un cuadro y el siguiente, avanza el juego y arma la
respuesta: la tabla de atributos de sprites (128 bytes), los patrones del
borde de la cueva (hasta 512) y la tabla de nombres entera (768). Son hasta
1.416 bytes, que el Z80 copia a 29 ciclos por byte: unos 41.000 ciclos, un
58 % de un cuadro de 50 Hz y un 69 % de uno de 60 Hz.

La tabla de nombres se copia una fila de caracteres en la mitad del tiempo
en que el VDP la dibuja. A 50 Hz empieza antes que el haz y va siempre
adelante: la pantalla no se corta. A 60 Hz el borde de abajo es más corto y
el haz puede ganarle las primeras filas: ahí se vería, por un cuadro, la
cueva un píxel atrás.

### La cueva

La pantalla es SCREEN 1: la fuente de la BIOS sirve para el texto. El mapa
tiene la altura del techo y del piso en cada columna de píxeles. Se arma con
tramos rectos de 16 a 48 píxeles, cada uno con una pendiente de un juego fijo
(0, ¼, ½ o 1 píxel por columna, para arriba o para abajo), y a veces una
punta en forma de triángulo, de pendiente 1 o 2. Con pocas pendientes, los
bordes se repiten y caben en los patrones de un cuadro.

Cada celda de 8x8 de la pantalla es una de tres:

| Celda | Carácter |
|---|---|
| Pared llena | 00h, fijo |
| Vacía | El espacio, o una estrella (08h-0Fh, un punto en la columna `s`) |
| Borde | Un patrón dibujado píxel a píxel en ese cuadro |

Los patrones de borde se mandan sin repetir: hacen falta unos 35 por cuadro,
y como mucho 62 (medido en 10.000 cuadros). Van en uno de dos bancos de 64
caracteres, 80h-BFh y C0h-FFh, que se alternan: mientras la pantalla muestra
la tabla de nombres que usa un banco, se escribe el otro, y después la tabla
nueva. Si algún cuadro necesitara más de 64, las celdas que no entran quedan
llenas o vacías según la mitad de sus píxeles. Las estrellas del fondo
avanzan a la mitad de velocidad.

### Sprites

Cuatro dibujos de 16x16: la nave, el enemigo, la bala y la explosión. La
nave va siempre en el plano 0; el orden de los demás rota en cada cuadro, y
así los que pasan de 4 en una línea parpadean en lugar de desaparecer. Los
enemigos que salen por la izquierda usan el bit EC.

### Sonido

El canal A toca la melodía y el B el bajo, un paso cada 7 cuadros, con los
volúmenes bajados por la Pico cuadro a cuadro (sin la envolvente del PSG).
El C es para los efectos: tono descendente para el disparo y ruido para las
explosiones. El registro 7 siempre deja el puerto A del PSG como entrada:
es el de los joysticks.

## Protocolo

| Código | Nombre | Parámetros | Respuesta |
|---|---|---|---|
| 07h | CUEVAINI | — | Arranca en la presentación; patrones, colores, sprites y pantalla en blanco |
| 08h | CUEVACUAD | registro 14 del PSG, filas 5 y 2 del teclado | Avanza un cuadro |

Las dos respuestas tienen el mismo formato, que sirve para cualquier juego:

| Bytes | Contenido |
|---|---|
| 11 | Registros 0 a 10 del PSG |
| dir H, dir L, largo L, largo H, datos | Un tramo: los datos a la VRAM desde dir |
| FEh, registro, valor | Un registro del VDP |
| FFh | Fin |

`CUEVA.COM` verifica el registro 7 antes de tocar el PSG: un firmware que no
conoce el comando contesta FFh, y sin esa verificación el puerto A del PSG
quedaría como salida.
