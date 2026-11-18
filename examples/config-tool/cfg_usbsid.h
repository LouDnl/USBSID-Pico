/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * cfg_usbsid.h
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

#ifndef _CFG_USBSID_H_
#define _CFG_USBSID_H_
#pragma once

#include <usbsid_protocol.h>

#ifdef __cplusplus
  extern "C" {
#endif


/* Macros */
#ifndef count_of
#define count_of(a) (sizeof(a)/sizeof((a)[0]))
#endif

/* libusb variables */
#define VENDOR_ID      0xCAFE
#define PRODUCT_ID     0x4011
#define ACM_CTRL_DTR   0x01
#define ACM_CTRL_RTS   0x02

/* init local driver variables */
static int debug = 0;
static USBSIDitf us = NULL;
static int len, rc;
static int usid_dev = -1;
static char target_serial[USBSID_SERIAL_LEN] = {0}; /* Board to open, empty = use target_index */
static int target_index = 0;                         /* Index in enumerate_USBSID() order */

/* Config constants not in usbsid_protocol.h */
enum {
  FLASH_PAGE_SIZE    = 256,
  MAX_BUFFER_SIZE    = 64,
};

/* Clock cycles per second
 * Clock speed: 0.985 MHz (PAL) or 1.023 MHz (NTSC)
 *
 * For some reason both 1022727 and 1022730 are
 * mentioned as NTSC clock cycles per second
 * Going for the rates specified by Vice it should
 * be 1022730, except in the link about raster time
 * on c64 wiki it's 1022727.
 * I chose to implement both, let's see how this
 * works out
 *
 * https://sourceforge.net/p/vice-emu/code/HEAD/tree/trunk/vice/src/c64/c64.h
 */
typedef enum {
    CLOCK_DEFAULT  = 1000000,  /* @125MHz clock ~ 125000000 / 62,5f = 2000000 / 2 = 1000000 == 1.00MHz @ 125MHz and 1.06MHz @ 133MHz */
    CLOCK_PAL      = 985248,   /* @125MHz clock ~ 125000000 / 63,435804995f = 1970496,000009025 / 2 = 985248,000004513 */
    CLOCK_NTSC     = 1022727,  /* @125MHz clock ~ 125000000 / 61,111127407f = 2045454,000013782 / 2 = 1022727,000006891 */
    CLOCK_DREAN    = 1023440,  /* @125MHz clock ~ 125000000 / 61,068553115f = 2046879,999999489 / 2 = 1023439,999999745 */
    CLOCK_NTSC2    = 1022730,  /* @125MHz clock ~ 125000000 / 61,111127407f = 2045454,000013782 / 2 = 1022727,000006891 */
} clock_rates;
enum config_clockrates
{
    DEFAULT = CLOCK_DEFAULT,  /* 0 */
    PAL     = CLOCK_PAL,      /* 1 */
    NTSC    = CLOCK_NTSC,     /* 2 */
    DREAN   = CLOCK_DREAN,    /* 3 */
    NTSC2   = CLOCK_NTSC2,    /* 4 */
};

/* Buffer arrays */
uint8_t read_data[1];
uint8_t read_data_max[64];
uint8_t read_data_uber[128];
uint8_t read_buffer[3]     = { (uint8_t)(READ << 6), 0x0, 0x0 };
uint8_t write_buffer[3]    = { (uint8_t)(WRITE << 6), 0x0, 0x0 };
uint8_t command_buffer[3]  = { (uint8_t)(COMMAND << 6), 0x0, 0x0 };
uint8_t config_buffer[6]   = { (uint8_t)((COMMAND << 6) | 18), 0x0, 0x0, 0x0, 0x0, 0x0 };

/* Config string variables */
const char * error_type[]  = { "ERROR" };
const char * enabled[]     = { "Disabled", "Enabled" };
const char * intext[]      = { "Internal", "External" };
const char * onoff[]       = { "Off", "On" };
const char * usbnet_ip_modes[] = { "dhcp", "static", "server" };
const char * truefalse[]   = { "False", "True" };
const char * locked[]      = { "Unlocked", "Locked" };
const char * us_socket[]   = { "Single SID", "Dual SID" };
const char * mono_stereo[] = { "Mono", "Stereo" };
/* Embedded C64 MIDI apps, index matches the firmware C64App enum (usbsid_constants.h) */
const char * c64_midiapps[] = { "Cynthcart", "SID SYNTH" };

const char * chiptypes[]   = { "MOS", "Unknown", "SKPico", "ARMSID", "ARM2SID", "FPGASID", "RedipSID", "PDSID", "BackSID", "SIDEmu" };
const char * sidtypes[]    = { "Unknown", "N/A", "8580", "6581", "FMOpl" };

const uint8_t sid_addresses[4] = { 0x00, 0x20, 0x40, 0x60 };

/* USBSID-Pico config struct */
typedef struct SIDChip {
  uint8_t id;    /* ID for this SID 0 ~ 4, 255 = disabled */
  uint8_t addr;  /* Starting address to use for this SID, 255 = disabled */
  uint8_t type;  /* 0 = unknown, 1 = N/A, 2 = MOS8580, 3 = MOS6581, 4 = FMopl */
} SIDChip;

typedef struct Socket {
  uint8_t chiptype;              /* 0 = real, 1 = unknown, 2...etc see config_logging.c */
  SIDChip sid1;
  SIDChip sid2;
  bool    enabled : 1;           /* enable / disable this socket */
  bool    dualsid : 1;           /* enable / disable dual SID support for this socket (requires clone) */
} Socket;

/* WiFi/Bluetooth/NSD config, wire-visible fields only (no flash-persistence
 * fields like magic/version/save_id, and no PSK - it is write-only and never
 * read back, mirrors firmware's WIFI_SET_PSK handling) */
typedef struct NetConfig {
  char     ssid[33];
  char     hostname[24];
  uint16_t nsd_port;
  struct {
    bool wifi_enabled      : 1;
    bool nsd_enabled       : 1;
    bool bt_nsd_enabled    : 1;
    bool discovery_enabled : 1;
  } flags;
  uint8_t usbnet_ip_mode;  /* USB network build only, 0 dhcp / 1 static / 2 server (default), flags byte bits 4-5 */
} NetConfig;

typedef struct Config {
  /* Don't care from here */
  uint32_t clock_rate;           /* clock speed identifier */
  Socket   socketOne;            /* 1 */
  Socket   socketTwo;            /* 2 */
  struct {
    bool enabled : 1;
    bool idle_breathe : 1;
  } LED;                         /* 3 */
  struct {
    uint8_t brightness;
    int     sid_to_use;          /* 0/-1 = off, 1...4 = sid 1 ... sid 4 */
    bool    enabled : 1;
    bool    idle_breathe : 1;
  } RGBLED;                      /* 4 */
  struct {
    bool enabled : 1;            /* Cannot be disabled */
  } Cdc;                         /* 5 */
  struct {
    bool enabled : 1;            /* Cannot be disabled */
  } WebUSB;                      /* 6 */
  struct {
    bool enabled : 1;
    /* bool buffered : 1; */           /* Enable/Disable ASID buffering by default (protocal can enable this) */
  } Asid;                        /* 7 */
  struct {
    bool enabled : 1;
  } Midi;                        /* 8 */
  struct {
    int sidno;                   /* 0 = disabled, saves the sidno of the sid set to FMOpl */
    bool enabled : 1;            /* Requires a clone SID! */
  } FMOpl;                       /* 9 */
  bool external_clock : 1;       /* enable / disable external oscillator */
  bool board_clock_link : 1;     /* link PHI1 generation to the external clock input (READ_CONFIG byte 6 bit 1) */
  bool lock_clockrate : 1;       /* lock the set clockspeed from being changed */
  bool stereo_en : 1;            /* audio switch is off (mono) or on (stereo) ~ (PCB v1.3+ only) */
  bool lock_audio_sw : 1;        /* lock the audio switch into it's current stateand prevent it from being changed ~ (PCB v1.3+ only) */
  bool mirrored : 1;             /* act as socket 1 */
  bool flipped : 1;              /* socket 1 is socket 2 and vice versa */
  bool mixed : 1;                /* addresses are mixed up (quad SID only!) */
  bool need_confirmation : 1;    /* (PCB v1.5+) current configuration needs confirmation, SID's are disabled until confirmed! */
  bool socket_change_detect : 1; /* (PCB v1.5+) disables socket change detection on boot */
  bool preset_auto_detect : 1;   /* disables silent auto detection before present change */
  bool rx_ring : 1;              /* USB RX ringbuffer, applied after save and reboot */
} Config;

#define USBSID_DEFAULT_CONFIG_INIT { \
  .clock_rate = DEFAULT, \
  .socketOne = { \
    .chiptype = 1,  /* unknown */ \
    .sid1 = { \
      .id = 0, \
      .addr = 0x00, \
      .type = 0,  /* unknown */ \
    }, \
    .sid2 = { \
      .id = 0xFF, \
      .addr = 0xFF, \
      .type = 1,  /* n/a */ \
    }, \
    .enabled = true, \
    .dualsid = false, \
  }, \
  .socketTwo = { \
    .chiptype = 1,  /* unknown */ \
    .sid1 = { \
      .id = 1, \
      .addr = 0x20, \
      .type = 0,  /* unknown */ \
    }, \
    .sid2 = { \
      .id = 0xFF, \
      .addr = 0xFF, \
      .type = 1,  /* n/a */ \
    }, \
    .enabled = true, \
    .dualsid = false, \
  }, \
  .LED = { \
    .enabled = true, \
    .idle_breathe = true \
  }, \
  .RGBLED = { \
    .brightness = 0x7F,  /* Half of max brightness or disabled if no RGB LED */ \
    .sid_to_use = 1, \
    .enabled = true, \
    .idle_breathe = true, \
  }, \
  .Cdc = { \
    .enabled = true \
  }, \
  .WebUSB = { \
    .enabled = true \
  }, \
  .Asid = { \
    .enabled = true, \
    /* .buffered = false */ \
  }, \
  .Midi = { \
    .enabled = true \
  }, \
  .FMOpl = { \
    .sidno = 0, \
    .enabled = false, \
  }, \
  .external_clock = false, \
  .board_clock_link = false, \
  .lock_clockrate = false, \
  .stereo_en = false, \
  .lock_audio_sw = false, \
  .mirrored = false, \
  .flipped = false, \
  .mixed = false, \
  .need_confirmation = false, \
  .socket_change_detect = true, /* WARNING: This setting _can_ and _will_ fry your 9v SID if config is set to 6581 (12v) */ \
  .preset_auto_detect = true, \
  .rx_ring = true, \
} \

/* init local usbsid-pico variables */
static const enum config_clockrates clockrates[] = { DEFAULT, PAL, NTSC, DREAN, NTSC2 };
static uint32_t read_clock_rate;
static char version[MAX_BUFFER_SIZE] = {0};
static char pcb_version[MAX_BUFFER_SIZE] = {0};
static char project_version[MAX_BUFFER_SIZE] = {0};
static uint8_t config[FLASH_PAGE_SIZE] = {0};
static uint8_t socket_config[SOCKET_BUFFER_SIZE] = {0};
static Config usbsid_config = USBSID_DEFAULT_CONFIG_INIT;
static uint8_t net_config[MAX_BUFFER_SIZE] = {0};
static uint8_t wifi_status[2] = {0};
static NetConfig net_cfg = {0};
static uint8_t us_features = 0;
static bool us_features_read = false;

/* Help menu declarations */
const char * help_args[] = {
  "options", "sidchips", "config",
  "features", "network", "presets",
  "ini", "manual", "midi", "other"
};
const int menu_count = count_of(help_args);

void print_help_separator(void);
void print_help_extended(void);
void print_help_options(void);
void print_help_sidchips(void);
void print_help_config(void);
void print_help_features(void);
void print_help_network(void);
void print_help_presets(void);
void print_help_ini(void);
void print_help_manual(void);
void print_help_midi(void);
void print_help_other(void);

typedef void (*f_help_menu)(void);
f_help_menu help_menus[] = {
  print_help_options, print_help_sidchips, print_help_config,
  print_help_features, print_help_network, print_help_presets,
  print_help_ini, print_help_manual, print_help_midi, print_help_other
};

/* NSD declarations */
static const char * const wifi_states[4] = { "Off", "Down", "Connecting", "Connected" };
bool board_has_net(void);
bool board_has_nsd(void);
bool require_net_support(void);
bool read_us_features(void);


#ifdef __cplusplus
  }
#endif

#endif /* _CFG_USBSID_H_ */
