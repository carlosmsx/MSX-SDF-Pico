# FatFs R0.15

Copia sin cambios de `ff.c`, `ff.h`, `ffunicode.c` y `diskio.h` de FatFs R0.15 (ChaN), tomada
del pico-sdk 2.3.0 (`lib/tinyusb/lib/fatfs`). Licencia en `LICENSE.txt`.

`ffconf.h` es la configuración del SDF-Pico: un solo volumen, con nombres largos
(LFN, para `docs/ARCHIVOS.md`; las imágenes se siguen usando por el nombre
corto 8.3), la fecha del reloj en los archivos (`get_fattime` en
`sd_spi.c`), sin exFAT, con `f_expand` para reservar
las imágenes nuevas contiguas.

La capa de disco (`disk_read`, `disk_write`...) está en `src/sd_spi.c`.
