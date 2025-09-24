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

/* Pico libs */
#include "pico/stdlib.h"


/* Midi bus states */
typedef enum {
  CLAIMED,
  FREE
} bus_state;

/* Midi sysex state */
typedef enum {
  IDLE,
  RECEIVING,
  WAITING_FOR_END
} sysex_state;

/* Midi receiving type */
typedef enum {
  NONE,
  MIDI,
  SYSEX,
  ASID
} midi_type;

/* Base midi communication struct */
typedef struct {
  /* comms */
  bus_state bus;
  sysex_state state;
  midi_type type;
  int fmopl;
  int midi_bytes;
  uint8_t index;
  uint8_t usbstreambuffer[64];   /* Normal speed max buffer size for TinyUSB */
  uint8_t streambuffer[64];   /* Normal speed max buffer size for TinyUSB */
} midi_machine;

/* Is init in usbsid.c */
extern midi_machine midimachine;

/* Cynthcart~Emudore queue */
typedef struct {
  uint8_t data;
} cynthcart_queue_entry_t;



#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_H_ */
