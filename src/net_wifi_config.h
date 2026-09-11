/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_wifi_config.h
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

#ifndef _USBSID_NET_WIFI_CONFIG_H_
#define _USBSID_NET_WIFI_CONFIG_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <net_wifi.h>


void default_wifi_config(WifiConfig* config);
void save_wifi_config(WifiConfig *config);
void load_wifi_config(WifiConfig* config);
void start_wifi(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_NET_WIFI_CONFIG_H_ */
