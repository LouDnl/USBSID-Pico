/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_cc.h
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

#ifdef __cplusplus
  extern "C" {
#endif

// TODO:
// Add command for enabling/disabling multivoice settings
// Add command for linking voices e.g. copy settings from selected voice to voice x
typedef struct midi_ccvalues {
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
  uint8_t CC_CVCE;  /* Enable copy voice mode */
  uint8_t CC_CSID;  /* Enable copy SID mode */
  uint8_t CC_LVCE;  /* Link/Unlink voice */
  uint8_t CC_LSID;  /* Link/Unlink SID */
  /* Fixed CC values ~ Cynthcart related */
  uint8_t CC_CEN;   /* Enable  Cynthcart */
  uint8_t CC_CDI;   /* Disable Cynthcart */
  uint8_t CC_CRE;   /* Restart Cynthcart */
  /* Unused values ~ Midi related */
  uint8_t CC_BMSB;  /* Bank Select MSB */
  uint8_t CC_BLSB;  /* Bank Select LSB */
  uint8_t CC_MOD;   /* Modulation wheel */
  uint8_t CC_MODL;  /* Modulation wheel LSB */
  uint8_t CC_AOFF;  /* All notes off */
} midi_ccvalues;

/* Default Control Change Byte values */
#define MIDI_DEFAULT_CCVALUES_INIT { \
  /* Default values ~ Voice related */ \
  .CC_NOTE =  0x16,  /*  22 ~ Note frequency for manual input */ \
  .CC_PWM  =  0x15,  /*  21 ~ Pulse Width Modulation (Modulation wheel) */ \
  .CC_NOIS =  0x00,  /*   0 ~ Noise waveform */ \
  .CC_PULS =  0x01,  /*   1 ~ Pulse waveform */ \
  .CC_SAWT =  0x02,  /*   2 ~ Sawtooth waveform */ \
  .CC_TRIA =  0x03,  /*   3 ~ Triangle waveform */ \
  .CC_TEST =  0x04,  /*   4 ~ Test bit */ \
  .CC_RMOD =  0x05,  /*   5 ~ Ring modulator bit */ \
  .CC_SYNC =  0x06,  /*   6 ~ Sync bit */ \
  .CC_GATE =  0x07,  /*   7 ~ Gate bit */ \
  .CC_ATT  =  0x11,  /*  17 ~ Attack */ \
  .CC_DEC  =  0x12,  /*  18 ~ Decay */ \
  .CC_SUS  =  0x13,  /*  19 ~ Sustain */ \
  .CC_REL  =  0x14,  /*  20 ~ Release */ \
  /* Default values ~ Chip related */ \
  .CC_FFC  =  0x20,  /*  32 ~ Filter Frequency Cutoff */ \
  .CC_RES  =  0x21,  /*  33 ~ Filter resonance */ \
  .CC_FLT1 =  0x22,  /*  34 ~ Filter voice 1 */ \
  .CC_FLT2 =  0x23,  /*  35 ~ Filter voice 2 */ \
  .CC_FLT3 =  0x24,  /*  36 ~ Filter voice 3 */ \
  .CC_FLTE =  0x25,  /*  37 ~ Filter external */ \
  .CC_3OFF =  0x30,  /*  48 ~ Voice 3 disconnect */ \
  .CC_HPF  =  0x31,  /*  49 ~ High pass */ \
  .CC_BPF  =  0x32,  /*  50 ~ Band pass */ \
  .CC_LPF  =  0x33,  /*  51 ~ Low pass */ \
  .CC_VOL  =  0x34,  /*  52 ~ Channel Volume */ \
  /* Default CC values ~ SID / Voice selection */ \
  .CC_SID1 =  0x68,  /* 104 ~ Select SID 1 */ \
  .CC_SID2 =  0x69,  /* 105 ~ Select SID 2 */ \
  .CC_SID3 =  0x6A,  /* 106 ~ Select SID 3 */ \
  .CC_SID4 =  0x6B,  /* 107 ~ Select SID 4 */ \
  .CC_VCE1 =  0x6C,  /* 108 ~ Select Voice 1 */ \
  .CC_VCE2 =  0x6D,  /* 109 ~ Select Voice 2 */ \
  .CC_VCE3 =  0x6E,  /* 110 ~ Select Voice 3 */ \
  /* Default CC values ~ custom commands */ \
  .CC_GTEN =  0x40,  /*  64 ~ Gate auto enabled on noteon note off */ \
  .CC_CVCE =  0x6F,  /* 111 ~ Enable copy voice mode */ \
  .CC_CSID =  0x08,  /*   8 ~ Enable copy SID mode */ \
  .CC_LVCE =  0x18,  /*  24 ~ Link/Unlink voice */ \
  .CC_LSID =  0x28,  /*  40 ~ Link/Unlink SID */ \
  /* Cynthcart related _FIXED_ CC values */ \
  .CC_CEN  =  0x55,  /*  85 ~ Enable Cynthcart */ \
  .CC_CDI  =  0x56,  /*  86 ~ Disable Cynthcart */ \
  .CC_CRE  =  0x57,  /*  87 ~ Reset Cynthcart */ \
  /* Unused values ~ Midi related */ \
  .CC_BMSB =  0xFF,  /* 255 ~ Bank Select MSB */ \
  .CC_BLSB =  0xFF,  /* 255 ~ Bank Select LSB */ \
  .CC_MOD  =  0xFF,  /* 255 ~ Modulation wheel */ \
  .CC_MODL =  0xFF,  /* 255 ~ Modulation wheel LSB */ \
  .CC_AOFF =  0xFF,  /* 255 ~ All notes off */ \
} \

/* static const midi_ccvalues midi_ccvalues_defaults = MIDI_DEFAULT_CCVALUES_INIT; */
/* const midi_ccvalues midi_ccvalues_defaults = MIDI_DEFAULT_CCVALUES_INIT; */


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_CC_H_ */
