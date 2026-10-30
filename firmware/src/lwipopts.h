// lwIP para el Wi-Fi, sin sistema operativo: corre en las interrupciones del
// nucleo 0 (pico_cyw43_arch_lwip_threadsafe_background).

#pragma once

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    16000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

#define LWIP_IPV4                   1
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3

#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

// SNTP (reloj.c): la hora de Internet al conectar y despues cada hora.
#define SNTP_SERVER_DNS             1
#define SNTP_UPDATE_DELAY           (60 * 60 * 1000)
#include <stdint.h>
void reloj_internet(uint32_t utc);
#define SNTP_SET_SYSTEM_TIME(sec)   reloj_internet(sec)
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 3)

// MQTT (mqtt.c): una publicacion entera tiene que entrar en el anillo de
// salida, y el topico de lo que llega en el bufer de encabezado.
#define MQTT_OUTPUT_RINGBUF_SIZE    1024
#define MQTT_VAR_HEADER_BUFFER_LEN  300
#define MQTT_REQ_MAX_IN_FLIGHT      8
