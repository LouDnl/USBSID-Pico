/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_engine.h
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

#ifndef _USBSID_MIDI_ENGINE_H_
#define _USBSID_MIDI_ENGINE_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Called from the core1 loop in usbsid.c, next to led_runner() and the SID
 * test queue drain. Drains whatever midi_queue_pop() has waiting and hands
 * each event to process_midi(), which is where the SID bus writes happen.
 * Safe to call every core1 iteration; it is a no-op when the ring is empty
 * or MIDI is disabled. */
void midi_engine_task(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_ENGINE_H_ */
