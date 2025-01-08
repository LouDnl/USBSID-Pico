/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * cfg_usbsid.c
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#define _POSIX_C_SOURCE 199309L
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>  // `errno`
#include <stdint.h> // `UINT64_MAX`
#include <stdio.h>  // `printf()`
#include <string.h> // `strerror(errno)`
#include <ctype.h>
#include <stdbool.h>
#include <libusb.h>

#include "inih/ini.h"

/* Compile with:
 * gcc -g3 -L/usr/local/lib inih/ini.c cfg_usbsid.c -o cfg_usbsid $(pkg-config --libs --cflags libusb-1.0)
 */

static int debug = 0;

#define VENDOR_ID      0xCAFE
#define PRODUCT_ID     0x4011
#define ACM_CTRL_DTR   0x01
#define ACM_CTRL_RTS   0x02

static struct libusb_device_handle *devh = NULL;
static unsigned char encoding[] = { 0x00, 0x10, 0x0E, 0x00, 0x00, 0x00, 0x08 };
static int ep_out_addr = 0x02;
static int ep_in_addr  = 0x82;

static int len, rc, actual_length, transferred;
static int usid_dev = -1;

uint8_t read_data[1];
uint8_t read_data_max[64];
uint8_t read_data_uber[128];
uint8_t read_buffer[3]  = { 0x1, 0x0, 0x0 };
uint8_t write_buffer[3]  = { 0x0, 0x0, 0x0 };
uint8_t command_buffer[3]  = { 0x0, 0x0, 0x0 };
uint8_t config_buffer[5]  = { 0x0, 0x0, 0x0, 0x0, 0x0 };

/* ---------------------- */

enum
{
  WRITE = 0,      /* 0 */
  READ,           /* 1 */
  PAUSE,          /* 2 */
  RESET_SID,      /* 3 */
  DISABLE_SID,    /* 4 */
  ENABLE_SID,     /* 5 */
  CLEAR_BUS,      /* 6 */
  RESET_MCU,      /* 7 */
  BOOTLOADER,     /* 8 */
};
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
static const enum config_clockrates clockrates[] = { DEFAULT, PAL, NTSC, DREAN };

static char version[64] = {0};
static char project_version[64] = {0};
static uint8_t config[256] = {0};

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
} \

static Config usbsid_config = USBSID_DEFAULT_CONFIG_INIT;


/* ---------------------- */

int clockspeed_n(int value)
{
  switch (value) {
    case DEFAULT:
      return 0;
    case PAL:
      return 1;
    case NTSC:
      return 2;
    case DREAN:
      return 3;
    default:
      return 666;
  }
}

int value_position(const char * value, const char ** array)
{
  #define END_OF_ARRAY -1
  int p = 0;
  while (*array[p] != END_OF_ARRAY) {
    if (strcmp(value, array[p]) == 0) {
      return p;
    }
    p++;
  };
  return 666;
}

static int import_ini(void* user, const char* section, const char* name, const char* value)
{
  int p = 0;
  Config* ini_config = (Config*)user;
  #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
  if (MATCH("General", "clock_rate")) {
    ini_config->clock_rate = atoi(value);
  }
  if (MATCH("socketOne", "enabled")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->socketOne.enabled = p;
  }
  if (MATCH("socketOne", "dualsid")) {
    p = value_position(value, enabled);
    if (p != 666) ini_config->socketOne.dualsid = p;
  }
  if (MATCH("socketOne", "chiptype")) {
    p = value_position(value, chiptypes);
    if (p != 666) ini_config->socketOne.chiptype = p;
  }
  if (MATCH("socketOne", "clonetype")) {
    p = value_position(value, clonetypes);
    if (p != 666) ini_config->socketOne.clonetype = p;
  }
  if (MATCH("socketOne", "sid1type")) {
    p = value_position(value, sidtypes);
    if (p != 666) ini_config->socketOne.sid1type = p;
  }
  if (MATCH("socketOne", "sid2type")) {
    p = value_position(value, sidtypes);
    if (p != 666) ini_config->socketOne.sid2type = p;
  }
  if (MATCH("socketTwo", "enabled")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->socketTwo.enabled = p;
  }
  if (MATCH("socketTwo", "dualsid")) {
    p = value_position(value, enabled);
    if (p != 666) ini_config->socketTwo.dualsid = p;
  }
  if (MATCH("socketTwo", "chiptype")) {
    p = value_position(value, chiptypes);
    if (p != 666) ini_config->socketTwo.chiptype = p;
  }
  if (MATCH("socketTwo", "clonetype")) {
    p = value_position(value, clonetypes);
    if (p != 666) ini_config->socketTwo.clonetype = p;
  }
  if (MATCH("socketTwo", "sid1type")) {
    p = value_position(value, sidtypes);
    if (p != 666) ini_config->socketTwo.sid1type = p;
  }
  if (MATCH("socketTwo", "sid2type")) {
    p = value_position(value, sidtypes);
    if (p != 666) ini_config->socketTwo.sid2type = p;
  }
  if (MATCH("socketTwo", "act_as_one")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->socketTwo.act_as_one = p;
  }
  if (MATCH("LED", "enabled")) {
    p = value_position(value, enabled);
    if (p != 666) ini_config->LED.enabled = p;
  }
  if (MATCH("LED", "idle_breathe")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->LED.idle_breathe = p;
  }
  if (MATCH("RGLED", "enabled")) {
    p = value_position(value, enabled);
    if (p != 666) ini_config->RGBLED.enabled = p;
  }
  if (MATCH("RGBLED", "idle_breathe")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->RGBLED.idle_breathe = p;
  }
  if (MATCH("RGBLED", "brightness")) {
    p = atoi(value);
    if (p >= 0 && p <= 255) ini_config->RGBLED.brightness = p;
  }
  if (MATCH("RGBLED", "sid_to_use")) {
    p = atoi(value);
    if (p >= 1 && p <= 4) ini_config->RGBLED.sid_to_use = p;
  }
  return 1;
}

void write_config_ini(Config * config, char * filename)
{
  FILE *f = fopen( filename, "wb" );
  if (f != NULL) {
    fprintf(f, "[General]\n");
    fprintf(f, "; Version is for reference only\n");
    fprintf(f, "version = v%s\n", (project_version[0] == 0 ? "0.0.0-DEFAULT.19000101" : project_version + 1));
    fprintf(f, "; Possible clockrates: %d, %d, %d, %d\n", DEFAULT, PAL, NTSC, DREAN);
    fprintf(f, "clock_rate = %d\n", config->clock_rate);
    fprintf(f, "\n");
    fprintf(f, "[socketOne]\n");
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "enabled = %s\n", truefalse[config->socketOne.enabled]);
    fprintf(f, "; Possible options: %s, %s\n", enabled[0], enabled[1]);
    fprintf(f, "dualsid = %s\n", enabled[config->socketOne.dualsid]);
    fprintf(f, "; Possible chiptypes: %s, %s\n", chiptypes[0], chiptypes[1]);
    fprintf(f, "chiptype = %s\n", chiptypes[config->socketOne.chiptype]);
    fprintf(f, "; Possible clonetypes: %s, %s, %s, %s, %s, %s\n",
            clonetypes[0], clonetypes[1], clonetypes[2],
            clonetypes[3], clonetypes[4], clonetypes[5]);
    fprintf(f, "clonetype = %s\n", clonetypes[config->socketOne.clonetype]);
    fprintf(f, "; Possible sidtypes: %s, %s, %s, %s, %s\n",
            sidtypes[0], sidtypes[1], sidtypes[2],
            sidtypes[3], sidtypes[4]);
    fprintf(f, "sid1type = %s\n", sidtypes[config->socketOne.sid1type]);
    fprintf(f, "sid2type = %s\n", sidtypes[config->socketOne.sid2type]);
    fprintf(f, "\n");
    fprintf(f, "[socketTwo]\n");
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "enabled = %s\n", truefalse[config->socketTwo.enabled]);
    fprintf(f, "; Possible options: %s, %s\n", enabled[0], enabled[1]);
    fprintf(f, "dualsid = %s\n", enabled[config->socketTwo.dualsid]);
    fprintf(f, "; Possible chiptypes: %s, %s\n", chiptypes[0], chiptypes[1]);
    fprintf(f, "chiptype = %s\n", chiptypes[config->socketTwo.chiptype]);
    fprintf(f, "; Possible clonetypes: %s, %s, %s, %s, %s, %s\n",
            clonetypes[0], clonetypes[1], clonetypes[2],
            clonetypes[3], clonetypes[4], clonetypes[5]);
    fprintf(f, "clonetype = %s\n", clonetypes[config->socketTwo.clonetype]);
    fprintf(f, "; Possible sidtypes: %s, %s, %s, %s, %s\n",
            sidtypes[0], sidtypes[1], sidtypes[2],
            sidtypes[3], sidtypes[4]);
    fprintf(f, "sid1type = %s\n", sidtypes[config->socketTwo.sid1type]);
    fprintf(f, "sid2type = %s\n", sidtypes[config->socketTwo.sid2type]);
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "act_as_one = %s\n", truefalse[config->socketTwo.act_as_one]);
    fprintf(f, "\n");
    fprintf(f, "[LED]\n");
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "enabled = %s\n", truefalse[config->LED.enabled]);
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "idle_breathe = %s\n", truefalse[config->LED.idle_breathe]);
    fprintf(f, "\n");
    fprintf(f, "[RGBLED]\n");
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "enabled = %s\n", truefalse[config->RGBLED.enabled]);
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "idle_breathe = %s\n", truefalse[config->RGBLED.idle_breathe]);
    fprintf(f, "; Possible brightness values 0 ~ 255\n");
    fprintf(f, "brightness = %d\n", config->RGBLED.brightness);
    fprintf(f, "; Possible sids to use are 1, 2, 3 or 4\n");
    fprintf(f, "sid_to_use = %d\n", config->RGBLED.sid_to_use);
    fprintf(f, "\n");
    fclose(f);
  };
}

/* ---------------------- */

int usbsid_init(void)
{
    if (devh != NULL) {
        libusb_close(devh);
    }

    rc = libusb_init(NULL);
    if (rc != 0) {
        fprintf(stderr, "Error initializing libusb: %s: %s\n",
        libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    devh = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, PRODUCT_ID);
    if (!devh) {
        fprintf(stderr, "Error finding USB device\n");
        rc = -1;
        goto out;
    }

    for (int if_num = 0; if_num < 2; if_num++) {
        if (libusb_kernel_driver_active(devh, if_num)) {
            libusb_detach_kernel_driver(devh, if_num);
        }
        rc = libusb_claim_interface(devh, if_num);
        if (rc < 0) {
            fprintf(stderr, "Error claiming interface: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
            goto out;
        }
    }

    rc = libusb_control_transfer(devh, 0x21, 0x22, ACM_CTRL_DTR | ACM_CTRL_RTS, 0, NULL, 0, 0);
    if (rc != 0 && rc != 7) {
        fprintf(stderr, "?Error configuring line state during control transfer: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    rc = libusb_control_transfer(devh, 0x21, 0x20, 0, 0, encoding, sizeof(encoding), 0);
    if (rc != 0 && rc != 7) {
        fprintf(stderr, "Error configuring line encoding during control transfer: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    usid_dev = (rc == 0 || rc == 7) ? 0 : -1;

    if (usid_dev < 0)
    {
        fprintf(stderr, "Could not open SID device USBSID.\n");
        goto out;
    }

    /* fprintf(stdout, "usbsid_init: detected [rc]%d [usid_dev]%d\n", rc, usid_dev); */

	return usid_dev;
out:
    if (devh != NULL)
        libusb_close(devh);
    libusb_exit(NULL);
    return rc;

}

void usbsid_close(void)
{
  for (int if_num = 0; if_num < 2; if_num++) {
    libusb_release_interface(devh, if_num);
    if (libusb_kernel_driver_active(devh, if_num)) {
        libusb_detach_kernel_driver(devh, if_num);
    }
  }
  libusb_close(devh);
  libusb_exit(NULL);
}

/* ---------------------- */

void write_chars(unsigned char * data, int size)
{
  int actual_length;
  if (libusb_bulk_transfer(devh, ep_out_addr, data, size, &actual_length, 0) < 0) {
    fprintf(stderr, "Error while sending char\n");
  }
  /* printf("[W]$%02X:%02X\n", data[1], data[2]); */
}

int read_chars(unsigned char * data, int size)
{
  int actual_length;
  int rc = libusb_bulk_transfer(devh, ep_in_addr, data, size, &actual_length, 0);
  /* printf("[R]$%02X:%02X\n", read_buffer[1], data[0]); */
  if (rc == LIBUSB_ERROR_TIMEOUT) {
    printf("timeout (%d)\n", actual_length);
    return -1;
  } else if (rc < 0) {
    fprintf(stderr, "Error while waiting for char\n");
    return -1;
  }
  return actual_length;
}

/* ---------------------- */

void write_command(uint8_t command)
{
  command_buffer[0] = command;
  write_chars(command_buffer, 3);
}

void write_config_command(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e)
{
  config_buffer[0] = a;
  config_buffer[1] = b;
  config_buffer[2] = c;
  config_buffer[3] = d;
  config_buffer[4] = e;
  write_chars(config_buffer, 5);
  return;
}

void save_config(int reboot)
{
  write_config_command((reboot == 1)?0x33:0x34,0x0,0x0,0x0,0x0);
  return;
}

void write_config(Config * config)
{
  /* General */
  write_config_command(STORE_CONFIG,0x0,clockspeed_n(config->clock_rate),0,0);

  /* socketOne */
  write_config_command(STORE_CONFIG,0x1,0x0,config->socketOne.enabled,0);
  write_config_command(STORE_CONFIG,0x1,0x1,config->socketOne.dualsid,0);
  write_config_command(STORE_CONFIG,0x1,0x2,config->socketOne.chiptype,0);
  write_config_command(STORE_CONFIG,0x1,0x3,config->socketOne.clonetype,0);
  write_config_command(STORE_CONFIG,0x1,0x4,config->socketOne.sid1type,0);
  write_config_command(STORE_CONFIG,0x1,0x5,config->socketOne.sid2type,0);

  /* socketTwo */
  write_config_command(STORE_CONFIG,0x2,0x0,config->socketTwo.enabled,0);
  write_config_command(STORE_CONFIG,0x2,0x1,config->socketTwo.dualsid,0);
  write_config_command(STORE_CONFIG,0x2,0x2,config->socketTwo.chiptype,0);
  write_config_command(STORE_CONFIG,0x2,0x3,config->socketTwo.clonetype,0);
  write_config_command(STORE_CONFIG,0x2,0x4,config->socketTwo.sid1type,0);
  write_config_command(STORE_CONFIG,0x2,0x5,config->socketTwo.sid2type,0);
  write_config_command(STORE_CONFIG,0x2,0x6,config->socketTwo.act_as_one,0);

  /* LED */
  write_config_command(STORE_CONFIG,0x3,0x0,config->LED.enabled,0);
  write_config_command(STORE_CONFIG,0x3,0x1,config->LED.idle_breathe,0);

  /* RGBLED */
  write_config_command(STORE_CONFIG,0x4,0x0,config->RGBLED.enabled,0);
  write_config_command(STORE_CONFIG,0x4,0x1,config->RGBLED.idle_breathe,0);
  write_config_command(STORE_CONFIG,0x4,0x2,config->RGBLED.brightness,0);
  write_config_command(STORE_CONFIG,0x4,0x3,config->RGBLED.sid_to_use,0);

  save_config(0);

  return;
}

void print_cfg_buffer(const uint8_t *buf, size_t len)
{
  printf("[PRINT CFG BUFFER START]\n");
  for (int i = 0; i < (int)len; ++i) {
    if (i == 0)
      printf("[R%03d] ", i);
    printf("%02x", buf[i]);
    if (i == (len - 1)) {
      printf("\n");
    } else if ((i != 0) && (i % 16 == 15)) {
      printf("\n[R%03d] ", i);
    } else {
      printf(" ");
    }
  }
  printf("[PRINT CFG BUFFER END]\n");
  return;
}

void read_version(int print_version)
{
  memset(config_buffer, 0, (sizeof(config_buffer)/sizeof(config_buffer[0])));
  config_buffer[0] = 0x80;
  write_chars(config_buffer, (sizeof(config_buffer)/sizeof(config_buffer[0])));

  int len;
  memset(read_data_uber, 0, (sizeof(read_data_uber)/sizeof(read_data_uber[0])));
  len = read_chars(read_data_uber, (sizeof(read_data_uber)/sizeof(read_data_uber[0])));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);

  for (int i = 0; i < sizeof(version) / sizeof(version[0]) ; i++) {
    version[i] = read_data_uber[i];
  }
  if (len == 128 && read_data_uber[0] == 0x80) len = read_chars(read_data_uber, (sizeof(read_data_uber)/sizeof(read_data_uber[0])));
  memcpy(&project_version[0], &version[1], 63 * sizeof(project_version[0]));
  if(print_version == 1 || debug == 1) printf("v%s\n", project_version);
  if(debug == 1) print_cfg_buffer(read_data_uber, 128);
  return;
}

void set_cfg_from_buffer(const uint8_t *buff, size_t len)
{
    static uint32_t clockrate = 0;
    for (int i = 0; i < (int)len; ++i) {
        switch(i) {
            case 0 ... 1:
                continue;
            case 6:
                usbsid_config.external_clock = buff[i];
                break;
            case 7:
                clockrate |= (buff[i] << 16);
                clockrate |= (buff[i+1] << 8);
                clockrate |= buff[i+2];
                usbsid_config.clock_rate = clockrate;
                break;
            case 10:
                usbsid_config.socketOne.enabled = buff[i];
                break;
            case 11:
                usbsid_config.socketOne.dualsid = buff[i];
                break;
            case 12:
                usbsid_config.socketOne.chiptype = buff[i];
                break;
            case 13:
                usbsid_config.socketOne.clonetype = buff[i];
                break;
            case 14:
                usbsid_config.socketOne.sid1type = buff[i];
                break;
            case 15:
                usbsid_config.socketOne.sid2type = buff[i];
                break;
            case 20:
                usbsid_config.socketTwo.enabled = buff[i];
                break;
            case 21:
                usbsid_config.socketTwo.dualsid = buff[i];
                break;
            case 22:
                usbsid_config.socketTwo.act_as_one = buff[i];
                break;
            case 23:
                usbsid_config.socketTwo.chiptype = buff[i];
                break;
            case 24:
                usbsid_config.socketTwo.clonetype = buff[i];
                break;
            case 25:
                usbsid_config.socketTwo.sid1type = buff[i];
                break;
            case 26:
                usbsid_config.socketTwo.sid2type = buff[i];
                break;
            case 30:
                usbsid_config.LED.enabled = buff[i];
                break;
            case 31:
                usbsid_config.LED.idle_breathe = buff[i];
                break;
            case 40:
                usbsid_config.RGBLED.enabled = buff[i];
                break;
            case 41:
                usbsid_config.RGBLED.idle_breathe = buff[i];
                break;
            case 42:
                usbsid_config.RGBLED.brightness = buff[i];
                break;
            case 43:
                usbsid_config.RGBLED.sid_to_use = buff[i];
                break;
            case 51:
                usbsid_config.Cdc.enabled = buff[i];
                break;
            case 52:
                usbsid_config.WebUSB.enabled = buff[i];
                break;
            case 53:
                usbsid_config.Asid.enabled = buff[i];
                break;
            case 54:
                usbsid_config.Midi.enabled = buff[i];
                break;
            default:
                break;
        }
    }
    return;
}

void print_config(void)
{
  if (project_version[0] != 0) printf("[CONFIG] USBSID-Pico version is: %s\n", project_version);
  printf("[CONFIG] SID Clock is: %s\n", intext[usbsid_config.external_clock]);
  if (usbsid_config.external_clock == 0) {
    printf("[CONFIG] SID Clock speed set @ %dHz\n", (int)usbsid_config.clock_rate);
  } else {
    printf("[CONFIG] SID Clock externl defaults to 1MHz\n");
  }
  printf("[CONFIG] [SOCKET ONE] %s as %s\n",
    enabled[(int)usbsid_config.socketOne.enabled],
    socket[(int)usbsid_config.socketOne.dualsid]);
  printf("[CONFIG] [SOCKET ONE] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketOne.chiptype],
    clonetypes[usbsid_config.socketOne.clonetype]);
  printf("[CONFIG] [SOCKET ONE] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketOne.sid1type],
    sidtypes[usbsid_config.socketOne.sid2type]);
  printf("[CONFIG] [SOCKET TWO] %s as %s\n",
    enabled[(int)usbsid_config.socketTwo.enabled],
    socket[(int)usbsid_config.socketTwo.dualsid]);
  printf("[CONFIG] [SOCKET TWO] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketTwo.chiptype],
    clonetypes[usbsid_config.socketTwo.clonetype]);
  printf("[CONFIG] [SOCKET TWO] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
   sidtypes[usbsid_config.socketTwo.sid1type],
    sidtypes[usbsid_config.socketTwo.sid2type]);
  printf("[CONFIG] [SOCKET TWO AS ONE] %s\n",
    truefalse[(int)usbsid_config.socketTwo.act_as_one]);

  printf("[CONFIG] [LED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.LED.enabled],
    truefalse[(int)usbsid_config.LED.idle_breathe]);
  printf("[CONFIG] [RGBLED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.RGBLED.enabled],
    truefalse[(int)usbsid_config.RGBLED.idle_breathe]);
  printf("[CONFIG] [RGBLED SIDTOUSE] %d\n",
    (int)usbsid_config.RGBLED.sid_to_use);
  printf("[CONFIG] [RGBLED BRIGHTNESS] %d\n",
    (int)usbsid_config.RGBLED.brightness);

  printf("[CONFIG] [CDC] %s\n",
    enabled[(int)usbsid_config.Cdc.enabled]);
  printf("[CONFIG] [WebUSB] %s\n",
    enabled[(int)usbsid_config.WebUSB.enabled]);
  printf("[CONFIG] [Asid] %s\n",
    enabled[(int)usbsid_config.Asid.enabled]);
  printf("[CONFIG] [Midi] %s\n",
    enabled[(int)usbsid_config.Midi.enabled]);

  return;
}

void sid_autodetect(void)
{
  config_buffer[0] = 0x51;
  write_chars(config_buffer, sizeof(config_buffer));

  int len;
  len = read_chars(read_data_uber, sizeof(read_data_uber));
  //if (len == 0) len = read_chars(read_data_uber, sizeof(read_data_uber));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);
  memcpy(&config[0], &read_data_uber[0], 64/* * sizeof(config[0])*/);

  if (debug == 1) print_cfg_buffer(config, 64 * sizeof(config[0]));
  set_cfg_from_buffer(config, 64 * sizeof(config[0]));
  return;
}

void read_config(void)
{
  config_buffer[0] = 0x30;
  write_chars(config_buffer, sizeof(config_buffer));

  int len;
  len = read_chars(read_data_uber, sizeof(read_data_uber));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);
  memcpy(&config[0], &read_data_uber[0], 64 * sizeof(config[0]));
  if (len == 128) len = read_chars(read_data_uber, sizeof(read_data_uber));

  if (debug == 1) print_cfg_buffer(config, 256 * sizeof(config[0]));
  set_cfg_from_buffer(config, 64 * sizeof(config[0]));
  return;
}


/* ---------------------- */

void print_help(void)
{
  printf("--------------------------------------------------------------------------------------------------------------------\n");
  printf("USBSID-Pico configtool\n");
  printf("For the tool to work correct, this tool requires firmware v0.2.4 and up\n");
  printf("\n");
  printf("Usage:\n");
  printf("$ cfg_usbsid [options]\n");
  printf("--[OPTIONS]---------------------------------------------------------------------------------------------------------\n");
  printf("  -h,       --help              : Show this help message\n");
  printf("  -debug    --debug             : Enable debug prints\n");
  printf("--[BASICS]----------------------------------------------------------------------------------------------------------\n");
  printf("  -v,       --version           : Read and print USBSID-Pico firmware version\n");
  printf("  -r,       --read-config       : Read and print USBSID-Pico config settings\n");
  printf("  -detect,  --detect-sid-types  : Send SID autodetect command to device, returns the config as with '-r' afterwards\n");
  printf("  -w,       --write-config      : Write single config item to USBSID-Pico (will read the full config first!)\n");
  printf("  -s,       --save-config       : Send the save config command to USBSID-Pico\n");
  printf("  -sr,      --save-reboot       : Send the save config command to USBSID-Pico and reboot it\n");
  printf("  -reset,   --reset-sids        : Reset SID's\n");
  printf("  -reboot,  --reboot-usp        : Reboot USBSID-Pico\n");
  printf("  -boot,    --bootloader        : Reboot USBSID-Pico to the bootloader for firmware upload\n");
  printf("--[INI FILE CONFIGURATION]------------------------------------------------------------------------------------------\n");
  printf("  -default, --default-ini       : Generate an ini file with default USBSID-Pico config named `USBSID-Pico-cfg.ini`\n");
  printf("  -export F,--export-config F   : Read config from USBSID-Pico and export it to provided ini file or default in\n");
  printf("                                  current directory when not provided\n");
  printf("  -import F,--import-config F   : Read config from provided ini file, write to USBSID-Pico, send save command\n");
  printf("                                  and read back config for a visual confirmation\n");
  printf("--[MANUAL CONFIGURATION]--------------------------------------------------------------------------------------------\n");
  printf("  All the following options require '-w'\n");
  printf("  -c N,     --sid-clock N       : Change SID clock to\n");
  printf("                                  0: %d, 1: %d, 2: %d, 4: %d\n",
         CLOCK_DEFAULT, CLOCK_PAL, CLOCK_NTSC, CLOCK_DREAN);
  printf("  -led N,   --led-enabled N     : LED is Enabled (1) or Disabled (0)\n");
  printf("  -lbr N,   --led-breathe N     : LED idle breathing is Enabled (1) or Disabled (0)\n");
  printf("  -rgb N,   --rgb-enabled N     : RGBLED is Enabled (1) or Disabled (0)\n");
  printf("  -rgbbr N  --rgb-breathe N     : RGBLED idle breathing is Enabled (1) or Disabled (0)\n");
  printf("  -rgbsid N,--rgb-sidtouse N    : Set the SID number the RGBLED uses (1, 2, 3 or 4)\n");
  printf("  -br N,    --rgb-brightness N  : Set the RGBLED Brightness to N (0 ~ 255)\n");
  printf("  -sock N,  --socket N          : Configure socket N ~ 1 or 2\n");
  printf("  The following options additionally require '-sock N'\n");
  printf("  Note that you can only configure 1 socket at a time!\n");
  printf("  -en N,    --enabled N         : Socket is Enabled (1) or Disabled (0)\n");
  printf("  -d N,     --dualsid N         : DualSID is On (1) or Off (0)\n");
  printf("  -chip N,  --chiptype N        : Set the socket chiptype to:\n");
  printf("                                  0: %s, 1: %s\n", chiptypes[0], chiptypes[1]);
  printf("  -clone N, --clonetype N       : If chiptype is 'Clone' set the clonetype to:\n");
  printf("                                  0: %s, 1: %s, 2: %s, 3: %s, 4: %s, 5: %s\n",
         clonetypes[0], clonetypes[1], clonetypes[2], clonetypes[3], clonetypes[4], clonetypes[5]);
  printf("  -sid1 N,  --sid1type N        : Set the 1st SID type for the socket to:\n");
  printf("                                  0: %s, 1: %s, 2: %s, 3: %s, 4: %s\n",
         sidtypes[0], sidtypes[1], sidtypes[2], sidtypes[3], sidtypes[4]);
  printf("  -sid2 N,  --sid2type N        : Set the 2nd SID type for the socket to:\n");
  printf("                                  0: %s, 1: %s, 2: %s, 3: %s, 4: %s\n",
         sidtypes[0], sidtypes[1], sidtypes[2], sidtypes[3], sidtypes[4]);
  printf("                                  (Available for 'Clone' chiptype only!)\n");
  printf("  -a1,      --as-one            : Socket 2 mirrors socket 1 (Socket 2 setting only!)\n");
  printf("--------------------------------------------------------------------------------------------------------------------\n");
}

int main(int argc, char **argv)
{
  memset(version, 0, 64);
  memset(project_version, 0, 64);
  memset(config, 0, 256);
  memset(read_data, 0, 1);
  memset(read_data_max, 0, 64);
  memset(read_data_uber, 0, 128);
  if (argc <= 1) {
    printf("Please supply atleast 1 option\n");
    print_help();
    goto exit;
  }
  if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-")) {
    print_help();
    goto exit;
  }
  fprintf(stdout, "Detecting Linux usbsid boards\n");

  if (usid_dev != 0) {
    rc = usbsid_init();
    if (rc != 0) {
        return -1;
    }
    /* zero length read to clear any lingering data */
    unsigned char buffer[1];
    libusb_bulk_transfer(devh, ep_out_addr, buffer, 0, &transferred, 1);
    printf("Connected to USBSID-Pico\n");
  }

  for (int param_count = 1; param_count < argc; param_count++) {
    if (!strcmp(argv[param_count], "-h") || !strcmp(argv[param_count], "--help")) {
      print_help();
      break;
    }

    if (!strcmp(argv[param_count], "-v") || !strcmp(argv[param_count], "--version")) {
      printf("Reading version: ");
      read_version(1);
      break;
    }

    if (!strcmp(argv[param_count], "-debug") || !strcmp(argv[param_count], "--debug")) {
      debug = 1;
      continue;
    }

    if (!strcmp(argv[param_count], "-r") || !strcmp(argv[param_count], "--read-config")) {
      printf("Reading config\n");
      read_config();
      printf("Printing config\n");
      print_config();
      break;
    }

    if (!strcmp(argv[param_count], "-detect") || !strcmp(argv[param_count], "--detect-sid-types")) {
      printf("Sending autodetect SID's command to USBSID-Pico and reading config\n");
      sid_autodetect();
      printf("Printing config\n");
      print_config();
      break;
    }

    if (!strcmp(argv[param_count], "-w") || !strcmp(argv[param_count], "--write-config")) {
      printf("Reading config\n");
      read_config();
      for (int pc = 1; pc < argc; pc++) {

        if (!strcmp(argv[pc], "-c") || !strcmp(argv[pc], "--sid-clock")) {
          pc++;
          int clockrate = atoi(argv[pc]);
          if(clockrate >= sizeof(clockrates) / sizeof(clockrates[0])) {
            printf("%d is not a correct clockrate option!\n", clockrate);
            goto exit;
          }
          printf("Set SID clockrate from %d to: %d\n", usbsid_config.clock_rate, clockrates[clockrate]);
          usbsid_config.clock_rate = clockrate;
          write_config_command(STORE_CONFIG, 0x0, clockspeed_n(usbsid_config.clock_rate), 0x0, 0x0);
          continue;
        }

        if (!strcmp(argv[pc], "-led") || !strcmp(argv[pc], "--led-enabled")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= sizeof(enabled) / sizeof(enabled[0])) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set LED from %s to: %s\n", enabled[usbsid_config.LED.enabled], enabled[en]);
          usbsid_config.LED.enabled = en;
          write_config_command(STORE_CONFIG, 0x3, 0x0, en, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-lbr") || !strcmp(argv[pc], "--led-breathe")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= sizeof(enabled) / sizeof(enabled[0])) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set LED idle breathing from %s to: %s\n", enabled[usbsid_config.LED.idle_breathe], enabled[en]);
          usbsid_config.LED.idle_breathe = en;
          write_config_command(STORE_CONFIG, 0x3, 0x1, en, 0x0);
          continue;
        }

        if (!strcmp(argv[pc], "-rgb") || !strcmp(argv[pc], "--rgb-enabled")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= sizeof(enabled) / sizeof(enabled[0])) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set RGBLED from %s to: %s\n", enabled[usbsid_config.RGBLED.enabled], enabled[en]);
          usbsid_config.RGBLED.enabled = en;
          write_config_command(STORE_CONFIG, 0x4, 0x0, en, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-rgbbr") || !strcmp(argv[pc], "--rgb-breathe")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= sizeof(enabled) / sizeof(enabled[0])) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set RGBLED idle breathing from %s to: %s\n", enabled[usbsid_config.RGBLED.idle_breathe], enabled[en]);
          usbsid_config.RGBLED.idle_breathe = en;
          write_config_command(STORE_CONFIG, 0x4, 0x1, en, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-br") || !strcmp(argv[pc], "--rgb-brightness")) {
          pc++;
          int brightness = atoi(argv[pc]);
          if(brightness < 0 || brightness > 255) {
            printf("%d is not a correct brightness value!\n", brightness);
            goto exit;
          }
          printf("Set RGBLED brightness from %d to: %d\n", usbsid_config.RGBLED.brightness, brightness);
          usbsid_config.RGBLED.brightness = brightness;
          write_config_command(STORE_CONFIG, 0x4, 0x2, brightness, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-rgbsid") || !strcmp(argv[pc], "--rgb-sidtouse")) {
          pc++;
          int sid = atoi(argv[pc]);
          if(sid == 0 || sid > 4) {
            printf("%d is not a correct SID number!\n", sid);
            goto exit;
          }
          printf("Set RGBLED SID to use from %d to: %d\n", usbsid_config.RGBLED.sid_to_use, sid);
          usbsid_config.RGBLED.sid_to_use = sid;
          write_config_command(STORE_CONFIG, 0x4, 0x3, sid, 0x0);
          continue;
        }

        if (!strcmp(argv[pc], "-sock") || !strcmp(argv[pc], "--socket")) {
          pc++;
          int socket = atoi(argv[pc]);
          if (socket == 0 || socket > 2) {
            printf("Incorrect socket number: %d\n", socket);
            goto exit;
          }
          char * str = (socket == 1 ? "One" : "Two");
          struct { /* anonymous struct for type safety */
            bool    enabled : 1;
            bool    dualsid : 1;
            //bool    act_as_one : 1;
            uint8_t chiptype;
            uint8_t clonetype;
            uint8_t sid1type;
            uint8_t sid2type;
            /* warning: pointer type mismatch in conditional expression */
          } *socket_pointer = (socket == 1 ? &usbsid_config.socketOne : &usbsid_config.socketTwo);
          for (int p = 1; p < argc; p++) {

            if (!strcmp(argv[p], "-test") || !strcmp(argv[p], "--test")) {
              printf("%d ?\n", socket_pointer->enabled);
              printf("%d ?\n", socket_pointer->dualsid);
              printf("%d ?\n", socket_pointer->chiptype);
              printf("%d ?\n", socket_pointer->clonetype);
              printf("%d ?\n", socket_pointer->sid1type);
              printf("%d ?\n", socket_pointer->sid2type);
              if (socket == 2) printf("%d ?\n", usbsid_config.socketTwo.act_as_one);
              goto exit;
            }

            if (socket == 2 && (!strcmp(argv[p], "-a1") || !strcmp(argv[p], "--as-one"))) {
              p++;
              int asone = atoi(argv[p]);
              if (asone >= sizeof(enabled) / sizeof(enabled[0])) {
                printf("%d is not an enable option!\n", asone);
                goto exit;
              }
              printf("Set SocketTwo act-as-one from %s to: %s\n", enabled[usbsid_config.socketTwo.act_as_one], enabled[asone]);
              usbsid_config.socketTwo.act_as_one = asone;
              write_config_command(STORE_CONFIG, 0x2, 0x6, usbsid_config.socketTwo.act_as_one, 0);
              continue;
            }

            if (!strcmp(argv[p], "-en") || !strcmp(argv[p], "--enabled")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int en = atoi(argv[p]);
              if (en >= sizeof(enabled) / sizeof(enabled[0])) {
                printf("%d is not an enable option!\n", en);
                goto exit;
              }
              printf("Set Socket%s enabled from %s to: %s\n", str, enabled[socket_pointer->enabled], enabled[en]);
              socket_pointer->enabled = en;
              write_config_command(STORE_CONFIG, socket, 0x0, socket_pointer->enabled, 0);
              continue;
            }

            if (!strcmp(argv[p], "-d") || !strcmp(argv[p], "--dualsid")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int en = atoi(argv[p]);
              if (en >= sizeof(enabled) / sizeof(enabled[0])) {
                printf("%d is not an dualsid option!\n", en);
                goto exit;
              }
              printf("Set Socket%s dualsid from %s to: %s\n", str, enabled[socket_pointer->dualsid], enabled[en]);
              socket_pointer->dualsid = en;
              write_config_command(STORE_CONFIG, socket, 0x1, socket_pointer->dualsid, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-chip") || !strcmp(argv[p], "--chiptype")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= sizeof(chiptypes) / sizeof(chiptypes[0])) {
                printf("%d is not an chiptype option!\n", t);
                goto exit;
              }
              printf("Set Socket%s chiptype from %s to: %s\n", str, chiptypes[socket_pointer->chiptype], chiptypes[t]);
              socket_pointer->chiptype = t;
              write_config_command(STORE_CONFIG, socket, 0x2, socket_pointer->chiptype, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-clone") || !strcmp(argv[p], "--clonetype")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= sizeof(clonetypes) / sizeof(clonetypes[0])) {
                printf("%d is not an clonetype option!\n", t);
                goto exit;
              }
              printf("Set Socket%s clonetype from %s to: %s\n", str, clonetypes[socket_pointer->clonetype], clonetypes[t]);
              socket_pointer->clonetype = t;
              write_config_command(STORE_CONFIG, socket, 0x3, socket_pointer->clonetype, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-sid1") || !strcmp(argv[p], "--sid1type")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= sizeof(sidtypes) / sizeof(sidtypes[0])) {
                printf("%d is not an sid1type option!\n", t);
                goto exit;
              }
              printf("Set Socket%s sid1type from %s to: %s\n", str, sidtypes[socket_pointer->sid1type], sidtypes[t]);
              socket_pointer->sid1type = t;
              write_config_command(STORE_CONFIG, socket, 0x4, socket_pointer->sid1type, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-sid2") || !strcmp(argv[p], "--sid2type")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= sizeof(sidtypes) / sizeof(sidtypes[0])) {
                printf("%d is not an sid2type option!\n", t);
                goto exit;
              }
              printf("Set Socket%s sid2type from %s to: %s\n", str, sidtypes[socket_pointer->sid2type], sidtypes[t]);
              socket_pointer->sid2type = t;
              write_config_command(STORE_CONFIG, socket, 0x5, socket_pointer->sid2type, 0x0);
              continue;
            }
          }
        }
      }
      continue;
    }

    if (!strcmp(argv[param_count], "-import") || !strcmp(argv[param_count], "--import-config")) {
      param_count++;
      char * filename = argv[param_count];
      if (filename == NULL) {
        printf("No filename supplied\n");
        goto exit;
      }
      int len = strlen(filename);
      const char *last_four = &filename[len-4];
      if ((strcmp(last_four,  ".ini"))) {
        printf("Incorrect file extension: '%s'\n", last_four);
        goto exit;
      }
      FILE *f = fopen(filename, "rb");
      if (f == NULL) {
        printf("Error reading config file: %s\n", filename);
        goto exit;
      } else {
       fclose(f);
      }
      printf("Reading config from file %s\n", filename);
      ini_parse(filename, import_ini, &usbsid_config);
      print_config();
      printf("Writing config to USBSID-Pico\n");
      write_config(&usbsid_config);
      printf("Sending save config command\n");
      save_config(0);
      printf("Reading back config from USBSID-Pico for visual verification\n");
      read_config();
      print_config();
      goto exit;
    }

    if (!strcmp(argv[param_count], "-export") || !strcmp(argv[param_count], "--export-config")) {
      param_count++;
      char * filename = argv[param_count];
      if (filename == NULL || filename == 0x0) {
        filename = "USBSID-Pico-cfg.ini";
      }
      int len = strlen(filename);
      const char *last_four = &filename[len-4];
      if ((strcmp(last_four,  ".ini"))) {
        printf("Incorrect file extension: '%s', using default filename\n", last_four);
        filename = "USBSID-Pico-cfg.ini";
      }
      printf("Reading USBSID-Pico firmware version\n");
      read_version(0);
      printf("Reading config from USBSID-Pico\n");
      read_config();
      print_config();
      printf("Writing config to new ini file\n");
      write_config_ini(&usbsid_config, filename);
      goto exit;
    }

    if (!strcmp(argv[param_count], "-default") || !strcmp(argv[param_count], "--default-ini")) {
      printf("Generating default config\n");
      write_config_ini(&usbsid_config, "USBSID-Pico-cfg.ini");
      goto exit;
    }

    if (!strcmp(argv[param_count], "-s") || !strcmp(argv[param_count], "--save-config")) {
      printf("Sending save config command\n");
      save_config(0);
      continue;
    }
    if (!strcmp(argv[param_count], "-sr") || !strcmp(argv[param_count], "--save-reboot")) {
      printf("Sending save config and reboot command\n");
      save_config(1);
      continue;
    }
    if (!strcmp(argv[param_count], "-reset") || !strcmp(argv[param_count], "--reset-sids")) {
      printf("Sending reset command\n");
      write_command(RESET_SID);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-reboot") || !strcmp(argv[param_count], "--reboot-usp")) {
      printf("Sending reboot command\n");
      write_command(RESET_MCU);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-boot") || !strcmp(argv[param_count], "--bootloader")) {
      printf("Sending reboot to bootloader command\n");
      write_command(BOOTLOADER);
      goto exit;
    }
  }
exit:
  if (usid_dev != -1) {
    usbsid_close();
  }
  return 0;
};

#pragma diagnostic pop
