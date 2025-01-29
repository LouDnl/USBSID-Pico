/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * cfg_usbsid.h
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

/* Macros */
#ifndef count_of
#define count_of(a) (sizeof(a)/sizeof((a)[0]))
#endif

/* libusb variables */
#define VENDOR_ID      0xCAFE
#define PRODUCT_ID     0x4011
#define ACM_CTRL_DTR   0x01
#define ACM_CTRL_RTS   0x02

/* usbsid-pico variables */
enum
{
  PACKET_TYPE  = 0xC0,  /* 0b11000000 ~ 192  */
  BYTE_MASK    = 0x3F,  /*   0b111111 ~  63  */
  COMMAND_MASK = 0x1F,  /*    0b11111 ~  31  */

  /* BYTE 0 */
  /* Top 2 bits */
  WRITE        =   0,   /*        0b0 ~ 0x00 */
  READ         =   1,   /*        0b1 ~ 0x40 */
  CYCLED_WRITE =   2,   /*       0b10 ~ 0x80 */
  COMMAND      =   3,   /*       0b11 ~ 0xC0 */
  /* Lower 6 bits for byte count */
  /* Lower 6 bits for Commands */
  PAUSE        =  10,   /*     0b1010 ~ 0x0A */
  UNPAUSE      =  11,   /*     0b1011 ~ 0x0B */
  MUTE         =  12,   /*     0b1100 ~ 0x0C */
  UNMUTE       =  13,   /*     0b1101 ~ 0x0D */
  RESET_SID    =  14,   /*     0b1110 ~ 0x0E */
  DISABLE_SID  =  15,   /*     0b1111 ~ 0x0F */
  ENABLE_SID   =  16,   /*    0b10000 ~ 0x10 */
  CLEAR_BUS    =  17,   /*    0b10001 ~ 0x11 */
  CONFIG       =  18,   /*    0b10010 ~ 0x12 */
  RESET_MCU    =  19,   /*    0b10011 ~ 0x13 */
  BOOTLOADER   =  20,   /*    0b10100 ~ 0x14 */
};
enum
{
  RESET_USBSID     = 0x20,

  READ_CONFIG      = 0x30,  /* Read full config as bytes */
  APPLY_CONFIG     = 0x31,  /* Load and apply config */
  SET_CONFIG       = 0x32,  /* Set single config item */
  SAVE_CONFIG      = 0x33,
  SAVE_NORESET     = 0x34,
  RESET_CONFIG     = 0x35,
  WRITE_CONFIG     = 0x36,  /* Write full config as bytes */
  READ_SOCKETCFG   = 0x37,  /* Read socket config as bytes */

  SINGLE_SID       = 0x40,
  DUAL_SID         = 0x41,
  QUAD_SID         = 0x42,
  TRIPLE_SID       = 0x43,
  TRIPLE_SID_TWO   = 0x44,
  MIRRORED_SID     = 0x45,

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
typedef enum {
    CLOCK_DEFAULT  = 1000000,  /* @125MHz clock ~ 125000000 / 62,5f = 2000000 / 2 = 1000000 == 1.00MHz @ 125MHz and 1.06MHz @ 133MHz */
    CLOCK_PAL      = 985248,   /* @125MHz clock ~ 125000000 / 63,435804995f = 1970496,000009025 / 2 = 985248,000004513 */
    CLOCK_NTSC     = 1022727,  /* @125MHz clock ~ 125000000 / 61,111127407f = 2045454,000013782 / 2 = 1022727,000006891 */
    CLOCK_DREAN    = 1023440   /* @125MHz clock ~ 125000000 / 61,068553115f = 2046879,999999489 / 2 = 1023439,999999745 */
} clock_rates;
enum config_clockrates
{
    DEFAULT = CLOCK_DEFAULT,  /* 0 */
    PAL     = CLOCK_PAL,      /* 1 */
    NTSC    = CLOCK_NTSC,     /* 2 */
    DREAN   = CLOCK_DREAN,    /* 3 */
};

uint8_t read_data[1];
uint8_t read_data_max[64];
uint8_t read_data_uber[128];
uint8_t read_buffer[3]    = { (READ << 6), 0x0, 0x0 };
uint8_t write_buffer[3]   = { (WRITE << 6), 0x0, 0x0 };
uint8_t command_buffer[3] = { (COMMAND << 6), 0x0, 0x0 };
uint8_t config_buffer[6]  = { ((COMMAND << 6) | 18), 0x0, 0x0, 0x0, 0x0, 0x0 };

const char * error_type = "ERROR";
const char * enabled[] = {"Disabled", "Enabled"};
const char * intext[] = { "Internal", "External" };
const char * onoff[] = {"Off", "On"};
const char * truefalse[] = {"False", "True"};
const char * socket[] = { "Single SID", "Dual SID" };
const char * chiptypes[] = {"Real", "Clone"};
const char * sidtypes[] = {"Unknown", "N/A", "MOS8580", "MOS6581", "FMopl"};
const char * clonetypes[] = { "Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID" };

/* USBSID-Pico config struct */
typedef struct Config {
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

#define USBSID_DEFAULT_CONFIG_INIT { \
  .external_clock = false, \
  .clock_rate = 1000000, \
  .socketOne = { \
    .enabled = true, \
    .dualsid = false, \
    .chiptype = 0x0,  /* real */ \
    .clonetype = 0x0, /* disabled */ \
    .sid1type = 0x0,  /* unknown */ \
    .sid2type = 0x0,  /* unknown */ \
  }, \
  .socketTwo = { \
    .enabled = true, \
    .dualsid = false, \
    .act_as_one = false, \
    .chiptype = 0x0,  /* real */ \
    .clonetype = 0x0, /* disabled */ \
    .sid1type = 0x0,  /* unknown */ \
    .sid2type = 0x0,  /* unknown */ \
  }, \
  .LED = { \
    .enabled = true, \
    .idle_breathe = true, \
  }, \
  .RGBLED = { \
    .enabled = true, \
    .idle_breathe = true, \
    .brightness = 0x7F,  /* Half of max brightness or disabled if no RGB LED */ \
    .sid_to_use = 1, \
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
}
