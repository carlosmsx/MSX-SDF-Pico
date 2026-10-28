# Actualización del firmware desde GitHub

La Pico puede bajar el último firmware de los releases de este repositorio y
grabarlo ella misma, sin la PC ni el modo BOOTSEL. Necesita el Wi-Fi
conectado (`WIFI.md`) y el firmware 0.14 o posterior. Las versiones
anteriores se actualizan una vez por USB, con el `.uf2`.

## Desde MSX-DOS: `SDF UPDATE`

```
B>SDF UPDATE
Preguntando a GitHub...
Instalado: 0.14
En GitHub: 0.15
La Pico lo baja, lo graba y se
reinicia. Seguro? (S/N) S
Bajando 466 de 466 KB
Verificando...
Grabando: no apague el MSX.
Listo. Ahora: SDF-Pico 0.15.
```

Si la versión de GitHub no es más nueva que la instalada, `SDF UPDATE` lo
dice y no pregunta. `SDF UPDATE /F` pregunta igual: sirve para reinstalar la
misma versión o para volver a una anterior que siga siendo el último release.

Las imágenes montadas vuelven solas después del reinicio (`SDFPICO.CFG`, en
`SD.md`), y el DOS sigue andando. La red, la zona horaria y la URL de la
cámara se conservan: viven en otro sector de la flash.

Desde la consola USB, `actualizar` pregunta la versión y `actualizar si` hace
todo, con el avance cada 64 KB.

## Cómo lo hace

1. Pregunta a la API de GitHub por el último release
   (`api.github.com/repos/carlosmsx/MSX-SDF-Pico/releases/latest`) y toma su
   tag, por ejemplo `v0.15`. Los borradores y los pre-release no cuentan.
2. Baja `sdf_pico.bin.sha256` y `sdf_pico.bin` de ese release. GitHub
   redirige los archivos a `release-assets.githubusercontent.com`.
3. Graba el `.bin` en una zona de la flash que el firmware no usa, desde los
   2 MB, y lo verifica ahí: la suma SHA-256, el bloque `IMAGE_DEF` del RP2350
   en el primer sector (lo que busca el bootrom para arrancar) y el texto
   `SDF-Pico ` del firmware.
4. Recién entonces copia la zona sobre el firmware, sector por sector, con
   las interrupciones cortadas, y se reinicia con el watchdog.

Todo va por HTTPS con mbedTLS (TLS 1.2). La Pico reconoce a GitHub por la
cadena de certificados, contra las raíces de `firmware/src/raices.c`, y por
el nombre del servidor. No revisa las fechas: el reloj puede no estar puesto.
Si GitHub cambia de autoridad certificante a una que no está en la lista, la
actualización falla con un error de TLS; `tools/raices.py` arma la lista de
nuevo desde la de certifi.

Mientras baja, el núcleo 1 sigue atendiendo el bus: el MSX puede consultar el
avance. Lo que tarda depende de la red; el TLS agrega unos segundos al
principio de cada conexión, y son tres (la API, la suma y el `.bin`).

> **Si se corta la luz mientras graba** (los últimos segundos), la Pico queda
> sin firmware. No hay riesgo de dañarla: el modo BOOTSEL está en la ROM del
> RP2350, así que el `.uf2` se vuelve a grabar por USB como siempre. Si se
> corta antes, mientras baja, no pasa nada: el firmware instalado sigue
> intacto.

### La flash

| Desde | Hasta | Qué |
|---|---|---|
| 0 | el largo del firmware | El firmware (hasta 1 MB) |
| 2 MB | 3 MB | La zona de la actualización |
| 4 MB − 8 KB | 4 MB − 4 KB | Los ajustes: red, zona horaria y cámara (`WIFI.md`) |

El firmware se copia entero a la RAM al arrancar (`copy_to_ram`), salvo
mbedTLS, lwIP, el Wi-Fi y la conversión de la cámara, que corren desde la
flash para dejarle RAM al TLS (`firmware/ld/`). Es todo código del núcleo 0, y
el núcleo 0 no ejecuta nada de la flash mientras la graba. El búfer de
entrada del TLS (16 KB) va en el de la conversión de la cámara: las dos cosas
nunca pasan a la vez.

## Publicar un release

Después de compilar con `firmware/build.ps1`:

```
python tools/release.py
```

Deja en `out/release/vX.Y/` los tres archivos del release, después de revisar
lo mismo que revisa la Pico: `sdf_pico.uf2`, `sdf_pico.bin` y
`sdf_pico.bin.sha256`. En GitHub se crea un release con el tag `vX.Y`, la
versión de `FW_VERSION` en `firmware/src/protocolo.h`, y se suben los tres.
El release no tiene que ser borrador ni pre-release.

El `.bin` es lo mismo que graba el `.uf2`, sin el bloque que el SDK agrega en
0x10FFFF00 para la errata RP2350-E10, que solo hace falta al grabar por USB.

## Comandos del protocolo

Son nuevos (0BEh y 0BFh) y se agregan sin cambiar los del protocolo 8. Como
en `CAMARA.md`, la actualización se pide y después se consulta.

### `CMD_UPDATE` (0BEh)

```
OUT (01h), CMD_UPDATE
OUT (00h), modo         0 = solo consultar la última versión,
                        1 = bajarla, verificarla, grabarla y reiniciar
OUT (00h), 5Ah          obligatorio con el modo 1
IN  (00h)               0 = pedido aceptado, 1 = hay una en curso, 2 = pedido inválido
```

Un firmware anterior al 0.14 no contesta: el `IN` lee FFh.

### `CMD_UPDPOLL` (0BFh)

```
OUT (01h), CMD_UPDPOLL
IN  (00h) x 9           estado, fase o resultado, detalle (L, H), KB bajados (L, H),
                        KB en total (L, H), 1 si la de GitHub es más nueva
IN  (00h) ... 0         la versión instalada, por ejemplo "0.14"
IN  (00h) ... 0         la de GitHub, sin la v (vacía hasta que termina)
```

Estado: 0 = no se pidió nada, 1 = en curso, 2 = terminó. En curso, el
segundo byte es la fase: 1 = consultando, 2 = bajando, 3 = verificando,
4 = grabando. Con el modo 1 y todo bien, el estado nunca llega a 2: después
de la fase 4 la Pico se reinicia (el estado lee FFh unos instantes, el bus
sin nadie) y vuelve con el estado en 0 y la versión nueva.

Con el estado en 2:

| Resultado | Significado | Detalle |
|---|---|---|
| 0 | Listo (la consulta) | |
| 1 | Sin Wi-Fi | |
| 2 | El nombre no se resolvió | |
| 3 | Falló la conexión, o cortó antes de terminar | |
| 4 | Falló el TLS o el certificado | El error de mbedTLS, en positivo |
| 5 | GitHub no contestó 200 | El código HTTP (404: no hay release, o le falta un archivo) |
| 6 | GitHub dejó de mandar (15 s) | |
| 7 | URL inválida, o demasiadas redirecciones | |
| 8 | No alcanzó la RAM para el TLS | El error de mbedTLS |
| 9 | La respuesta de la API no tiene la versión | |
| 10 | El `.bin` tiene menos de 4 KB o más de 1 MB | |
| 11 | El `.bin` no coincide con `sdf_pico.bin.sha256` | |
| 12 | No es un firmware del SDF-Pico para RP2350 | |
| 13 | La zona de la flash no quedó como se grabó | |

> **Estado:** compilado y probado `SDF.COM` en WebMSX
> (`tools/webmsx_pico_sd.js` simula la Pico, con las fases y el reinicio).
> Falta probar la bajada y la grabación en la Pico.
