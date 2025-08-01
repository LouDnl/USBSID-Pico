/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
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

#ifndef _USBSID_MIDI_H_
#define _USBSID_MIDI_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Pico libs */
#include "pico/stdlib.h"


typedef enum {
  CLAIMED,
  FREE
} bus_state;

typedef enum {
  IDLE,
  RECEIVING,
  WAITING_FOR_END
} sysex_state;

typedef enum {
  NONE,
  MIDI,
  SYSEX,
  ASID
} midi_type;

extern int midi_bytes;

typedef struct {
  /* comms */
  bus_state bus;
  sysex_state state;
  midi_type type;
  uint8_t index;
  uint8_t usbstreambuffer[64];   /* Normal speed max buffer for TinyUSB */
  uint8_t streambuffer[64];   /* Normal speed max buffer for TinyUSB */
  uint8_t channel_states[16][4][32];  /* Stores channel states of each SID ~ 4 sids max */
  uint8_t channelkey_states[16][4][4];  /* Stores channelkey states of each SID ~ 4 sids max */
  uint8_t channelkey_notestates[16][4][128];  /* Stores channelkey states of each SID ~ 4 sids max */
  uint8_t channelbank[16];  /* [Channel][Bank(0)] relation */
  uint8_t channelprogram[16];  /* [Channel][Patch/Program(1)] relation */
  bool bank1channelgate[16];  /* Auto gate on/off per channel on noteon and noteoff */
  uint8_t sidaddress;
  int fmopl;

} midi_machine;

extern midi_machine midimachine;
extern int curr_midi_channel;

enum {
  N_KEYS = 0,  /* n keys pressed */
  V1_ACTIVE = 1,
  V2_ACTIVE = 2,
  V3_ACTIVE = 3,
};

/* Processes the received Midi stream */
// void process_stream(uint8_t* buffer);  // 20240723 ~ disabled, unused

/* Custom values from CMakeLists import */
// TODO: Create import file to use

#ifndef CC_VALUES  /* Customizable CC Bytes */
#define CC_VALUES  /* Fallback for backwards compatibility */

/* Default Control Change Byte values */
#ifndef CC_PWM
#define CC_PWM  0x01  /*   1 ~ Pulse Width Modulation (Modulation wheel) */
#endif /* CC_PWM */
#ifndef CC_NOTE
#define CC_NOTE 0x14  /*  20 ~ Note frequency */
#endif /* CC_NOTE */
#ifndef CC_NOIS
#define CC_NOIS 0x15  /*  21 ~ Noise waveform */
#endif /* CC_NOIS */
#ifndef CC_PULS
#define CC_PULS 0x16  /*  22 ~ Pulse waveform */
#endif /* CC_PULS */
#ifndef CC_SAWT
#define CC_SAWT 0x17  /*  23 ~ Sawtooth waveform */
#endif /* CC_SAWT */
#ifndef CC_TRIA
#define CC_TRIA 0x18  /*  24 ~ Triangle waveform */
#endif /* CC_TRIA */
#ifndef CC_TEST
#define CC_TEST 0x19  /*  25 ~ Test bit */
#endif /* CC_TEST */
#ifndef CC_RMOD
#define CC_RMOD 0x1A  /*  26 ~ Ring modulator bit */
#endif /* CC_RMOD */
#ifndef CC_SYNC
#define CC_SYNC 0x1B  /*  27 ~ Sync bit */
#endif /* CC_SYNC */
#ifndef CC_GATE
#define CC_GATE 0x1C  /*  28 ~ Gate bit */
#endif /* CC_GATE */
#ifndef CC_GTEN
#define CC_GTEN 0x1D  /*  29 ~ Gate auto enabled on noteon note off */
#endif /* CC_GTEN */
#ifndef CC_FFC
#define CC_FFC 0x6E   /* 110 ~ Filter Frequency Cutoff */
#endif /* CC_FFC */
#ifndef CC_RES
#define CC_RES 0x6F   /* 111 ~ Filter resonance */
#endif /* CC_RES */
#ifndef CC_3OFF
#define CC_3OFF 0x70  /* 112 ~ Voice 3 disconnect */
#endif /* CC_3OFF */
#ifndef CC_FLT1
#define CC_FLT1 0x71  /* 113 ~ Filter channel 1 */
#endif /* CC_FLT1 */
#ifndef CC_FLT2
#define CC_FLT2 0x72  /* 114 ~ Filter channel 2 */
#endif /* CC_FLT2 */
#ifndef CC_FLT3
#define CC_FLT3 0x73  /* 115 ~ Filter channel 3 */
#endif /* CC_FLT3 */
#ifndef CC_FLTE
#define CC_FLTE 0x74  /* 116 ~ Filter external */
#endif /* CC_FLTE */
#ifndef CC_HPF
#define CC_HPF 0x75   /* 117 ~ High pass */
#endif /* CC_HPF */
#ifndef CC_BPF
#define CC_BPF 0x76   /* 118 ~ Band pass */
#endif /* CC_BPF */
#ifndef CC_LPF
#define CC_LPF 0x77   /* 119 ~ Low pass */
#endif /* CC_LPF */

#endif /* CC_VALUES */

/* Fixed CC Bytes */
#define CC_BMSB 0x00  /*   0 ~ Bank Select MSB */
#define CC_BLSB 0x20  /*  32 ~ Bank Select LSB */
#define CC_VOL  0x07  /*   7 ~ Set Master Volume */
#define CC_ATT  0x49  /*  73 ~ Attack */
#define CC_DEL  0x4B  /*  75 ~ Decay */
#define CC_SUS  0x40  /*  64 ~ Sustain */
#define CC_REL  0x48  /*  72 ~ Release */

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_H_ */
