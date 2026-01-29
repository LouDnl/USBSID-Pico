
/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * midi_patches.h
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

#ifndef _USBSID_MIDI_PATCHES_H_
#define _USBSID_MIDI_PATCHES_H_
#pragma once

#include "midi_defaults.h"

#ifdef __cplusplus
  extern "C" {
#endif

/* TODO:
 * - Add bank / patch switching
 * - Move fixed instruments to bank 1
 * - Add custom value insturments to bank 0
 */

/* Bank 0 ~ Patch 0 */
Instrument snare = {
  .attack = 0x0,
  .decay = 0x0,
  .sustain = 0xF,
  .release = 0x9,
  .has_pt = true,
  .has_wt = true,
  .wt[0].reg = GATE_ON(NOISE),
  .wt[0].pitch_offset = 0xC8,
  .wt[1].reg = GATE_ON(PULSEWAVE),
  .wt[1].pitch_offset = 0xAC,
  .wt[2].reg = GATE_ON(PULSEWAVE),
  .wt[2].pitch_offset = 0xA4,
  .wt[3].reg = NOISE,
  .wt[3].pitch_offset = 0xC4,
  .wt[4].reg = NOISE,
  .wt[4].pitch_offset = 0xC0,
  .wt[4].end = true,
  .pt[0].pw_ts = false,
  .pt[0].pw_lo = 0x0,
  .pt[0].pw_hi = 0x80,
  .pt[0].end = true,
};


/* Bank 0 */
static void default_patches(void)
{
  banks[0].ins[0] = snare;
}

void default_banks(void)
{
  default_patches(); /* Bank 0 */
}

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_PATCHES_H_ */
