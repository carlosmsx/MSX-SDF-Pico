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
static uint off_bus, off_est;
static PIO pio_espia = pio1;
static uint sm_espia;   // solo con DIAGNOSTICO
static int dma_tx;
static uint8_t contador[256];

// SOLO_ESCUCHA=1: los pines de datos quedan como entradas del SIO, asi que la
// PIO sigue leyendo y registrando todo pero nunca maneja el bus. Para probar
// el cableado sin riesgo de pisar un ciclo del MSX (sdf_pico_escucha.uf2).
#ifndef SOLO_ESCUCHA
#define SOLO_ESCUCHA 0
#endif

// DIAGNOSTICO=1: latido por segundo y espia (8 fotos del bus por acceso).
// Imprime mucho: solo para buscar fallas de cableado o de tiempos.
#ifndef DIAGNOSTICO
#define DIAGNOSTICO 0
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
    off_bus = pio_add_program(pio, &sdf_bus_program);
    off_est = pio_add_program(pio, &sdf_estado_program);
    sm_bus = pio_claim_unused_sm(pio, true);
    sm_est = pio_claim_unused_sm(pio, true);

    pio_sm_config c = sdf_bus_program_get_default_config(off_bus);
    sm_config_set_in_pin_base(&c, PIN_D0);
    sm_config_set_out_pins(&c, PIN_D0, 8);
    sm_config_set_jmp_pin(&c, PIN_IOSEL);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio, sm_bus, off_bus + sdf_bus_offset_entrada, &c);

    c = sdf_estado_program_get_default_config(off_est);
    sm_config_set_in_pin_base(&c, PIN_D0);
    sm_config_set_out_pins(&c, PIN_D0, 8);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio, sm_est, off_est, &c);  // RX en modo GET, por .fifo txget
    // Las SM arrancan en bus_despertar(), cuando el MSX esta encendido.

    if (!DIAGNOSTICO)
        return;

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

// Con el MSX apagado todo el bus esta en 0, /IOSEL incluido: la PIO lo
// tomaria como una lectura que no termina y quedaria manejando el bus sobre
// un '245 sin alimentacion. Las SM solo corren con el bus vivo: se arrancan
// cuando /IOSEL lleva 100 ms en alto, y se detienen si queda en bajo mas de
// 50 us, cosa que ningun ciclo de I/O real hace (dura menos de 1 us).
#define BUS_VIVO_MS     100
#define BUS_MUERTO_US   50

static void bus_dormir(void)
{
    pio_set_sm_mask_enabled(pio, (1u << sm_bus) | (1u << sm_est), false);
    pio_sm_set_consecutive_pindirs(pio, sm_bus, PIN_D0, 8, false);
}

// true si /IOSEL se mantiene en bajo, sin ninguna subida, durante us.
static bool iosel_bajo_sostenido(uint32_t us)
{
    uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < us) {
        if (gpio_get(PIN_IOSEL))
            return false;
    }
    return true;
}

static void bus_despertar(void)
{
    // Esperar /IOSEL en alto sin interrupcion durante BUS_VIVO_MS.
    absolute_time_t desde = get_absolute_time();
    while (absolute_time_diff_us(desde, get_absolute_time()) < BUS_VIVO_MS * 1000) {
        if (!gpio_get(PIN_IOSEL))
            desde = get_absolute_time();
    }

    pio_sm_clear_fifos(pio, sm_bus);
    pio_sm_restart(pio, sm_bus);
    pio_sm_restart(pio, sm_est);
    pio_sm_exec(pio, sm_bus, pio_encode_jmp(off_bus + sdf_bus_offset_entrada));
    pio_sm_exec(pio, sm_bus, pio_encode_mov_not(pio_x, pio_null)); // X = FFh
    pio_sm_exec(pio, sm_est, pio_encode_jmp(off_est));
    pio_sm_exec(pio, sm_est, pio_encode_set(pio_y, 2));
    pio_enable_sm_mask_in_sync(pio, (1u << sm_bus) | (1u << sm_est));
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
    bus_despertar();
    printf("bus vivo: atendiendo 00h/01h\n");
    uint32_t iosel_bajo_desde = 0;
    bool iosel_bajo = false;

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

        if (iosel) {
            iosel_bajo = false;
        } else if (!iosel_bajo) {
            iosel_bajo = true;
            iosel_bajo_desde = time_us_32();
        } else if (time_us_32() - iosel_bajo_desde > BUS_MUERTO_US) {
            // Entre las dos lecturas pudo haber un printf largo: confirmar
            // mirando el pin sin pausa antes de dar el bus por muerto.
            if (iosel_bajo_sostenido(BUS_MUERTO_US)) {
                bus_dormir();
                dma_channel_abort(dma_tx);
                eco = false;
                printf("bus muerto (MSX apagado?): Pico en espera\n");
                bus_despertar();
                printf("bus vivo: atendiendo 00h/01h\n");
            }
            iosel_bajo = false;
        }

        if (DIAGNOSTICO)
            espia_informe();

        if (DIAGNOSTICO && time_reached(prox_latido)) {
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
                // Un comando nuevo descarta lo que haya quedado para leer de
                // 00h. El drenaje ejecuta pull noblock en la SM: es seguro
                // porque ahora esta esperando la proxima bajada de /IOSEL, y
                // en entrada el OSR se recarga con los pines.
                dma_channel_abort(dma_tx);
                pio_sm_drain_tx_fifo(pio, sm_bus);
                eco = false;
                printf("OUT 01h = %02X\n", dato);
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
