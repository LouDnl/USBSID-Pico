/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_fmopl.h
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

#ifndef _USBSID_MIDI_FMOPL_H_
#define _USBSID_MIDI_FMOPL_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>


/* __todo/plans/midi-full-input-expansion/_project/TODO.md item 14: first-pass
 * MIDI control of a board's FMOpl chip, if one is configured (RuntimeCFG's
 * cfg.fmopl_enabled/cfg.fmopl_sid, set via config_socket.c). A real OPL2 has
 * exactly 9 hardware melodic channels total (no per-chip multiplication the
 * way SID voices spread across cfg.numsids) - this is a single small
 * allocator over those 9, entirely separate from midi_voice.c's SID pool.
 *
 * UNVERIFIED ON REAL HARDWARE: there is no FMOpl-equipped board available to
 * test any of this against (see TODO 14's own text). Everything here builds
 * and is internally consistent, but the note-on/off register sequencing, the
 * clock-scaled Fnum math (build_fmopl_note_table() in the .c, which assumes
 * the FMOpl chip shares the same PIO-generated PHI clock as every SID socket
 * - there is no separate FMOpl clock generator anywhere in this firmware),
 * and the factory instrument bytes are all first-pass and need real-hardware
 * audition before anyone should trust them musically. */
#define MIDI_FMOPL_CHANNELS 9

/* A minimal 2-operator FM instrument. Nothing like midi_patch_t: OPL voices
 * are 2 independently-configured operators (modulator, carrier) combined
 * either in series (FM) or in parallel (additive), not one waveform+one
 * ADSR. Packed exactly the way the real OPL2 registers want them, so
 * fmopl_write_instrument() (midi_fmopl.c) is a direct copy, no further
 * bit-twiddling - and so sysex.c's pack/unpack can do the same. Public
 * (rather than kept inside the .c the way the old fixed factory-instrument
 * table was) now that TODO 15 lets a patch be authored/loaded at runtime,
 * the same shape TODO 7 already gave midi_patch_t. */
typedef struct {
  uint8_t op_mult[2];    /* bit7=AM, bit6=Vibrato, bit5=EG type (1=sustained), bit4=KSR, bits3:0=Multiple */
  uint8_t op_ksl_tl[2];  /* bits7:6=Key Scale Level, bits5:0=Total Level (0=loudest, 63=silent) */
  uint8_t op_ar_dr[2];   /* bits7:4=Attack Rate, bits3:0=Decay Rate */
  uint8_t op_sl_rr[2];   /* bits7:4=Sustain Level, bits3:0=Release Rate */
  uint8_t op_wave[2];    /* bits1:0=waveform, OPL2 only has 4 (0=sine,1=half-sine,2=abs-sine,3=quarter-sine) */
  uint8_t feedback_algo; /* bits3:1=modulator self-feedback (0-7), bit0=algorithm (0=FM/series, 1=additive/parallel) */
} opl_instrument_t;

/* 32 patches, same count and same Program-Change-selects-one-directly
 * reasoning as MIDI_PATCH_COUNT (midi_patch.h) - programs 32-127 simply
 * don't map to anything, same "no bank select needed" logic. Patches 0-5
 * are the 6 compiled-in factory instruments (Organ, Electric piano, Bass,
 * Brass, Bell, Pad; see midi_fmopl_patch_init() in the .c); 6-31 start
 * zeroed - all-zero OPL registers is a safe, near-silent default (Attack
 * Rate 0 means the envelope never rises), same "inert rather than
 * surprising" reasoning midi_patch_init() uses for its own unset slots. */
#define MIDI_FMOPL_PATCH_COUNT 32
extern opl_instrument_t fmopl_patches[MIDI_FMOPL_PATCH_COUNT];

/* Called once from midi_processor_init() (also runs on a MIDI System Reset,
 * same as everything else that function initialises). No-ops harmlessly if
 * cfg.fmopl_enabled is false - safe to call unconditionally. Calls
 * midi_fmopl_patch_init() itself, same way midi_processor_init() calls
 * midi_patch_init() separately for the SID side - kept as one call here
 * since, unlike the SID pool, there is nothing else to initialise first. */
void midi_fmopl_init(void);

/* Note on/off. Called from midi_handler.c's note_on()/note_off() only when
 * the channel has MIDI_CH_TARGET_FMOPL set (midi_config.h) - a channel with
 * that flag never touches the SID voice pool at all, these are the entire
 * note path for it. Velocity is accepted but not yet used (OPL2 has no
 * per-note velocity input; velocity-to-Total-Level scaling is unimplemented,
 * see TODO 14's own follow-up list). */
void midi_fmopl_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void midi_fmopl_note_off(uint8_t channel, uint8_t note, uint8_t velocity);

/* Program Change on an FMOpl-targeted channel: selects one of
 * fmopl_patches[] (range-checked against MIDI_FMOPL_PATCH_COUNT, out of
 * range is ignored, matches how apply_patch_to_channel()'s own caller
 * range-checks before calling at all) for that channel's *next* note-on.
 * Does not rewrite already-sounding voices, same as a real hardware
 * synth's patch change affecting new notes only. */
void midi_fmopl_program_change(uint8_t channel, uint8_t program);

/* CC_FMEN (midi_defs.h): toggles MIDI_CH_TARGET_FMOPL on `channel` - 127 on,
 * 0 off, anything else flips whatever it currently is (same convention as
 * every other boolean CC in this firmware, see set_handler() in
 * midi_handler.c). Dispatched as a special case inside handle_control_change()
 * rather than through the normal cc_func_ptr_array table, since this is the
 * one CC that must reach a channel *before* that channel is treated as
 * FMOpl-targeted. */
void midi_fmopl_set_target(uint8_t channel, uint8_t value);

/* CC_VOL (the *same* CC number a SID-targeted channel already uses) on an
 * FMOpl-targeted channel: sets this channel's attenuation baseline and live
 * -rewrites Total Level on every voice it currently holds, not just the
 * next note-on - OPL2 has no separate master-volume register the way SID's
 * MODVOL nibble is, so "live" here means finding and rewriting each held
 * voice's own TL bytes. */
void midi_fmopl_set_volume(uint8_t channel, uint8_t value);

/* CC_PWM (the *same* CC number - default the mod wheel, CC 1 - a
 * SID-targeted channel already uses for pulse width) on an FMOpl-targeted
 * channel: live modulator-only brightness/FM-intensity control, see
 * fmopl_scale_tl()'s own comment in the .c for why this specific mapping.
 * Live the same way CC_VOL is. */
void midi_fmopl_set_mod_wheel(uint8_t channel, uint8_t value);

/* Pitch bend (0xE0) on an FMOpl-targeted channel: uses
 * midi_channels[channel].bend_range (semitones) the same way the SID side's
 * pitch_notefrequency() does, and live-rewrites Fnum/block on every voice
 * this channel currently holds, same "live" reasoning as CC_VOL/CC_PWM
 * above - a bent note that doesn't audibly bend until released is not
 * usable pitch bend. `lsb`/`msb` are the raw 14-bit MIDI pitch bend bytes,
 * same shape process_midi()'s 0xE0 case already extracts for the SID path. */
void midi_fmopl_pitch_bend(uint8_t channel, uint8_t lsb, uint8_t msb);

/* SYSEX_FMOPL_SET_CLOCK (0x26, sysex.c): overrides the clock used to
 * compute Fnum/block from a MIDI note, live - rewrites every currently
 * held OPL voice immediately, not just future notes. `hz` = 0 resets to
 * the compiled-in default (FMOPL_CLOCK_HZ, midi_fmopl.c - the standard
 * OPL2 crystal frequency, 3579545). A debugging/tuning tool: the compiled
 * default is verified correct against both the general OPL2 spec and
 * SIDKick-pico's own emulator source, but still measured wrong on at least
 * one real board even after that verification - see midi_fmopl.c's own
 * comment on `fmopl_clock_override` for the full story. Not persisted to
 * flash; resets to 0 (default) on every midi_fmopl_init() call. */
void midi_fmopl_set_clock(uint32_t hz);

/* Silences every currently-sounding OPL voice, regardless of which MIDI
 * channel triggered it. Called from all_notes_off() (midi_handler.c) so the
 * existing CC_ASOF/CC_RACT/CC_ANOF panic buttons cover FMOpl too. No-ops if
 * cfg.fmopl_enabled is false. */
void midi_fmopl_all_notes_off(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_FMOPL_H_ */
