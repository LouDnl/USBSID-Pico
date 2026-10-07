/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_patch.h
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

#ifndef _USBSID_MIDI_PATCH_H_
#define _USBSID_MIDI_PATCH_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>


/* 32 patches, selected by MIDI Program Change (0-31; 32-127 unmapped, no
 * Bank Select needed yet). */
#define MIDI_PATCH_COUNT 32
#define MIDI_PATCH_NONE  0xFF

/* A patch is the persistable subset of a channel's timbre. Deliberately
 * not the same shape as midi_channel_cfg_t, which also carries routing
 * and live runtime state a patch has no business touching. */
typedef struct {
  /* Per-voice register template, applied to the channel exactly the way a
   * CC_NOIS/CC_ATT/... change would: stamped onto new notes, pushed live
   * to whatever the channel already holds. Excludes the gate bit. */
  uint8_t  tmpl_contr;
  uint8_t  tmpl_attdec;
  uint8_t  tmpl_susrel;
  uint8_t  tmpl_pwmlo;
  uint8_t  tmpl_pwmhi;

  /* Chip-wide filter settings, written straight to every SID in the
   * channel's mask when the patch loads (RESFLT/FC_HI/FC_LO). */
  uint16_t filter_cutoff;   /* 0..CUTOFF_MAX (2047) */
  uint8_t  resonance;       /* 0..15 */
  uint8_t  filter_routing;  /* BIT_0=voice1, BIT_1=voice2, BIT_2=voice3, BIT_3=external, matches RESFLT's own bit layout */
  /* MODVOL's filter mode select bits (BIT_6=HPF, BIT_5=BPF, BIT_4=LPF).
   * A voice routed into the filter with none of these selected produces
   * no output at all - real SID behaviour, not a bug. */
  uint8_t  filter_mode;

  uint8_t  lfo_wave;
  uint8_t  lfo_rate;
  uint8_t  lfo_depth;
  uint8_t  lfo_dest;

  uint8_t  arp_mode;
  uint8_t  arp_rate;
  uint8_t  arp_octaves;

  uint8_t  bend_range;

  /* LFO 2 and unison fields: appended, not inserted, so an old (shorter)
   * SysEx dump (sysex.c pack_patch()/unpack_patch()) is refused by the
   * size check rather than misread. */
  uint8_t  lfo2_wave;
  uint8_t  lfo2_rate;
  uint8_t  lfo2_depth;
  uint8_t  lfo2_dest;
  uint8_t  unison_enabled;  /* 0/1 - a patch has no flags byte to fold this into */
  uint8_t  unison_detune;
} midi_patch_t;

extern midi_patch_t midi_patches[MIDI_PATCH_COUNT];

/* Patch 0 mirrors midi_config_init()'s channel defaults. Patches 1-31
 * start zeroed until loaded from flash or set another way. */
void midi_patch_init(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_PATCH_H_ */
