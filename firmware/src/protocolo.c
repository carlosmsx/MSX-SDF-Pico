// Protocolo con la DiskROM, en el nucleo 1.
//
// Cada escritura del MSX llega como una palabra del anillo de bus.c. Un byte
// en 01h es un comando: pone OCUPADO en el estado, descarta lo que hubiera
// para leer y junta los parametros que siguen en 00h. Con los parametros
// completos se ejecuta, se encola la respuesta por DMA y recien ahi se baja
// OCUPADO. La ROM espera OCUPADO en 0 antes de leer (docs/PROTOCOLO.md).
//
// La excepcion es CMD_DEBUG: la ROM lo manda entre sector y sector de un
// CMD_READ, con el checksum del sector. No corta la lectura en curso.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "bus.h"
#include "consola.h"
#include "disco.h"
#include "protocolo.h"

// ---- Parametros de cada comando ----

enum { P_FIJO, P_NOMBRE, P_ASCIIZ };

typedef struct {
    uint8_t tipo;
    uint8_t n;      // P_FIJO: cuantos; P_NOMBRE: fijos antes del largo
} params_t;

static params_t params_de(uint8_t cmd)
{
    switch (cmd) {
    case CMD_WRITE: case CMD_READ:      return (params_t){P_FIJO, 7};
    case CMD_DSKCHG: case CMD_DSKFMT:   return (params_t){P_FIJO, 1};
    case CMD_DEBUG:                     return (params_t){P_FIJO, 1};
    case CMD_RTCSETDA: case CMD_RTCSETTI: return (params_t){P_FIJO, 3};
    case CMD_SDFUMOUNT: case CMD_SDFDRIVES: return (params_t){P_FIJO, 1};
    case CMD_SDFMOUNT: case CMD_SDFNEW: return (params_t){P_NOMBRE, 1};
    case CMD_DEVNAME:                   return (params_t){P_ASCIIZ, 0};
    case CMD_DEVOPEN:                   return (params_t){P_NOMBRE, 2};
    case CMD_DEVCLOSE:                  return (params_t){P_FIJO, 2};
    case CMD_DEVOUT:                    return (params_t){P_FIJO, 1};
    default:                            return (params_t){P_FIJO, 0};
    }
}

// ---- Estado de la transaccion ----

enum { E_LIBRE, E_PARAMS, E_DATOS, E_ECO };

#define PAR_MAX 64

static int etapa = E_LIBRE;
static uint8_t cmd;
static uint8_t par[PAR_MAX];
static int npar;
static uint8_t resp[64];

// CMD_READ en curso: para comparar el checksum que manda CMD_DEBUG
static uint32_t lec_sector, lec_quedan;

// CMD_WRITE en curso
static uint32_t esc_sector, esc_quedan, esc_n;
static uint8_t esc_buf[SECTOR];

static uint8_t contador[256];

static struct {
    uint32_t lecturas, escrituras, sectores_leidos, sectores_escritos;
    uint32_t chk_ok, chk_mal, errores_flash;
} st;

static void listo(void)
{
    bus_estado(EST_FIJO);
}

static void ocupado(void)
{
    bus_estado(EST_FIJO | EST_OCUPADO);
}

static void responder(uint32_t n)
{
    bus_tx_enviar(resp, n);
}

static bool params_completos(void)
{
    params_t p = params_de(cmd);
    if (npar >= PAR_MAX)
        return true;
    switch (p.tipo) {
    case P_FIJO:   return npar >= p.n;
    case P_NOMBRE: return npar > p.n && npar >= p.n + 1 + par[p.n];
    case P_ASCIIZ: return npar > 0 && par[npar - 1] == 0;
    }
    return true;
}

// ---- Comandos del disco ----

static uint8_t dskio_validar(uint8_t drive, uint32_t sector, uint32_t n, bool escritura)
{
    if (drive != 0)
        return DSKIO_ERR_NOT_READY;
    // En blanco no se puede leer, pero si escribir: asi se formatea.
    if (!escritura && disco_media() == 0)
        return DSKIO_ERR_NOT_READY;
    uint32_t total = escritura ? DISCO_SECTORES_MAX : disco_sectores();
    if (n == 0 || sector + n > total)
        return DSKIO_ERR_RECORD_NOT_FOUND;
    return 0;
}

static void cmd_read(void)
{
    // drive, sectores, media, direccion (H, L), sector (H, L)
    uint32_t n = par[1], sector = (par[5] << 8) | par[6];
    uint8_t err = dskio_validar(par[0], sector, n, false);
    st.lecturas++;
    if (err) {
        con_printf("READ %lu+%lu: error %u\n", (unsigned long)sector,
                   (unsigned long)n, err);
        bus_tx_byte(err);
        return;
    }
    // El estado va por el FIFO, los sectores por DMA directo de la flash.
    bus_tx_byte(0);
    bus_tx_enviar(disco_ptr(sector), n * SECTOR);
    lec_sector = sector;
    lec_quedan = n;
    st.sectores_leidos += n;
}

static void cmd_debug(void)
{
    // Checksum XOR del sector que la ROM acaba de leer.
    if (lec_quedan == 0)
        return;
    const uint8_t *p = disco_ptr(lec_sector);
    uint8_t x = 0;
    for (int i = 0; i < SECTOR; i++)
        x ^= p[i];
    if (x == par[0]) {
        st.chk_ok++;
    } else {
        st.chk_mal++;
        con_printf("checksum del sector %lu: MSX %02X, Pico %02X\n",
                   (unsigned long)lec_sector, par[0], x);
    }
    lec_sector++;
    lec_quedan--;
}

static void cmd_write(void)
{
    uint32_t n = par[1], sector = (par[5] << 8) | par[6];
    uint8_t err = dskio_validar(par[0], sector, n, true);
    st.escrituras++;
    if (err) {
        con_printf("WRITE %lu+%lu: error %u\n", (unsigned long)sector,
                   (unsigned long)n, err);
        bus_tx_byte(err);
        return;
    }
    bus_tx_byte(0);
    esc_sector = sector;
    esc_quedan = n;
    esc_n = 0;
    etapa = E_DATOS;
}

static void dato_de_escritura(uint8_t b)
{
    // OCUPADO desde el primer byte de cada sector hasta que queda guardado:
    // la ROM nueva espera eso despues de cada sector.
    if (esc_n == 0)
        ocupado();
    esc_buf[esc_n++] = b;
    if (esc_n < SECTOR)
        return;

    esc_n = 0;
    if (!disco_escribir(esc_sector, esc_buf))
        st.errores_flash++;
    st.sectores_escritos++;
    esc_sector++;
    if (--esc_quedan == 0) {
        if (!disco_vaciar())
            st.errores_flash++;
        etapa = E_LIBRE;
    }
    listo();
}

// ---- Ejecucion ----

static void ejecutar(void)
{
    int n = 0;
    etapa = E_LIBRE;

    switch (cmd) {
    case CMD_READ:      cmd_read();  break;
    case CMD_WRITE:     cmd_write(); break;

    case CMD_DRIVES:    resp[n++] = 1; break;
    case CMD_DSKCHG:    resp[n++] = 1; break;           // sin cambio
    case CMD_DSKFMT: {
        // En blanco se formatea como 720 KB.
        uint8_t m = disco_media();
        resp[n++] = par[0] != 0 ? 0 : (m ? m : 0xF9);
        break;
    }

    case CMD_RTCCHK:    resp[n++] = 0; break;           // sin reloj
    case CMD_RTCGET:    memset(resp, 0, 7); n = 7; break;

    case CMD_SDFTEST:
        resp[n++] = PROTOCOL_VERSION;
        n += sprintf((char *)resp + n, "%s", FW_VERSION) + 1;
        break;
    case CMD_SDFDEBUG:
        n = snprintf((char *)resp, sizeof resp - 1,
                     "R %lu/%lu W %lu/%lu CHK %lu/%lu\r\n",
                     (unsigned long)st.lecturas, (unsigned long)st.sectores_leidos,
                     (unsigned long)st.escrituras, (unsigned long)st.sectores_escritos,
                     (unsigned long)st.chk_ok, (unsigned long)st.chk_mal) + 1;
        break;

    // Sin SD todavia: los CALL SDF... contestan "Disk offline".
    case CMD_SDFFILES:  resp[n++] = 0; break;           // lista vacia
    case CMD_SDFMOUNT: case CMD_SDFUMOUNT: case CMD_SDFNEW:
        resp[n++] = ERR_DISK_OFFLINE;
        break;
    case CMD_SDFDRIVES: resp[n++] = par[0] == 1 ? 0 : ERR_ILLEGAL_FUNCTION; break;

    // Sin dispositivos todavia: DEVNAME no reconoce ningun nombre.
    case CMD_DEVNAME:   resp[n++] = DEV_NONE; break;
    case CMD_DEVOPEN:   resp[n++] = ERR_DEVICE_IO; break;
    case CMD_DEVIN:     resp[n++] = 0; break;
    case CMD_DEVEOF:    resp[n++] = 0xFF; break;
    case CMD_DEVOUT:    if (par[0] == 13) resp[n++] = ERR_DEVICE_IO; break;

    // Prueba del bus
    case CMD_ECO:       etapa = E_ECO; break;
    case CMD_CONTADOR:  bus_tx_enviar(contador, sizeof contador); break;

    default:            break;  // INIHRD, INIENV, MTOFF, GETDPB...: sin respuesta
    }

    if (n)
        responder(n);
    listo();    // en CMD_WRITE, para que la ROM lea el estado antes de los datos
}

static void comando(uint8_t c)
{
    // CMD_DEBUG no corta la lectura en curso: solo junta su parametro.
    if (c != CMD_DEBUG) {
        ocupado();
        bus_tx_cortar();
        if (etapa == E_DATOS) {
            con_printf("WRITE cortado: faltaban %lu sectores\n",
                       (unsigned long)esc_quedan);
            disco_vaciar();
        }
        lec_quedan = 0;
    }
    cmd = c;
    npar = 0;
    etapa = E_PARAMS;
    if (params_completos())
        ejecutar();
}

static void dato(uint8_t b)
{
    switch (etapa) {
    case E_PARAMS:
        par[npar++] = b;
        if (params_completos()) {
            if (cmd == CMD_DEBUG) {
                cmd_debug();
                etapa = E_LIBRE;
            } else {
                ejecutar();
            }
        }
        break;
    case E_DATOS:
        dato_de_escritura(b);
        break;
    case E_ECO:
        bus_tx_byte(b);
        break;
    default:
        con_printf("dato suelto en 00h: %02X\n", b);
        break;
    }
}

void protocolo_core1(void)
{
    for (int i = 0; i < 256; i++)
        contador[i] = (uint8_t)i;

    for (;;) {
        listo();
        bus_despertar();
        con_printf("bus vivo: atendiendo 00h/01h\n");
        etapa = E_LIBRE;

        while (!bus_vigilar()) {
            while (bus_rx_hay()) {
                uint32_t w = bus_rx_leer();
                uint8_t b = w & 0xFF;
                if (w & (1u << BUS_A0))
                    comando(b);
                else
                    dato(b);
            }
        }

        if (etapa == E_DATOS)
            disco_vaciar();
        con_printf("bus muerto (MSX apagado?): Pico en espera\n");
    }
}
