/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
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
 * Copyright (c) 2024 LouD
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

#ifndef _MIDI_H_
#define _MIDI_H_
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
#include "pico/mem_ops.h"


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
  // unsigned int index;
  uint8_t index;
  uint8_t readbuffer[1];
  uint8_t streambuffer[64];   /* Normal speed max buffer for TinyUSB */
  /* states */
  // uint8_t sid_states[4][32];  /* Stores register states of each SID ~ 4 sids max */
  // uint8_t sid_extras[4][32];  /* Stores extra settings of each SID ~ 4 sids max */
  uint8_t channel_states[16][4][32];  /* Stores channel states of each SID ~ 4 sids max */
  // uint8_t channelkey_notestates[16][4][128];  /* Stores channelkey states of each SID ~ 4 sids max */
  uint8_t channelkey_states[16][4][4];  /* Stores channelkey states of each SID ~ 4 sids max */
  uint8_t channelbank[16];  /* [Channel][Bank(0)] relation */
  uint8_t channelprogram[16];  /* [Channel][Patch/Program(1)] relation */
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

/* Initialize the midi handlers */
void midi_init(void);

/* Processes a 1 byte incoming midi buffer
 * Figures out if we're receiving midi or sysex */
void process_buffer(uint8_t buffer);

/* Processes the received Midi stream */
// void process_stream(uint8_t* buffer);  // 20240723 ~ disabled, unused


#ifdef __cplusplus
  }
#endif

#endif /* _MIDI_H_ */
