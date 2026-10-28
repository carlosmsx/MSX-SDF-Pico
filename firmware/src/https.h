// GET por HTTPS, para bajar el firmware de GitHub (actualizar.c). Nucleo 0,
// desde el bucle principal: cada funcion espera hasta terminar.
//
// TCP como en camara.c (lwIP en las interrupciones, lo recibido en cola de
// pbuf hasta que se lee) y TLS 1.2 con mbedTLS encima, en el bucle. El
// servidor se reconoce con los certificados de raices.h y su nombre. Sigue
// las redirecciones (los archivos de un release estan en otro servidor).
//
// mbedTLS pide unos 40 KB de la RAM libre (malloc) por conexion, y los
// devuelve al cerrar.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define HTTPS_URL_MAX   1536    // la de una redireccion de GitHub pasa de 800
#define HTTPS_ESPERA_MS 15000   // sin recibir nada, se abandona

// Resultado de https_get y https_leer
#define HTTPS_OK        0
#define HTTPS_SIN_WIFI  1
#define HTTPS_NOMBRE    2       // el nombre no se resolvio
#define HTTPS_CONEXION  3       // no conecto, o corto antes de terminar
#define HTTPS_TLS       4       // fallo el TLS (o el certificado): el error en https_detalle
#define HTTPS_HTTP      5       // no contesto 200: el codigo en https_detalle
#define HTTPS_TIEMPO    6       // el servidor dejo de mandar
#define HTTPS_URL       7       // no es https://host[:puerto]/ruta, o demasiadas redirecciones
#define HTTPS_MEMORIA   8       // no alcanzo la RAM para el TLS

// El codigo HTTP, o el error de mbedTLS (en positivo), del ultimo fallo
extern int https_detalle;

// Pide la URL y lee la respuesta hasta el cuerpo. HTTPS_OK con 200: el
// cuerpo se lee con https_leer y el largo (o -1 si no lo dijo) queda en
// *largo. Con cualquier resultado, despues va https_cerrar.
int https_get(const char *url, int32_t *largo);

// Hasta n bytes del cuerpo (al menos 1). 0 al terminar; negativo, el error
// (-HTTPS_CONEXION, -HTTPS_TLS, -HTTPS_TIEMPO).
int https_leer(uint8_t *buf, int n);

void https_cerrar(void);
