// Bus del MSX: las dos SM de bus.pio, el anillo de recepcion y el DMA de
// respuesta. Todo lo usa el nucleo 1, salvo bus_init y el diagnostico.

#pragma once
#include <stdbool.h>
#include <stdint.h>

void bus_init(void);

// Arranca las SM cuando el MSX esta encendido (/IOSEL en alto 100 ms).
// Bloquea hasta entonces.
void bus_despertar(void);

// Llamar seguido desde el bucle. Si /IOSEL quedo en bajo mas de 50 us (MSX
// apagado), detiene las SM, suelta el bus y devuelve true.
bool bus_vigilar(void);

// Recepcion: cada escritura del MSX es una palabra con D0-D7 en los bits 0-7
// y A0 en el bit BUS_A0 (1 = comando). Un DMA las copia a un anillo.
#define BUS_A0 9
bool bus_rx_hay(void);
uint32_t bus_rx_leer(void);

// Respuesta: lo que el MSX lee de 00h.
void bus_tx_cortar(void);                         // descarta lo pendiente
void bus_tx_byte(uint8_t b);                      // al FIFO; hay lugar tras cortar
void bus_tx_enviar(const void *datos, uint32_t n);  // por DMA, de RAM o de flash

// Registro de estado, IN (01h).
void bus_estado(uint8_t v);

// Diagnostico (nucleo 0): latido y espia, solo con DIAGNOSTICO=1.
void bus_diagnostico(void);
