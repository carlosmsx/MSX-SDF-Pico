# Disco en la flash de la Pico

Hasta que el SDF-Pico tenga la tarjeta SD, el disco A: vive en la flash de la
Pico 2 W, a partir de 1 MB. Permite probar el protocolo y la DiskROM sin el
módulo SD, y más adelante pasar a la tarjeta cambia solamente de dónde salen
los sectores.

## Qué admite

- Una imagen `.DSK` de 720 KB (media F9) o de 360 KB (media F8). El formato se
  reconoce por el primer byte de la FAT.
- Lectura y escritura. La escritura se agrupa por bloques de 4 KB de flash
  (8 sectores), que se graban al cambiar de bloque y al terminar cada
  `DSKIO`.
- Una zona en blanco no se puede leer (el DOS informa "Disk offline"), pero sí
  formatear desde el MSX con `CALL FORMAT`: queda un disco de 720 KB.
- Un solo drive: `DRIVES` contesta 1, y el DOS trata a B: como fantasma de A:.

Los comandos `CALL SDF...` contestan "Disk offline" hasta que exista la SD.

## Cargar una imagen

La imagen se convierte en un `.uf2` que se copia a la Pico en modo BOOTSEL,
igual que el firmware. El firmware no se borra: el `.uf2` del disco escribe
solamente la zona del disco.

```
python tools/dsk2uf2.py JUEGO.DSK
python tools/dsk2uf2.py --vacio 720
python tools/dsk2uf2.py --borrar
```

La primera forma genera `JUEGO.uf2` con la imagen; la segunda, un disco de
720 KB formateado y vacío; la tercera deja la zona en blanco.

Después de copiar el `.uf2` de un disco, la Pico puede quedar en modo BOOTSEL
en lugar de reiniciarse sola. En ese caso alcanza con desconectarla y volver a
conectarla.

## Probar desde BASIC

Con la EEPROM grabada con `out/sdfpico.rom` y el disco cargado:

```basic
FILES
SAVE "PRUEBA.BAS"
FILES
LOAD "PRUEBA.BAS"
CALL SDFTEST
CALL SDFDEBUG
```

`CALL SDFTEST` muestra la versión del firmware (`SDF-Pico 0.2`, protocolo 8).
`CALL SDFDEBUG` muestra la cantidad de lecturas y escrituras, en comandos y
en sectores, y cuántos checksums de sector coincidieron y cuántos no. La ROM
manda el checksum de cada sector leído; un checksum que no coincide indica
un problema en el bus, y además se informa por la consola USB.

## La ROM del SDF-1 con este firmware

La DiskROM del SDF-1 no consulta el registro de estado: lee la respuesta
unos 15 µs después de enviar el último parámetro, confiando en el `/WAIT` que
ya no existe. Con el disco en la flash las lecturas funcionan, porque la
respuesta está lista en pocos microsegundos, pero las escrituras no: grabar un
bloque de flash lleva decenas de milisegundos, la ROM lee el estado del
`WRITE` siguiente antes de tiempo y el DOS termina en "Bad FAT". Se necesita
la DiskROM del SDF-Pico (`make rom`), que espera `OCUPADO` en 0.
