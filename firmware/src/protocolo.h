// Protocolo con la DiskROM: los codigos del SDF-1 (protocolo 7, ver
// diskrom/PROTOCOL.INC de ese repo) y el registro de estado del SDF-Pico.

#pragma once
#include <stdint.h>

#define PROTOCOL_VERSION  8   // el 7 del SDF-1, con registro de estado
#define FW_VERSION        "SDF-Pico 0.2"

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

// 0xDx: diagnostico
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
#define DSKIO_ERR_NOT_READY         2
#define DSKIO_ERR_RECORD_NOT_FOUND  8

// Errores de BASIC que contestan los comandos CALL SDF...
#define ERR_ILLEGAL_FUNCTION  5
#define ERR_DEVICE_IO         19
#define ERR_DISK_OFFLINE      70

#define DEV_NONE          0xFF

void protocolo_core1(void);
