// SDF-Pico: primer firmware, prueba del bus.
//
// Todavia no hay SD ni DiskROM. El objetivo es comprobar desde BASIC que la
// Pico contesta los puertos 00h/01h sin /WAIT, y que el DMA alimenta las
// lecturas. Ver docs/PRUEBA-BUS.md.
//
// Comandos (OUT 1,n):
//   01h  ECO       cada byte escrito en 00h se devuelve en la lectura siguiente
//   02h  CONTADOR  el DMA carga 00h, 01h, ..., FFh para leer de 00h
//   03h  NADA      corta el eco y el DMA
//
// Estado (INP(1)): bit 6 fijo en 1 (firmware vivo, distinto del FFh del bus
// al aire), bits 0-2 = bytes esperando en el TX FIFO (0-4), bit 3 = DMA en
// curso.
//
// Cada acceso se informa por la consola USB.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pins.h"
#include "bus.pio.h"

#define CMD_ECO         0x01
#define CMD_CONTADOR    0x02
#define CMD_NADA        0x03

#define EST_VIVO        0x40
#define EST_DMA         0x08

static PIO pio = pio0;
static uint sm_bus, sm_est;
static PIO pio_espia = pio1;
static uint sm_espia;
static int dma_tx;
static uint8_t contador[256];

// SOLO_ESCUCHA=1: los pines de datos quedan como entradas del SIO, asi que la
// PIO sigue leyendo y registrando todo pero nunca maneja el bus. Para probar
// el cableado sin riesgo de pisar un ciclo del MSX.
#ifndef SOLO_ESCUCHA
#define SOLO_ESCUCHA 1
#endif

static void bus_init(void)
{
    // Bus de datos: PIO, sin pulls (errata E9 del A2).
    for (uint pin = PIN_D0; pin < PIN_D0 + 8; pin++) {
        if (SOLO_ESCUCHA)
            gpio_init(pin);
        else
            pio_gpio_init(pio, pin);
        gpio_disable_pulls(pin);
    }
    pio_sm_set_consecutive_pindirs(pio, 0, PIN_D0, 8, false);

    // /IOSEL, A0 y /RD: entradas. La PIO las lee sin importar la funcion.
    for (uint pin = PIN_IOSEL; pin <= PIN_RD; pin++) {
        gpio_init(pin);
        gpio_disable_pulls(pin);
    }

    // EN fijo en alto: el decodificador siempre habilitado.
    gpio_init(PIN_EN);
    gpio_put(PIN_EN, 1);
    gpio_set_dir(PIN_EN, GPIO_OUT);

    // sdf_bus va en la direccion 0 (.origin 0), por la tabla de out pc.
    uint off_bus = pio_add_program(pio, &sdf_bus_program);
    uint off_est = pio_add_program(pio, &sdf_estado_program);
    sm_bus = pio_claim_unused_sm(pio, true);
    sm_est = pio_claim_unused_sm(pio, true);

    pio_sm_config c = sdf_bus_program_get_default_config(off_bus);
    sm_config_set_in_pin_base(&c, PIN_D0);
    sm_config_set_out_pins(&c, PIN_D0, 8);
    sm_config_set_jmp_pin(&c, PIN_IOSEL);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio, sm_bus, off_bus + sdf_bus_offset_entrada, &c);
    pio_sm_exec(pio, sm_bus, pio_encode_mov_not(pio_x, pio_null)); // X = FFh

    c = sdf_estado_program_get_default_config(off_est);
    sm_config_set_in_pin_base(&c, PIN_D0);
    sm_config_set_out_pins(&c, PIN_D0, 8);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio, sm_est, off_est, &c);  // RX en modo GET, por .fifo txget
    pio_sm_exec(pio, sm_est, pio_encode_set(pio_y, 1));

    pio_enable_sm_mask_in_sync(pio, (1u << sm_bus) | (1u << sm_est));

    // Espia, en PIO1: solo lee los pines.
    uint off_esp = pio_add_program(pio_espia, &espia_program);
    sm_espia = pio_claim_unused_sm(pio_espia, true);
    c = espia_program_get_default_config(off_esp);
    sm_config_set_in_pin_base(&c, PIN_D0);
    pio_sm_init(pio_espia, sm_espia, off_esp, &c);
    pio_sm_set_enabled(pio_espia, sm_espia, true);
}

// Imprime las 8 fotos de un acceso, tomadas cada ~220 ns desde la bajada de
// /IOSEL. Espera a que esten las 8: el FIFO unido tiene justo ese lugar.
static void espia_informe(void)
{
    uint nivel = pio_sm_get_rx_fifo_level(pio_espia, sm_espia);
    if (nivel < 8)
        return;
    printf("espia:\n");
    for (int i = 0; i < 8; i++) {
        uint32_t g = pio_sm_get(pio_espia, sm_espia);
        printf("  %5d ns  /IOSEL=%d A0=%d /RD=%d  D=%02lX\n", i * 220,
               (int)((g >> PIN_IOSEL) & 1), (int)((g >> PIN_A0) & 1),
               (int)((g >> PIN_RD) & 1), (unsigned long)((g >> PIN_D0) & 0xFF));
    }
}

static void estado(uint8_t v)
{
    pio->rxf_putget[sm_est][0] = v;
}

static void dma_init(void)
{
    for (int i = 0; i < 256; i++)
        contador[i] = (uint8_t)i;
    dma_tx = dma_claim_unused_channel(true);
}

static void dma_contador(void)
{
    dma_channel_config c = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_bus, true));
    dma_channel_configure(dma_tx, &c, &pio->txf[sm_bus], contador, 256, true);
}

int main(void)
{
    stdio_init_all();
    bus_init();
    if (SOLO_ESCUCHA)
        printf("SOLO_ESCUCHA: la Pico no maneja el bus\n");
    dma_init();
    estado(EST_VIVO);

    bool eco = false;
    uint8_t ultimo_est = 0;

    // Latido: una linea por segundo con el estado de los pines, para saber
    // que el firmware corre y que llega cada senal aunque la PIO no vea nada.
    // Las bajadas de /IOSEL las cuenta el nucleo mirando el pin: puede perder
    // alguna, alcanza para diagnostico.
    absolute_time_t prox_latido = make_timeout_time_ms(1000);
    uint32_t latidos = 0, bajadas = 0, escrituras = 0;
    bool iosel_ant = true;

    for (;;) {
        bool iosel = gpio_get(PIN_IOSEL);
        if (iosel_ant && !iosel)
            bajadas++;
        iosel_ant = iosel;

        espia_informe();

        if (time_reached(prox_latido)) {
            prox_latido = make_timeout_time_ms(1000);
            uint32_t g = gpio_get_all();
            printf("vivo %lu  /IOSEL=%d A0=%d /RD=%d EN=%d  D=%02lX  "
                   "bajadas=%lu escrituras=%lu\n",
                   (unsigned long)++latidos,
                   (int)((g >> PIN_IOSEL) & 1), (int)((g >> PIN_A0) & 1),
                   (int)((g >> PIN_RD) & 1), (int)((g >> PIN_EN) & 1),
                   (unsigned long)((g >> PIN_D0) & 0xFF),
                   (unsigned long)bajadas, (unsigned long)escrituras);
        }


        if (!pio_sm_is_rx_fifo_empty(pio, sm_bus)) {
            uint32_t w = pio_sm_get(pio, sm_bus);
            escrituras++;
            uint8_t dato = w & 0xFF;
            bool cmd = (w >> (PIN_A0 - PIN_D0)) & 1;

            if (cmd) {
                printf("OUT 01h = %02X\n", dato);
                dma_channel_abort(dma_tx);
                eco = false;
                switch (dato) {
                case CMD_ECO:      eco = true;     break;
                case CMD_CONTADOR: dma_contador(); break;
                case CMD_NADA:                     break;
                default: printf("  comando desconocido\n");
                }
            } else {
                printf("OUT 00h = %02X\n", dato);
                if (eco && !pio_sm_is_tx_fifo_full(pio, sm_bus))
                    pio_sm_put(pio, sm_bus, dato);
            }
        }

        uint8_t est = EST_VIVO | (pio_sm_get_tx_fifo_level(pio, sm_bus) & 0x07);
        if (dma_channel_is_busy(dma_tx))
            est |= EST_DMA;
        if (est != ultimo_est) {
            estado(est);
            ultimo_est = est;
        }
    }
}
