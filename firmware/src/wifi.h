// Wi-Fi del CYW43 de la Pico 2 W, en el nucleo 0.
//
// La red (el driver y lwIP) corre en segundo plano, por interrupciones del
// nucleo 0: el nucleo 1 y el bus no se enteran. Las credenciales se guardan
// en el anteultimo sector de la flash de la Pico, solo despues de conectar bien,
// y nunca se muestran: la consola y el menu informan la red, no la clave.
//
// Todo esto se llama solo desde el nucleo 0.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WIFI_SSID_MAX   32
#define WIFI_CLAVE_MAX  63      // WPA2: 8-63 caracteres
#define WIFI_CLAVE_MIN  8

// Arranca el chip y se conecta a la red guardada, si hay. false si el chip no
// contesta (el resto del firmware sigue igual).
bool wifi_init(void);

// Llamar seguido desde el bucle del nucleo 0: sigue la conexion, reintenta,
// graba las credenciales y avisa los cambios por la consola.
void wifi_tarea(void);

enum { WIFI_APAGADO, WIFI_SIN_RED, WIFI_CONECTANDO, WIFI_CONECTADO, WIFI_ESPERA };
int wifi_estado(void);
const char *wifi_red(void);             // la conectada o la que se intenta; "" si ninguna
const char *wifi_red_guardada(void);    // "" si ninguna
const char *wifi_ip(void);
int wifi_senal(void);                   // dBm

// Conecta a una red nueva; si conecta, la guarda. Clave vacia: red abierta.
void wifi_conectar(const char *ssid, const char *clave);
// Resultado de la ultima wifi_conectar: 0 en curso, 1 conecto, -1 fallo (con
// el motivo).
int wifi_resultado(const char **motivo);
// Desconecta y borra la red guardada.
void wifi_olvidar(void);

// Busqueda de redes, sin bloquear: la lista queda ordenada por senal.
typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    int16_t rssi;
    uint8_t canal;
    bool abierta;
} wifi_red_t;

bool wifi_buscar(void);                 // false si no se pudo empezar
bool wifi_buscando(void);
int wifi_redes(const wifi_red_t **lista);

// Comandos de la consola USB. true si la linea era para el Wi-Fi.
bool wifi_comando(const char *linea);

// true mientras la consola espera la clave: la linea va sin eco.
bool wifi_pide_clave(void);
