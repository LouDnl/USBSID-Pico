/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * bluetooth.h
 * Bluetooth Classic SPP transport for the Network SID Device (NSD) protocol
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

#ifndef _USBSID_BLUETOOTH_H_
#define _USBSID_BLUETOOTH_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdbool.h>

/* One-time BTstack/SPP service init, called once from usbsid.c's boot
 * sequence. Does not power the radio - see net_bt_set_power(). */
void setup_bluetooth(void);

/* True once an SPP RFCOMM channel is open to a client */
bool net_bt_is_connected(void);

/* Powers the Bluetooth radio on/off (hci_power_control()). Safe to call
 * repeatedly, but setup_bluetooth() must have run first. */
void net_bt_set_power(bool on);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_BLUETOOTH_H_ */
