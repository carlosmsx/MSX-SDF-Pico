// Foto de una camara IP en SCREEN 2, para CAM.COM (CMD_CAMSHOT, CMD_CAMPOLL,
// CMD_CAMREAD y CMD_CAMURL, docs/CAMARA.md).
//
// El nucleo 1 pide y consulta, como en ping.h. El nucleo 0 baja el JPEG por
// HTTP o HTTPS (https.c), lo decodifica a medida que llega (TJpgDec), lo lleva a 256x192 y
// elige, para cada tramo de 8x1 pixeles, el par de colores del TMS9918 que
// mejor lo representa, con difusion de error. Es el algoritmo de
// tools/jpg2sc2.py. El resultado son las tablas de patrones y de colores de
// SCREEN 2, 6 KB cada una, que el MSX lee de a bloques.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CAM_URL_MAX     200     // http://usuario:clave@host:puerto/ruta, o https://host/ruta
#define CAM_PANTALLA    12288   // patrones (6144) y colores (6144)
#define CAM_BLOQUE      256     // lo que entrega cada CMD_CAMREAD
#define CAM_BLOQUES     (CAM_PANTALLA / CAM_BLOQUE)

// Respuesta de CMD_CAMSHOT y CMD_CAMURL
#define CAM_PEDIDO_OK   0
#define CAM_OCUPADA     1       // hay una foto en curso
#define CAM_URL_MAL     2       // no es http://[usuario:clave@]host[:puerto]/ruta ni
                                // https://host[:puerto]/ruta, o es larga

// Estado de CMD_CAMPOLL, primer byte de la respuesta
#define CAM_LIBRE       0       // no se pidio nada
#define CAM_EN_CURSO    1
#define CAM_LISTA       2

// Segundo byte, cuando esta lista
#define CAM_OK          0
#define CAM_SIN_WIFI    1
#define CAM_NOMBRE      2       // el nombre no se resolvio
#define CAM_CONEXION    3       // no conecto, o corto antes de terminar
#define CAM_HTTP        4       // no contesto 200: el codigo va en el detalle
#define CAM_JPEG        5       // no es un JPEG que TJpgDec entienda: el error va en el detalle
#define CAM_TAMANO      6       // la imagen es demasiado grande o demasiado chica
#define CAM_TIEMPO      7       // la camara dejo de mandar
#define CAM_SIN_URL     8       // no hay URL guardada
#define CAM_TLS         9       // fallo el HTTPS (o el certificado): el error de mbedTLS en el detalle
#define CAM_MEMORIA     10      // no alcanzo la RAM para el HTTPS

#define CAM_ESPERA_MS   8000    // sin recibir nada, se abandona

// ---- Nucleo 1 ----

// Pide una foto. n = 0: con la URL guardada.
uint8_t cam_pedir(const char *url, int n);

// Guarda la URL en la flash (la graba el nucleo 0). n = 0 la olvida.
uint8_t cam_guardar_url(const char *url, int n);

// La URL guardada, con la clave cambiada por asteriscos (ASCIIZ). Devuelve
// cuantos bytes escribio, con el 0.
int cam_url_visible(char *out);

// Escribe 10 bytes: estado, resultado, detalle (L, H), ancho (L, H) y alto
// (L, H) del JPEG, tiempo en ms (L, H). Devuelve 10.
int cam_leer_estado(uint8_t *out);

// El bloque i (0 a CAM_BLOQUES - 1) de la pantalla, o NULL.
const uint8_t *cam_bloque(int i);

// ---- Nucleo 0 ----

void cam_iniciar(void);
// Olvida la URL (guardada o pedida) y la ultima foto. Nucleo 0.
void cam_olvidar(void);
void cam_tarea(void);

// Comandos de la consola USB. true si la linea era para la camara.
bool cam_comando(const char *linea);
