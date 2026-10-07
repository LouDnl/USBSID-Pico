/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_queue.c
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
#include <macros.h>
#include <midi_queue.h>


/* 128 entries, 512 bytes. Deliberately not queue_t: that takes a spinlock
 * per entry, which is exactly the per-message cost this ring exists to
 * avoid. One slot is always left empty so head == tail is unambiguously
 * "empty" without a separate count. */
#define MIDI_QUEUE_SIZE 128
#define MIDI_QUEUE_MASK (MIDI_QUEUE_SIZE - 1)

static midi_event_t __not_in_flash("midi") ring[MIDI_QUEUE_SIZE];

/* head: written only by the producer (core0), read by both.
 * tail: written only by the consumer (core1), read by both. */
static volatile uint8_t ring_head = 0;
static volatile uint8_t ring_tail = 0;
static volatile uint32_t queue_dropped = 0;

/**
 * @brief Reset the MIDI ring buffer to empty
 *
 * Clears head, tail and the dropped-event counter.
 */
void midi_queue_init(void)
{
  ring_head = 0;
  ring_tail = 0;
  queue_dropped = 0;
  return;
}

/**
 * @brief Push one MIDI event onto the ring buffer
 *
 * @note single-producer: must only be called from core0 (the USB callback)
 * @note a data memory barrier separates the entry write from publishing
 *       the new head, so the consumer never sees a half-written entry
 *
 * @param uint8_t const * buf
 * @param uint8_t len number of valid bytes in buf (1 to 3)
 * @return bool true if the event was queued, false if the ring was full
 */
bool midi_queue_push(const uint8_t *buf, uint8_t len)
{
  uint8_t h = ring_head;
  uint8_t next = (uint8_t)((h + 1) & MIDI_QUEUE_MASK);
  if __us_unlikely(next == ring_tail) {
    /* Ring full: the engine is not draining fast enough. Drop the newest
     * event rather than block core0's USB callback on the SID bus. */
    queue_dropped++;
    return false;
  }
  ring[h].status = buf[0];
  ring[h].d1 = (len > 1 ? buf[1] : 0);
  ring[h].d2 = (len > 2 ? buf[2] : 0);
  ring[h].len = len;
  __dmb();  /* entry must be visible before the consumer sees the new head */
  ring_head = next;
  return true;
}

/**
 * @brief Pop one MIDI event off the ring buffer
 *
 * @note single-consumer: must only be called from core1
 * @note a data memory barrier pairs with the producer's barrier, ordering
 *       the entry read after the head is observed to have advanced
 *
 * @param midi_event_t * out
 * @return bool true if an event was popped, false if the ring was empty
 */
bool midi_queue_pop(midi_event_t *out)
{
  uint8_t t = ring_tail;
  if (t == ring_head) return false;  /* empty */
  __dmb();  /* pairs with the producer's barrier, orders the entry read after it */
  *out = ring[t];
  ring_tail = (uint8_t)((t + 1) & MIDI_QUEUE_MASK);
  return true;
}

/**
 * @brief Get the number of MIDI events dropped due to a full ring buffer
 *
 * @return uint32_t total dropped event count since the last midi_queue_init()
 */
uint32_t midi_queue_dropped(void)
{
  return queue_dropped;
}
