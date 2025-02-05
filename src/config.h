/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * config.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
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
#include "pico/flash.h"
#include "pico/stdlib.h"

/* Hardware api's */
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"


/* Config constants */
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_SIZE (FLASH_SECTOR_SIZE / 4)  /* 1024 Bytes */
/* Compile time variable settings */
#ifndef MAGIC_SMOKE
#define MAGIC_SMOKE 19700101  /* DATEOFRELEASE */
#endif
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.3.0-BETA.20250205"  /* Must be the same as in CMakeLists.txt */
#endif

#ifdef PICO_DEFAULT_LED_PIN
#define LED_PWM true
#else
#define LED_PWM false
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
    bool    dualsid : 1;       /* enable / disable dual SID support for this socket (requires clone) */
    uint8_t chiptype;          /* 0 = real, 1 = clone */
    uint8_t clonetype;         /* 0 = disabled, 1 = SKPico, 2 = ARMSID, 3 = FPGASID, 4 = other */
    uint8_t sid1type;          /* 0 = unknown, 1 = n/a, 2 = MOS8085, 3 = MOS6581 */
    uint8_t sid2type;          /* 0 = unknown, 1 = FMopl, 2 = MOS8085, 3 = MOS6581 */
  } socketOne;                 /* 1 */
  struct {
    bool    enabled : 1;       /* enable / disable this socket */
    bool    dualsid : 1;       /* enable / disable dual SID support for this socket (requires clone) */
    bool    act_as_one : 1;    /* act as socket 1 */
    uint8_t chiptype;          /* 0 = real, 1 = clone */
    uint8_t clonetype;         /* 0 = disabled, 1 = SKPico, 2 = ARMSID, 3 = FPGASID, 4 = other */
    uint8_t sid1type;          /* 0 = unknown, 1 = n/a, 2 = MOS8085, 3 = MOS6581 */
    uint8_t sid2type;          /* 0 = unknown, 1 = FMopl, 2 = MOS8085, 3 = MOS6581 */
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

  READ_CONFIG      = 0x30,  /* Read full config as bytes */
  APPLY_CONFIG     = 0x31,  /* Apply config from memory */
  SET_CONFIG       = 0x32,  /* Set single config item */
  SAVE_CONFIG      = 0x33,  /* Save and load config and then reboot */
  SAVE_NORESET     = 0x34,  /* Save, load and apply config */
  RESET_CONFIG     = 0x35,  /* Reset to default settings */
  WRITE_CONFIG     = 0x36,  /* Write full config as bytes */
  READ_SOCKETCFG   = 0x37,  /* Read socket config as bytes */
  RELOAD_CONFIG    = 0x38,  /* Reload and apply stored config from flash */

  SINGLE_SID       = 0x40,
  DUAL_SID         = 0x41,
  QUAD_SID         = 0x42,
  TRIPLE_SID       = 0x43,
  TRIPLE_SID_TWO   = 0x44,
  MIRRORED_SID     = 0x45,
  DUAL_SOCKET1     = 0x46,
  DUAL_SOCKET2     = 0x47,

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
