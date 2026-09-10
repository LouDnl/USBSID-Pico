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
 * released. Brings up cyw43 itself (unless USE_BLUETOOTH already owns
 * that). Does NOT power the radio or start station mode/lwIP, see
 * net_wifi_start(). */
void net_wifi_init(void);

/* Call once station mode should come up: after wifi_cfg is loaded and
 * wifi_cfg.flags.wifi_enabled is true. The one call that actually powers
 * the radio - confirmed to disrupt SID chip/model detection while
 * powered, hence the explicit opt-in instead of folding into
 * net_wifi_init(). No-op if called more than once. */
void net_wifi_start(void);

/* Call every core 0 main-loop iteration alongside cyw43_arch_poll(). A
 * no-op until net_wifi_start() has been called. Drives station
 * (re)connect; does not drain the NSD write ring, that is
 * nsd_drain_task()'s job on core 1. */
void net_wifi_update(void);

/* True once the station link is up and has an IP address */
bool net_wifi_is_connected(void);

/* Seams for flash-persisted credentials; harmless no-ops/defaults until
 * WifiConfig fills them in. */
void net_wifi_set_credentials(const char *ssid, const char *psk);
void net_wifi_set_hostname(const char *hostname);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_NET_WIFI_H_ */
