# Utilidades de MSX-DOS

Programas para MSX-DOS 1 que no usan la Pico: andan en cualquier MSX. Los
fuentes están en `comandos/` y se ensamblan con `python tools/comandos.py`,
que los deja en `out/msx/`. Los que usan la Pico están en la documentación de
cada tema: `PING.COM` y `GET.COM` en `RED.md`, `WIFI.COM` en `WIFI.md`,
`SDF.COM` en `SD.md`, `SD.COM` en `ARCHIVOS.md`, `CAM.COM` en `CAMARA.md`, y
el editor `VI.COM` en `VI.md`.

## GREP.COM

```
GREP [/I] [/N] [/V] [/C] patron archivo...
```

Muestra las líneas de los archivos que contienen el patrón. Los archivos
pueden llevar comodines (`*.TXT`, `B:CAP?.DOC`) y ser varios; con más de uno,
cada línea sale con el nombre del archivo adelante.

| Opción | Qué hace |
|---|---|
| `/I` | No distingue mayúsculas de minúsculas |
| `/N` | Muestra el número de cada línea |
| `/V` | Muestra las líneas que **no** contienen el patrón |
| `/C` | Solo cuenta las líneas, por archivo |

Las opciones van antes del patrón, juntas (`/IN`) o separadas.

```
B>GREP /N /I hola *.TXT
A.TXT:1:Hola mundo
A.TXT:2:hola MSX
GRANDE.TXT:2500:aca esta HOLA
```

El patrón es un subconjunto chico de las expresiones regulares:

| En el patrón | Coincide con |
|---|---|
| una letra | esa letra |
| `.` | cualquier carácter |
| `*` | cero o más veces lo anterior (`ho*la`: hla, hola, hoola...) |
| `^` al principio | el principio de la línea |
| `$` al final | el final de la línea |
| `\` y un carácter | ese carácter tal cual (`\.` es un punto) |

Un patrón con espacios va entre comillas: `GREP "hola mundo" *.TXT`.

- Las líneas pueden terminar en CR LF o solo LF. Un `^Z` (1Ah) termina el
  archivo, como en los archivos de texto de CP/M.
- De cada línea se miran los primeros 255 caracteres.
- Hasta 112 archivos por vez.
- CTRL+C corta y CTRL+S pausa, como en cualquier salida del DOS.

Probado en la DPC-200. En WebMSX recorre un archivo de 35 KB y 3000 líneas
en unos 3,5 s.
