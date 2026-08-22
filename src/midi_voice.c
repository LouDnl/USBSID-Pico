/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_voice.c
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
#include <sid_defs.h>
#include <midi_config.h>
#include <midi_voice.h>


typedef struct {
  uint8_t             channel;   /* MIDI_CH_NONE = free */
  uint8_t             note;
  uint8_t             velocity;
  uint32_t            age;
  midi_voice_state_t  state;
  int32_t             cur_pitch_x256;     /* portamento position, note-index * 256 */
  int32_t             target_pitch_x256;
} voice_slot_t;

static voice_slot_t __not_in_flash("midi") voices[MIDI_VOICE_COUNT];
static uint32_t alloc_serial = 0;

void midi_voice_init(void)
{
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    voices[i].channel  = MIDI_CH_NONE;
    voices[i].note     = 0;
    voices[i].velocity = 0;
    voices[i].age      = 0;
    voices[i].state    = MIDI_VOICE_FREE;
    voices[i].cur_pitch_x256    = 0;
    voices[i].target_pitch_x256 = 0;
  }
  alloc_serial = 0;
  return;
}

uint8_t midi_voice_sidindex(uint8_t slot)
{
  return (uint8_t)(slot / MAX_VOICES);
}

uint8_t midi_voice_sidbase(uint8_t slot)
{
  return cfg.sidaddr[cfg.ids[midi_voice_sidindex(slot)]];
}

uint8_t midi_voice_regbase(uint8_t slot)
{
  return (uint8_t)((slot % MAX_VOICES) * VOICE_REGS);
}

uint8_t midi_voice_find(uint8_t channel, uint8_t note)
{
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (voices[i].state == MIDI_VOICE_GATED
      && voices[i].channel == channel
      && voices[i].note == note) {
      return i;
    }
  }
  return MIDI_VOICE_NONE;
}

/**
 * @brief Pick a voice to steal from a candidate set, per steal_mode
 *
 * @param uint16_t candidates, bitmask of slots eligible to be stolen from
 * @param uint8_t steal_mode, midi_steal_mode_t
 * @return uint8_t the chosen slot, or MIDI_VOICE_NONE if candidates is
 *         empty or steal_mode is MIDI_STEAL_NONE
 */
static uint8_t pick_steal(uint16_t candidates, uint8_t steal_mode)
{
  if (steal_mode == MIDI_STEAL_NONE) return MIDI_VOICE_NONE;

  uint8_t  best = MIDI_VOICE_NONE;
  uint32_t best_age = 0;
  uint8_t  best_note = 0;

  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (!(candidates & (1u << i))) continue;
    if (voices[i].state != MIDI_VOICE_GATED) continue;

    if (best == MIDI_VOICE_NONE) {
      best = i; best_age = voices[i].age; best_note = voices[i].note;
      continue;
    }
    switch (steal_mode) {
      case MIDI_STEAL_OLDEST:
        if (voices[i].age < best_age) { best = i; best_age = voices[i].age; }
        break;
      case MIDI_STEAL_LOWEST:
        if (voices[i].note < best_note) { best = i; best_note = voices[i].note; }
        break;
      case MIDI_STEAL_HIGHEST:
        if (voices[i].note > best_note) { best = i; best_note = voices[i].note; }
        break;
      default:
        break;
    }
  }
  return best;
}

uint8_t midi_voice_alloc(uint8_t channel, uint8_t note, uint8_t velocity)
{
  /* A note-on for a (channel, note) that is already gated - a key repeat,
   * or any fast re-strike that arrives before the matching note-off - must
   * retrigger the voice it is already holding, not allocate a second one.
   * Without this, two voices end up gated for the same key, and the one
   * note-off that eventually arrives (via midi_voice_find(), which returns
   * only the first match) frees just one of them: the other is orphaned
   * forever, since nothing will ever send a second note-off for the same
   * key. Reported stuck notes on real hardware playing chords in VMPK
   * traced to exactly this. */
  uint8_t existing = midi_voice_find(channel, note);
  if (existing != MIDI_VOICE_NONE) {
    voices[existing].velocity = velocity;
    voices[existing].age = ++alloc_serial;
    return existing;
  }

  uint16_t mask = midi_channel_effective_mask(channel);
  if (mask == 0) return MIDI_VOICE_NONE;

  uint8_t limit = midi_channels[channel].poly_limit;
  if (limit == 0) limit = 1;

  /* Count what this channel already holds, and build the bitmask of its
   * own held voices in the same pass: poly_limit is enforced by stealing
   * from the channel's own notes, never from another channel's, even if
   * masks happen to overlap. */
  uint8_t  held = 0;
  uint16_t own_gated = 0;
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (voices[i].state == MIDI_VOICE_GATED && voices[i].channel == channel) {
      held++;
      own_gated |= (1u << i);
    }
  }

  uint8_t slot = MIDI_VOICE_NONE;

  if (held >= limit) {
    /* At the channel's own polyphony limit: steal one of its own voices
     * regardless of whether a free slot exists elsewhere in the mask. */
    slot = pick_steal(own_gated, midi_channels[channel].steal_mode);
  } else {
    for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
      if ((mask & (1u << i)) && voices[i].state == MIDI_VOICE_FREE) { slot = i; break; }
    }
    if (slot == MIDI_VOICE_NONE) {
      /* No free slot despite being under the channel's own limit: the mask
       * itself is exhausted, which only happens if the configured masks of
       * two or more channels overlap. Steal from whoever is sitting in the
       * mask, still governed by this channel's steal_mode. */
      slot = pick_steal(mask, midi_channels[channel].steal_mode);
    }
  }

  if (slot == MIDI_VOICE_NONE) return MIDI_VOICE_NONE;

  voices[slot].channel  = channel;
  voices[slot].note     = note;
  voices[slot].velocity = velocity;
  voices[slot].age      = ++alloc_serial;
  voices[slot].state    = MIDI_VOICE_GATED;
  return slot;
}

void midi_voice_release(uint8_t slot)
{
  voices[slot].channel = MIDI_CH_NONE;
  voices[slot].note    = 0;
  voices[slot].state   = MIDI_VOICE_FREE;
  return;
}

midi_voice_state_t midi_voice_state(uint8_t slot)
{
  return voices[slot].state;
}

uint8_t midi_voice_channel(uint8_t slot)
{
  return voices[slot].channel;
}

uint8_t midi_voice_note(uint8_t slot)
{
  return voices[slot].note;
}

uint8_t midi_voice_velocity(uint8_t slot)
{
  return voices[slot].velocity;
}

void midi_voice_set_pitch(uint8_t slot, int32_t cur_x256, int32_t target_x256)
{
  voices[slot].cur_pitch_x256    = cur_x256;
  voices[slot].target_pitch_x256 = target_x256;
  return;
}

int32_t midi_voice_cur_pitch(uint8_t slot)
{
  return voices[slot].cur_pitch_x256;
}

int32_t midi_voice_target_pitch(uint8_t slot)
{
  return voices[slot].target_pitch_x256;
}
