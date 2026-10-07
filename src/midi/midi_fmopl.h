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


/* MIDI control of a board's FMOpl chip (cfg.fmopl_enabled/cfg.fmopl_sid,
 * config_socket.c). Real OPL2 has 9 hardware channels total, no per-chip
 * multiplication like SID voices - a small allocator over those 9,
 * separate from midi_voice.c's SID pool. Not yet verified on real
 * hardware. */
#define MIDI_FMOPL_CHANNELS 9

/* 2-operator FM instrument (modulator + carrier, series or parallel).
 * Packed to match the real OPL2 register layout directly, so
 * fmopl_write_instrument() (midi_fmopl.c) and sysex.c's pack/unpack need
 * no bit-twiddling. Public for SYSEX_FMOPL_PATCH_LOAD/DUMP/DATA (sysex.c). */
typedef struct {
  uint8_t op_mult[2];    /* bit7=AM, bit6=Vibrato, bit5=EG type (1=sustained), bit4=KSR, bits3:0=Multiple */
  uint8_t op_ksl_tl[2];  /* bits7:6=Key Scale Level, bits5:0=Total Level (0=loudest, 63=silent) */
  uint8_t op_ar_dr[2];   /* bits7:4=Attack Rate, bits3:0=Decay Rate */
  uint8_t op_sl_rr[2];   /* bits7:4=Sustain Level, bits3:0=Release Rate */
  uint8_t op_wave[2];    /* bits1:0=waveform, OPL2 only has 4 (0=sine,1=half-sine,2=abs-sine,3=quarter-sine) */
  uint8_t feedback_algo; /* bits3:1=modulator self-feedback (0-7), bit0=algorithm (0=FM/series, 1=additive/parallel) */
} opl_instrument_t;

/* 32 patches; Program Change selects one directly, programs 32-127 unused
 * (same convention as MIDI_PATCH_COUNT, midi_patch.h). Patches 0-5 are the
 * factory instruments (midi_fmopl_patch_init()); 6-31 start zeroed, a
 * safe near-silent default. */
#define MIDI_FMOPL_PATCH_COUNT 32
extern opl_instrument_t fmopl_patches[MIDI_FMOPL_PATCH_COUNT];

/* Called from midi_processor_init() (and on MIDI System Reset). No-op if
 * cfg.fmopl_enabled is false. */
void midi_fmopl_init(void);

/* Note on/off. Called from midi_handler.c's note_on()/note_off() when the
 * channel has MIDI_CH_TARGET_FMOPL set (midi_config.h). Velocity accepted
 * but unused (OPL2 has no per-note velocity input). */
void midi_fmopl_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void midi_fmopl_note_off(uint8_t channel, uint8_t note, uint8_t velocity);

/* Program Change on an FMOpl-targeted channel: selects a patch from
 * fmopl_patches[] for the channel's next note-on (range-checked,
 * out-of-range ignored). Already-sounding voices are unaffected. */
void midi_fmopl_program_change(uint8_t channel, uint8_t program);

/* Captures a channel's current instrument into another patch slot (RAM
 * only). Caller must range-check channel and patch_index first, see
 * sysex.c's handle_fmopl_patch_save(). */
void midi_fmopl_capture_patch(uint8_t channel, uint8_t patch_index);

/* CC_FMEN (midi_defs.h): toggles MIDI_CH_TARGET_FMOPL on `channel` (127
 * on, 0 off, else toggles). Handled as a special case in
 * handle_control_change() since it must apply before the channel is
 * treated as FMOpl-targeted. */
void midi_fmopl_set_target(uint8_t channel, uint8_t value);

/* CC_VOL on an FMOpl-targeted channel: sets attenuation baseline and
 * live-rewrites Total Level on every held voice (OPL2 has no separate
 * master-volume register). */
void midi_fmopl_set_volume(uint8_t channel, uint8_t value);

/* CC_PWM (mod wheel) on an FMOpl-targeted channel: live modulator
 * brightness/FM-intensity control (see fmopl_scale_tl() in midi_fmopl.c). */
void midi_fmopl_set_mod_wheel(uint8_t channel, uint8_t value);

/* Pitch bend (0xE0) on an FMOpl-targeted channel: uses
 * midi_channels[channel].bend_range and live-rewrites Fnum/block on every
 * held voice. `lsb`/`msb` are the raw 14-bit MIDI pitch bend bytes. */
void midi_fmopl_pitch_bend(uint8_t channel, uint8_t lsb, uint8_t msb);

/* SYSEX_FMOPL_SET_CLOCK (0x26, sysex.c): overrides the clock used to
 * compute Fnum/block, live. `hz` = 0 resets to the compiled-in default
 * (FMOPL_CLOCK_HZ, midi_fmopl.c). Debugging/tuning knob for boards whose
 * clone clock measures off from the standard 3579545 - see
 * fmopl_clock_override in midi_fmopl.c. Not persisted to flash. */
void midi_fmopl_set_clock(uint32_t hz);

/* Silences every sounding OPL voice. Called from all_notes_off()
 * (midi_handler.c) so CC_ASOF/CC_RACT/CC_ANOF cover FMOpl too. No-op if
 * cfg.fmopl_enabled is false. */
void midi_fmopl_all_notes_off(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_FMOPL_H_ */
