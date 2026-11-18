/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_defs.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
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

#ifndef _USBSID_MIDI_CC_H_
#define _USBSID_MIDI_CC_H_
#pragma once

#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
  extern "C" {
#endif


typedef struct midi_ccvalues {
  /* Field order = CC function id (enum midi_cc_fn). Append only, never reorder. */
  /* Voice related */
  uint8_t CC_NOTE;  /* Note frequency */
  uint8_t CC_PWM;   /* Pulse Width Modulation (Modulation wheel) */
  uint8_t CC_NOIS;  /* Noise waveform */
  uint8_t CC_PULS;  /* Pulse waveform */
  uint8_t CC_SAWT;  /* Sawtooth waveform */
  uint8_t CC_TRIA;  /* Triangle waveform */
  uint8_t CC_TEST;  /* Test bit */
  uint8_t CC_RMOD;  /* Ring modulator bit */
  uint8_t CC_SYNC;  /* Sync bit */
  uint8_t CC_GATE;  /* Gate bit */
  uint8_t CC_ATT;   /* Attack */
  uint8_t CC_DEC;   /* Decay */
  uint8_t CC_SUS;   /* Sustain */
  uint8_t CC_REL;   /* Release */
  /* Chip related */
  uint8_t CC_FFC;   /* Filter Frequency Cutoff */
  uint8_t CC_RES;   /* Filter resonance */
  uint8_t CC_FLT1;  /* Filter voice 1 */
  uint8_t CC_FLT2;  /* Filter voice 2 */
  uint8_t CC_FLT3;  /* Filter voice 3 */
  uint8_t CC_FLTE;  /* Filter external */
  uint8_t CC_3OFF;  /* Voice 3 disconnect */
  uint8_t CC_HPF;   /* High pass */
  uint8_t CC_BPF;   /* Band pass */
  uint8_t CC_LPF;   /* Low pass */
  uint8_t CC_VOL;   /* Channel Volume */
  /* SID / Voice selection */
  uint8_t CC_SID1;  /* Select SID 1 */
  uint8_t CC_SID2;  /* Select SID 2 */
  uint8_t CC_SID3;  /* Select SID 3 */
  uint8_t CC_SID4;  /* Select SID 4 */
  uint8_t CC_VCE1;  /* Select Voice 1 */
  uint8_t CC_VCE2;  /* Select Voice 2 */
  uint8_t CC_VCE3;  /* Select Voice 3 */
  /* Custom commands */
  uint8_t CC_GTEN;  /* Gate auto enabled on noteon note off */
  uint8_t CC_SPLY;  /* Turn on polyfonic for current SID */
  uint8_t CC_VELM;  /* Velocity Mode */
  /* Modulation and timing */
  uint8_t CC_LFOW;  /* LFO waveform */
  uint8_t CC_LFOR;  /* LFO rate */
  uint8_t CC_LFOD;  /* LFO depth */
  uint8_t CC_LFOT;  /* LFO destination (target) */
  uint8_t CC_PORT;  /* Portamento time */
  uint8_t CC_ARPM;  /* Arpeggiator mode */
  uint8_t CC_ARPR;  /* Arpeggiator rate */
  uint8_t CC_ARPO;  /* Arpeggiator octave range */
  uint8_t CC_ARPE;  /* Arpeggiator enable */
  /* Arpeggiator tables (midi_arp_table.h) */
  uint8_t CC_ARPT;  /* Arpeggiator table select (used when arp_mode == MIDI_ARP_TABLE) */
  /* Second LFO, stacked with LFO1 at whichever destination(s) each targets
   * (midi_tick(), midi_handler.c) */
  uint8_t CC_LFO2W; /* LFO 2 waveform */
  uint8_t CC_LFO2R; /* LFO 2 rate */
  uint8_t CC_LFO2D; /* LFO 2 depth */
  uint8_t CC_LFO2T; /* LFO 2 destination (target) */
  /* 3-oscillator unison (midi_voice_alloc_unison(), midi_voice.h/.c) */
  uint8_t CC_UNIS;  /* Unison mode on/off (channel drops to 1 note/SID while on) */
  uint8_t CC_UDET;  /* Unison detune spread */
  /* FMOpl (midi_fmopl.h/.c) */
  uint8_t CC_FMEN;  /* Target this channel's notes at the FMOpl chip instead of a SID */
  /* Fixed CC values ~ Embedded application (Cynthcart, SID SYNTH) */
  uint8_t CC_CEN;   /* Enable embedded application */
  uint8_t CC_CDI;   /* Disable embedded application */
  uint8_t CC_CRE;   /* Reset embedded application */
  /* Midi related */
  uint8_t CC_BMSB;  /* Bank Select MSB */
  uint8_t CC_BLSB;  /* Bank Select LSB */
  uint8_t CC_MOD;   /* Modulation wheel */
  uint8_t CC_MODL;  /* Modulation wheel LSB */
  uint8_t CC_ASOF;  /* All Sound Off */
  uint8_t CC_RACT;  /* Reset All Controllers */
  uint8_t CC_ANOF;  /* All Notes Off */
  uint8_t CC_MONO;  /* Mono Mode On ~ Poly off & all notes off */
  uint8_t CC_POLY;  /* Poly Mode On ~ Mono off & all notes off */
} midi_ccvalues;

#define MIDI_CC_MAP_SLOTS 96
#define MIDI_CC_UNASSIGNED 0xFF
#define MIDI_CC_FN_COUNT 64

enum midi_cc_fn {
  MIDI_CC_FN_NOTE = 0,
  MIDI_CC_FN_PWM,
  MIDI_CC_FN_NOIS,
  MIDI_CC_FN_PULS,
  MIDI_CC_FN_SAWT,
  MIDI_CC_FN_TRIA,
  MIDI_CC_FN_TEST,
  MIDI_CC_FN_RMOD,
  MIDI_CC_FN_SYNC,
  MIDI_CC_FN_GATE,
  MIDI_CC_FN_ATT,
  MIDI_CC_FN_DEC,
  MIDI_CC_FN_SUS,
  MIDI_CC_FN_REL,
  MIDI_CC_FN_FFC,
  MIDI_CC_FN_RES,
  MIDI_CC_FN_FLT1,
  MIDI_CC_FN_FLT2,
  MIDI_CC_FN_FLT3,
  MIDI_CC_FN_FLTE,
  MIDI_CC_FN_3OFF,
  MIDI_CC_FN_HPF,
  MIDI_CC_FN_BPF,
  MIDI_CC_FN_LPF,
  MIDI_CC_FN_VOL,
  MIDI_CC_FN_SID1,
  MIDI_CC_FN_SID2,
  MIDI_CC_FN_SID3,
  MIDI_CC_FN_SID4,
  MIDI_CC_FN_VCE1,
  MIDI_CC_FN_VCE2,
  MIDI_CC_FN_VCE3,
  MIDI_CC_FN_GTEN,
  MIDI_CC_FN_SPLY,
  MIDI_CC_FN_VELM,
  MIDI_CC_FN_LFOW,
  MIDI_CC_FN_LFOR,
  MIDI_CC_FN_LFOD,
  MIDI_CC_FN_LFOT,
  MIDI_CC_FN_PORT,
  MIDI_CC_FN_ARPM,
  MIDI_CC_FN_ARPR,
  MIDI_CC_FN_ARPO,
  MIDI_CC_FN_ARPE,
  MIDI_CC_FN_ARPT,
  MIDI_CC_FN_LFO2W,
  MIDI_CC_FN_LFO2R,
  MIDI_CC_FN_LFO2D,
  MIDI_CC_FN_LFO2T,
  MIDI_CC_FN_UNIS,
  MIDI_CC_FN_UDET,
  MIDI_CC_FN_FMEN,
  MIDI_CC_FN_CEN,
  MIDI_CC_FN_CDI,
  MIDI_CC_FN_CRE,
  MIDI_CC_FN_BMSB,
  MIDI_CC_FN_BLSB,
  MIDI_CC_FN_MOD,
  MIDI_CC_FN_MODL,
  MIDI_CC_FN_ASOF,
  MIDI_CC_FN_RACT,
  MIDI_CC_FN_ANOF,
  MIDI_CC_FN_MONO,
  MIDI_CC_FN_POLY
};

static_assert(offsetof(midi_ccvalues, CC_NOTE) == MIDI_CC_FN_NOTE, "CC_NOTE id mismatch");
static_assert(offsetof(midi_ccvalues, CC_PWM) == MIDI_CC_FN_PWM, "CC_PWM id mismatch");
static_assert(offsetof(midi_ccvalues, CC_NOIS) == MIDI_CC_FN_NOIS, "CC_NOIS id mismatch");
static_assert(offsetof(midi_ccvalues, CC_PULS) == MIDI_CC_FN_PULS, "CC_PULS id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SAWT) == MIDI_CC_FN_SAWT, "CC_SAWT id mismatch");
static_assert(offsetof(midi_ccvalues, CC_TRIA) == MIDI_CC_FN_TRIA, "CC_TRIA id mismatch");
static_assert(offsetof(midi_ccvalues, CC_TEST) == MIDI_CC_FN_TEST, "CC_TEST id mismatch");
static_assert(offsetof(midi_ccvalues, CC_RMOD) == MIDI_CC_FN_RMOD, "CC_RMOD id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SYNC) == MIDI_CC_FN_SYNC, "CC_SYNC id mismatch");
static_assert(offsetof(midi_ccvalues, CC_GATE) == MIDI_CC_FN_GATE, "CC_GATE id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ATT) == MIDI_CC_FN_ATT, "CC_ATT id mismatch");
static_assert(offsetof(midi_ccvalues, CC_DEC) == MIDI_CC_FN_DEC, "CC_DEC id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SUS) == MIDI_CC_FN_SUS, "CC_SUS id mismatch");
static_assert(offsetof(midi_ccvalues, CC_REL) == MIDI_CC_FN_REL, "CC_REL id mismatch");
static_assert(offsetof(midi_ccvalues, CC_FFC) == MIDI_CC_FN_FFC, "CC_FFC id mismatch");
static_assert(offsetof(midi_ccvalues, CC_RES) == MIDI_CC_FN_RES, "CC_RES id mismatch");
static_assert(offsetof(midi_ccvalues, CC_FLT1) == MIDI_CC_FN_FLT1, "CC_FLT1 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_FLT2) == MIDI_CC_FN_FLT2, "CC_FLT2 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_FLT3) == MIDI_CC_FN_FLT3, "CC_FLT3 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_FLTE) == MIDI_CC_FN_FLTE, "CC_FLTE id mismatch");
static_assert(offsetof(midi_ccvalues, CC_3OFF) == MIDI_CC_FN_3OFF, "CC_3OFF id mismatch");
static_assert(offsetof(midi_ccvalues, CC_HPF) == MIDI_CC_FN_HPF, "CC_HPF id mismatch");
static_assert(offsetof(midi_ccvalues, CC_BPF) == MIDI_CC_FN_BPF, "CC_BPF id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LPF) == MIDI_CC_FN_LPF, "CC_LPF id mismatch");
static_assert(offsetof(midi_ccvalues, CC_VOL) == MIDI_CC_FN_VOL, "CC_VOL id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SID1) == MIDI_CC_FN_SID1, "CC_SID1 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SID2) == MIDI_CC_FN_SID2, "CC_SID2 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SID3) == MIDI_CC_FN_SID3, "CC_SID3 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SID4) == MIDI_CC_FN_SID4, "CC_SID4 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_VCE1) == MIDI_CC_FN_VCE1, "CC_VCE1 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_VCE2) == MIDI_CC_FN_VCE2, "CC_VCE2 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_VCE3) == MIDI_CC_FN_VCE3, "CC_VCE3 id mismatch");
static_assert(offsetof(midi_ccvalues, CC_GTEN) == MIDI_CC_FN_GTEN, "CC_GTEN id mismatch");
static_assert(offsetof(midi_ccvalues, CC_SPLY) == MIDI_CC_FN_SPLY, "CC_SPLY id mismatch");
static_assert(offsetof(midi_ccvalues, CC_VELM) == MIDI_CC_FN_VELM, "CC_VELM id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFOW) == MIDI_CC_FN_LFOW, "CC_LFOW id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFOR) == MIDI_CC_FN_LFOR, "CC_LFOR id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFOD) == MIDI_CC_FN_LFOD, "CC_LFOD id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFOT) == MIDI_CC_FN_LFOT, "CC_LFOT id mismatch");
static_assert(offsetof(midi_ccvalues, CC_PORT) == MIDI_CC_FN_PORT, "CC_PORT id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ARPM) == MIDI_CC_FN_ARPM, "CC_ARPM id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ARPR) == MIDI_CC_FN_ARPR, "CC_ARPR id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ARPO) == MIDI_CC_FN_ARPO, "CC_ARPO id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ARPE) == MIDI_CC_FN_ARPE, "CC_ARPE id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ARPT) == MIDI_CC_FN_ARPT, "CC_ARPT id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFO2W) == MIDI_CC_FN_LFO2W, "CC_LFO2W id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFO2R) == MIDI_CC_FN_LFO2R, "CC_LFO2R id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFO2D) == MIDI_CC_FN_LFO2D, "CC_LFO2D id mismatch");
static_assert(offsetof(midi_ccvalues, CC_LFO2T) == MIDI_CC_FN_LFO2T, "CC_LFO2T id mismatch");
static_assert(offsetof(midi_ccvalues, CC_UNIS) == MIDI_CC_FN_UNIS, "CC_UNIS id mismatch");
static_assert(offsetof(midi_ccvalues, CC_UDET) == MIDI_CC_FN_UDET, "CC_UDET id mismatch");
static_assert(offsetof(midi_ccvalues, CC_FMEN) == MIDI_CC_FN_FMEN, "CC_FMEN id mismatch");
static_assert(offsetof(midi_ccvalues, CC_CEN) == MIDI_CC_FN_CEN, "CC_CEN id mismatch");
static_assert(offsetof(midi_ccvalues, CC_CDI) == MIDI_CC_FN_CDI, "CC_CDI id mismatch");
static_assert(offsetof(midi_ccvalues, CC_CRE) == MIDI_CC_FN_CRE, "CC_CRE id mismatch");
static_assert(offsetof(midi_ccvalues, CC_BMSB) == MIDI_CC_FN_BMSB, "CC_BMSB id mismatch");
static_assert(offsetof(midi_ccvalues, CC_BLSB) == MIDI_CC_FN_BLSB, "CC_BLSB id mismatch");
static_assert(offsetof(midi_ccvalues, CC_MOD) == MIDI_CC_FN_MOD, "CC_MOD id mismatch");
static_assert(offsetof(midi_ccvalues, CC_MODL) == MIDI_CC_FN_MODL, "CC_MODL id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ASOF) == MIDI_CC_FN_ASOF, "CC_ASOF id mismatch");
static_assert(offsetof(midi_ccvalues, CC_RACT) == MIDI_CC_FN_RACT, "CC_RACT id mismatch");
static_assert(offsetof(midi_ccvalues, CC_ANOF) == MIDI_CC_FN_ANOF, "CC_ANOF id mismatch");
static_assert(offsetof(midi_ccvalues, CC_MONO) == MIDI_CC_FN_MONO, "CC_MONO id mismatch");
static_assert(offsetof(midi_ccvalues, CC_POLY) == MIDI_CC_FN_POLY, "CC_POLY id mismatch");
static_assert(sizeof(midi_ccvalues) == MIDI_CC_FN_COUNT, "midi_ccvalues size != MIDI_CC_FN_COUNT");

/* Default Control Change Byte values */
#define MIDI_DEFAULT_CCVALUES_INIT { \
  /* Default values ~ Voice related */ \
  .CC_NOTE  =  0x1C,  /*  28 ~ Note frequency for manual input */ \
  /* TODO: Move CC_PWM off CC 1 once CC_MOD has a real modulation wheel implementation */ \
  .CC_PWM   =  0x01,  /*   1 ~ Pulse Width Modulation (Modulation wheel) */ \
  .CC_NOIS  =  0x14,  /*  20 ~ Noise waveform */ \
  .CC_PULS  =  0x15,  /*  21 ~ Pulse waveform */ \
  .CC_SAWT  =  0x16,  /*  22 ~ Sawtooth waveform */ \
  .CC_TRIA  =  0x17,  /*  23 ~ Triangle waveform */ \
  .CC_TEST  =  0x18,  /*  24 ~ Test bit */ \
  .CC_RMOD  =  0x19,  /*  25 ~ Ring modulator bit */ \
  .CC_SYNC  =  0x1A,  /*  26 ~ Sync bit */ \
  .CC_GATE  =  0x1B,  /*  27 ~ Gate bit */ \
  /* Default values ~ Envelope. MIDI 1.0 Sound Controllers */ \
  .CC_ATT   =  0x49,  /*  73 ~ Attack (Attack Time) */ \
  .CC_DEC   =  0x4B,  /*  75 ~ Decay (Decay Time) */ \
  .CC_SUS   =  0x46,  /*  70 ~ Sustain (Sound Controller 1) */ \
  .CC_REL   =  0x48,  /*  72 ~ Release (Release Time) */ \
  /* Default values ~ Chip related */ \
  .CC_FFC   =  0x4A,  /*  74 ~ Filter Frequency Cutoff (Brightness) */ \
  .CC_RES   =  0x47,  /*  71 ~ Filter resonance (Timbre/Harmonic Intensity) */ \
  /* Filter routing ~ General Purpose Controllers 1-4 */ \
  .CC_FLT1  =  0x10,  /*  16 ~ Filter voice 1 */ \
  .CC_FLT2  =  0x11,  /*  17 ~ Filter voice 2 */ \
  .CC_FLT3  =  0x12,  /*  18 ~ Filter voice 3 */ \
  .CC_FLTE  =  0x13,  /*  19 ~ Filter external */ \
  /* Filter mode ~ General Purpose Controllers 5-8 */ \
  .CC_LPF   =  0x50,  /*  80 ~ Low pass */ \
  .CC_BPF   =  0x51,  /*  81 ~ Band pass */ \
  .CC_HPF   =  0x52,  /*  82 ~ High pass */ \
  .CC_3OFF  =  0x53,  /*  83 ~ Voice 3 disconnect */ \
  .CC_VOL   =  0x07,  /*   7 ~ Channel Volume */ \
  /* Default CC values ~ SID / Voice selection */ \
  .CC_SID1  =  0x68,  /* 104 ~ Select SID 1 */ \
  .CC_SID2  =  0x69,  /* 105 ~ Select SID 2 */ \
  .CC_SID3  =  0x6A,  /* 106 ~ Select SID 3 */ \
  .CC_SID4  =  0x6B,  /* 107 ~ Select SID 4 */ \
  .CC_VCE1  =  0x6C,  /* 108 ~ Select Voice 1 */ \
  .CC_VCE2  =  0x6D,  /* 109 ~ Select Voice 2 */ \
  .CC_VCE3  =  0x6E,  /* 110 ~ Select Voice 3 */ \
  /* Default CC values ~ custom commands */ \
  .CC_GTEN  =  0x1D,  /*  29 ~ Gate auto enabled on noteon note off */ \
  .CC_SPLY  =  0x6F,  /* 111 ~ Turn on polyfonic for current SID */ \
  .CC_VELM  =  0x1E,  /*  30 ~ Velocity Mode */ \
  /* Default values ~ modulation and timing */ \
  .CC_LFOW  =  0x66,  /* 102 ~ LFO waveform */ \
  .CC_LFOR  =  0x4C,  /*  76 ~ LFO rate (Vibrato Rate) */ \
  .CC_LFOD  =  0x4D,  /*  77 ~ LFO depth (Vibrato Depth) */ \
  .CC_LFOT  =  0x67,  /* 103 ~ LFO destination */ \
  .CC_PORT  =  0x05,  /*   5 ~ Portamento time */ \
  /* Default values ~ Arpeggiator */ \
  .CC_ARPE  =  0x70,  /* 112 ~ Arpeggiator enable */ \
  .CC_ARPM  =  0x71,  /* 113 ~ Arpeggiator mode */ \
  .CC_ARPR  =  0x72,  /* 114 ~ Arpeggiator rate */ \
  .CC_ARPO  =  0x73,  /* 115 ~ Arpeggiator octave range */ \
  .CC_ARPT  =  0x74,  /* 116 ~ Arpeggiator table select */ \
  /* Default values ~ second LFO */ \
  .CC_LFO2W =  0x59,  /*  89 ~ LFO 2 waveform */ \
  .CC_LFO2R =  0x0E,  /*  14 ~ LFO 2 rate */ \
  .CC_LFO2D =  0x0F,  /*  15 ~ LFO 2 depth */ \
  .CC_LFO2T =  0x5A,  /*  90 ~ LFO 2 destination */ \
  /* Default values ~ unison. MIDI 1.0 Effects 3/4 Depth (chorus, detune) */ \
  .CC_UNIS  =  0x5D,  /*  93 ~ Unison mode on/off */ \
  .CC_UDET  =  0x5E,  /*  94 ~ Unison detune spread */ \
  /* Default values ~ FMOpl */ \
  .CC_FMEN  =  0x1F,  /*  31 ~ Target this channel at the FMOpl chip instead of a SID */ \
  /* Embedded application (Cynthcart, SID SYNTH) _FIXED_ CC values */ \
  .CC_CEN   =  0x75,  /* 117 ~ Enable embedded application */ \
  .CC_CDI   =  0x76,  /* 118 ~ Disable embedded application */ \
  .CC_CRE   =  0x77,  /* 119 ~ Reset embedded application */ \
  /* Midi related ~ Fixed values */ \
  .CC_ASOF  =  0x78,  /* 120 ~ All Sound Off */ \
  .CC_RACT  =  0x79,  /* 121 ~ Reset All Controllers */ \
  .CC_ANOF  =  0x7B,  /* 123 ~ All Notes Off */ \
  .CC_MONO  =  0x7E,  /* 126 ~ Mono Mode On ~ Poly off & all notes off */ \
  .CC_POLY  =  0x7F,  /* 127 ~ Poly Mode On ~ Mono off & all notes off */ \
  /* Unused values ~ Midi related */ \
  .CC_BMSB  =  0xFF,  /* 255 ~ Bank Select MSB */ \
  .CC_BLSB  =  0xFF,  /* 255 ~ Bank Select LSB */ \
  .CC_MOD   =  0xFF,  /* 255 ~ Modulation wheel */ \
  .CC_MODL  =  0xFF,  /* 255 ~ Modulation wheel LSB */ \
} \


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_CC_H_ */
