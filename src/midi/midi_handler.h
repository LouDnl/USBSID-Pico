/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_handler.h
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

#ifndef _MIDI_HANDLER_H_
#define _MIDI_HANDLER_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <midi_defs.h>  /* midi_ccvalues */


/* Functions from midi_handler.c */
void midi_processor_init(void);
void process_midi(uint8_t *buffer, int size);

/* Recomputes every channel's poly_limit against the SID count actually
 * present. Call once at boot after cfg.numsids is authoritative, before
 * the host is allowed to enumerate. */
void midi_config_sync_poly_limits(void);

/* The 1kHz modulation tick (LFO, portamento, arpeggiator). Called from
 * midi_engine_task() on core1, never from core0. */
void midi_tick(void);

/* Capture channel's current live timbre/LFO/arp/filter state into a patch
 * slot (RAM only) - the inverse of Program Change/apply_patch_to_channel().
 * Caller must range-check channel (< MAX_CHANNELS) and patch_index
 * (< MIDI_PATCH_COUNT) first. See sysex.c's handle_patch_save(). */
void midi_handler_capture_patch(uint8_t channel, uint8_t patch_index);

/* Live CC map, private to midi_handler.c. Setters validate, rebind the
 * dispatch table and stay RAM only, persistence goes through midi_config.c.
 * Fixed and unwired ids (embedded application, channel mode, placeholders)
 * never change. */
#define MIDI_CCMAP_RESET_DEFAULTS 0  /* compiled default map */
#define MIDI_CCMAP_RESET_CLEAR    1  /* every remappable id MIDI_CC_UNASSIGNED */

void midi_handler_get_ccmap(midi_ccvalues *out);
bool midi_handler_set_cc(uint8_t id, uint8_t cc);
bool midi_handler_set_ccmap(const midi_ccvalues *in);
void midi_handler_reset_ccmap(uint8_t mode);


#ifdef __cplusplus
  }
#endif

#endif /* _MIDI_HANDLER_H_ */
