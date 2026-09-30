# FatFs R0.15

Copia sin cambios de `ff.c`, `ff.h` y `diskio.h` de FatFs R0.15 (ChaN), tomada
del pico-sdk 2.3.0 (`lib/tinyusb/lib/fatfs`). Licencia en `LICENSE.txt`.

`ffconf.h` es la configuración del SDF-Pico: un solo volumen, nombres cortos
8.3 (sin LFN, como los ve MSX-DOS 1), sin exFAT, con `f_expand` para reservar
las imágenes nuevas contiguas.

La capa de disco (`disk_read`, `disk_write`...) está en `src/sd_spi.c`.
