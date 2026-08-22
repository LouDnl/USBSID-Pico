/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_engine.c
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
#include <config.h>
#include <sid.h>
#include <midi_queue.h>
#include <midi_handler.h>
#include <midi_engine.h>


/* 1kHz modulation tick. Re-derived from time_us_64() rather than
 * incremented by a fixed period from the last scheduled time: a
 * flash_safe_execute() freeze on core1 (a config save) can stall this loop
 * for a while, and re-deriving means it resumes with exactly one tick
 * firing, phase picked up from wherever `now` actually is, rather than a
 * burst of catch-up ticks trying to make up for lost time. */
#define MIDI_TICK_PERIOD_US 1000
static uint64_t next_tick_us = 0;

void midi_engine_task(void)
{
  if __us_unlikely(get_reset_state()) return;
  if __us_unlikely(!usbsid_config.Midi.enabled) return;

  midi_event_t ev;
  /* Drain everything waiting rather than one per call: core1's loop also
   * services the LED runner, the SID test queue and (optionally) the SID
   * player, so a single event per pass could starve the ring under a fast
   * player next to it. */
  while (midi_queue_pop(&ev)) {
    uint8_t buf[3] = { ev.status, ev.d1, ev.d2 };
    process_midi(buf, ev.len);
  }

  uint64_t now = time_us_64();
  if __us_unlikely(next_tick_us == 0) {
    next_tick_us = now + MIDI_TICK_PERIOD_US;  /* first call: arm without firing */
  } else if (now >= next_tick_us) {
    midi_tick();
    next_tick_us = now + MIDI_TICK_PERIOD_US;
  }
  return;
}
