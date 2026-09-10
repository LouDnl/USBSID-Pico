/*
 * lwipopts.h - USBSID-Pico Network SID Device (NSD) build
 *
 * Tuned for latency over throughput: the NSD write ring already absorbs
 * bursts (net_ring.c), lwIP just moves small packets off the wire quickly.
 * Based on pico-examples/pico_w/wifi/lwipopts_examples_common.h.
 */

#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

/* Bare-metal, single-threaded: driven entirely by cyw43_arch_poll() from
 * core 0 (pico_cyw43_arch_lwip_poll), never from an IRQ or a second core. */
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4

/* Pool sizes: generous enough for one NSD TCP session plus the UDP
 * discovery responder, not sized for throughput. */
#define MEM_SIZE                    16000
#define PBUF_POOL_SIZE              24
#define MEMP_NUM_PBUF               24
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_TCP_PCB            4
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_UDP_PCB            2
#define MEMP_NUM_SYS_TIMEOUT        8

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_NETIF_TX_SINGLE_PBUF   1

#define LWIP_TCP                    1
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)
#define LWIP_TCP_KEEPALIVE          1

#define LWIP_UDP                    1

#define LWIP_DHCP                   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_DNS                    0

#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1

/* pico_rand is already linked (CMakeLists.txt TARGET_LL) - use it instead
 * of stdlib rand() so this header doesn't need its own <stdlib.h>. */
#include "pico/rand.h"
#define LWIP_RAND() ((u32_t)get_rand_32())

#ifndef NDEBUG
#define LWIP_DEBUG                  1
#define LWIP_STATS                  0
#define UDP_DEBUG                   LWIP_DBG_ON
#define TCP_DEBUG                   LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_ON
#define IP_DEBUG                    LWIP_DBG_OFF
#define ETHARP_DEBUG                LWIP_DBG_ON
#endif

#endif /* _LWIPOPTS_H */
