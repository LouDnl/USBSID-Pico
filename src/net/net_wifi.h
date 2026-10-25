/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_wifi.h
 * WiFi network interface header file
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

#ifndef _USBSID_NET_WIFI_H_
#define _USBSID_NET_WIFI_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>


/* Call once from core 0's main(), after config load, before core 1 is
 * released and after net_bluetooth.c's setup_bluetooth() has brought up cyw43.
 * Does NOT power the radio or start station mode/lwIP, see
 * net_wifi_start(). */
void net_wifi_init(void);

/* Call once station mode should come up: after net_cfg is loaded and
 * net_cfg.flags.wifi_enabled is true. The one call that actually powers
 * the radio - confirmed to disrupt SID chip/model detection while
 * powered: an explicit opt-in instead of folding into
 * net_wifi_init(). No-op if called more than once. */
void net_wifi_start(void);

/* Call every core 0 main-loop iteration alongside cyw43_arch_poll(). A
 * no-op until net_wifi_start() has been called. Drives station
 * (re)connect. Does not drain the NSD write ring, that is
 * nsd_drain_task()'s job on core 1. Retries a failed NSD listener bind
 * regardless of radio state. */
void net_wifi_update(void);

struct NetConfig;

/* Listen for NSD clients on port (0 = NSD_DEFAULT_PORT), reopen the
 * listener on change. Keeps an accepted client session. Core 0 only */
void net_wifi_set_nsd_port(uint16_t port);

/* Apply cfg's nsd_enabled, nsd_port and discovery_enabled live. NSD off
 * closes the listener and ends a TCP NSD session. Core 0 only */
void net_wifi_apply_nsd_config(const struct NetConfig *cfg);

/* True once the station link is up and has an IP address */
bool net_wifi_is_connected(void);

/* WIFI_READ_IP state codes, see net_wifi_state_code() */
enum {
  NET_WIFI_CODE_OFF        = 0,  /* Radio off, net_wifi_start() not called */
  NET_WIFI_CODE_DOWN       = 1,  /* No link, waiting for (re)connect */
  NET_WIFI_CODE_CONNECTING = 2,  /* Join issued, waiting for link + DHCP */
  NET_WIFI_CODE_CONNECTED  = 3,  /* Link up with IP, net_wifi_is_connected() */
};

/* Return station state as a NET_WIFI_CODE_* value. Core 0 only */
uint8_t net_wifi_state_code(void);

/* Fill out[0..3] with the station IPv4 address, first octet in out[0].
 * All zero unless NET_WIFI_CODE_CONNECTED. Core 0 only */
void net_wifi_get_ip4(uint8_t out[4]);

/* Seams for flash-persisted credentials. Harmless no-ops/defaults until
 * NetConfig fills them in. */
void net_wifi_set_credentials(const char *ssid, const char *psk);
void net_wifi_set_hostname(const char *hostname);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_NET_WIFI_H_ */
