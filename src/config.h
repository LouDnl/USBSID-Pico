/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config.h
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

/* Required for default config */
#include "sid.h"


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

/* Init config vars
 * Based on: https://community.element14.com/products/raspberry-pi/b/blog/posts/raspberry-pico-c-sdk-reserve-a-flash-memory-block-for-persistent-storage
 */

 /* .section_persistent from the linker script, 4 bytes aligned */
extern uint32_t ADDR_PERSISTENT[];
/* Base address */
#define ADDR_PERSISTENT_BASE_ADDR (ADDR_PERSISTENT)
/* rp2040: 0x0x1FF000, rp2350: 0x3FF000 */
#define FLASH_PERSISTENT_OFFSET ((uint32_t)ADDR_PERSISTENT_BASE_ADDR - XIP_BASE)
/* Storage size in flash memory */
#define FLASH_PERSISTENT_SIZE (PICO_FLASH_SIZE_BYTES - FLASH_PERSISTENT_OFFSET)
/* Default config offset in flash memory */
#define FLASH_CONFIG_OFFSET (FLASH_PERSISTENT_OFFSET + ((FLASH_PERSISTENT_SIZE / 4) * 3))
/* Max config size = 256 Bytes == FLASH_PAGE_SIZE (FLASH_SECTOR_SIZE / 16 config saves) */
#define CONFIG_SIZE (FLASH_SECTOR_SIZE / 16)


/* USBSID-Pico config struct */
typedef struct SIDChip {
  uint8_t id;        /* ID for this SID 0 ~ 4, 255 = disabled */
  uint8_t addr;      /* Starting address to use for this SID, 255 = disabled */
  uint8_t type;      /* 0 = unknown, 1 = N/A, 2 = MOS8580, 3 = MOS6581, 4 = FMopl */
} SIDChip;

typedef struct Socket {
  uint8_t chiptype;          /* 0 = real, 1 = clone, 2 = unknown */
  uint8_t clonetype;         /* 0 = disabled, 1 = other, 2 = SKPico, 3 = ARMSID, 4 = FPGASID, 5 = RedipSID */
  SIDChip sid1;
  SIDChip sid2;
  bool    enabled : 1;       /* enable / disable this socket */
  bool    dualsid : 1;       /* enable / disable dual SID support for this socket (requires clone) */
} Socket;

typedef struct Config {
  /* First three items must stay in the same order! */
  uint32_t magic;
  int default_config;
  uint8_t config_saveid;
  /* Don't care from here */
  uint32_t clock_rate;         /* clock speed identifier */
  uint16_t refresh_rate;       /* refresh rate identifier based on clockspeed ~ not configurable */
  uint16_t raster_rate;        /* raster rate identifier based on clockspeed ~ not configurable */
  Socket   socketOne;          /* 1 */
  Socket   socketTwo;          /* 2 */
  struct {
    bool enabled : 1;
    bool idle_breathe : 1;
  } LED;                        /* 3 */
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
    // uint8_t sid_states[4][32];  /* Stores states of each SID ~ 4 sids max */
    bool enabled : 1;
  } Midi;                       /* 8 */
  struct {
    int sidno;                  /* 0 = disabled, saves the sidno of the sid set to FMOpl */
    bool enabled : 1;           /* Requires a clone SID! */
  } FMOpl;                      /* 9 */
  bool external_clock : 1;      /* enable / disable external oscillator */
  bool lock_clockrate : 1;      /* lock the set clockspeed from being changed */
  bool stereo_en : 1;           /* audio switch is off (mono) or on (stereo) ~ (PCB v1.3+ only) */
  bool lock_audio_sw : 1;       /* lock the audio switch into it's current stateand prevent it from being changed ~ (PCB v1.3+ only) */
  bool mirrored : 1;       /* act as socket 1 */
  bool fauxstereo : 1;     /* faux stereo effect */
} Config;

#define USBSID_DEFAULT_CONFIG_INIT { \
  .magic = MAGIC_SMOKE, \
  .default_config = 1, \
  .config_saveid = 0, \
  .clock_rate = DEFAULT, \
  .refresh_rate = HZ_DEFAULT, \
  .raster_rate = R_DEFAULT, \
  .socketOne = { \
    .chiptype = 0x0,  /* real */ \
    .clonetype = 0x0, /* disabled */ \
    .sid1 = { \
      .id = 0, \
      .addr = 0x00, \
      .type = 0,  /* unknown */ \
    }, \
    .sid2 = { \
      .id = -1, \
      .addr = 0xFF, \
      .type = 0,  /* n/a */ \
    }, \
    .enabled = true, \
    .dualsid = false, \
  }, \
  .socketTwo = { \
    .chiptype = 0x0,  /* real */ \
    .clonetype = 0x0, /* disabled */ \
    .sid1 = { \
      .id = 1, \
      .addr = 0x20, \
      .type = 0,  /* unknown */ \
    }, \
    .sid2 = { \
      .id = -1, \
      .addr = 0xFF, \
      .type = 0,  /* n/a */ \
    }, \
    .enabled = true, \
    .dualsid = false, \
  }, \
  .LED = { \
    .enabled = true, \
    .idle_breathe = LED_PWM \
  }, \
  .RGBLED = { \
    .brightness = (RGB_ENABLED ? 0x7F : 0),  /* Half of max brightness or disabled if no RGB LED */ \
    .sid_to_use = (RGB_ENABLED ? 1 : -1), \
    .enabled = RGB_ENABLED, \
    .idle_breathe = RGB_ENABLED, \
  }, \
  .Cdc = { \
    .enabled = true \
  }, \
  .WebUSB = { \
    .enabled = true \
  }, \
  .Asid = { \
    .enabled = true \
  }, \
  .Midi = { \
    .enabled = true \
  }, \
  .FMOpl = { \
    .sidno = 0, \
    .enabled = false, \
  }, \
  .external_clock = false, \
  .lock_clockrate = false, \
  .stereo_en = false, \
  .lock_audio_sw = false, \
  .mirrored = false, \
  .fauxstereo = false, \
} \

static const Config usbsid_default_config = USBSID_DEFAULT_CONFIG_INIT;

typedef struct RuntimeCFG {

  /* Number of SID's available */
  uint8_t sids_one;    /* config_socket.c:apply_socket_config() */
  uint8_t sids_two;    /* config_socket.c:apply_socket_config() */
  uint8_t numsids;     /* config_socket.c:apply_socket_config() */

  /* Chip & SID type access */
  uint8_t chip_one;    /* config_socket.c:apply_socket_config() */
  uint8_t chip_two;    /* config_socket.c:apply_socket_config() */
  uint8_t fmopl_sid;   /* config_socket.c:apply_fmopl_config() */

  /* RW, CS1, CS2 mask */
  uint8_t one;         /* config_bus.c:apply_bus_config() */
  uint8_t two;         /* config_bus.c:apply_bus_config() */
  uint8_t three;       /* config_bus.c:apply_bus_config() */
  uint8_t four;        /* config_bus.c:apply_bus_config() */

  /* Address mask */
  uint8_t one_mask;    /* config_bus.c:apply_bus_config() */
  uint8_t two_mask;    /* config_bus.c:apply_bus_config() */
  uint8_t three_mask;  /* config_bus.c:apply_bus_config() */
  uint8_t four_mask;   /* config_bus.c:apply_bus_config() */

  /* Max 4 sids {S1, S2, S3, S4} defaults to 'unknown' */
  uint8_t sidtype[4];  /* config_bus.c:apply_bus_config() -> used for RGB LED verification */
  /* contains the configured sid order to physical sid order relation */
  uint8_t ids[4];      /* config_bus.c:apply_bus_config() */
  /* contains the physical sid order */
  uint8_t sidid[4];    /* config_bus.c:apply_bus_config() */
  /* contains the sid address in configured sid order */
  uint8_t sidaddr[4];  /* config_bus.c:apply_bus_config() */ // NOTE: UNUSED, FOR FUTURE USE
  /* contains the sid address masks */
  uint8_t sidmask[4];  /* config_bus.c:apply_bus_config() */
  uint8_t addrmask[4]; /* config_bus.c:apply_bus_config() */

  /* Check values */
  bool sock_one : 1;       /* config_socket.c:apply_socket_config() */
  bool sock_two : 1;       /* config_socket.c:apply_socket_config() */
  bool sock_one_dual : 1;  /* config_socket.c:apply_socket_config() */
  bool sock_two_dual : 1;  /* config_socket.c:apply_socket_config() */
  bool mirrored : 1;       /* config_socket.c:apply_socket_config() */
  bool fmopl_enabled : 1;  /* config_socket.c:apply_fmopl_config() */

} RuntimeCFG;

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
  READ_FMOPLSID    = 0x3A,  /* Returns the sidno for FMOpl 1~4, 0 is disable */

  SINGLE_SID       = 0x40,  /* Single SID Socket One */
  DUAL_SID         = 0x41,  /* Dual SID Socket One */
  QUAD_SID         = 0x42,  /* Four SID's in 2 sockets */
  TRIPLE_SID       = 0x43,  /* Two SID's in socket One, One SID in socket two */
  TRIPLE_SID_TWO   = 0x44,  /* One SID in Socket One, Two SID's in socket two */
  MIRRORED_SID     = 0x45,  /* Socket Two is linked to Socket One */
  DUAL_SOCKET1     = 0x46,  /* Two SID's in socket One, Socket Two disabled */
  DUAL_SOCKET2     = 0x47,  /* Two SID's in socket Two, Socket One disabled */

  SET_CLOCK        = 0x50,  /* Change SID clock frequency by array id */
  DETECT_SIDS      = 0x51,  /* Try to detect the SID types per socket ~ routines see sid_detection.c */
  TEST_ALLSIDS     = 0x52,  /* Runs a very long test on all SID's */
  TEST_SID1        = 0x53,  /* Runs a very long test on SID 1 */
  TEST_SID2        = 0x54,  /* Runs a very long test on SID 2 */
  TEST_SID3        = 0x55,  /* Runs a very long test on SID 3 */
  TEST_SID4        = 0x56,  /* Runs a very long test on SID 4 */
  GET_CLOCK        = 0x57,  /* Returns the clockrate as array id in byte 0 */
  LOCK_CLOCK       = 0x58,  /* Locks the clockrate from being changed, saved in config */
  STOP_TESTS       = 0x59,  /* Interrupt any running SID tests */
  DETECT_CLONES    = 0x5A,  /* Detect clone SID types */
  AUTO_DETECT      = 0x5B,  /* Run auto detection routine (fallback/workaround for rp2350 bug) */

  LOAD_MIDI_STATE  = 0x60,
  SAVE_MIDI_STATE  = 0x61,
  RESET_MIDI_STATE = 0x63,

  USBSID_VERSION   = 0x80,  /* Read version identifier as uint32_t */
  US_PCB_VERSION   = 0x81,  /* Read PCB version */

  RESTART_BUS      = 0x85,  /* Restart DMA & PIO */
  RESTART_BUS_CLK  = 0x86,  /* Restart PIO clocks */
  SYNC_PIOS        = 0x87,  /* Sync PIO clocks */
  TOGGLE_AUDIO     = 0x88,  /* Toggle mono <-> stereo (v1.3+ boards only) */
  SET_AUDIO        = 0x89,  /* Set mono <-> stereo (v1.3+ boards only) */
  LOCK_AUDIO       = 0x90,  /* Locks the audio switch into it's current state (v1.3+ boards only) */
  GET_AUDIO        = 0x91,  /* Get current audio switch setting */

  TEST_FN          = 0x99,  /* TODO: Remove before v1 release */
  TEST_FN2         = 0x9A,  /* TODO: Remove before v1 release */
  TEST_FN3         = 0x9B,  /* TODO: Remove before v1 release */

  /* Hardware SID clone configuration related */
  FPGASID          = 0xA0,  /* Config initiator byte for FPGASID */
  SKPICO           = 0xA1,  /* Config initiator byte for SIDKICK-pico */
  ARMSID           = 0xA2,  /* Config initiator byte for ARMSID */
  PDSID            = 0xA3,  /* Holds the reset line for 5 seconds to change SID type on a PDSID */

#if defined(ONBOARD_EMULATOR) || defined(ONBOARD_SIDPLAYER)
  /* Internal SID player */
  UPLOAD_SID_START = 0xD0,  /* Start command for USBSID to go into receiving mode */
  UPLOAD_SID_DATA  = 0xD1,  /* Init byte for each packet containing data */
  UPLOAD_SID_END   = 0xD2,  /* End command for USBSID to exit receiving mode */
  UPLOAD_SID_SIZE  = 0xD3,  /* Packet containing the actual file size */

  /* Internal SID player */
  SID_PLAYER_TUNE  = 0xE0,  /* Load SID file into SID player memory and initialize internal SID player */
  SID_PLAYER_START = 0xE1,  /* Start SID file play */
  SID_PLAYER_STOP  = 0xE2,  /* Stop SID file play */
  SID_PLAYER_PAUSE = 0xE3,  /* Pause/Unpause SID file play */
  SID_PLAYER_NEXT  = 0xE4,  /* Next SID subtune play */
  SID_PLAYER_PREV  = 0xE5,  /* Previous SID subtune play */
#endif
};

/* Config write command byte */
enum {
  FULL_CONFIG   = 0x00,
  SOCKET_CONFIG = 0x10,
  MIDI_CONFIG   = 0x20,
  MIDI_CCVALUES = 0x30,
};

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_CONFIG_H_ */
