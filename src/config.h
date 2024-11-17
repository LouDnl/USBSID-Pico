/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * config.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
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

#ifndef _USBSID_CONFIG_H_
#define _USBSID_CONFIG_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pico libs */
#include "pico/mem_ops.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

/* Hardware api's */
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"


/* Config constants */
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_SIZE (FLASH_SECTOR_SIZE / 4)  /* 1024 Bytes */
#define PROJECT_VERSION "0.2.1-BETA.20241109"
/* Compile time variable settings */
#ifndef MAGIC_SMOKE
#define MAGIC_SMOKE 19700101  /* DATEOFRELEASE */
#endif
#ifdef USE_RGB
#define RGB_ENABLED true
#else
#define RGB_ENABLED false
#endif

/* USBSID-Pico config struct */
typedef struct Config {
  uint32_t magic;
  int default_config;
  bool external_clock : 1;     /* enable / disable external oscillator */
  uint32_t clock_rate;         /* clock speed identifier */
  struct
  {
    bool    enabled : 1;       /* enable / disable this socket */
    bool    dualsid : 1;       /* enable / disable dual SID support for this socket */
    uint8_t sidtype;           /* 0 = empty, 1 = 6581, 2 = 8085, 3 = SKPico or other clone */
  } socketOne;                 /* 1 */
  struct {
    bool    enabled : 1;       /* enable / disable this socket */
    bool    dualsid : 1;       /* enable / disable dual SID support for this socket */
    bool    act_as_one : 1;    /* act as socket 1 */
    uint8_t sidtype;           /* 0 = empty, 1 = 6581, 2 = 8085, 3 = SKPico or other clone */
  } socketTwo;                 /* 2 */
  struct {
    bool enabled : 1;
    bool idle_breathe : 1;
  } LED;                       /* 3 */
  struct {
    bool    enabled : 1;
    bool    idle_breathe : 1;
    uint8_t brightness;
    int     sid_to_use;         /* 0 = sid 1&2, 1...4 = sid 1 ... sid 4 */
  } RGBLED;                     /* 4 */
  struct {
    bool enabled : 1;
  } Cdc;                        /* 5 */
  struct {
    bool enabled : 1;
  } WebUSB;                     /* 6 */
  struct {
    bool enabled : 1;
  } Asid;                       /* 7 */
  struct {
    bool enabled : 1;
    uint8_t sid_states[4][32];  /* Stores states of each SID ~ 4 sids max */
  } Midi;                       /* 8 */
} Config;

extern Config usbsid_config;  /* Make Config struct global */

/* Config command byte */
enum
{
  RESET_USBSID     = 0x20,

  READ_CONFIG      = 0x30,
  APPLY_CONFIG     = 0x31,
  STORE_CONFIG     = 0x32,
  SAVE_CONFIG      = 0x33,
  SAVE_NORESET     = 0x34,
  RESET_CONFIG     = 0x35,

  SINGLE_SID       = 0x40,
  DUAL_SID         = 0x41,
  QUAD_SID         = 0x42,
  TRIPLE_SID       = 0x43,

  SET_CLOCK        = 0x50,
  DETECT_SIDS      = 0x51,
  TEST_ALLSIDS     = 0x52,
  TEST_SID1        = 0x53,
  TEST_SID2        = 0x54,
  TEST_SID3        = 0x55,
  TEST_SID4        = 0x56,

  LOAD_MIDI_STATE  = 0x60,
  SAVE_MIDI_STATE  = 0x61,
  RESET_MIDI_STATE = 0x63,

  USBSID_VERSION   = 0x80,

  TEST_FN          = 0x99,  /* TODO: Remove before v1 release */
};


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_CONFIG_H_ */
