// Protocolo con la DiskROM: los codigos del SDF-1 (protocolo 7, ver
// diskrom/PROTOCOL.INC de ese repo) y el registro de estado del SDF-Pico.

#pragma once
#include <stdint.h>

#define PROTOCOL_VERSION  8   // el 7 del SDF-1, con registro de estado
#define FW_VERSION        "SDF-Pico 0.17"

// Registro de estado, IN (01h). Ver docs/PROTOCOLO.md.
#define EST_OCUPADO       0x80  // el firmware prepara la respuesta: no leer 00h
#define EST_FIJO          0x40  // siempre 1
                                // bit 5 siempre 0: FFh es el bus al aire
#define EST_CAMBIO        0x02
#define EST_ERROR         0x01

// 0x0x: prueba del bus (docs/PRUEBA-BUS.md)
#define CMD_ECO           0x01
#define CMD_CONTADOR      0x02
#define CMD_NADA          0x03
// Coprocesador de juegos, prueba (copro.h, docs/COPRO.md)
#define CMD_COPROBLQ      0x04  // n, semilla; contesta n bloques de 256 bytes
#define CMD_COPROINI      0x05  // sprites; contesta el patron de 16x16 (32 bytes)
#define CMD_COPROCUAD     0x06  // joystick, fila 8; contesta la tabla de sprites (128 bytes)
// CUEVA, un juego que corre en la Pico (cueva.h, docs/CUEVA.md)
#define CMD_CUEVAINI      0x07  // contesta el PSG y los tramos de VRAM de la pantalla inicial
#define CMD_CUEVACUAD     0x08  // joystick, filas 5 y 2; contesta el PSG y los tramos del cuadro
// PLANETAS, el sistema solar calculado en la Pico (planetas.h, docs/PLANETAS.md)
#define CMD_PLANETASINI   0x09  // contesta el PSG y los tramos de VRAM de SCREEN 2
#define CMD_PLANETASCUAD  0x0A  // joystick y filas 0 a 8; contesta el PSG y los tramos del cuadro

// 0x9x: dispositivos de BASIC
#define CMD_DEVNAME       0x90
#define CMD_DEVOPEN       0x91
#define CMD_DEVCLOSE      0x92
#define CMD_DEVIN         0x93
#define CMD_DEVOUT        0x94
#define CMD_DEVEOF        0x95

// 0xAx: reloj del kernel
#define CMD_RTCCHK        0xA0
#define CMD_RTCGET        0xA1
#define CMD_RTCSETDA      0xA2
#define CMD_RTCSETTI      0xA3

// MQTT (mqtt.h, docs/MQTT.md)
#define CMD_MQTTCONN      0xA8  // largo, URL (0: la guardada); contesta 0 o error
#define CMD_MQTTCLOSE     0xA9  // desconecta y olvida las suscripciones; contesta 0
#define CMD_MQTTSUB       0xAA  // modo, largo, topico; contesta 0 o error
#define CMD_MQTTPUB       0xAB  // flags, largo, topico, largo, texto; contesta 0 o error
#define CMD_MQTTPOLL      0xAC  // modo; contesta estado, motivo y, si hay, un mensaje
#define CMD_MQTTURL       0xAD  // largo, URL; la guarda (largo 0 la olvida), contesta 0 o error
#define CMD_MQTTVER       0xAE  // contesta la URL guardada, sin la clave (ASCIIZ)

// 0xBx: red
#define CMD_WIFIMENU      0xB0  // tecla, ancho, filas; contesta fin y texto (menu_wifi.h)
#define CMD_NETCONN       0xB1  // puerto (L, H), largo, host; contesta 0 o error (red.h)
#define CMD_NETIO         0xB2  // max, n, n bytes; contesta estado, m, m bytes (red.h)
#define CMD_PINGSEND      0xB3  // largo, host; contesta 0 o error (ping.h)
#define CMD_PINGPOLL      0xB4  // contesta estado, resultado, IP, tiempo, TTL: 9 bytes (ping.h)
#define CMD_WIFISTAT      0xB5  // contesta estado, resultado, senal, red, IP, motivo (wifi_dos.h)
#define CMD_WIFISCAN      0xB6  // empieza a buscar redes; contesta 0 o error (wifi_dos.h)
#define CMD_WIFIREDES     0xB7  // contesta buscando, n, n redes (wifi_dos.h)
#define CMD_WIFIJOIN      0xB8  // largo, red, largo, clave; contesta 0 o error (wifi_dos.h)
#define CMD_CAMSHOT       0xB9  // largo, URL (0: la guardada); contesta 0 o error (camara.h)
#define CMD_CAMPOLL       0xBA  // contesta estado, resultado, detalle, ancho, alto, ms: 10 bytes
#define CMD_CAMREAD       0xBB  // bloque (0-47); contesta 256 bytes de patrones y colores
#define CMD_CAMURL        0xBC  // largo, URL; la guarda (largo 0 la olvida), contesta 0 o error
#define CMD_CAMVER        0xBD  // contesta la URL guardada, sin la clave (ASCIIZ)
#define CMD_UPDATE        0xBE  // modo, confirmacion; contesta 0 o error (actualizar.h)
#define CMD_UPDPOLL       0xBF  // contesta estado, fase o resultado, detalle, KB, versiones

// 0xDx: diagnostico
// Archivos de la SD, para SD.COM y un explorador (archivos.h, docs/ARCHIVOS.md)
#define CMD_FSCD          0xC0  // largo, ruta; contesta error
#define CMD_FSPWD         0xC1  // contesta la ruta actual (ASCIIZ)
#define CMD_FSDIR         0xC2  // opciones, largo, ruta o patron; contesta error, cantidad (L, H), cortado
#define CMD_FSLIST        0xC3  // desde (L, H), max; contesta n y n entradas de FS_ENTRADA bytes
#define CMD_FSOPEN        0xC4  // modo, largo, ruta; contesta error y largo (4 bytes)
#define CMD_FSREAD        0xC5  // max (L, H); contesta error, n (L, H) y n bytes
#define CMD_FSWRITE       0xC6  // n (L, H) y n bytes; contesta error
#define CMD_FSCLOSE       0xC7  // contesta error
#define CMD_FSDEL         0xC8  // largo, ruta; contesta error
#define CMD_FSMKDIR       0xC9  // largo, ruta; contesta error
#define CMD_FSREN         0xCA  // largo, ruta, largo, nombre nuevo; contesta error
#define CMD_FSSTAT        0xCB  // largo, ruta; contesta error y una entrada
#define CMD_FSFREE        0xCC  // contesta error, KB libres y KB en total (4 y 4 bytes)

#define CMD_DEBUG         0xD0
#define CMD_SDFTEST       0xD1
#define CMD_SDFDEBUG      0xD2

// 0xEx: SD e imagenes DSK
#define CMD_SENDSTR       0xE0
#define CMD_SDFMOUNT      0xE1
#define CMD_SDFFILES      0xE2
#define CMD_SDFUMOUNT     0xE3
#define CMD_SDFNEW        0xE4
#define CMD_SDFDRIVES     0xE5
#define CMD_SDFINFO       0xE6  // contesta drives, imagen de A: y de B: (ASCIIZ) (SDF.COM)
#define CMD_SDFBORRAR     0xE7  // SDFBORRAR_SI; borra los datos personales, contesta 0 o error
#define SDFBORRAR_SI      0x5A  // un byte suelto no tiene que alcanzar para borrar

// 0xFx: driver de disco de MSX-DOS
#define CMD_WRITE         0xF0
#define CMD_READ          0xF1
#define CMD_INIHRD        0xF2
#define CMD_INIENV        0xF3
#define CMD_DRIVES        0xF4
#define CMD_DSKCHG        0xF5
#define CMD_CHOICE        0xF6
#define CMD_DSKFMT        0xF7
#define CMD_OEMSTAT       0xF8
#define CMD_MTOFF         0xF9
#define CMD_GETDPB        0xFA

// Errores de DSKIO, en el byte de estado de CMD_READ y CMD_WRITE
#define DSKIO_ERR_NOT_READY         2   // drive sin imagen, o la imagen ya no esta
#define DSKIO_ERR_RECORD_NOT_FOUND  8   // sector fuera de la imagen
#define DSKIO_ERR_WRITE_FAULT       10  // DPB de 720 KB en una imagen de 360, o fallo la SD

// Errores de BASIC que contestan los comandos CALL SDF...
#define ERR_ILLEGAL_FUNCTION      5
#define ERR_DEVICE_IO             19
#define ERR_FILE_NOT_FOUND        53
#define ERR_FILE_ALREADY_OPEN     54    // archivos.c: imagen montada, o el archivo abierto
#define ERR_BAD_FILE_NAME         56
#define ERR_FILE_NOT_OPEN         59    // archivos.c: FSREAD o FSWRITE sin FSOPEN
#define ERR_BAD_FILE_MODE         61    // la imagen no es de 360 ni de 720 KB
#define ERR_BAD_DRIVE_NAME        62
#define ERR_FILE_ALREADY_EXISTS   65
#define ERR_DISK_FULL             66
#define ERR_WRITE_PROTECTED       68    // archivos.c: archivo de solo lectura
#define ERR_DISK_IO               69
#define ERR_DISK_OFFLINE          70    // no hay SD, o todavia no inicializo
// Propios de archivos.c, sin equivalente en el BASIC de MSX-DOS 1
#define ERR_PATH_NOT_FOUND        80    // no existe el directorio
#define ERR_DIR_NOT_EMPTY         81
#define ERR_IS_DIR                82    // se esperaba un archivo

// Mientras se crea la imagen de CALL SDFNEW, la ROM lee esto en lugar del
// resultado y vuelve a consultar.
#define SDFNEW_BUSY       0xFE

#define DEV_NONE          0xFF

void protocolo_core1(void);
