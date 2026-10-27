# TJpgDec R0.03

Decodificador de JPEG de ChaN (el autor de FatFs), para `src/camara.c`. Copia
de `tjpgd.c`, `tjpgd.h` y `tjpgdcnf.h` de R0.03 tal como la trae LVGL 8
(`src/extra/libs/sjpg`), sin las tres lineas con que LVGL la encierra
(`lv_conf_internal.h` y `#if LV_USE_SJPG`). La licencia esta al principio de
`tjpgd.c`: uso libre, conservando el aviso de copyright.

`tjpgdcnf.h` es la configuracion del SDF-Pico: salida RGB888, con reduccion
(1/2, 1/4, 1/8) y `JD_FASTDECODE 1` (para procesadores de 32 bits).

No soporta JPEG progresivo; las fotos de las camaras IP son "baseline".
