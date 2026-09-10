/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_ring.c
 * Single-producer single-consumer ring buffer for NSD write operations
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
#include <net_ring.h>
#include <string.h>

/* Ring buffer storage - in flash-unfriendly region, single producer (core 0
 * receive path) / single consumer (core 1 nsd_drain_task). No lock: the
 * plain read/write index pair below is safe under SPSC as long as each side
 * only ever touches its own index and observes the other's with a barrier. */
#define NET_RING_BYTES (NET_RING_ENTRIES * 4)
static uint8_t __not_in_flash("nsd") ring_buf[NET_RING_BYTES] __aligned(4);

/* Indices count entries, not bytes, and are left to free-run: the gap
 * `write_idx - read_idx` is always the live entry count even after either
 * counter wraps past UINT32_MAX, so there is no separate empty/full flag to
 * keep in sync. */
static volatile uint32_t write_idx = 0;
static volatile uint32_t read_idx = 0;

void net_ring_init(void)
{
  memset(ring_buf, 0, sizeof(ring_buf));
  write_idx = 0;
  read_idx = 0;
}

uint32_t net_ring_count(void)
{
  return write_idx - read_idx;
}

uint32_t net_ring_free(void)
{
  return NET_RING_ENTRIES - (write_idx - read_idx);
}

bool net_ring_push(const uint8_t quad[4])
{
  if ((write_idx - read_idx) >= NET_RING_ENTRIES) {
    return false; /* Ring full - caller is responsible for BUSY backpressure */
  }

  uint32_t byte_off = (write_idx % NET_RING_ENTRIES) * 4;
  ring_buf[byte_off + 0] = quad[0];
  ring_buf[byte_off + 1] = quad[1];
  ring_buf[byte_off + 2] = quad[2];
  ring_buf[byte_off + 3] = quad[3];
  __dmb(); /* Entry must be visible before the write index that exposes it */
  write_idx++;
  return true;
}

bool net_ring_pop(uint8_t quad[4])
{
  if (read_idx == write_idx) {
    return false; /* Ring empty */
  }

  uint32_t byte_off = (read_idx % NET_RING_ENTRIES) * 4;
  quad[0] = ring_buf[byte_off + 0];
  quad[1] = ring_buf[byte_off + 1];
  quad[2] = ring_buf[byte_off + 2];
  quad[3] = ring_buf[byte_off + 3];
  __dmb(); /* Finish reading the entry before advancing past it */
  read_idx++;
  return true;
}

void net_ring_clear(void)
{
  read_idx = write_idx;
}
