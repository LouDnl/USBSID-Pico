/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * vu.h
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

#ifndef _USBSID_VU_H_
#define _USBSID_VU_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>


/* LED breathe levels */
enum
{
  ALWAYS_OFF   = 99999,   /* non zero is off */
  ALWAYS_ON    = 0,       /* zero is on */
  CHECK_INTV   = 100000,  /* 100ms == 100000us */
  MAX_CHECKS   = 100,     /* 100 checks times 100ms == 10 seconds */
  BREATHE_INTV = 1000,    /* 1ms == 1000us */
  BREATHE_STEP = 100,     /* steps in brightness */
  VU_MAX       = 65534,   /* max vu brightness */
  HZ_MAX       = 40       /* No clue where I got this from 😅 but hey it works! */
};

/* VU meter value (written by Core 1 bus operations, read by LED runner) */
extern volatile uint16_t vu;

/* Functions from vu.c */
void init_vu(void);
void led_runner(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_VU_H_ */
