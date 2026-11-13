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

/* Ring depth in entries, power of two, 16 KB RAM. Above NSD_ENTRIES_MIN
 * (nsd.h) it holds NSD_CYCLES_HIGH at up to ~480 writes per PAL frame */
#define NET_RING_ENTRIES 4096

/* Each entry is the raw 4-byte NSD wire quad: {cyc_hi, cyc_lo, reg, data} */

/* Initialize the ring buffer (clears all state) */
void net_ring_init(void);

/* Push a single 4-byte NSD operation quad {cyc_hi, cyc_lo, reg, data} into
 * the ring. Returns false if the ring is full (nothing pushed). */
bool net_ring_push(const uint8_t quad[4]);

/* Read the oldest 4-byte NSD operation quad without removing it. Consumer
 * only. Sets *dropped when net_ring_clear() dropped the entry: count it, do
 * not execute it. The entry stays queued until net_ring_advance(). Returns
 * false if the ring is empty. */
bool net_ring_peek(uint8_t quad[4], bool *dropped);

/* Remove the entry net_ring_peek() returned. Consumer only, call after its
 * bus write returned: the producer sees the ring idle only after that. */
void net_ring_advance(void);

/* Peek and advance in one call, for entries without a bus write */
bool net_ring_pop(uint8_t quad[4], bool *dropped);

/* True when the consumer has advanced past every entry, dropped ones
 * included: no entry queued, no entry in progress */
bool net_ring_idle(void);

/* Number of whole entries queued, not counting dropped entries */
uint32_t net_ring_count(void);

/* Number of dropped entries the consumer has not popped yet */
uint32_t net_ring_dropped(void);

/* Number of whole entries that can still be pushed before the ring is full.
 * Dropped entries keep their slot until the consumer pops them. */
uint32_t net_ring_free(void);

/* Drop everything queued, without executing it. Producer only: marks the
 * entries dropped, the consumer pops them with *dropped set */
void net_ring_clear(void);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_NET_RING_H_ */
