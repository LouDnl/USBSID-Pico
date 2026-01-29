/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_ccdefaults.h
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

#ifndef _USBSID_MIDI_CCDEFAULTS_H_
#define _USBSID_MIDI_CCDEFAULTS_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


typedef struct midi_ccvalues {
  /* Modifyable values ~ Voice related */
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
  uint8_t CC_GTEN;  /* Gate auto enabled on noteon note off */
  /* Modifyable values ~ Chip related */
  uint8_t CC_FFC;   /* Filter Frequency Cutoff */
  uint8_t CC_RES;   /* Filter resonance */
  uint8_t CC_3OFF;  /* Voice 3 disconnect */
  uint8_t CC_FLT1;  /* Filter voice 1 */
  uint8_t CC_FLT2;  /* Filter voice 2 */
  uint8_t CC_FLT3;  /* Filter voice 3 */
  uint8_t CC_FLTE;  /* Filter external */
  uint8_t CC_HPF;   /* High pass */
  uint8_t CC_BPF;   /* Band pass */
  uint8_t CC_LPF;   /* Low pass */
  /* Fixed values ~ Midi related */
  uint8_t CC_BMSB;  /* Bank Select MSB */
  uint8_t CC_BLSB;  /* Bank Select LSB */
  uint8_t CC_MOD;   /* Modulation wheel */
  uint8_t CC_MODL;  /* Modulation wheel LSB */
  uint8_t CC_AOFF;  /* All notes off */
  /* Fixed values ~ Chip related */
  uint8_t CC_VOL;   /* Channel Volume */
  /* Fixed values ~ Voice related */
  uint8_t CC_ATT;   /* Attack */
  uint8_t CC_DEL;   /* Decay */
  uint8_t CC_SUS;   /* Sustain */
  uint8_t CC_REL;   /* Release */
  /* Fixed undefined CC values ~ Cynthcart related */
  uint8_t CC_CEN;   /* Enable  Cynthcart */
  uint8_t CC_CDI;   /* Disable Cynthcart */
  uint8_t CC_CRE;   /* Restart Cynthcart */
} midi_ccvalues;

/* Default Control Change Byte values */
#define MIDI_DEFAULT_CCVALUES_INIT { \
  /* Modifyable values ~ Voice related */ \
  .CC_NOTE =  0x14,  /*  20 ~ Note frequency */ \
  .CC_PWM  =  0x15,  /*  21 ~ Pulse Width Modulation (Modulation wheel) */ \
  .CC_NOIS =  0x16,  /*  22 ~ Noise waveform */ \
  .CC_PULS =  0x17,  /*  23 ~ Pulse waveform */ \
  .CC_SAWT =  0x18,  /*  24 ~ Sawtooth waveform */ \
  .CC_TRIA =  0x19,  /*  25 ~ Triangle waveform */ \
  .CC_TEST =  0x1A,  /*  26 ~ Test bit */ \
  .CC_RMOD =  0x1B,  /*  27 ~ Ring modulator bit */ \
  .CC_SYNC =  0x1C,  /*  28 ~ Sync bit */ \
  .CC_GATE =  0x1D,  /*  29 ~ Gate bit */ \
  .CC_GTEN =  0x1E,  /*  30 ~ Gate auto enabled on noteon note off */ \
  /* Modifyable values ~ Chip related */ \
  .CC_FFC  =  0x6E,  /* 110 ~ Filter Frequency Cutoff */ \
  .CC_RES  =  0x6F,  /* 111 ~ Filter resonance */ \
  .CC_3OFF =  0x70,  /* 112 ~ Voice 3 disconnect */ \
  .CC_FLT1 =  0x71,  /* 113 ~ Filter voice 1 */ \
  .CC_FLT2 =  0x72,  /* 114 ~ Filter voice 2 */ \
  .CC_FLT3 =  0x73,  /* 115 ~ Filter voice 3 */ \
  .CC_FLTE =  0x74,  /* 116 ~ Filter external */ \
  .CC_HPF  =  0x75,  /* 117 ~ High pass */ \
  .CC_BPF  =  0x76,  /* 118 ~ Band pass */ \
  .CC_LPF  =  0x77,  /* 119 ~ Low pass */ \
  /* Fixed values ~ Midi related */ \
  .CC_BMSB =  0x00,  /*   0 ~ Bank Select MSB */ \
  .CC_BLSB =  0x20,  /*  32 ~ Bank Select LSB */ \
  .CC_MOD  =  0x01,  /*   1 ~ Modulation wheel */ \
  .CC_MODL =  0x21,  /*  33 ~ Modulation wheel LSB */ \
  .CC_AOFF =  0x78,  /* 123 ~ All notes off */ \
  /* Fixed values ~ Chip related */ \
  .CC_VOL  =  0x07,  /*   7 ~ Channel Volume */ \
  /* Fixed values ~ Voice related */ \
  .CC_ATT  =  0x49,  /*  73 ~ Attack */ \
  .CC_DEL  =  0x4B,  /*  75 ~ Decay */ \
  .CC_SUS  =  0x40,  /*  64 ~ Sustain */ \
  .CC_REL  =  0x48,  /*  72 ~ Release */ \
  /* Fixed (undefined) CC values ~ Cynthcart related */ \
  .CC_CEN  =  0x55,  /*  85 ~ Enable Cynthcart */ \
  .CC_CDI  =  0x56,  /*  86 ~ Disable Cynthcart */ \
  .CC_CRE  =  0x57,  /*  87 ~ Reset Cynthcart */ \
} \

/* static const midi_ccvalues midi_ccvalues_defaults = MIDI_DEFAULT_CCVALUES_INIT; */
/* const midi_ccvalues midi_ccvalues_defaults = MIDI_DEFAULT_CCVALUES_INIT; */


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_CCDEFAULTS_H_ */
