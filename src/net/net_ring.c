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
 * only ever touches its own index and observes the other's with a barrier.
 * net_ring_clear() keeps that rule: the producer only moves `clear_idx`,
 * read_idx stays consumer-owned. */
#define NET_RING_BYTES (NET_RING_ENTRIES * 4)
static uint8_t __not_in_flash("nsd") ring_buf[NET_RING_BYTES] __aligned(4);

/* Indices count entries, not bytes, and are left to free-run: the gap
 * `write_idx - read_idx` is always the live entry count even after either
 * counter wraps past UINT32_MAX, so there is no separate empty/full flag to
 * keep in sync. */
static volatile uint32_t write_idx = 0;
static volatile uint32_t read_idx = 0;

/* Producer-owned drop mark, set by net_ring_clear(): entries below it are
 * dropped. The consumer still pops them, flagged, and frees their slots;
 * a slot is never reused before the consumer moved past it. */
static volatile uint32_t clear_idx = 0;

/**
 * @brief Index of the oldest entry not dropped by net_ring_clear()
 *
 * @return uint32_t free-running entry index
 */
static inline uint32_t net_ring_live_tail(void)
{
  uint32_t r = read_idx;
  uint32_t c = clear_idx;
  return ((int32_t)(c - r) > 0) ? c : r;
}

void net_ring_init(void)
{
  memset(ring_buf, 0, sizeof(ring_buf));
  write_idx = 0;
  read_idx = 0;
  clear_idx = 0;
}

uint32_t net_ring_count(void)
{
  return write_idx - net_ring_live_tail();
}

uint32_t net_ring_dropped(void)
{
  uint32_t r = read_idx;
  uint32_t c = clear_idx;
  return ((int32_t)(c - r) > 0) ? (c - r) : 0;
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

bool net_ring_peek(uint8_t quad[4], bool *dropped)
{
  uint32_t r = read_idx;
  if (r == write_idx) {
    return false; /* Ring empty */
  }
  __dmb(); /* Read the entry only after the write index that exposed it */

  uint32_t byte_off = (r % NET_RING_ENTRIES) * 4;
  quad[0] = ring_buf[byte_off + 0];
  quad[1] = ring_buf[byte_off + 1];
  quad[2] = ring_buf[byte_off + 2];
  quad[3] = ring_buf[byte_off + 3];
  *dropped = ((int32_t)(clear_idx - r) > 0);
  return true;
}

void net_ring_advance(void)
{
  uint32_t r = read_idx;
  if (r == write_idx) {
    return; /* Ring empty, nothing peeked */
  }
  __dmb(); /* Finish the entry and its bus write before freeing the slot */
  read_idx = r + 1;
}

bool net_ring_pop(uint8_t quad[4], bool *dropped)
{
  if (!net_ring_peek(quad, dropped)) {
    return false;
  }
  net_ring_advance();
  return true;
}

bool net_ring_idle(void)
{
  return (read_idx == write_idx);
}

void net_ring_clear(void)
{
  clear_idx = write_idx;
}
