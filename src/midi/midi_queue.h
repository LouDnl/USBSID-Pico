/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_queue.h
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

#ifndef _USBSID_MIDI_QUEUE_H_
#define _USBSID_MIDI_QUEUE_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>


/* A parsed channel voice message, handed from core0 (parsing) to core1
 * (the MIDI engine, which owns the SID bus writes). Single producer
 * (core0), single consumer (core1), so no lock is needed on the ring
 * itself, only on the SID bus both cores now write to (see bus.c). */
typedef struct {
  uint8_t status;  /* buffer[0] passed to process_midi() */
  uint8_t d1;      /* buffer[1], 0 if len < 2 */
  uint8_t d2;      /* buffer[2], 0 if len < 3 */
  uint8_t len;     /* 2 or 3, the `size` process_midi() expects */
} midi_event_t;

void midi_queue_init(void);

/* Core0 only. Returns false and drops the event if the ring is full. */
bool midi_queue_push(const uint8_t *buf, uint8_t len);

/* Core1 only. Returns false if the ring is empty. */
bool midi_queue_pop(midi_event_t *out);

/* Count of events dropped for a full ring since boot. Either core may read
 * this for diagnostics; it is not part of the SPSC contract above. */
uint32_t midi_queue_dropped(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_QUEUE_H_ */
