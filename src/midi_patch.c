/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_patch.c
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
#include <midi_config.h>  /* MIDI_LFO_TRI, MIDI_LFO_DEST_PITCH, MIDI_ARP_UP */
#include <midi_patch.h>


midi_patch_t midi_patches[MIDI_PATCH_COUNT];

/**
 * @brief Initialise midi_patches[] with default and factory-preset patches
 *
 * Patch 0 mirrors midi_config_init()'s channel 1 defaults; patches 1-15
 * (bank 0) are curated presets; 16-31 (bank 1) stay zeroed for user saves.
 */
void midi_patch_init(void)
{
  memset(midi_patches, 0, sizeof(midi_patches));

  /* Patch 0 mirrors midi_config_init()'s channel defaults field for field,
   * so selecting it right after boot is a no-op. */
  midi_patches[0].tmpl_contr     = BIT_4;  /* triangle */
  midi_patches[0].tmpl_attdec    = 0x00;
  midi_patches[0].tmpl_susrel    = 0xF0;   /* full sustain, fast release */
  midi_patches[0].tmpl_pwmlo     = 0;
  midi_patches[0].tmpl_pwmhi     = 0;
  midi_patches[0].filter_cutoff  = 0;
  midi_patches[0].resonance      = 0;
  midi_patches[0].filter_routing = 0;
  midi_patches[0].lfo_wave       = MIDI_LFO_TRI;
  midi_patches[0].lfo_rate       = 32;
  midi_patches[0].lfo_depth      = 0;
  midi_patches[0].lfo_dest       = MIDI_LFO_DEST_PITCH;
  midi_patches[0].arp_mode       = MIDI_ARP_UP;
  midi_patches[0].arp_rate       = 64;
  midi_patches[0].arp_octaves    = 0;
  midi_patches[0].bend_range     = 2;
  midi_patches[0].lfo2_wave      = MIDI_LFO_TRI;
  midi_patches[0].lfo2_rate      = 32;
  /* lfo2_depth 0, lfo2_dest MIDI_LFO_DEST_PITCH (0), unison_enabled 0,
   * unison_detune 0 - already correct from the memset above, same as
   * lfo_depth/lfo_dest/arp_mode's own zero-is-correct fields. */

  /* Patches 1-15 (bank 0): curated basic instruments to play via Program
   * Change out of the box. ADSR nibbles: tmpl_attdec is attack|decay,
   * tmpl_susrel is sustain|release (set_adsr(), midi_handler.c).
   * filter_routing 0x07 routes all three physical voice positions through
   * the filter - RESFLT's routing bits are wired to position, not to a
   * logical note, so a patch needs all three set to reliably apply. */
  static const midi_patch_t bank0[] = {
    /* 1: Sawtooth lead */
    { .tmpl_contr = BIT_5, .tmpl_attdec = 0x00, .tmpl_susrel = 0xF0,
      .bend_range = 2 },
    /* 2: Pulse lead, ~50% duty (0x800 of 0xFFF) */
    { .tmpl_contr = BIT_6, .tmpl_pwmhi = 0x08, .tmpl_attdec = 0x00, .tmpl_susrel = 0xF0,
      .bend_range = 2 },
    /* 3: Bass - sawtooth, punchy decay, low-passed */
    { .tmpl_contr = BIT_5, .tmpl_attdec = 0x03, .tmpl_susrel = 0x80,
      .filter_cutoff = 400, .resonance = 4, .filter_routing = 0x07, .filter_mode = BIT_4, .bend_range = 2 },
    /* 4: Pluck - triangle, fast decay, no sustain */
    { .tmpl_contr = BIT_4, .tmpl_attdec = 0x00, .tmpl_susrel = 0x03,
      .bend_range = 2 },
    /* 5: Organ - triangle, slight attack, full sustain */
    { .tmpl_contr = BIT_4, .tmpl_attdec = 0x10, .tmpl_susrel = 0xF0,
      .bend_range = 2 },
    /* 6: Filtered lead - sawtooth through an open resonant filter */
    { .tmpl_contr = BIT_5, .tmpl_attdec = 0x00, .tmpl_susrel = 0xC0,
      .filter_cutoff = 1200, .resonance = 8, .filter_routing = 0x07, .filter_mode = BIT_4, .bend_range = 2 },
    /* 7: Slow pad - triangle, slow attack, long release */
    { .tmpl_contr = BIT_4, .tmpl_attdec = 0x90, .tmpl_susrel = 0xF5,
      .bend_range = 2 },
    /* 8: Noise hit - percussive, no sustain, no filter */
    { .tmpl_contr = BIT_7, .tmpl_attdec = 0x00, .tmpl_susrel = 0x03,
      .bend_range = 2 },
    /* 9: Reed - narrow pulse (~19% duty), moderate decay */
    { .tmpl_contr = BIT_6, .tmpl_pwmhi = 0x03, .tmpl_attdec = 0x02, .tmpl_susrel = 0xB0,
      .bend_range = 2 },
    /* 10: Sync lead - sawtooth with the sync bit set, for pairing with the
     *     adjacent voice; audible alone too, just without the sync effect */
    { .tmpl_contr = (uint8_t)(BIT_5 | BIT_1), .tmpl_attdec = 0x01, .tmpl_susrel = 0xE0,
      .bend_range = 2 },
    /* 11: Ring mod lead - triangle with the ring mod bit set, same caveat */
    { .tmpl_contr = (uint8_t)(BIT_4 | BIT_2), .tmpl_attdec = 0x00, .tmpl_susrel = 0xD0,
      .bend_range = 2 },
    /* 12: Vibrato lead - sawtooth, LFO on pitch */
    { .tmpl_contr = BIT_5, .tmpl_attdec = 0x00, .tmpl_susrel = 0xF0,
      .lfo_wave = MIDI_LFO_TRI, .lfo_rate = 40, .lfo_depth = 50, .lfo_dest = MIDI_LFO_DEST_PITCH,
      .bend_range = 2 },
    /* 13: Wah pad - triangle, LFO on cutoff */
    { .tmpl_contr = BIT_4, .tmpl_attdec = 0x20, .tmpl_susrel = 0xF3,
      .filter_cutoff = 800, .resonance = 10, .filter_routing = 0x07, .filter_mode = BIT_4,
      .lfo_wave = MIDI_LFO_TRI, .lfo_rate = 20, .lfo_depth = 80, .lfo_dest = MIDI_LFO_DEST_CUTOFF,
      .bend_range = 2 },
    /* 14: Wide bass - pulse, ~65% duty, punchy */
    { .tmpl_contr = BIT_6, .tmpl_pwmhi = 0x0A, .tmpl_attdec = 0x02, .tmpl_susrel = 0x90,
      .bend_range = 2 },
    /* 15: Bright pluck - sawtooth, open bright filter, fast release */
    { .tmpl_contr = BIT_5, .tmpl_attdec = 0x00, .tmpl_susrel = 0x05,
      .filter_cutoff = 1800, .resonance = 4, .filter_routing = 0x07, .filter_mode = BIT_4, .bend_range = 2 },
  };
  static_assert(count_of(bank0) == 15, "[MIDI PATCH] bank0 must define exactly patches 1-15");
  for (uint8_t i = 0; i < count_of(bank0); i++) {
    midi_patches[1 + i] = bank0[i];
  }

  /* Patches 16-31 (bank 1) stay zeroed: inert and silent until loaded from
   * flash or set another way. */

  return;
}
