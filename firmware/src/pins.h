// Asignacion de pines del SDF-Pico. Ver hardware/CABLEADO.md.
//
// Todo lo que toca el cartucho esta del lado izquierdo de la Pico
// (pines fisicos 1 a 20): bus del MSX y SD.

#pragma once

// Bus del MSX, a traves del '245 y del decodificador de la rev1.
// El programa PIO asume este orden: D0-D7 consecutivos desde GP0, y
// /IOSEL, A0 y /RD inmediatamente despues (mov osr, pins + out pc, 2).
#define PIN_D0          0   // D0-D7 en GP0-GP7, lado B del '245
#define PIN_IOSEL       8   // U5.Y0: acceso a 00h/01h (decodificacion pura)
#define PIN_A0          9   // registro: 0 = datos, 1 = comando/estado
#define PIN_RD          10  // /RD del MSX: 0 = el MSX lee
#define PIN_EN          11  // G1 de U5: se deja fijo en alto

// SD, en SPI1 (modulo colgado de H1)
#define PIN_SD_MISO     12  // SPI1 RX
#define PIN_SD_CS       13  // SPI1 CSn
#define PIN_SD_SCK      14  // SPI1 SCK
#define PIN_SD_MOSI     15  // SPI1 TX

// Lado derecho, por cable
#define PIN_UART_TX     16  // UART0, ex PB0 (H1-7)
#define PIN_UART_RX     17  // UART0, ex PB1 (H1-6)
#define PIN_SDA         20  // I2C0 (H1-8)
#define PIN_SCL         21  // I2C0 (H1-9)
