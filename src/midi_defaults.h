/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_defaults.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
 *
 * Copyright (c) 2024-2025 LouD
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

#ifndef _USBSID_MIDI_DEFAULTS_H_
#define _USBSID_MIDI_DEFAULTS_H_
#pragma once

#include "sid.h"

#ifdef __cplusplus
  extern "C" {
#endif


#define NOTEHI(n) ((musical_scale_values[(n - ((n >= 0x81) ? 0x80 : 0))] & 0xFF00) >> 8)
#define NOTELO(n) (musical_scale_values[(n - ((n >= 0x81) ? 0x80 : 0))] & 0xFF)

#define INSTRUMENT_TYPE "Goattracker2"
#define MAX_TABLE_ENTRIES 16 /* TODO: DECIDE ON MAX VALUE */
#define NOISE     0b10000000
#define PULSEWAVE 0b01000000
#define SAWTOOTH  0b00100000
#define TRIANGLE  0b00010000
#define GATE_ON(w)   ((w) | (0b1))
#define GATE_OFF(w)  ((w) ^ (0b1))


struct WaveTable_s {
  /* 0b0000xxxx Left nibble: noise, pulsewave, sawtooth, triangle */
  /* 0bxxxx0000 Right nibble: test, ringmod, sync, gate */
  uint8_t reg;
  /* 0x00 ->  0x80 = note addition */
  /* 0x81 ->  0xE0 = fixed notes from notetable */
  uint8_t pitch_offset;
  /* Last entry, end */
  bool end : 1;
  /* Jump to entry in pitch_offset */
  bool jump : 1;
} WaveTable_d = {0} ;
struct PulseTable_s {
  uint8_t pw_lo; /* 0b11111111 */
  uint8_t pw_hi; /* 0bxxxx1111 */
  uint8_t time;  /* TODO */
  uint8_t speed; /* TODO */
  bool pw_ts : 1;  /* false pwm, true time speed */
  /* Last entry, end */
  bool end : 1;
  /* Jump to entry */
  bool jump : 1;
} PulseTable_d = {0};
struct FilterTable_s {
  uint8_t cutoff_lo; /* 0bxxxxx111 */
  uint8_t cutoff_hi; /* 0b11111111 */
  /*  */
  uint8_t resonance; /* 0b1111xxxx */
  uint8_t voices;    /* 0bxxxxx321 */
  uint8_t filter;    /* 0bx111xxxx */
  /* Last entry, end */
  bool end : 1;
  /* Jump to entry  */
  bool jump : 1;
} FilterTable_d = {0};
struct SpeedTable_s { /* Vibrato */
  uint8_t speed;
  uint8_t width;
  uint8_t delay;
} SpeedTable_d = {0};

typedef struct WaveTable_s WaveTable;
typedef struct PulseTable_s PulseTable;
typedef struct FilterTable_s FilterTable;
typedef struct SpeedTable_s SpeedTable;

struct Instrument_s {
  uint8_t attack;
  uint8_t decay;
  uint8_t sustain;
  uint8_t release;
  WaveTable wt[MAX_TABLE_ENTRIES];
  PulseTable pt[MAX_TABLE_ENTRIES];
  FilterTable ft[MAX_TABLE_ENTRIES];
  SpeedTable speedtable;
  bool has_wt : 1;
  bool has_pt : 1;
  bool has_ft : 1;
  bool has_st : 1;
  bool polyphonic : 1;
} Instrument_d = {0};
typedef struct Instrument_s Instrument;

struct Bank_s {
  Instrument ins[MAX_TABLE_ENTRIES];
} Bank_d = {0};
typedef struct Bank_s Bank;

/* Stores the current Runtime values */
typedef struct Channel {
  // Instrument * instr;
  uint8_t bankno;
  uint8_t insno;
  /* Any other settings */
} Channel;
typedef struct RuntimeMidi {
  Channel channel[MAX_TABLE_ENTRIES]; /* Current channel */
} RuntimeMidi;

Bank banks[MAX_TABLE_ENTRIES]; /* No of banks */


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_DEFAULTS_H_ */
