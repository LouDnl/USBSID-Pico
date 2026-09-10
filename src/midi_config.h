/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_config.h
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

#ifndef _USBSID_MIDI_CONFIG_H_
#define _USBSID_MIDI_CONFIG_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <sid_defs.h>   /* MAX_CHANNELS, MAX_SIDS, MAX_VOICES */
#include <midi_defs.h>  /* midi_ccvalues, for the persisted blob below */
#include <midi_patch.h> /* midi_patch_t, MIDI_PATCH_COUNT, for the persisted blob below */
#include <midi_arp_table.h> /* midi_arp_table_t, MIDI_ARP_TABLE_COUNT, for the persisted blob below */


typedef enum {
  MIDI_STEAL_OLDEST = 0,  /* lowest age, i.e. the note held longest */
  MIDI_STEAL_LOWEST,      /* lowest raw note number among held voices */
  MIDI_STEAL_HIGHEST,     /* highest raw note number among held voices */
  MIDI_STEAL_NONE,        /* refuse the new note instead of stealing */
} midi_steal_mode_t;

typedef enum {
  MIDI_AT_FILTER = 0,
  MIDI_AT_VOLUME,
  MIDI_AT_VIBRATO,  /* sets lfo_depth on the fly, see handle_aftertouch() */
} midi_aftertouch_target_t;

typedef enum {
  MIDI_LFO_TRI = 0,
  MIDI_LFO_SAW,
  MIDI_LFO_SQUARE,
  MIDI_LFO_SH,       /* sample & hold: a new random value once per cycle */
} midi_lfo_wave_t;

typedef enum {
  MIDI_LFO_DEST_PITCH = 0,
  MIDI_LFO_DEST_PWM,
  MIDI_LFO_DEST_CUTOFF,
} midi_lfo_dest_t;

typedef enum {
  MIDI_ARP_UP = 0,
  MIDI_ARP_DOWN,
  MIDI_ARP_UPDOWN,
  MIDI_ARP_RANDOM,
  MIDI_ARP_AS_PLAYED,
  MIDI_ARP_TABLE,  /* steps arp_tables[arp_table_sel] instead of a fixed shape - see
                       arp_advance_table() in midi_handler.c */
} midi_arp_mode_t;

#define MIDI_CH_AUTO_GATE      (1u << 0)  /* gate follows note-on/off automatically */
#define MIDI_CH_VELOCITY_MODE  (1u << 1)  /* velocity scales decay instead of doing nothing */
#define MIDI_CH_ARP_ENABLED    (1u << 2)  /* note-on/off feed the arpeggiator instead of the pool directly */
/* Set/cleared by CC_FMEN (midi_fmopl.c's midi_fmopl_set_target()):
 * a channel with this set never touches the SID voice pool - note-on/off,
 * Program Change and CC_VOL are redirected to midi_fmopl.c entirely, and
 * every other SID-specific CC becomes a no-op for it (see
 * handle_control_change(), midi_handler.c). Independent of voice_mask/
 * sid_override - deliberately so, since the one physical FMOpl chip has
 * nothing to do with SID slot routing. */
#define MIDI_CH_TARGET_FMOPL   (1u << 3)
/* Channel claims MIDI_VOICE_UNISON_COUNT (3) adjacent pool slots on
 * one SID per note instead of 1 - detuned unison, MBSID Lead-engine style.
 * Real tradeoff, not a free addition: that SID drops from 3-voice polyphony
 * to monophonic while a channel with this set is sounding on it. See
 * midi_voice_alloc_unison() (midi_voice.h/.c) and note_on()/note_off()'s
 * unison branch (midi_handler.c). */
#define MIDI_CH_UNISON         (1u << 4)

/* Held-note capacity for the arpeggiator's input pattern. A held key beyond
 * this is simply not added to the pattern; the arp still plays whatever
 * fits. 8 covers any chord a hand can play plus a few extra. */
#define MIDI_ARP_MAX_NOTES 8

/* Fixed point scale for note-index based pitch math (portamento position,
 * pitch bend offset): 8 fractional bits, i.e. value*256 = 1.0 note index. */
#define MIDI_PITCH_FRAC_BITS 8

/* CC_SID1-4 / CC_VCE1-3 set these to narrow the channel's configured
 * voice_mask at runtime without altering the configured mask itself.
 * MIDI_OVERRIDE_NONE clears the restriction. */
#define MIDI_OVERRIDE_NONE 0xFF

/* Per-channel routing and voice-template state, one entry per MIDI channel. */
typedef struct {
  uint16_t voice_mask;      /* configured pool slots this channel may use, bit i = slot i */
  uint8_t  poly_limit;      /* max simultaneously held notes; 1 = mono */
  uint8_t  steal_mode;      /* midi_steal_mode_t */
  int8_t   transpose;       /* semitones, replaces the old compile-time MIDI_TRANSPOSE */
  uint8_t  bend_range;      /* pitch bend range in semitones, default 2 */
  uint8_t  at_target;       /* midi_aftertouch_target_t */
  uint8_t  flags;           /* MIDI_CH_* bits */
  uint8_t  sid_override;    /* MIDI_OVERRIDE_NONE, or 0..MAX_SIDS-1 */
  uint8_t  voice_override;  /* MIDI_OVERRIDE_NONE, or 0..MAX_VOICES-1 */
  /* Per-voice register template. Stamped onto a voice when it is allocated,
   * and pushed live to every voice this channel currently holds whenever a
   * CC changes it, so held notes reflect timbre changes immediately. Excludes
   * the gate bit of CONTR, which the allocator and CC_GATE own directly. */
  uint8_t  tmpl_contr;
  uint8_t  tmpl_attdec;
  uint8_t  tmpl_susrel;
  uint8_t  tmpl_pwmlo;
  uint8_t  tmpl_pwmhi;

  /* Unison detune spread (0-255, MBSID's own single-field
   * convention - see mbsidv2_sysex_implementation.txt addr 0x051). Voice 2
   * and 3 of a claimed unison triplet are offset +detune/-detune from the
   * struck pitch; unused unless MIDI_CH_UNISON is set in `flags`. */
  uint8_t  unison_detune;

  /* Pitch bend: current offset in note-index*256 fixed point, updated
   * immediately on a pitch-bend message and re-applied every tick (so LFO
   * pitch modulation and portamento glide can stack on top of it without
   * a bend message needing to arrive again). */
  int32_t  bend_x256;

  /* Portamento. porta_time 0 = off. `last_target_x256` is the note-index
   * (fixed point) of the most recently triggered note on this channel; a
   * freshly allocated voice glides in from here when porta_time > 0, giving
   * classic mono-synth-style glide behaviour even across the flat pool. */
  uint8_t  porta_time;
  int32_t  last_target_x256;

  /* LFO: one oscillator per channel, applied to every voice the channel
   * holds. lfo_depth 0 = off (also the tick's cue to skip this channel). */
  uint8_t  lfo_wave;   /* midi_lfo_wave_t */
  uint8_t  lfo_rate;   /* CC 0-127 -> 0.1-20.0 Hz, free-running */
  uint8_t  lfo_depth;  /* CC 0-127 -> modulation amount */
  uint8_t  lfo_dest;   /* midi_lfo_dest_t */
  uint16_t lfo_phase;  /* 0..65535, one full cycle, wraps */
  uint32_t lfo_sh_seed;
  int16_t  lfo_sh_value;

  /* A second, fully independent LFO. Same shape as the fields
   * above, stacked at the tick (midi_tick(), midi_handler.c): if both
   * target the same destination their offsets simply add before the one
   * bus write for that destination, same principle write_voice_pitch()
   * already uses for bend+portamento+LFO. Reuses midi_lfo_wave_t/
   * midi_lfo_dest_t - two destinations, not two enums. */
  uint8_t  lfo2_wave;
  uint8_t  lfo2_rate;
  uint8_t  lfo2_depth;
  uint8_t  lfo2_dest;
  uint16_t lfo2_phase;
  uint32_t lfo2_sh_seed;
  int16_t  lfo2_sh_value;

  /* Arpeggiator. Held notes are the arp's *input* pattern, separate from
   * the voice pool: only the currently stepped note is ever a gated voice.
   * `arp_rate` free-runs like the LFO when no MIDI clock is present; when a
   * clock is present it is read as a PPQN division instead (see
   * midi_clock_present() in midi.h). */
  uint8_t  arp_mode;    /* midi_arp_mode_t */
  uint8_t  arp_rate;
  uint8_t  arp_octaves; /* repeat the held pattern transposed by 0..arp_octaves extra octaves */
  uint8_t  arp_table_sel;  /* which arp_tables[] slot MIDI_ARP_TABLE reads, set by CC_ARPT */
  uint8_t  arp_held[MIDI_ARP_MAX_NOTES];
  uint8_t  arp_held_count;
  uint8_t  arp_step;        /* index into the expanded (octaves-multiplied) pattern */
  bool     arp_step_up;     /* MIDI_ARP_UPDOWN direction state */
  uint8_t  arp_voice_slot;  /* 0xFF (matches midi_voice.h's MIDI_VOICE_NONE), or the pool slot
                                currently sounding the arp - not including midi_voice.h here to
                                avoid a circular include, since midi_voice.c already includes
                                this header for the policy fields it reads */
  uint16_t arp_phase;       /* free-run phase accumulator, same shape as lfo_phase */
  uint32_t arp_last_step_pulses; /* midi_clock_total_pulses() value at the last step, clock-synced mode */

  /* Last CC_FFC-set cutoff (0..CUTOFF_MAX), independent of any live LFO
   * offset riding on top of it: the LFO-CUTOFF tick modulates around this
   * value rather than around whatever the register currently holds, which
   * would drift since the tick writes that same register. */
  uint16_t filter_cutoff;

  /* Currently selected Program Change patch, MIDI_PATCH_NONE if
   * none has been selected since boot (the compiled-in defaults above are
   * what a channel plays until then). */
  uint8_t  patch;
} midi_channel_cfg_t;

extern midi_channel_cfg_t midi_channels[MAX_CHANNELS];

/* Sets every channel's defaults: channel 1 gets all 12 slots, full poly,
 * oldest-steal; channels 2-5 get one SID each, exclusive, 3-voice poly;
 * channels 6-16 are disabled (empty mask). */
void midi_config_init(void);

/* The mask actually available to `channel` right now: the configured
 * voice_mask, clipped to SIDs `cfg.numsids` says are present, further
 * narrowed by sid_override / voice_override if either is set. Computed
 * fresh on every call rather than cached, so a config change (numsids,
 * an override) is reflected immediately with nothing to invalidate. */
uint16_t midi_channel_effective_mask(uint8_t channel);


/* Flash persistence: fixed magic distinct from MAGIC_SMOKE (the build
 * date, globals.h - tying to it would wipe the MIDI blob on every
 * rebuild), plus version and a CRC32 over everything after the CRC field.
 * `sequence` is a monotonic counter, not a wrapping slot index: on load,
 * all 16 sector slots are scanned and the highest-sequence slot that also
 * passes magic+size+crc is loaded. Only magic/size/CRC failure falls back
 * to defaults - a slot's claimed position alone can't distinguish old
 * valid data from garbage once the ring has wrapped. */

#define MIDI_CONFIG_MAGIC   0x4D494431u  /* 'MID1', fixed, independent of MAGIC_SMOKE */
#define MIDI_CONFIG_VERSION 3  /* 2: arp_tables[] added to the blob and
                                   arp_table_sel added to midi_channel_cfg_t.
                                   3: the lfo2 fields and unison_detune added
                                   to midi_channel_cfg_t, and the lfo2 and
                                   unison fields added to midi_patch_t (see
                                   midi_patch.h). Old size no longer matches
                                   either bump; midi_config_load's existing
                                   size check falls back to defaults on an
                                   old blob rather than misreading it. */

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t size;      /* sizeof(midi_config_blob_t) at write time */
  uint32_t crc32;      /* over every byte from `saveid` onward */
  uint8_t  saveid;      /* which of the MIDICONFIG_SAVE_SLOTS slots this was written to */
  uint32_t sequence;    /* monotonic write counter; highest valid wins on load */
  midi_ccvalues       ccmap;
  midi_channel_cfg_t  channel[MAX_CHANNELS];  /* whole live struct, see midi_config_load()
                                                  for why the transient runtime fields inside
                                                  it are safe to persist as-is */
  midi_patch_t        patch[MIDI_PATCH_COUNT];
  midi_arp_table_t    arp_table[MIDI_ARP_TABLE_COUNT];  /* midi_arp_table.h */
} midi_config_blob_t;

/* Erase-and-write a new slot with the current live state (midi_channels[],
 * midi_patches[], the CC map). Round-robins across MIDICONFIG_SAVE_SLOTS
 * sectors the same way Config's own save does across its 16 pages. Refuses
 * (logs and returns) if verify_midiconfig_offset() found the linker and
 * macro views of the flash layout disagree - see config.c. */
void midi_config_save(void);

/* Scans all MIDICONFIG_SAVE_SLOTS slots, loads the highest-sequence one
 * that passes magic+size+crc32, and sanitises the transient runtime fields
 * in midi_channels[] afterward (pitch bend offset, portamento glide target,
 * SID/voice overrides, LFO phase, arpeggiator held notes and voice) since
 * those describe a moment mid-performance, not a setting - a reboot should
 * never resume with a note the arp thinks is still held. Falls back to
 * midi_config_init()/midi_patch_init() defaults, unmodified, if no slot
 * validates. */
void midi_config_load(void);

/* Re-initialises midi_channels[]/midi_patches[] to compiled-in defaults and
 * erases every save slot, so a subsequent load() also finds nothing and
 * stays on defaults - a real factory reset, not just "load defaults into
 * RAM until the next boot re-reads stale flash". */
void midi_config_reset(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_CONFIG_H_ */
