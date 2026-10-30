# PLANETAS: el sistema solar calculado en la Pico

Ejemplo de la Pico como coprocesador para cálculos pesados (`COPRO.md`): los
8 planetas giran en sus órbitas reales, acelerados, con la vista inclinada
en 3D. La Pico resuelve la mecánica de cada cuadro y arma la pantalla; el
MSX solo copia lo que recibe. El cálculo está en `firmware/src/planetas.c`;
el programa, en `comandos/planetas.asm`, con el mismo motor que `CUEVA.COM`
(`comandos/juego.inc`).

> **Estado.** Firmware 0.17. El cálculo y la pantalla se probaron fuera de la
> placa; falta probar en la Pico y en un MSX.

```
PLANETAS
```

| Tecla | Acción |
|---|---|
| Cursores o joystick 1 | Arriba y abajo: inclinar el plano, de verlo desde arriba a de canto (90°) y hasta desde abajo; izquierda y derecha: girarlo |
| A y Z | Acercar y alejar |
| R y L | Más rápido y más lento; por debajo de 1 hora por cuadro, el tiempo va para atrás |
| O | Mostrar u ocultar las órbitas |
| H | Volver a hoy |
| Barra o disparo | Pausa |
| ESC | Salir a MSX-DOS |

Arranca en la fecha del reloj de la Pico (la que pone SNTP con el Wi-Fi), a
un día por cuadro: los planetas empiezan donde están ahora. Si el reloj no
está puesto, arranca en el 1/1/2000. La fecha va abajo, con la velocidad:
de 1 hora a 100 días por cuadro, para adelante o para atrás, hasta mil años
desde el 2000 para cada lado.

## Por qué en la Pico

Para ubicar un planeta hay que resolver la ecuación de Kepler,
M = E − e·sen E, que no tiene solución directa: se busca E por aproximaciones
(cinco pasos de Newton). Después se pasa del plano de la órbita al de la
eclíptica (tres rotaciones), se gira e inclina la vista y se proyecta con
perspectiva. Son unos quince senos y cosenos por planeta. Con la aritmética
de la BIOS, cada uno le lleva al Z80 varios milisegundos: los 8 planetas no
entran en un cuadro. La Pico 2 tiene punto flotante por hardware y los hace
en microsegundos, más las órbitas enteras (180 puntos cada una) cuando
cambia la vista.

## Las órbitas

Cada planeta sale de sus seis elementos orbitales (semieje, excentricidad,
inclinación, longitud media, del perihelio y del nodo) y de cuánto cambia
cada uno por siglo, de la tabla de JPL "Approximate Positions of the
Planets" (E. M. Standish). Así cada planeta va más rápido cerca del Sol que
lejos, y las órbitas tienen su forma, su inclinación y su orientación
reales. Sin las perturbaciones entre planetas, el error es de minutos de
arco en siglos: mucho menos que un píxel.

La escala es la real. Neptuno está 77 veces más lejos del Sol que Mercurio:
con todo el sistema en pantalla, los planetas de adentro quedan sobre el
Sol. Con A y Z se pasa sin saltos de ver hasta Mercurio a ver hasta
Neptuno. Arranca mostrando hasta Marte.

La vista arranca inclinada 30°. Con abajo se inclina más, hasta ver el
plano de canto (90°): ahí se notan las inclinaciones reales de las órbitas,
como los 7° de Mercurio. Con arriba se vuelve a la vista desde arriba (0°)
y se sigue hasta verlo desde abajo (−90°). Los planetas no están a escala: son
dibujos de 3 a 7 píxeles.

## La pantalla

SCREEN 2. Las órbitas (cada una del color de su planeta) y el Sol van en el
bitmap; los planetas son sprites de 8x8, el más cercano adelante. Si en una
línea hay más de 4, el orden rota cada cuadro y parpadean en lugar de
desaparecer, como en CUEVA. Las filas de arriba y de abajo son de los
textos, con una fuente de la Pico.

La Pico tiene dos imágenes de la pantalla: la nueva y la que tiene la VRAM.
En cada cuadro manda las celdas de 8x8 que difieren (8 bytes de patrón y 8
de color cada una), juntando las que van seguidas en una fila, hasta 1.500
bytes por cuadro (unos 43.500 ciclos del Z80). Con la vista quieta solo
cambian los sprites y la fecha: unos 100 bytes. Al girar, inclinar o hacer
zoom se redibujan las órbitas, y lo que no entra en un cuadro sigue en el
siguiente: una vista nueva queda completa en 2 a 4 cuadros.

Las dos imágenes ocupan 24 KB, que no salen de la RAM de la Pico: viven en
el buffer de 127 KB de los DSKIO, que no se usa mientras corre el programa.

## Protocolo

| Código | Nombre | Parámetros | Respuesta |
|---|---|---|---|
| 09h | PLANETASINI | — | Registros del VDP de SCREEN 2, las tablas enteras y los sprites |
| 0Ah | PLANETASCUAD | registro 14 del PSG y filas 0 a 8 del teclado (10 bytes) | Avanza un cuadro |

El formato de las respuestas es el de `CUEVA.md`: los registros 0 a 10 del
PSG (aquí, en silencio), tramos de VRAM, registros del VDP y FFh al final.
