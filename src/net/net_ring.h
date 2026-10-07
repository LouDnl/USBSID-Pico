/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_ring.h
 * Single-producer single-consumer ring buffer header file
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

#ifndef _USBSID_NET_RING_H_
#define _USBSID_NET_RING_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Ring buffer configuration - match NSD_RING_ENTRIES for consistency */
#if PICO_RP2350
#define NET_RING_ENTRIES 4096
#else
#define NET_RING_ENTRIES 2048
#endif

/* Each entry is the raw 4-byte NSD wire quad: {cyc_hi, cyc_lo, reg, data} */

/* Initialize the ring buffer (clears all state) */
void net_ring_init(void);

/* Push a single 4-byte NSD operation quad {cyc_hi, cyc_lo, reg, data} into
 * the ring. Returns false if the ring is full (nothing pushed). */
bool net_ring_push(const uint8_t quad[4]);

/* Pop the next 4-byte NSD operation quad from the ring.
 * Returns false if the ring is empty. */
bool net_ring_pop(uint8_t quad[4]);

/* Number of whole entries currently queued */
uint32_t net_ring_count(void);

/* Number of whole entries that can still be pushed before the ring is full */
uint32_t net_ring_free(void);

/* Drop everything queued, without executing it */
void net_ring_clear(void);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_NET_RING_H_ */
