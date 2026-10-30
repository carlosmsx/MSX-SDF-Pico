#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pins.h"
#include "bus.h"
#include "bus.pio.h"

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

// Con el MSX apagado todo el bus esta en 0, /IOSEL incluido: la PIO lo
// tomaria como una lectura que no termina y quedaria manejando el bus sobre
// un '245 sin alimentacion. Las SM solo corren con el bus vivo: se arrancan
// cuando /IOSEL lleva 100 ms en alto, y se detienen si queda en bajo mas de
// 50 us, cosa que ningun ciclo de I/O real hace (dura menos de 1 us).
#define BUS_VIVO_MS     100
#define BUS_MUERTO_US   50

// Anillo de recepcion: 8192 palabras (32 KB). Con la ROM que no espera entre
// sectores, alcanza para ~120 ms de escrituras mientras se graba la flash.
#define RX_BITS         15
#define RX_PALABRAS     (1u << (RX_BITS - 2))

static PIO pio = pio0;
static uint sm_bus, sm_est;
static uint off_bus, off_est;
static int dma_rx, dma_tx;
// Va en los ultimos 32 KB de la RAM (ld/section_heap.incl): en la .bss, su
// alineacion empujaba toda la seccion y se perdian hasta 32 KB de heap.
static uint32_t rx_anillo[RX_PALABRAS] __attribute__((section(".anillo"), aligned(1u << RX_BITS)));
static uint32_t rx_leido;       // indice de la proxima palabra a leer

static PIO pio_espia = pio1;
static uint sm_espia;           // solo con DIAGNOSTICO

void bus_init(void)
{
    // Bus de datos: PIO, sin pulls (errata E9 del A2). Del lado B del '245
    // solo hay entradas: flanco lento y 2 mA alcanzan y meten menos ruido
    // en /IOSEL, A0 y /RD, que van en los pines de al lado.
    for (uint pin = PIN_D0; pin < PIN_D0 + 8; pin++) {
        if (SOLO_ESCUCHA)
            gpio_init(pin);
        else
            pio_gpio_init(pio, pin);
        gpio_disable_pulls(pin);
        gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
    }
    pio_sm_set_consecutive_pindirs(pio, 0, PIN_D0, 8, false);

    // /IOSEL, A0 y /RD: entradas, con Schmitt. La PIO las lee sin importar
    // la funcion.
    for (uint pin = PIN_IOSEL; pin <= PIN_RD; pin++) {
        gpio_init(pin);
        gpio_disable_pulls(pin);
        gpio_set_input_hysteresis_enabled(pin, true);
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
    sm_config_set_in_pin_base(&c, PIN_IOSEL);  // lee /IOSEL, A0 y /RD
    sm_config_set_out_pins(&c, PIN_D0, 8);
    sm_config_set_out_shift(&c, true, false, 32);
    pio_sm_init(pio, sm_est, off_est, &c);  // RX en modo GET, por .fifo txget
    // Las SM arrancan en bus_despertar(), cuando el MSX esta encendido.

    // Recepcion: del RX FIFO de sdf_bus al anillo, sin fin.
    dma_rx = dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_ring(&d, true, RX_BITS);
    channel_config_set_dreq(&d, pio_get_dreq(pio, sm_bus, false));
    dma_channel_configure(dma_rx, &d, rx_anillo, &pio->rxf[sm_bus],
                          dma_encode_endless_transfer_count(), true);

    // Respuesta: de memoria al TX FIFO, de a un byte.
    dma_tx = dma_claim_unused_channel(true);

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

static uint32_t rx_escrito(void)
{
    uintptr_t w = (uintptr_t)dma_hw->ch[dma_rx].write_addr;
    return (uint32_t)((w - (uintptr_t)rx_anillo) / 4);
}

bool bus_rx_hay(void)
{
    return rx_leido != rx_escrito();
}

uint32_t bus_rx_leer(void)
{
    uint32_t w = rx_anillo[rx_leido];
    rx_leido = (rx_leido + 1) & (RX_PALABRAS - 1);
    return w;
}

void bus_tx_cortar(void)
{
    // pio_sm_drain_tx_fifo ejecuta pull noblock en la SM: es seguro porque
    // entre accesos esta esperando la bajada de /IOSEL, y en entrada el OSR
    // se recarga con los pines.
    dma_channel_abort(dma_tx);
    pio_sm_drain_tx_fifo(pio, sm_bus);
}

void bus_tx_byte(uint8_t b)
{
    pio_sm_put(pio, sm_bus, b);
}

void bus_tx_enviar(const void *datos, uint32_t n)
{
    dma_channel_config d = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_8);
    channel_config_set_read_increment(&d, true);
    channel_config_set_write_increment(&d, false);
    channel_config_set_dreq(&d, pio_get_dreq(pio, sm_bus, true));
    dma_channel_configure(dma_tx, &d, &pio->txf[sm_bus], datos, n, true);
}

bool bus_tx_pendiente(void)
{
    return dma_channel_is_busy(dma_tx) || !pio_sm_is_tx_fifo_empty(pio, sm_bus);
}

void bus_estado(uint8_t v)
{
    pio->rxf_putget[sm_est][0] = v;
}

static void bus_dormir(void)
{
    pio_set_sm_mask_enabled(pio, (1u << sm_bus) | (1u << sm_est), false);
    pio_sm_set_consecutive_pindirs(pio, sm_bus, PIN_D0, 8, false);
    dma_channel_abort(dma_tx);
}

void bus_despertar(void)
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
    rx_leido = rx_escrito();    // lo que haya quedado en el anillo no vale
    pio_enable_sm_mask_in_sync(pio, (1u << sm_bus) | (1u << sm_est));
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

bool bus_vigilar(void)
{
    static bool bajo;
    static uint32_t desde;

    if (gpio_get(PIN_IOSEL)) {
        bajo = false;
        return false;
    }
    if (!bajo) {
        bajo = true;
        desde = time_us_32();
        return false;
    }
    if (time_us_32() - desde <= BUS_MUERTO_US)
        return false;

    // Entre las dos lecturas pudo haber una tarea larga (grabar la flash):
    // confirmar mirando el pin sin pausa antes de dar el bus por muerto.
    bajo = false;
    if (!iosel_bajo_sostenido(BUS_MUERTO_US))
        return false;
    bus_dormir();
    return true;
}

void bus_diagnostico(void)
{
    if (!DIAGNOSTICO)
        return;

    // Espia: las 8 fotos de un acceso, cada ~220 ns desde la bajada de /IOSEL.
    if (pio_sm_get_rx_fifo_level(pio_espia, sm_espia) >= 8) {
        printf("espia:\n");
        for (int i = 0; i < 8; i++) {
            uint32_t g = pio_sm_get(pio_espia, sm_espia);
            printf("  %5d ns  /IOSEL=%d A0=%d /RD=%d  D=%02lX\n", i * 220,
                   (int)((g >> PIN_IOSEL) & 1), (int)((g >> PIN_A0) & 1),
                   (int)((g >> PIN_RD) & 1), (unsigned long)((g >> PIN_D0) & 0xFF));
        }
    }

    // Latido: el estado de los pines una vez por segundo.
    static absolute_time_t prox;
    static uint32_t latidos;
    if (time_reached(prox)) {
        prox = make_timeout_time_ms(1000);
        uint32_t g = gpio_get_all();
        printf("vivo %lu  /IOSEL=%d A0=%d /RD=%d EN=%d  D=%02lX\n",
               (unsigned long)++latidos,
               (int)((g >> PIN_IOSEL) & 1), (int)((g >> PIN_A0) & 1),
               (int)((g >> PIN_RD) & 1), (int)((g >> PIN_EN) & 1),
               (unsigned long)((g >> PIN_D0) & 0xFF));
    }
}
