/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
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

#ifndef _ASID_H_
#define _ASID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* Pico libs */
#include "pico/mem_ops.h"


/* SID Register order for ASID */
static const uint8_t asid_sid_registers[] =
{
  /* register, bit */
  0x00, // 0
  0x01, // 1
  0x02, // 2
  0x03, // 3
  0x05, // 4
  0x06, // 5
  0x07, // 6
  0x08, // 7
  0x09, // 8
  0x0a, // 9
  0x0c, // 10
  0x0d, // 11
  0x0e, // 12
  0x0f, // 13
  0x10, // 14
  0x11, // 15
  0x13, // 16
  0x14, // 17
  0x15, // 18
  0x16, // 19
  0x17, // 20
  0x18, // 21
  0x04, // 22
  0x0b, // 23
  0x12, // 24
  0x04, // 25 <= secondary for reg 04
  0x0b, // 26 <= secondary for reg 11
  0x12, // 27 <= secondary for reg 18
};


#ifdef __cplusplus
  }
#endif

#endif /* _ASID_H_ */
