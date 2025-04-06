/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
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

/* Flash information
 *
 * USBSID-Pico flash layout:
 * - 1024KB FW (rp2040)
 * - 3072KB FW (rp2350)
 * - 1024KB Persistent storage
 *
 * On rp2350 the flash is cleared completely every time a new uf2 is loaded,
 * this results in a default config on each fw flash.
 * This can be solved by reserving a larger part of the flash for persistent
 * storage e.g. 1024KB instead of only 4 KB
 *
 * Non working solutions
 * - adding a partitiontable had no effect
 * - adding a 4KB persistent part to the flash had no effect
 *
 * Ram and flash defaults on official pico boards:
 * XIP_BASE ~ Flash base address 0x10000000
 *
 * rp2040 PICO_FLASH_SIZE_BYTES ~ (1 * 1024 * 1024) = 0x200000 == 2MB(2097152)
 * rp2350 PICO_FLASH_SIZE_BYTES ~ (2 * 1024 * 1024) = 0x400000 == 4MB(4194304)
 * rp2040 RAM_SIZE ~ 0x40000 == 256KB(262144)
 * rp2350 RAM_SIZE ~ 0x80000 == 512KB(524288)
 *
 * FLASH_PAGE_SIZE   ~ (1u << 8)  = 0x100   == 256B
 * FLASH_SECTOR_SIZE ~ (1u << 12) = 0x1000  == 4096B
 * FLASH_BLOCK_SIZE  ~ (1u << 16) = 0x10000 == 65536B
 */


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
  uint32_t clock_rate;         /* clock speed identifier */
  uint32_t raster_rate;        /* internal use raster rate identifier ~ not configurable */
  struct
  {
    uint8_t chiptype;          /* 0 = real, 1 = clone */
    uint8_t clonetype;         /* 0 = disabled, 1 = SKPico, 2 = ARMSID, 3 = FPGASID, 4 = other */
    uint8_t sid1type;          /* 0 = unknown, 1 = N/A, 2 = MOS8085, 3 = MOS6581, 4 = FMopl */
    uint8_t sid2type;          /* 0 = unknown, 1 = N/A, 2 = MOS8085, 3 = MOS6581, 4 = FMopl */
    bool    enabled : 1;       /* enable / disable this socket */
    bool    dualsid : 1;       /* enable / disable dual SID support for this socket (requires clone) */
  } socketOne;                 /* 1 */
  struct {
    uint8_t chiptype;          /* 0 = real, 1 = clone */
    uint8_t clonetype;         /* 0 = disabled, 1 = SKPico, 2 = ARMSID, 3 = FPGASID, 4 = other */
    uint8_t sid1type;          /* 0 = unknown, 1 = N/A, 2 = MOS8085, 3 = MOS6581, 4 = FMopl */
    uint8_t sid2type;          /* 0 = unknown, 1 = N/A, 2 = MOS8085, 3 = MOS6581, 4 = FMopl */
    bool    enabled : 1;       /* enable / disable this socket */
    bool    dualsid : 1;       /* enable / disable dual SID support for this socket (requires clone) */
    bool    act_as_one : 1;    /* act as socket 1 */
  } socketTwo;                 /* 2 */
  struct {
    bool enabled : 1;
    bool idle_breathe : 1;
  } LED;                       /* 3 */
  struct {
    uint8_t brightness;
    int     sid_to_use;         /* 0/-1 = off, 1...4 = sid 1 ... sid 4 */
    bool    enabled : 1;
    bool    idle_breathe : 1;
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
    uint8_t sid_states[4][32];  /* Stores states of each SID ~ 4 sids max */
    bool enabled : 1;
  } Midi;                       /* 8 */
  struct {
    int sidno;                  /* 0 = disabled, saves the sidno of the sid set to FMOpl */
    bool enabled : 1;           /* Requires a clone SID! */
  } FMOpl;                      /* 9 */
  bool external_clock : 1;      /* enable / disable external oscillator */
  bool lock_clockrate : 1;      /* lock the set clockspeed from being changed */
  bool stereo_en : 1;           /* audio switch is off (mono) or on (stereo) ~ (PCB v1.3+ only) */
} Config;

extern Config usbsid_config;  /* Make Config struct global */

/* Config command byte */
enum
{
  RESET_USBSID     = 0x20,  /* Resets the MCU including the USB connection */

  READ_CONFIG      = 0x30,  /* Read full config as bytes */
  APPLY_CONFIG     = 0x31,  /* Apply config from memory */
  SET_CONFIG       = 0x32,  /* Set single config item */
  SAVE_CONFIG      = 0x33,  /* Save and load config and then reboot */
  SAVE_NORESET     = 0x34,  /* Save, load and apply config */
  RESET_CONFIG     = 0x35,  /* Reset to default settings */
  WRITE_CONFIG     = 0x36,  /* Write full config as bytes */
  READ_SOCKETCFG   = 0x37,  /* Read socket config as bytes */
  RELOAD_CONFIG    = 0x38,  /* Reload and apply stored config from flash */
  READ_NUMSIDS     = 0x39,  /* Returns the number of SIDs in byte 0 */
  READ_FMOPLSID    = 0x3A,  /* Returns the sidno for FMOpl 1~4, 0 is disable  */

  SINGLE_SID       = 0x40,  /* Single SID Socket One */
  DUAL_SID         = 0x41,  /* Dual SID Socket One */
  QUAD_SID         = 0x42,  /* Four SID's in 2 sockets */
  TRIPLE_SID       = 0x43,  /* Two SID's in socket One, One SID in socket two */
  TRIPLE_SID_TWO   = 0x44,  /* One SID in Socket One, Two SID's in socket two */
  MIRRORED_SID     = 0x45,  /* Socket Two is linked to Socket One */
  DUAL_SOCKET1     = 0x46,  /* Two SID's in socket One, Socket Two disabled */
  DUAL_SOCKET2     = 0x47,  /* Two SID's in socket Two, Socket One disabled */

  SET_CLOCK        = 0x50,  /* Change SID clock frequency by array id */
  DETECT_SIDS      = 0x51,  /* Try to detect the SID types per socket */
  TEST_ALLSIDS     = 0x52,  /* Runs a very long test on all SID's */
  TEST_SID1        = 0x53,  /* Runs a very long test on SID 1 */
  TEST_SID2        = 0x54,  /* Runs a very long test on SID 2 */
  TEST_SID3        = 0x55,  /* Runs a very long test on SID 3 */
  TEST_SID4        = 0x56,  /* Runs a very long test on SID 4 */
  GET_CLOCK        = 0x57,  /* Returns the clockrate as array id in byte 0 */
  LOCK_CLOCK       = 0x58,  /* Locks the clockrate from being changed, saved in config */
  STOP_TESTS       = 0x59,  /* Interrupt any running SID tests */

  LOAD_MIDI_STATE  = 0x60,
  SAVE_MIDI_STATE  = 0x61,
  RESET_MIDI_STATE = 0x63,

  USBSID_VERSION   = 0x80,  /* Read version identifier as uint32_t */

  RESTART_BUS      = 0x85,  /* Restart DMA & PIO */
  RESTART_BUS_CLK  = 0x86,  /* Restart PIO clocks */
  SYNC_PIOS        = 0x87,  /* Sync PIO clocks */
  TOGGLE_AUDIO     = 0x88,  /* Toggle mono <-> stereo (v1.3+ boards only) */
  SET_AUDIO        = 0x89,  /* Set mono <-> stereo (v1.3+ boards only) */

  TEST_FN          = 0x99,  /* TODO: Remove before v1 release */
};


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_CONFIG_H_ */
