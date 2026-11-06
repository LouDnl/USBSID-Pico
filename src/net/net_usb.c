/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_usb.c
 * USB network (CDC-NCM) interface implementation: lwIP netif over TinyUSB
 * NCM, DHCP client / static / DHCP server addressing, NSD TCP listener and
 * UDP discovery responder.
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024-2026 LouD
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#if defined(USE_USB_NET)

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "tusb.h"

#include <globals.h>
#include <config.h>
#include <logging.h>
#include <mcu.h>
#include <nsd.h>
#include <net_config.h>
#include <net_usb.h>

#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/dhcp.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "dhserver.h"

#ifndef NSD_DEFAULT_PORT
#define NSD_DEFAULT_PORT 6581
#endif

#define NSD_DISCOVERY_MAGIC     "SidDevice"
#define NSD_DISCOVERY_MAGIC_LEN 9

#define USB_NET_HOSTNAME_DEFAULT "USBSID-Pico"

/* Received frames waiting for lwIP, filled inside tud_task() */
#define USB_NET_RX_QUEUE_N 8
/* Frames lwIP sent while no NTB had room, kept in order */
#define USB_NET_TX_FIFO_N  4


/* MAC address reported to the host in the NCM MAC string descriptor */
uint8_t tud_network_mac_address[6] = {0};

static bool mac_ready = false;

static struct netif usb_netif;
static bool usb_net_ready = false;  /* lwIP and netif initialized */
static bool link_up = false;        /* USB mounted and not suspended */

/* RX queue, single core: written in tud_network_recv_cb(), read in net_usb_task() */
static struct pbuf *rx_queue[USB_NET_RX_QUEUE_N];
static uint8_t rx_head = 0;
static uint8_t rx_count = 0;
static bool rx_stalled = false;     /* Driver holds a datagram until tud_network_recv_renew() */

/* TX FIFO of pbuf_ref()'d frames, flushed in net_usb_task() */
static struct pbuf *tx_fifo[USB_NET_TX_FIFO_N];
static uint8_t tx_head = 0;
static uint8_t tx_count = 0;
static uint32_t tx_drop_count = 0;

/* Addressing state */
typedef enum {
  ADDR_IDLE = 0,       /* Link down, no address */
  ADDR_DHCP_CLIENT,    /* DHCP client running, waiting for or holding a lease */
  ADDR_STATIC,         /* Static address from the applied IP config */
  ADDR_SERVER,         /* DHCP server mode: fixed address + DHCP server */
} addr_state_t;

static addr_state_t addr_state = ADDR_IDLE;
/* Applied IP config: flash config at init, net_cfg at net_usb_apply().
 * Sole source for addressing, unapplied net_cfg IP fields never used. */
static uint8_t addr_cfg_mode = USBNET_IP_MODE_DEFAULT;
static uint8_t addr_cfg_ip[4], addr_cfg_netmask[4], addr_cfg_gateway[4];
static uint32_t lease_logged_ip = 0;  /* Last logged DHCP lease, 0 = none */

#define INIT_IP4(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }

static bool dhserv_up = false;

/* DHCP server pool, in RAM: dhserver stores the client MAC per entry */
static dhcp_entry_t dhcp_entries[] = {
  { {0}, INIT_IP4(USB_NET_SUBNET_A, USB_NET_SUBNET_B, USB_NET_SUBNET_C, 2), 24 * 60 * 60 },
  { {0}, INIT_IP4(USB_NET_SUBNET_A, USB_NET_SUBNET_B, USB_NET_SUBNET_C, 3), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_server_config = {
  .router = INIT_IP4(0, 0, 0, 0),  /* No router option */
  .port = 67,
  .dns = INIT_IP4(0, 0, 0, 0),     /* No DNS option */
  .domain = NULL,
  .num_entry = TU_ARRAY_SIZE(dhcp_entries),
  .entries = dhcp_entries,
};

static struct tcp_pcb *nsd_listen_pcb = NULL;
static struct tcp_pcb *nsd_client_pcb = NULL;
static struct udp_pcb *discovery_pcb = NULL;
static nsd_transport_t tcp_transport;

/* Listener port bookkeeping. tcp_bind() fails with ERR_USE while an
 * accepted or TIME_WAIT pcb holds the port (SO_REUSE 0): retry from
 * net_usb_task() until it binds. */
static uint16_t nsd_listen_port = 0;      /* Port nsd_listen_pcb is bound to, 0 = closed */
static uint16_t nsd_listen_want = 0;      /* Requested listener port, 0 = listener off */
static uint16_t nsd_listen_fail_port = 0; /* Last port a bind failure was logged for */
static uint32_t nsd_listen_retry_ms = 0;
#define NSD_LISTEN_RETRY_MS 1000


/**
 * @brief Return the netif hostname from net_cfg
 *
 * @return const char * net_cfg.hostname, or the default when empty
 */
static const char *usb_net_hostname(void)
{
  return (net_cfg.hostname[0] != '\0') ? net_cfg.hostname : USB_NET_HOSTNAME_DEFAULT;
}

/* ==================== MAC address ==================== */

/**
 * @brief Fill `tud_network_mac_address` from the MCU unique id
 *
 * Byte 0 is 0x02 (locally administered, unicast). Bytes 1-5 are the low 5
 * id bytes, the top 3 id bytes folded in with XOR.
 */
void net_usb_mac_setup(void)
{
  if (mac_ready) return;

  uint64_t unique_id = mcu_get_unique_id();
  tud_network_mac_address[0] = 0x02;
  for (uint_fast8_t i = 0; i < 5; i++) {
    uint8_t b = (uint8_t)(unique_id >> (i * 8u));
    if (i < 3) b ^= (uint8_t)(unique_id >> ((i + 5) * 8u));
    tud_network_mac_address[1 + i] = b;
  }
  mac_ready = true;
}

/* ==================== RX queue / TX FIFO ==================== */

/**
 * @brief Free every queued RX frame and TX FIFO entry
 *
 */
static void usb_net_queues_reset(void)
{
  while (rx_count) {
    pbuf_free(rx_queue[rx_head]);
    rx_queue[rx_head] = NULL;
    rx_head = (uint8_t)((rx_head + 1) % USB_NET_RX_QUEUE_N);
    rx_count--;
  }
  rx_head = 0;
  rx_stalled = false;
  while (tx_count) {
    pbuf_free(tx_fifo[tx_head]);
    tx_fifo[tx_head] = NULL;
    tx_head = (uint8_t)((tx_head + 1) % USB_NET_TX_FIFO_N);
    tx_count--;
  }
  tx_head = 0;
}

/**
 * @brief Feed queued RX frames to lwIP, renew a stalled reception
 *
 */
static void usb_net_rx_drain(void)
{
  while (rx_count) {
    struct pbuf *p = rx_queue[rx_head];
    rx_queue[rx_head] = NULL;
    rx_head = (uint8_t)((rx_head + 1) % USB_NET_RX_QUEUE_N);
    rx_count--;
    if (usb_netif.input(p, &usb_netif) != ERR_OK) {
      pbuf_free(p);
    }
  }
  if (rx_stalled && tud_ready()) {
    rx_stalled = false;
    tud_network_recv_renew();  /* Fetch the datagram the driver holds */
  }
}

/**
 * @brief Copy TX FIFO frames into NTBs while the driver has room
 *
 */
static void usb_net_tx_flush(void)
{
  while (tx_count && tud_ready()) {
    struct pbuf *p = tx_fifo[tx_head];
    if (!tud_network_can_xmit(p->tot_len)) break;
    tud_network_xmit(p, 0);  /* Copies the frame at once */
    pbuf_free(p);
    tx_fifo[tx_head] = NULL;
    tx_head = (uint8_t)((tx_head + 1) % USB_NET_TX_FIFO_N);
    tx_count--;
  }
}

/* ==================== lwIP netif ==================== */

/**
 * @brief lwIP linkoutput: hand a frame to TinyUSB NCM
 *
 * Copy into an NTB at once when the FIFO is empty and the driver has room,
 * else queue a reference in the TX FIFO. Never runs tud_task().
 *
 * @param struct netif * netif unused
 * @param struct pbuf * p frame to send, lwIP keeps ownership
 * @return err_t ERR_OK, ERR_IF when USB is not ready, ERR_MEM when the FIFO is full
 */
static err_t usb_net_linkoutput(struct netif *netif, struct pbuf *p)
{
  (void)netif;
  if (!tud_ready()) return ERR_IF;

  if (tx_count == 0 && tud_network_can_xmit(p->tot_len)) {
    tud_network_xmit(p, 0);
    return ERR_OK;
  }
  if (tx_count < USB_NET_TX_FIFO_N) {
    pbuf_ref(p);
    tx_fifo[(tx_head + tx_count) % USB_NET_TX_FIFO_N] = p;
    tx_count++;
    return ERR_OK;
  }
  tx_drop_count++;
  if ((tx_drop_count & 0xFF) == 1) {  /* Log the first and every 256th drop */
    usUNET("TX FIFO full, %lu frames dropped\n", (unsigned long)tx_drop_count);
  }
  return ERR_MEM;
}

/**
 * @brief lwIP netif init callback
 *
 * @param struct netif * netif netif to set up
 * @return err_t ERR_OK
 */
static err_t usb_net_netif_init(struct netif *netif)
{
  netif->mtu = (u16_t)(CFG_TUD_NET_MTU - SIZEOF_ETH_HDR);
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
  netif->state = NULL;
  netif->name[0] = 'u';
  netif->name[1] = 'n';
  netif->linkoutput = usb_net_linkoutput;
  netif->output = etharp_output;
  /* Host and board MAC must differ: toggle the last bit */
  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;
  return ERR_OK;
}

/* ==================== Addressing ==================== */

/**
 * @brief Copy the net_cfg IP mode and static addresses to the applied IP config
 *
 */
static void usb_net_addr_cfg_take(void)
{
  addr_cfg_mode = net_cfg.flags.usbnet_ip_mode;
  memcpy(addr_cfg_ip, net_cfg.usbnet_ip, sizeof(addr_cfg_ip));
  memcpy(addr_cfg_netmask, net_cfg.usbnet_netmask, sizeof(addr_cfg_netmask));
  memcpy(addr_cfg_gateway, net_cfg.usbnet_gateway, sizeof(addr_cfg_gateway));
}

/**
 * @brief Resolve the IP mode to run from the applied IP config
 *
 * Static mode without a usable address and unknown values run as the
 * factory default (DHCP server).
 *
 * @return uint8_t USBNET_IP_MODE_DHCP_CLIENT, _STATIC or _DHCP_SERVER
 */
static uint8_t usb_net_ip_mode(void)
{
  uint8_t mode = addr_cfg_mode;
  if (mode == USBNET_IP_MODE_STATIC) {
    uint32_t ip;
    memcpy(&ip, addr_cfg_ip, sizeof(ip));
    if (ip != 0 && ip != 0xFFFFFFFFu) return mode;
    usUNET("Static IP %u.%u.%u.%u invalid, using default DHCP server mode\n",
      addr_cfg_ip[0], addr_cfg_ip[1], addr_cfg_ip[2], addr_cfg_ip[3]);
  } else if (mode == USBNET_IP_MODE_DHCP_SERVER || mode == USBNET_IP_MODE_DHCP_CLIENT) {
    return mode;
  } else {
    usUNET("Unknown IP mode %u, using default DHCP server mode\n", mode);
  }
  return USBNET_IP_MODE_DEFAULT;
}

/**
 * @brief Set the static address, netmask and gateway from the applied IP config
 *
 */
static void usb_net_static_start(void)
{
  const uint8_t *a = addr_cfg_ip, *m = addr_cfg_netmask, *g = addr_cfg_gateway;
  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip, a[0], a[1], a[2], a[3]);
  IP4_ADDR(&mask, m[0], m[1], m[2], m[3]);
  IP4_ADDR(&gw, g[0], g[1], g[2], g[3]);
  netif_set_addr(&usb_netif, &ip, &mask, &gw);
  usUNET("Static IP %u.%u.%u.%u/%u.%u.%u.%u gw %u.%u.%u.%u\n",
    a[0], a[1], a[2], a[3], m[0], m[1], m[2], m[3], g[0], g[1], g[2], g[3]);
}

/**
 * @brief Set the fixed server address and start the DHCP server
 *
 * Serve the USB link only: DHCP broadcasts never cross host routing or proxy
 * ARP. Do not bridge the USB network interface into a LAN that has its own
 * DHCP server in this mode.
 */
static void usb_net_server_start(void)
{
  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip, USB_NET_SUBNET_A, USB_NET_SUBNET_B, USB_NET_SUBNET_C, 1);
  IP4_ADDR(&mask, 255, 255, 255, 0);
  ip4_addr_set_zero(&gw);
  netif_set_addr(&usb_netif, &ip, &mask, &gw);
  dhserv_up = (dhserv_init(&dhcp_server_config) == ERR_OK);
  usUNET("DHCP server on USB link, %s/24\n", ip4addr_ntoa(&ip));
  if (!dhserv_up) usUNET("DHCP server start failed, retrying\n");
}

/**
 * @brief Start addressing in the IP mode from the applied IP config
 *
 * Client: dhcp_start(), lwIP retries DISCOVER with backoff until a lease.
 * Static: applied address at once. Server: fixed address + DHCP server.
 */
static void usb_net_addr_start(void)
{
  lease_logged_ip = 0;
  switch (usb_net_ip_mode()) {
    case USBNET_IP_MODE_STATIC:
      addr_state = ADDR_STATIC;
      usb_net_static_start();
      break;
    case USBNET_IP_MODE_DHCP_SERVER:
      addr_state = ADDR_SERVER;
      usb_net_server_start();
      break;
    default:
    {
      addr_state = ADDR_DHCP_CLIENT;
      err_t err = dhcp_start(&usb_netif);
      usUNET("DHCP client start (%d)\n", err);
      (void)err;
      break;
    }
  }
}

/**
 * @brief Stop DHCP client or server, clear the netif address
 *
 */
static void usb_net_addr_stop(void)
{
  if (addr_state == ADDR_DHCP_CLIENT) {
    dhcp_release_and_stop(&usb_netif);
  }
  if (addr_state == ADDR_SERVER) {
    dhserv_free();
    dhserv_up = false;
  }
  netif_set_addr(&usb_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
  addr_state = ADDR_IDLE;
}

/**
 * @brief Check net_cfg against the applied IP config
 *
 * @return bool true if the IP mode, or a static address in static mode, changed
 */
static bool usb_net_addr_changed(void)
{
  if (addr_cfg_mode != net_cfg.flags.usbnet_ip_mode) return true;
  if (addr_cfg_mode != USBNET_IP_MODE_STATIC) return false;
  return memcmp(addr_cfg_ip, net_cfg.usbnet_ip, sizeof(addr_cfg_ip)) != 0
      || memcmp(addr_cfg_netmask, net_cfg.usbnet_netmask, sizeof(addr_cfg_netmask)) != 0
      || memcmp(addr_cfg_gateway, net_cfg.usbnet_gateway, sizeof(addr_cfg_gateway)) != 0;
}

/**
 * @brief Per-loop addressing work: DHCP lease log, DHCP server retry
 *
 */
static void usb_net_addr_service(void)
{
  switch (addr_state) {
    case ADDR_DHCP_CLIENT:
      if (dhcp_supplied_address(&usb_netif)) {
        uint32_t ip = ip4_addr_get_u32(netif_ip4_addr(&usb_netif));
        if (ip != lease_logged_ip) {  /* Log each new lease once */
          usUNET("DHCP lease %s\n", ip4addr_ntoa(netif_ip4_addr(&usb_netif)));
          lease_logged_ip = ip;
        }
      } else {
        lease_logged_ip = 0;
      }
      break;
    case ADDR_SERVER:
      if (!dhserv_up) dhserv_up = (dhserv_init(&dhcp_server_config) == ERR_OK);
      break;
    default:
      break;
  }
}

/* ==================== NSD-over-TCP transport ==================== */

static void tcp_transport_send(void *ctx, const uint8_t *data, uint16_t len)
{
  struct tcp_pcb *pcb = (struct tcp_pcb *)ctx;
  if (!pcb || len == 0) return;
  if (tcp_sndbuf(pcb) >= len) {
    err_t err = tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
      tcp_output(pcb);
    }
  }
}

/* Returns tcp_close()'s result. On failure the pcb stays allocated with its
 * callbacks cleared: a caller outside an lwIP callback follows up with
 * tcp_abort(), a caller inside one ignores it (tcp_abort() there would need
 * an ERR_ABRT return). */
static err_t tcp_transport_close_pcb(struct tcp_pcb *pcb)
{
  if (!pcb) return ERR_OK;
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_sent(pcb, NULL);
  return tcp_close(pcb);
}

static void tcp_transport_close(void *ctx)
{
  struct tcp_pcb *pcb = (struct tcp_pcb *)ctx;
  tcp_transport_close_pcb(pcb);
  if (pcb == nsd_client_pcb) nsd_client_pcb = NULL;
}

static err_t nsd_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  (void)arg;
  if (p == NULL) {
    /* Remote closed the connection */
    nsd_session_close();
    tcp_transport_close_pcb(tpcb);
    if (tpcb == nsd_client_pcb) nsd_client_pcb = NULL;
    return ERR_OK;
  }
  if (err != ERR_OK) {
    pbuf_free(p);
    return err;
  }

  /* Feed pbuf by pbuf: nsd_feed() is a byte-stream state machine */
  for (struct pbuf *q = p; q != NULL; q = q->next) {
    nsd_feed((const uint8_t *)q->payload, q->len);
  }
  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

static void nsd_tcp_err(void *arg, err_t err)
{
  (void)arg;
  (void)err;
  /* lwIP already freed the pcb: drop the reference, end the session */
  nsd_session_close();
  nsd_client_pcb = NULL;
}

static err_t nsd_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  (void)arg;
  if (err != ERR_OK || newpcb == NULL) {
    return ERR_VAL;
  }

  tcp_transport.send = tcp_transport_send;
  tcp_transport.close = tcp_transport_close;
  tcp_transport.ctx = newpcb;

  if (!nsd_session_open(&tcp_transport)) {
    /* Single session only: reject and close the second connection */
    static const uint8_t busy_resp[1] = { 2 /* NSD_RESP_ERROR */ };
    tcp_write(newpcb, busy_resp, 1, TCP_WRITE_FLAG_COPY);
    tcp_output(newpcb);
    tcp_arg(newpcb, NULL);
    tcp_close(newpcb);
    return ERR_OK;
  }

  nsd_client_pcb = newpcb;
  tcp_nagle_disable(newpcb); /* Mandatory: Nagle adds ~40ms per 1KB packet */
  tcp_arg(newpcb, newpcb);
  tcp_recv(newpcb, nsd_tcp_recv);
  tcp_err(newpcb, nsd_tcp_err);
  return ERR_OK;
}

/**
 * @brief: Open the NSD TCP listener
 *
 * @param uint16_t port TCP port to listen on
 * @return bool true if listening
 */
static bool nsd_tcp_listen_start(uint16_t port)
{
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!pcb) {
    usNSD("tcp_new failed\n");
    return false;
  }
  err_t berr = tcp_bind(pcb, IP_ANY_TYPE, port);
  if (berr != ERR_OK) {
    if (port != nsd_listen_fail_port) { /* Log once per port, not per retry */
      usNSD("tcp_bind on port %u failed (%d), retrying\n", port, berr);
      nsd_listen_fail_port = port;
    }
    tcp_close(pcb);
    return false;
  }
  struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(pcb, 1);
  if (!listen_pcb) {
    usNSD("tcp_listen failed\n");
    tcp_close(pcb);
    return false;
  }
  nsd_listen_pcb = listen_pcb;
  nsd_listen_port = port;
  nsd_listen_fail_port = 0;
  tcp_accept(nsd_listen_pcb, nsd_tcp_accept);
  usNSD("TCP listener up on port %u\n", port);
  return true;
}

/**
 * @brief: Close the NSD TCP listener
 *
 * Leaves an accepted client pcb and its session untouched.
 */
static void nsd_tcp_listen_close(void)
{
  if (!nsd_listen_pcb) return;
  tcp_accept(nsd_listen_pcb, NULL);
  tcp_close(nsd_listen_pcb); /* Always ERR_OK for a LISTEN pcb */
  usNSD("TCP listener on port %u closed\n", nsd_listen_port);
  nsd_listen_pcb = NULL;
  nsd_listen_port = 0;
}

/**
 * @brief: End the TCP NSD client session and abort its pcb
 *
 * Board initiated close (NSD disable, link drop, address change): RST via
 * tcp_abort(), the pcb frees at once and the NSD port stays free for the
 * listener. Callbacks clear before tcp_abort(): no nsd_tcp_err() call, no
 * second nsd_session_close(). Main loop context only, not an lwIP callback.
 */
static void nsd_tcp_client_drop(void)
{
  if (!nsd_client_pcb) return;
  struct tcp_pcb *pcb = nsd_client_pcb;
  nsd_client_pcb = NULL;
  nsd_session_close();
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_abort(pcb); /* Frees pcb */
}

/**
 * @brief: Retry a failed listener bind, rate limited to NSD_LISTEN_RETRY_MS
 *
 */
static void nsd_tcp_listen_service(void)
{
  if (nsd_listen_want == 0 || nsd_listen_pcb) return;
  uint32_t now_ms = sys_now();
  if ((int32_t)(now_ms - nsd_listen_retry_ms) < 0) return;
  nsd_listen_retry_ms = now_ms + NSD_LISTEN_RETRY_MS;
  nsd_tcp_listen_start(nsd_listen_want);
}

/* ==================== UDP discovery responder ==================== */

static void udp_discovery_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  (void)arg;
  if (p == NULL) return;

  if (p->tot_len >= NSD_DISCOVERY_MAGIC_LEN) {
    uint8_t magic[NSD_DISCOVERY_MAGIC_LEN];
    pbuf_copy_partial(p, magic, NSD_DISCOVERY_MAGIC_LEN, 0);
    if (memcmp(magic, NSD_DISCOVERY_MAGIC, NSD_DISCOVERY_MAGIC_LEN) == 0) {
      char reply[96];
      int len = snprintf(reply, sizeof(reply), "SidDevice,%s,%s (fw %s)",
        usb_net_hostname(), us_product, project_version);
      if (len > 0) {
        if (len > (int)sizeof(reply)) len = sizeof(reply);
        struct pbuf *out = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
        if (out) {
          memcpy(out->payload, reply, (size_t)len);
          udp_sendto(pcb, out, addr, port);
          pbuf_free(out);
        }
      }
    }
  }
  pbuf_free(p);
}

/**
 * @brief: Open the UDP discovery responder, no-op if already open
 *
 * @param uint16_t port UDP port to bind
 */
static void udp_discovery_start(uint16_t port)
{
  if (discovery_pcb) return;
  struct udp_pcb *pcb = udp_new();
  if (!pcb) {
    usUNET("udp_new failed\n");
  } else if (udp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) {
    usUNET("udp_bind on port %u failed\n", port);
    udp_remove(pcb);
  } else {
    udp_recv(pcb, udp_discovery_recv, NULL);
    discovery_pcb = pcb;
    usUNET("UDP discovery responder up on port %u\n", port);
  }
}

/**
 * @brief: Close the UDP discovery responder, no-op if closed
 *
 */
static void udp_discovery_stop(void)
{
  if (!discovery_pcb) return;
  udp_remove(discovery_pcb);
  discovery_pcb = NULL;
  usUNET("UDP discovery responder closed\n");
}

/* ==================== NSD config apply ==================== */

/**
 * @brief: Listen for NSD clients on port, reopen the listener on change
 *
 * An accepted client session stays open. A failed bind retries from
 * net_usb_task().
 *
 * @param uint16_t port TCP port, 0 selects NSD_DEFAULT_PORT
 */
static void usb_net_set_nsd_port(uint16_t port)
{
  if (port == 0) port = NSD_DEFAULT_PORT;
  nsd_listen_want = port;
  if (nsd_listen_pcb && nsd_listen_port == port) return;
  nsd_tcp_listen_close();
  nsd_tcp_listen_start(port);
  nsd_listen_retry_ms = sys_now() + NSD_LISTEN_RETRY_MS;
}

/**
 * @brief: Apply nsd_enabled, nsd_port and discovery_enabled live
 *
 * nsd_enabled false closes the listener and ends a TCP NSD session.
 * Discovery always binds NSD_DEFAULT_PORT.
 *
 * @param const NetConfig *cfg network config to apply
 */
static void usb_net_apply_nsd_config(const NetConfig *cfg)
{
  if (cfg->flags.nsd_enabled) {
    usb_net_set_nsd_port(cfg->nsd_port);
  } else {
    nsd_listen_want = 0;
    nsd_tcp_listen_close();
    nsd_tcp_client_drop();
  }
  if (cfg->flags.discovery_enabled) {
    udp_discovery_start(NSD_DEFAULT_PORT);
  } else {
    udp_discovery_stop();
  }
}

/* ==================== Link state ==================== */

/**
 * @brief Bring the netif up or down with the USB link
 *
 * Up: link up, start addressing. Down: drop the NSD TCP session, stop
 * addressing, free queued frames, link down.
 *
 * @param bool up new link state
 */
static void usb_net_link_set(bool up)
{
  link_up = up;
  if (up) {
    usUNET("USB link up\n");
    netif_set_link_up(&usb_netif);
    usb_net_addr_start();
  } else {
    usUNET("USB link down\n");
    nsd_tcp_client_drop();
    usb_net_addr_stop();
    usb_net_queues_reset();
    netif_set_link_down(&usb_netif);
  }
}

/* ==================== Public API ==================== */

/**
 * @brief Initialize the USB network transport, core 0 only
 *
 */
void net_usb_init(void)
{
  usNFO("\n");
  usUNET("Init USB network (NCM)\n");
  load_net_config(&net_cfg);
  usb_net_addr_cfg_take();
  net_usb_mac_setup();
  nsd_init();

  lwip_init();
  netif_add(&usb_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4,
    NULL, usb_net_netif_init, ethernet_input);
  netif_set_hostname(&usb_netif, usb_net_hostname());
  netif_set_default(&usb_netif);
  netif_set_up(&usb_netif);
  usb_net_ready = true;

  /* Listener and discovery bind IP_ANY, addressing starts on link up */
  usb_net_apply_nsd_config(&net_cfg);
}

/**
 * @brief Service the USB network transport, core 0 loop after tud_task()
 *
 */
void net_usb_task(void)
{
  if (!usb_net_ready) return;

  bool up = tud_mounted() && !tud_suspended();
  if (up != link_up) usb_net_link_set(up);

  usb_net_tx_flush();
  usb_net_rx_drain();
  usb_net_tx_flush();
  sys_check_timeouts();
  usb_net_addr_service();
  nsd_tcp_listen_service();
}

/**
 * @brief Apply net_cfg live, core 0 only
 *
 */
void net_usb_apply(void)
{
  if (!usb_net_ready) return;
  usb_net_apply_nsd_config(&net_cfg);
  netif_set_hostname(&usb_netif, usb_net_hostname());
  bool addr_changed = usb_net_addr_changed();
  usb_net_addr_cfg_take();
  if (link_up && addr_changed) {
    usUNET("IP config changed, restarting addressing\n");
    nsd_tcp_client_drop();
    usb_net_addr_stop();
    usb_net_addr_start();
  }
}

/**
 * @brief Apply net_cfg NSD settings live, core 0 only
 *
 */
void net_usb_apply_nsd(void)
{
  if (!usb_net_ready) return;
  usb_net_apply_nsd_config(&net_cfg);
}

/**
 * @brief Return the USB network state
 *
 * @return uint8_t NET_USB_CODE_DOWN, _CONNECTING or _CONNECTED
 */
uint8_t net_usb_link_state(void)
{
  if (!usb_net_ready || !link_up) return NET_USB_CODE_DOWN;
  if (ip4_addr_isany_val(*netif_ip4_addr(&usb_netif))) return NET_USB_CODE_CONNECTING;
  return NET_USB_CODE_CONNECTED;
}

/**
 * @brief Return the IPv4 address of the USB netif
 *
 * @return uint32_t address in network byte order, 0 unless NET_USB_CODE_CONNECTED
 */
uint32_t net_usb_ip_addr(void)
{
  if (net_usb_link_state() != NET_USB_CODE_CONNECTED) return 0;
  return ip4_addr_get_u32(netif_ip4_addr(&usb_netif));
}

/* ==================== TinyUSB NCM callbacks ==================== */

/**
 * @brief TinyUSB NCM callback, invoked for each received datagram
 *
 * Copy the frame into a pool pbuf and queue it for net_usb_task(). Queue or
 * pool full: hold the datagram in the driver until net_usb_task() renews.
 *
 * @param const uint8_t * src frame data
 * @param uint16_t size frame length
 * @return bool true if the datagram is consumed, false if the driver holds it
 */
bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
  if (size == 0 || !usb_net_ready) {
    tud_network_recv_renew();
    return true;
  }
  if (rx_count >= USB_NET_RX_QUEUE_N) {
    rx_stalled = true;
    return false;
  }
  struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
  if (p == NULL) {
    rx_stalled = true;
    return false;
  }
  pbuf_take(p, src, size);
  rx_queue[(rx_head + rx_count) % USB_NET_RX_QUEUE_N] = p;
  rx_count++;
  tud_network_recv_renew();
  return true;
}

/**
 * @brief TinyUSB NCM callback, invoked by tud_network_xmit() to copy a frame
 *
 * @param uint8_t * dst NTB destination
 * @param void * ref struct pbuf * frame
 * @param uint16_t arg unused
 * @return uint16_t bytes copied
 */
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
  (void) arg;
  struct pbuf *p = (struct pbuf *)ref;
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

/**
 * @brief TinyUSB network callback, reset network state
 *
 * Free queued RX frames and TX FIFO entries.
 */
void tud_network_init_cb(void)
{
  if (usb_net_ready) usb_net_queues_reset();
}

#endif /* USE_USB_NET */
