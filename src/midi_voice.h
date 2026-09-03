/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_voice.h
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

#ifndef _USBSID_MIDI_VOICE_H_
#define _USBSID_MIDI_VOICE_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>
#include <sid_defs.h>  /* MAX_SIDS, MAX_VOICES, VOICE_REGS */


#define MIDI_VOICE_COUNT  (MAX_SIDS * MAX_VOICES)  /* 12 */
#define MIDI_VOICE_NONE   0xFF
#define MIDI_CH_NONE      0xFF

typedef enum {
  MIDI_VOICE_FREE = 0,
  MIDI_VOICE_GATED,
} midi_voice_state_t;

/* Pure bookkeeping: this file owns the pool's state machine and the
 * allocation policy, nothing else. It never touches sid_memory[] or the
 * bus; midi_handler.c does the register writes, using midi_voice_sidbase()
 * / midi_voice_regbase() to know where. */
void    midi_voice_init(void);

/* slot -> physical SID index (0..MAX_SIDS-1) and register addresses.
 * slot n is SID n/MAX_VOICES, voice n%MAX_VOICES within it. */
uint8_t midi_voice_sidindex(uint8_t slot);
uint8_t midi_voice_sidbase(uint8_t slot);   /* cfg.sidaddr[cfg.ids[sidindex]] */
uint8_t midi_voice_regbase(uint8_t slot);   /* (slot % MAX_VOICES) * VOICE_REGS */

/* Find the slot currently held by (channel, note), or MIDI_VOICE_NONE.
 * `note` is the raw MIDI note number (buffer[1]), matched as-is, so a
 * transpose change between a note-on and its note-off can never break the
 * match. */
uint8_t midi_voice_find(uint8_t channel, uint8_t note);

/* Unison claims MIDI_VOICE_UNISON_COUNT (3) slots per (channel, note)
 * instead of 1, all three tagged with the same key - normal single-voice
 * allocation never produces more than one match, so this is a safe
 * generalisation of midi_voice_find(), not a unison-specific function.
 * Writes up to MIDI_VOICE_UNISON_COUNT slot indices into `out` and returns
 * how many were found (0-3). */
#define MIDI_VOICE_UNISON_COUNT 3
uint8_t midi_voice_find_all(uint8_t channel, uint8_t note, uint8_t out_slots[MIDI_VOICE_UNISON_COUNT]);

/* Claim MIDI_VOICE_UNISON_COUNT adjacent slots on one SID together for
 * (channel, note) - a genuine tradeoff, not a bigger midi_voice_alloc():
 * that SID drops to 1 note of polyphony while the claim holds. First-pass
 * policy: refuse if no single SID in the channel's effective mask has all
 * 3 of its slots free (no stealing yet, simplest correct default). A repeat
 * note-on for an already-gated (channel, note) retriggers the same 3 slots,
 * same key-collision handling as midi_voice_alloc()'s own retrigger path.
 *
 * @param uint8_t out_slots[MIDI_VOICE_UNISON_COUNT], written regardless of
 *        outcome: MIDI_VOICE_NONE in every element on refusal.
 * @return uint8_t out_slots[0], or MIDI_VOICE_NONE on refusal
 */
uint8_t midi_voice_alloc_unison(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t out_slots[MIDI_VOICE_UNISON_COUNT]);

/* Allocate a slot for (channel, note): a free slot inside the channel's
 * effective mask if one exists and the channel is under its poly_limit;
 * otherwise steal per the channel's steal_mode. Returns MIDI_VOICE_NONE if
 * the channel's mask is empty or steal_mode is MIDI_STEAL_NONE and nothing
 * was free. Marks the slot MIDI_VOICE_GATED on success. */
uint8_t midi_voice_alloc(uint8_t channel, uint8_t note, uint8_t velocity);

/* Marks a slot MIDI_VOICE_FREE and forgets its channel/note. Does not touch
 * the bus; the caller is responsible for clearing the gate bit first if it
 * wants one cleared. */
void    midi_voice_release(uint8_t slot);

midi_voice_state_t midi_voice_state(uint8_t slot);
uint8_t             midi_voice_channel(uint8_t slot);
uint8_t             midi_voice_note(uint8_t slot);
uint8_t             midi_voice_velocity(uint8_t slot);

/* Portamento position, in note-index * 256 fixed point (8 fractional bits,
 * see MIDI_PITCH_FRAC_BITS in midi_config.h). Owned here because it is
 * per-voice runtime state, same as note/velocity/age; the glide policy
 * (how far to step per tick, where to start from) lives in midi_handler.c. */
void    midi_voice_set_pitch(uint8_t slot, int32_t cur_x256, int32_t target_x256);
int32_t midi_voice_cur_pitch(uint8_t slot);
int32_t midi_voice_target_pitch(uint8_t slot);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_VOICE_H_ */
