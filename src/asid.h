/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * asid.h
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

#ifndef _USBSID_ASID_H_
#define _USBSID_ASID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>


/* FMOPL ~ Really, CAPSLOCK ONLY HUH? */
#define MAX_FM_REG_PAIRS 16
#define OPL_REG_ADDRESS 0x00
#define OPL_REG_DATA 0x10

/* ASID, or ACID? */
#define NO_SID_REGISTERS_ASID 28 /* 25 + 3 extra */

/* SID Register order for ASID */
static const uint8_t asid_sid_registers[NO_SID_REGISTERS_ASID] =
{ /* register */
  /* hex, dec, bit */
  0x00, //  0 -  0
  0x01, //  1 -  1
  0x02, //  2 -  2
  0x03, //  3 -  3
  0x05, //  5 -  4
  0x06, //  6 -  5
  0x07, //  7 -  6
  0x08, //  8 -  7
  0x09, //  9 -  8
  0x0a, // 10 -  9
  0x0c, // 12 - 10
  0x0d, // 13 - 11
  0x0e, // 14 - 12
  0x0f, // 15 - 13
  0x10, // 16 - 14
  0x11, // 17 - 15
  0x13, // 19 - 16
  0x14, // 20 - 17
  0x15, // 21 - 18
  0x16, // 22 - 19
  0x17, // 23 - 20
  0x18, // 24 - 21
  0x04, //  4 - 22
  0x0b, // 11 - 23
  0x12, // 18 - 24
  0x04, //  4 - 25 <= secondary for reg 04
  0x0b, // 11 - 26 <= secondary for reg 11
  0x12, // 18 - 27 <= secondary for reg 18
};


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_ASID_H_ */
