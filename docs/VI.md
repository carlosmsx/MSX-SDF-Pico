# VI.COM

Editor al estilo de vi para MSX-DOS 1, en pantalla de 40 columnas. No depende
del SDF-Pico: sirve en cualquier MSX con MSX-DOS 1. El fuente es
`comandos/vi.asm` y se ensambla con PASMO (`pasmo vi.asm vi.com`).

```
VI [archivo]
```

Si el archivo no existe, abre uno nuevo. El texto entra entero en memoria
(unos 35 KB en una máquina de 64 KB); si el archivo es más grande se carga lo
que entra, se avisa y queda de solo lectura para no grabar un archivo
truncado.

## Teclas

| Modo normal | |
|---|---|
| `h j k l`, cursores | Movimiento. `0` y `$`: principio y fin de la línea |
| `w` `b` | Palabra siguiente y anterior |
| `G`, `gg`, `NG`, `:N` | Última línea, primera, línea N |
| `CTRL-F` `CTRL-B` | 20 líneas abajo y arriba (`CTRL-D` y `CTRL-U`: 11) |
| `i` `a` `I` `A` `o` `O` | Pasan a modo insertar |
| `x`, `r`, `J` | Borrar caracteres, reemplazar uno, unir con la línea siguiente |
| `dd`, `yy`, `p`, `P` | Borrar y copiar líneas, pegar abajo y arriba |
| `/texto`, `n` | Buscar hacia adelante (da la vuelta al final), repetir |
| `CTRL-G` | Nombre, línea actual y total |
| `ZZ` | Grabar y salir |

Casi todas aceptan una cuenta (`3j`, `2dd`, `5x`, `3p`).

| Modo insertar | |
|---|---|
| `ESC` | Vuelve al modo normal |
| `ENTER`, `BS`, `DEL`, `TAB`, cursores | Como en cualquier editor |

| Línea de comandos | |
|---|---|
| `:w` `:w nombre` | Graba; con nombre, graba en otro archivo sin cambiar el actual |
| `:q` `:q!` | Sale (sin `!` avisa si hay cambios sin grabar) |
| `:wq` `:x` | Graba y sale |
| `:N` | Va a la línea N |

## Qué no tiene

Deshacer (`u`), repetir (`.`), registros con nombre, expresiones regulares,
operadores con movimientos (`dw`, `d$`...), modo visual, varios archivos. Lo
que sí cubre es abrir, mirar, corregir y grabar fuentes y archivos de
configuración.

## Cómo está hecho

- El texto vive en un solo búfer con hueco: el cursor es el hueco, así
  insertar y borrar en el cursor no mueve nada, y moverse entre líneas son
  copias cortas. Ir al principio o al final copia en bloque con `LDIR`.
- Se lee y se graba con las funciones de archivo del DOS 1 (FCB). Al leer se
  quitan los `CR`, y se corta en el primer `^Z` o `NUL`; al grabar se vuelven
  a poner, se agrega el último fin de línea si falta y se completa el último
  registro con `^Z`.
- En MSX-DOS 1 la página 0 es RAM, así que el BIOS solo se llama con `CALSLT`
  y sin pasarle datos que estén ahí. Por eso la pantalla se escribe directo
  por los puertos del VDP (que se leen de la ROM principal), y el cursor es un
  carácter con el dibujo del de abajo invertido.
- El búfer termina siete mil bytes antes del DOS, para no pisar `COMMAND.COM`
  (que, si se pisa, se recarga del disco al salir).

## Cómo se probó

En el emulador WebMSX, con MSX-DOS 1.03: carga, desplazamiento, movimiento,
inserción, borrado, copiado, búsqueda, `:w`, `:q`, archivos nuevos, archivos
largos y un archivo de 60 KB. No se probó en un MSX real.
