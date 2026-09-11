/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_wifi.c
 * WiFi network interface implementation: cyw43/lwIP lifecycle, the NSD TCP
 * listener and the UDP discovery responder.
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

#include <globals.h>
#include <net_wifi.h>
#include <nsd.h>
#include <config.h>
#include <gpio_defs.h>
#include <bus.h>
#include <logging.h>
#include <string.h>
#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"

#ifndef NSD_DEFAULT_PORT
#define NSD_DEFAULT_PORT 6581
#endif

/* Temporary credential source, until flash-persisted NetConfig calls
 * net_wifi_set_credentials() after loading the stored config. */
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#define NSD_DISCOVERY_MAGIC     "SidDevice"
#define NSD_DISCOVERY_MAGIC_LEN 9

/* Station connect state machine. Connecting is non-blocking
 * (cyw43_arch_wifi_connect_async() + per-tick cyw43_tcpip_link_status()
 * polling from net_wifi_update(), see its own comment for why). */
typedef enum {
  WSTATE_DOWN = 0,     /* No credentials, or link permanently failed */
  WSTATE_CONNECTING,   /* cyw43_wifi_join() issued, waiting for link status */
  WSTATE_CONNECTED,
} wifi_state_t;

static wifi_state_t wifi_state = WSTATE_DOWN;
static char wifi_ssid[33] = WIFI_SSID;
static char wifi_psk[64]  = WIFI_PASSWORD;
static char wifi_hostname[24] = "USBSID-Pico";
static uint64_t next_connect_attempt_ms = 0;
static uint64_t connect_deadline_ms = 0;
static int connect_last_status = -1; /* sentinel, no real CYW43_LINK_* value is negative */
#define WIFI_RECONNECT_BACKOFF_MS 5000
#define WIFI_CONNECT_TIMEOUT_MS   30000

static struct tcp_pcb *nsd_listen_pcb = NULL;
static struct tcp_pcb *nsd_client_pcb = NULL;
static struct udp_pcb *discovery_pcb = NULL;
static nsd_transport_t tcp_transport;

/* ==================== NSD-over-TCP transport ==================== */

static void tcp_transport_send(void *ctx, const uint8_t *data, uint16_t len)
{
  struct tcp_pcb *pcb = (struct tcp_pcb *)ctx;
  if (!pcb || len == 0) return;
  cyw43_arch_lwip_begin();
  if (tcp_sndbuf(pcb) >= len) {
    err_t err = tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
      tcp_output(pcb);
    } else {
      // usWFI("tcp_write failed (%d), %u bytes dropped\n", err, len);
    }
  } else {
    // usWFI("tcp_sndbuf too small (%u < %u), %u bytes dropped\n", tcp_sndbuf(pcb), len, len);
  }
  cyw43_arch_lwip_end();
}

static void tcp_transport_close_pcb(struct tcp_pcb *pcb)
{
  if (!pcb) return;
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_close(pcb);
}

static void tcp_transport_close(void *ctx)
{
  struct tcp_pcb *pcb = (struct tcp_pcb *)ctx;
  cyw43_arch_lwip_begin();
  tcp_transport_close_pcb(pcb);
  cyw43_arch_lwip_end();
  if (pcb == nsd_client_pcb) nsd_client_pcb = NULL;
}

static err_t nsd_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  (void)arg;
  if (p == NULL) {
    /* Remote closed the connection */
    // usWFI("TCP client closed connection\n");
    nsd_session_close();
    tcp_transport_close_pcb(tpcb);
    if (tpcb == nsd_client_pcb) nsd_client_pcb = NULL;
    return ERR_OK;
  }
  if (err != ERR_OK) {
    // usWFI("TCP recv err %d\n", err);
    pbuf_free(p);
    return err;
  }

  // usWFI("TCP recv %u bytes\n", p->tot_len);
  /* Feed pbuf-by-pbuf: nsd_feed() is a byte-stream state machine, there is
   * no need to coalesce a possibly-chained pbuf into one buffer first. */
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
  // usWFI("TCP connection error %d\n", err);
  /* lwIP has already freed the pcb by the time this callback fires - do
   * not touch it, just drop our reference and tear down the session. */
  nsd_session_close();
  nsd_client_pcb = NULL;
}

static err_t nsd_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  (void)arg;
  if (err != ERR_OK || newpcb == NULL) {
    // usWFI("TCP accept callback err %d\n", err);
    return ERR_VAL;
  }

  // usWFI("TCP connection from %s:%u\n",
  //   ip4addr_ntoa(ip_2_ip4(&newpcb->remote_ip)), newpcb->remote_port);

  tcp_transport.send = tcp_transport_send;
  tcp_transport.close = tcp_transport_close;
  tcp_transport.ctx = newpcb;

  if (!nsd_session_open(&tcp_transport)) {
    /* Single session only - reject and close the second connection
     * without ever feeding it. */
    // usWFI("session already active, rejecting new connection\n");
    static const uint8_t busy_resp[1] = { 2 /* NSD_RESP_ERROR */ };
    tcp_write(newpcb, busy_resp, 1, TCP_WRITE_FLAG_COPY);
    tcp_output(newpcb);
    tcp_arg(newpcb, NULL);
    tcp_close(newpcb);
    return ERR_OK;
  }

  // usWFI("session opened, ready for commands\n");
  nsd_client_pcb = newpcb;
  tcp_nagle_disable(newpcb); /* Mandatory: Nagle adds ~40ms per 1KB packet */
  tcp_arg(newpcb, newpcb);
  tcp_recv(newpcb, nsd_tcp_recv);
  tcp_err(newpcb, nsd_tcp_err);
  return ERR_OK;
}

static void nsd_tcp_listen_start(uint16_t port)
{
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!pcb) {
    usNSD("tcp_new failed\n");
    return;
  }
  if (tcp_bind(pcb, IP_ANY_TYPE, port) != ERR_OK) {
    usNSD("tcp_bind on port %u failed\n", port);
    tcp_close(pcb);
    return;
  }
  struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(pcb, 1);
  if (!listen_pcb) {
    usNSD("tcp_listen failed\n");
    tcp_close(pcb);
    return;
  }
  nsd_listen_pcb = listen_pcb;
  tcp_accept(nsd_listen_pcb, nsd_tcp_accept);
  usNSD("TCP listener up on port %u\n", port);
}

/* ==================== UDP discovery responder ==================== */

static void udp_discovery_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  (void)arg;
  if (p == NULL) return;

  // usWFI("UDP datagram from %s:%u, %u bytes\n",
  //   ip4addr_ntoa(ip_2_ip4(addr)), port, p->tot_len);

  if (p->tot_len >= NSD_DISCOVERY_MAGIC_LEN) {
    uint8_t magic[NSD_DISCOVERY_MAGIC_LEN];
    pbuf_copy_partial(p, magic, NSD_DISCOVERY_MAGIC_LEN, 0);
    if (memcmp(magic, NSD_DISCOVERY_MAGIC, NSD_DISCOVERY_MAGIC_LEN) == 0) {
      char reply[96];
      int len = snprintf(reply, sizeof(reply), "SidDevice,%s,%s (fw %s)",
        wifi_hostname, us_product, project_version);
      if (len > 0) {
        if (len > (int)sizeof(reply)) len = sizeof(reply);
        struct pbuf *out = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
        if (out) {
          memcpy(out->payload, reply, (size_t)len);
          err_t serr = udp_sendto(pcb, out, addr, port);
          (void)serr;
          // usWFI("discovery reply -> %s:%u (%d)\n",
          //   ip4addr_ntoa(ip_2_ip4(addr)), port, serr);
          pbuf_free(out);
        }
      }
    } else {
      // usWFI("UDP magic mismatch, ignored\n");
    }
  }
  pbuf_free(p);
}

static void udp_discovery_start(uint16_t port)
{
  discovery_pcb = udp_new();
  if (!discovery_pcb) {
    usWFI("udp_new failed\n");
    return;
  }
  if (udp_bind(discovery_pcb, IP_ADDR_ANY, port) != ERR_OK) {
    usWFI("udp_bind on port %u failed\n", port);
    return;
  }
  udp_recv(discovery_pcb, udp_discovery_recv, NULL);
  usWFI("UDP discovery responder up on port %u\n", port);
}

/* ==================== Station lifecycle ==================== */

static bool wifi_started = false;

/**
 * @brief Finish net bring-up after bluetooth.c's setup_bluetooth() has
 *        already brought up cyw43 itself
 *
 * When USE_NET == 1, bluetooth.c/setup_bluetooth() calls
 * cyw43_arch_init() (called first in the boot sequence, usbsid.c).
 * This function never calls it.
 * pio.c's setup_vu() likewise skips its own cyw43_arch_init(),
 * so the LED GPIO is set here instead.
 */
void net_wifi_init(void)
{
  usNFO("\n");
  usWFI("Init WiFi\n");

  /* Starts OFF: the LED tracks actual connection status
   * (net_wifi_update()), not just whether the board is powered. */
  cyw43_arch_gpio_put(BUILTIN_LED, false);

  /* cyw43_arch_init()'s cyw43_driver_init()+lwip_init() sequence (CYW43_LWIP=1)
   * desyncs the bus PIO's timing, confirmed on v1.3/v1.5 hardware.
   * This requires a full restart of the BUS for everything to work again */
  restart_bus();

  nsd_init();
  nsd_tcp_listen_start(NSD_DEFAULT_PORT);
  udp_discovery_start(NSD_DEFAULT_PORT);
}

/**
 * @brief Actually power the WiFi radio and bring up station mode
 *
 * Radio power (cyw43_arch_enable_sta_mode()) disrupts SID chip/model
 * detection for as long as it stays powered, confirmed on real hardware.
 * Call only once net_cfg.flags.wifi_enabled is true, never unconditionally.
 */
void net_wifi_start(void)
{
  if (wifi_started) return;
  wifi_started = true;

  cyw43_arch_enable_sta_mode();
  /* Same defensive resync as net_wifi_init(): powering the radio does
   * substantial cyw43 hardware work of its own. */
  restart_bus();

  netif_set_hostname(netif_default, wifi_hostname);

  if (wifi_ssid[0] != '\0') {
    /* Arms net_wifi_update() to connect on its next tick. */
    wifi_state = WSTATE_DOWN;
    next_connect_attempt_ms = 0;
  } else {
    usWFI("no WiFi credentials configured, staying associated-only\n");
  }
}

static uint32_t wifi_auth(void)
{
  return wifi_psk[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
}

static void wifi_connect_fail(const char *why, int code)
{
  usWFI("WiFi connect failed (%s, %d), retrying in %ums\n", why, code, WIFI_RECONNECT_BACKOFF_MS);
  wifi_state = WSTATE_DOWN;
  next_connect_attempt_ms = time_us_64() / 1000 + WIFI_RECONNECT_BACKOFF_MS;
}

/**
 * @brief Per-loop update: station (re)connect state machine
 *
 * Runs on whichever core calls cyw43_arch_poll() (usbsid.c's main loop).
 * No-op until net_wifi_start() has been called.
 *
 * Non-blocking connect: cyw43_arch_wifi_connect_async() issued once, then
 * WSTATE_CONNECTING polls cyw43_tcpip_link_status() each tick until
 * UP/FAIL/BADAUTH or WIFI_CONNECT_TIMEOUT_MS. A blocking connect call here
 * stalls tud_task_ext() long enough to starve USB EP0 and panic TinyUSB's
 * dcd - do not go back to one.
 *
 * Reissuing the join is restricted to CYW43_LINK_NONET, matching
 * pico-sdk's own cyw43_arch_wifi_connect_bssid_until(): reissuing while
 * still in JOIN/NOIP calls cyw43_wifi_join() on top of a handshake the
 * chip is still running, which corrupts its internal state even when the
 * eventual link status reports clean UP. Keep the NONET-only rule if this
 * is ever touched again. */
void net_wifi_update(void)
{
  if (!wifi_started) return;   /* Radio never powered - see net_wifi_start() */
  if (wifi_ssid[0] == '\0') return; /* Not configured yet */

  switch (wifi_state) {
    case WSTATE_CONNECTED: {
      int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
      if (link != CYW43_LINK_UP) {
        usWFI("WiFi link dropped\n");
        wifi_state = WSTATE_DOWN;
        next_connect_attempt_ms = time_us_64() / 1000 + WIFI_RECONNECT_BACKOFF_MS;
        if (nsd_session_is_active()) nsd_session_close();
      }
      break;
    }

    case WSTATE_CONNECTING: {
      int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

      if (status == CYW43_LINK_NONET) {
        /* Only case where a reissue is safe - see the function comment. */
        int err = cyw43_arch_wifi_connect_async(wifi_ssid, wifi_psk, wifi_auth());
        if (err) { wifi_connect_fail("rejoin", err); break; }
        status = CYW43_LINK_JOIN;
      }

      if (status != connect_last_status) {
        usWFI("WiFi connect status: %d\n", status);
        connect_last_status = status;
      }

      if (status == CYW43_LINK_UP) {
        wifi_state = WSTATE_CONNECTED;
        usWFI("WiFi connected, IP %s\n",
          netif_default ? ip4addr_ntoa(netif_ip4_addr(netif_default)) : "?");
        /* Default PM (PM2 powersave) stops the CYW43439 retrieving
         * AP-buffered frames. Set fresh after every connect - setting it
         * before the join has no effect, the join resets PM state. */
        int pm_err = cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
        usWFI("cyw43_wifi_pm(CYW43_NONE_PM) returned %d\n", pm_err);
      } else if (status == CYW43_LINK_BADAUTH) {
        wifi_connect_fail("bad auth", status);
      } else if (status == CYW43_LINK_FAIL) {
        wifi_connect_fail("link fail", status);
      } else if ((time_us_64() / 1000) >= connect_deadline_ms) {
        wifi_connect_fail("timeout", status);
      }
      /* else: still JOIN/NOIP, keep waiting - no reissue, no state change. */
      break;
    }

    case WSTATE_DOWN:
    default:
      if ((time_us_64() / 1000) >= next_connect_attempt_ms) {
        usWFI("connecting to '%s' (%s)\n", wifi_ssid, wifi_psk[0] ? "WPA2" : "open");
        int err = cyw43_arch_wifi_connect_async(wifi_ssid, wifi_psk, wifi_auth());
        if (err) {
          wifi_connect_fail("join", err);
        } else {
          wifi_state = WSTATE_CONNECTING;
          connect_last_status = -1;
          connect_deadline_ms = time_us_64() / 1000 + WIFI_CONNECT_TIMEOUT_MS;
        }
      }
      break;
  }
}

bool net_wifi_is_connected(void)
{
  return wifi_state == WSTATE_CONNECTED;
}

void net_wifi_set_credentials(const char *ssid, const char *psk)
{
  strncpy(wifi_ssid, ssid ? ssid : "", sizeof(wifi_ssid) - 1);
  wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
  strncpy(wifi_psk, psk ? psk : "", sizeof(wifi_psk) - 1);
  wifi_psk[sizeof(wifi_psk) - 1] = '\0';
}

void net_wifi_set_hostname(const char *hostname)
{
  if (!hostname || !hostname[0]) return;
  strncpy(wifi_hostname, hostname, sizeof(wifi_hostname) - 1);
  wifi_hostname[sizeof(wifi_hostname) - 1] = '\0';
  if (netif_default) netif_set_hostname(netif_default, wifi_hostname);
}
