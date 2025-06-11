/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * cfg_usbsid.c
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
#include <time.h>
#include <libusb.h>

#include "inih/ini.h"
#include "cfg_usbsid.h"
#include "cfg_skpico.h"
#include "macros.h"

/* Compile with:
 * gcc -g3 -L/usr/local/lib inih/ini.c cfg_usbsid.c -o cfg_usbsid $(pkg-config --libs --cflags libusb-1.0)
 * gcc -g3 -L/usr/local/lib examples/config-tool/inih/ini.c examples/config-tool/cfg_usbsid.c -o examples/config-tool/cfg_usbsid $(pkg-config --libs --cflags libusb-1.0) -I./examples/config-tool/inih ; cp examples/config-tool/cfg_usbsid ~/.local/bin
 */

/* ---------------------- */

/* init local libusb variables */
static int debug = 0;
static struct libusb_device_handle *devh = NULL;
static libusb_context *ctx = NULL;
static unsigned char encoding[] = { 0x00, 0x10, 0x0E, 0x00, 0x00, 0x00, 0x08 };
static int ep_out_addr = 0x02;
static int ep_in_addr  = 0x82;
static int len, rc, actual_length, transferred;
static int usid_dev = -1;

/* -----USBSID-Pico------ */

/* init local usbsid-pico variables */
static const enum config_clockrates clockrates[] = { DEFAULT, PAL, NTSC, DREAN, NTSC2 };
static uint32_t read_clock_rate;
static char version[64] = {0};
static char project_version[64] = {0};
static uint8_t config[256] = {0};
static uint8_t socket_config[10] = {0};
static Config usbsid_config = USBSID_DEFAULT_CONFIG_INIT;

/* -----SIDKICK-pico----- */

/* init local skpico variables */
static uint8_t skpico_config[64] = {0xFF};
static uint8_t skpico_version[36] = {0xFF};
static uint8_t base_address = 0x0;
static int sid_socket = 1;

/* -----USBSID-Pico------ */

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
  if (MATCH("General", "lock_clockrate")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->lock_clockrate = p;
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
    p = value_position(value, truefalse);
    if (p != 666) ini_config->LED.enabled = p;
  }
  if (MATCH("LED", "idle_breathe")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->LED.idle_breathe = p;
  }
  if (MATCH("RGBLED", "enabled")) {
    p = value_position(value, truefalse);
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
  if (MATCH("FMOPL", "enabled")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->FMOpl.enabled = p;
  }
  if (MATCH("Audioswitch", "set_to")) {
    p = value_position(value, mono_stereo);
    if (p != 666) ini_config->stereo_en = p;
  }
  if (MATCH("Audioswitch", "lock_audio_switch")) {
    p = value_position(value, truefalse);
    if (p != 666) ini_config->lock_audio_sw = p;
  }
  if (debug == 1) {
    printf("SECTION: %s NAME: %s VALUE: %s\n", section, name, value);
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
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "lock_clockrate = %s\n", truefalse[config->lock_clockrate]);
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
    fprintf(f, "[FMOPL]\n");
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "enabled = %s\n", truefalse[config->FMOpl.enabled]);
    fprintf(f, "\n");
    fprintf(f, "[Audioswitch]\n");
    fprintf(f, "; Possible options: %s, %s\n", mono_stereo[0], mono_stereo[1]);
    fprintf(f, "set_to = %s\n", mono_stereo[config->stereo_en]);
    fprintf(f, "; Possible options: %s, %s\n", truefalse[0], truefalse[1]);
    fprintf(f, "lock_audio_switch = %s\n", truefalse[config->lock_audio_sw]);
    fprintf(f, "\n");
    fclose(f);
  };
}

/* -----USBSID-Pico------ */

void usbsid_close(void)
{
  printf("Closing USBSID-Pico\n");
  for (int if_num = 0; if_num < 2; if_num++) {
    if (libusb_kernel_driver_active(devh, if_num)) {
        libusb_detach_kernel_driver(devh, if_num);
    }
    libusb_release_interface(devh, if_num);
  }
  if (devh != NULL)
    libusb_close(devh);
  libusb_exit(ctx);
  devh = NULL;
}

int usbsid_init(void)
{
    if (devh != NULL) {
        libusb_close(devh);
    }

    rc = libusb_init(&ctx);
    if (rc != 0) {
        fprintf(stderr, "Error initializing libusb: %s: %s\n",
        libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);

    devh = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
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
    if (rc < 0) {
        fprintf(stderr, "Error configuring line state during control transfer: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    rc = libusb_control_transfer(devh, 0x21, 0x20, 0, 0, encoding, count_of(encoding), 0);
    if (rc < 0 || rc != 7) {
        fprintf(stderr, "Error configuring line encoding during control transfer: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    usid_dev = (rc > 0 && rc == 7) ? 0 : -1;

    if (usid_dev < 0)
    {
        fprintf(stderr, "Could not open SID device USBSID.\n");
        goto out;
    }

    /* zero length read to clear any lingering data */
    unsigned char buffer[1];
    libusb_bulk_transfer(devh, ep_out_addr, buffer, 0, &transferred, 1);
    libusb_bulk_transfer(devh, ep_in_addr, buffer, 0, &transferred, 1);
    /* fprintf(stdout, "usbsid_init: detected [rc]%d [usid_dev]%d\n", rc, usid_dev); */

  return usid_dev;
out:;
  if (devh != NULL)
    usbsid_close();
  return rc;
}

/* -----USBSID-Pico------ */

void write_chars(unsigned char * data, int size)
{
  int actual_length;
  if (libusb_bulk_transfer(devh, ep_out_addr, data, size, &actual_length, 0) < 0) {
    fprintf(stderr, "Error while sending char\n");
  }
}

int read_chars(unsigned char * data, int size)
{
  int actual_length;
  int rc = libusb_bulk_transfer(devh, ep_in_addr, data, size, &actual_length, 0);
  //printf("[R]$%02X:%02X\n", read_buffer[1], data[0]);
  if (rc == LIBUSB_ERROR_TIMEOUT) {
    printf("timeout (%d)\n", actual_length);
    return -1;
  } else if (rc < 0) {
    fprintf(stderr, "Error while waiting for char\n");
    return -1;
  }
  return actual_length;
}

/* -----USBSID-Pico------ */

void write_command(uint8_t command)
{
  command_buffer[0] |= command;
  // if (command == RESET_SID) command_buffer[1] = 0x1;
  write_chars(command_buffer, 3);
}

void write_config_command(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e)
{
  config_buffer[1] = a;
  config_buffer[2] = b;
  config_buffer[3] = c;
  config_buffer[4] = d;
  config_buffer[5] = e;
  write_chars(config_buffer, 6);
  return;
}

void apply_config(void)
{
  write_config_command(APPLY_CONFIG,0x0,0x0,0x0,0x0);
  return;
}

void save_config(int reboot)
{
  write_config_command((reboot == 1)?SAVE_CONFIG:SAVE_NORESET,0x0,0x0,0x0,0x0);
  return;
}

void write_config(Config * config)
{
  /* General */
  write_config_command(SET_CONFIG,0x0,
    clockspeed_n(config->clock_rate),
    (int)usbsid_config.lock_clockrate,0);

  /* socketOne */
  write_config_command(SET_CONFIG,0x1,0x0,config->socketOne.enabled,0);
  write_config_command(SET_CONFIG,0x1,0x1,config->socketOne.dualsid,0);
  write_config_command(SET_CONFIG,0x1,0x2,config->socketOne.chiptype,0);
  write_config_command(SET_CONFIG,0x1,0x3,config->socketOne.clonetype,0);
  write_config_command(SET_CONFIG,0x1,0x4,config->socketOne.sid1type,0);
  write_config_command(SET_CONFIG,0x1,0x5,config->socketOne.sid2type,0);

  /* socketTwo */
  write_config_command(SET_CONFIG,0x2,0x0,config->socketTwo.enabled,0);
  write_config_command(SET_CONFIG,0x2,0x1,config->socketTwo.dualsid,0);
  write_config_command(SET_CONFIG,0x2,0x2,config->socketTwo.chiptype,0);
  write_config_command(SET_CONFIG,0x2,0x3,config->socketTwo.clonetype,0);
  write_config_command(SET_CONFIG,0x2,0x4,config->socketTwo.sid1type,0);
  write_config_command(SET_CONFIG,0x2,0x5,config->socketTwo.sid2type,0);
  write_config_command(SET_CONFIG,0x2,0x6,config->socketTwo.act_as_one,0);

  /* LED */
  write_config_command(SET_CONFIG,0x3,0x0,config->LED.enabled,0);
  write_config_command(SET_CONFIG,0x3,0x1,config->LED.idle_breathe,0);

  /* RGBLED */
  write_config_command(SET_CONFIG,0x4,0x0,config->RGBLED.enabled,0);
  write_config_command(SET_CONFIG,0x4,0x1,config->RGBLED.idle_breathe,0);
  write_config_command(SET_CONFIG,0x4,0x2,config->RGBLED.brightness,0);
  write_config_command(SET_CONFIG,0x4,0x3,config->RGBLED.sid_to_use,0);

  /* FMOpl */
  write_config_command(SET_CONFIG,0x9,config->FMOpl.enabled,0,0);

  /* Audio switch (works on PCB v1.3+ only) */
  write_config_command(SET_CONFIG,0xA,config->stereo_en,0,0);
  write_config_command(SET_CONFIG,0xB,config->lock_audio_sw,0,0);

  printf("Sending save config command\n");
  save_config(0);

  return;
}

void print_cfg_buffer(const uint8_t *buf, size_t len)
{
  printf("[PRINT CFG BUFFER START (len: %ld)]\n", len);
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

void read_sid_clockspeed(void)
{
  memset(config_buffer+1, 0, (count_of(config_buffer))-1);
  config_buffer[1] = 0x57;
  write_chars(config_buffer, count_of(config_buffer));

  int len;
  memset(read_data, 0, count_of(read_data));
  len = read_chars(read_data, count_of(read_data));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data[0]);
  read_clock_rate = clockrates[read_data[0]];
  return;
}

void print_sid_clockspeed(void)
{
  printf("USBSID-Pico SID Clockrate is set to: %u\n", read_clock_rate);
}

void read_version(uint8_t cmd, int print_version)
{
  memset(config_buffer+1, 0, (count_of(config_buffer))-1);
  config_buffer[1] = cmd;
  write_chars(config_buffer, count_of(config_buffer));

  int len;
  memset(read_data_uber, 0, count_of(read_data_uber));
  len = read_chars(read_data_uber, count_of(read_data_uber));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);

  for (int i = 0; i < count_of(version) ; i++) {
    version[i] = read_data_uber[i];
  }
  if (len == 128 && read_data_uber[0] == cmd) len = read_chars(read_data_uber, count_of(read_data_uber));
  memcpy(&project_version[0], &version[1], count_of(project_version) - 1);
  if(print_version == 1 || debug == 1) printf("v%s\n", project_version);
  if(debug == 1) print_cfg_buffer(read_data_uber, 128);
  return;
}

void set_cfg_from_buffer(const uint8_t * buff, size_t len)
{
  static uint32_t clockrate = 0;
  for (int i = 0; i < (int)len; ++i) {
    if (debug == 1) {
      printf("BUFF[%d] = %02X\n", i, buff[i]);
    }
    switch(i) {
      case 0 ... 1:
        continue;
      case 5:
        usbsid_config.lock_clockrate = buff[i];
        break;
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
      case 55:
        usbsid_config.FMOpl.enabled = buff[i];
        break;
      case 56:
        usbsid_config.FMOpl.sidno = buff[i];
        break;
      case 57:
        usbsid_config.stereo_en = buff[i];
        break;
      case 58:
        usbsid_config.lock_audio_sw = buff[i];
        break;
      default:
        break;
    }
  }
  return;
}

void set_socketcfg_from_buffer(const uint8_t * buff, size_t len)
{
  if (buff[0] != READ_SOCKETCFG
      || buff[1] != 0x7F
      || buff[len - 1] != 0xFF
      || (len - 1) != 9) {
        printf("Socket config buffer with length %ld verification failed %02X %02X %02X %ld,\ndisplayed information can be incorrect!\n",
          count_of(buff), buff[0], buff[1], buff[len - 1], len - 1);
        return;
      }

  usbsid_config.socketOne.enabled    = (socket_config[2] >> 4) & 0xF;
  usbsid_config.socketOne.dualsid    = (socket_config[2] & 0xF);
  usbsid_config.socketOne.chiptype   = (socket_config[3] >> 4) & 0xF;
  usbsid_config.socketOne.clonetype  = (socket_config[3] & 0xF);
  usbsid_config.socketOne.sid1type   = (socket_config[4] >> 4) & 0xF;
  usbsid_config.socketOne.sid2type   = (socket_config[4] & 0xF);

  usbsid_config.socketTwo.enabled    = (socket_config[5] >> 4) & 0xF;
  usbsid_config.socketTwo.dualsid    = (socket_config[5] & 0xF);
  usbsid_config.socketTwo.chiptype   = (socket_config[6] >> 4) & 0xF;
  usbsid_config.socketTwo.clonetype  = (socket_config[6] & 0xF);
  usbsid_config.socketTwo.sid1type   = (socket_config[7] >> 4) & 0xF;
  usbsid_config.socketTwo.sid2type   = (socket_config[7] & 0xF);

  usbsid_config.socketTwo.act_as_one = (socket_config[8] & 0xF);

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
  printf("[CONFIG] [CLOCK RATE LOCKED] %s\n",
    truefalse[(int)usbsid_config.lock_clockrate]);
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

  printf("[CONFIG] [FMOpl] %s\n",
    enabled[(int)usbsid_config.FMOpl.enabled]);
  printf("[CONFIG] [FMOpl] SIDno %d\n",
    usbsid_config.FMOpl.sidno);
  printf("[CONFIG] [AUDIO_SWITCH] %s\n",
    mono_stereo[(int)usbsid_config.stereo_en]);
  printf("[CONFIG] [AUDIO_SWITCH_LOCKED] %s\n",
    truefalse[(int)usbsid_config.lock_audio_sw]);
    return;
}

void sid_autodetect(uint8_t detection_routine)
{
  config_buffer[1] = DETECT_SIDS;
  config_buffer[2] = detection_routine;
  write_chars(config_buffer, count_of(config_buffer));

  int len;
  len = read_chars(read_data_uber, count_of(read_data_uber));
  //if (len == 0) len = read_chars(read_data_uber, count_of(read_data_uber));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);
  memcpy(&config[0], &read_data_uber[0], count_of(read_data_uber));

  if (debug == 1) print_cfg_buffer(config, count_of(config));
  set_cfg_from_buffer(config, count_of(config));
  return;
}

void clone_autodetect(void)
{
  config_buffer[1] = DETECT_CLONES;
  write_chars(config_buffer, count_of(config_buffer));

  int len;
  len = read_chars(read_data_uber, count_of(read_data_uber));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);
  memcpy(&config[0], &read_data_uber[0], count_of(read_data_uber));

  if (debug == 1) print_cfg_buffer(config, count_of(config));
  set_cfg_from_buffer(config, count_of(config));
  return;
}

void run_autodetection(bool reboot)
{
  config_buffer[1] = AUTO_DETECT;
  if (reboot) config_buffer[2] = 0x1;
  write_chars(config_buffer, count_of(config_buffer));
  if (reboot) return;

  int len;
  len = read_chars(read_data_uber, count_of(read_data_uber));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);
  memcpy(&config[0], &read_data_uber[0], count_of(read_data_uber));

  if (debug == 1) print_cfg_buffer(config, count_of(config));
  set_cfg_from_buffer(config, count_of(config));
  return;
}

void read_config(void)
{
  memset(config, 0, count_of(config));
  config_buffer[1] = 0x30;
  write_chars(config_buffer, count_of(config_buffer));

  int len;
  len = read_chars(read_data_uber, count_of(read_data_uber));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_uber[0]);
  memcpy(&config, &read_data_uber, count_of(read_data_uber));
  if (len == 128) len = read_chars(read_data_uber, count_of(read_data_uber));

  if (debug == 1) print_cfg_buffer(config, count_of(config));
  set_cfg_from_buffer(config, count_of(config));
  return;
}

void read_socket_config(void)
{
  memset(socket_config, 0, count_of(socket_config));
  config_buffer[1] = 0x37;
  write_chars(config_buffer, count_of(config_buffer));

  int len;
  len = read_chars(read_data_max, count_of(read_data_max));
  if (debug == 1) printf("Read %d bytes of data, byte 0 = %02X\n", len, read_data_max[0]);
  memcpy(&socket_config, &read_data_max, count_of(socket_config));

  if (debug == 1) print_cfg_buffer(socket_config, count_of(socket_config));
  set_socketcfg_from_buffer(socket_config, count_of(socket_config));
  return;
}

void apply_default_socket_settings(void)
{
  /* Pre applying default socket settings if needed */
  if (usbsid_config.socketOne.enabled == true) {
    if (usbsid_config.socketOne.dualsid == true) {
      if (usbsid_config.socketOne.chiptype != 1)
        usbsid_config.socketOne.chiptype = 1;  /* chiptype cannot be real with dualsid */
      if (usbsid_config.socketOne.clonetype == 0)
        usbsid_config.socketOne.clonetype = 1;  /* clonetype cannot be disabled with dualsid */
    } else {
      if (usbsid_config.socketOne.chiptype == 1) {
        if (usbsid_config.socketOne.clonetype == 0) {
          usbsid_config.socketOne.clonetype = 1;  /* clonetype cannot be disabled when chiptype is clone */
        }
      } else {
        usbsid_config.socketOne.clonetype = 0;  /* clonetype cannot be anything else when chiptype is real */
      }
    }
  }
  /* Pre applying default socket settings if needed */
  if (usbsid_config.socketTwo.enabled == true) {
    if (usbsid_config.socketTwo.dualsid == true) {
      if (usbsid_config.socketTwo.chiptype != 1)
        usbsid_config.socketTwo.chiptype = 1;  /* chiptype cannot be real with dualsid */
      if (usbsid_config.socketTwo.clonetype == 0) {
        usbsid_config.socketTwo.clonetype = 1;  /* clonetype cannot be disabled with dualsid */
      }
    } else {
      if (usbsid_config.socketTwo.chiptype == 1) {
        if (usbsid_config.socketTwo.clonetype == 0) {
          usbsid_config.socketTwo.clonetype = 1;  /* clonetype cannot be disabled when chiptype is clone */
        }
      } else {
        usbsid_config.socketTwo.clonetype = 0;  /* clonetype cannot be anything else when chiptype is real */
      }
    }
  }
}

void print_socket_config(void)
{
  int sock_one = 0, sock_two = 0, sids_one = 0, sids_two = 0, numsids = 0, act_as_one = 0;
  uint8_t one = 0, two = 0, three = 0, four = 0;
  uint8_t one_mask = 0, two_mask = 0, three_mask = 0, four_mask = 0;
  apply_default_socket_settings();

  act_as_one = usbsid_config.socketTwo.act_as_one;

  sock_one = usbsid_config.socketOne.enabled;
  sock_two = usbsid_config.socketTwo.enabled;

  sids_one = (sock_one == true) ? (usbsid_config.socketOne.dualsid == true) ? 2 : 1 : 0;
  sids_two = (sock_two == true) ? (usbsid_config.socketTwo.dualsid == true) ? 2 : 1 : 0;
  numsids = (sids_one + sids_two);

  printf("[CONFIG] Calculating socket settings\n");
  /* one == 0x00, two == 0x20, three == 0x40, four == 0x60 */
  /* act-as-one enabled overrules all settings */
  if (act_as_one) {                    /* act-as-one enabled overrules all settings */
    one = two = 0;                     /* CS1 low, CS2 low */
    three = four = 0;                  /* CS1 low, CS2 low */
  } else {
    if (sock_one && !sock_two) {       /* SocketOne enabled, SocketTwo disabled */
      one = 0b100;                     /* CS1 low, CS2 high */
      two = (sids_one == 2) ? 0b100    /* CS1 low, CS2 high */
        : 0b110;                       /* CS1 high, CS2 high */
      three = four = 0b110;            /* CS1 high, CS2 high */
      one_mask = 0x1F;
      two_mask = (sids_one == 2) ? 0x3F : 0x0;
      three_mask = 0x0;
      four_mask = 0x0;
    }
    if (!sock_one && sock_two) {       /* SocketOne disabled, SocketTwo enabled */
      one = 0b010;                     /* CS1 high, CS2 low */
      two = (sids_two == 2) ? 0b010    /* CS1 high, CS2 low */
        : 0b110;                       /* CS1 high, CS2 high */
      three = four = 0b110;            /* CS1 high, CS2 high */
      one_mask = 0x1F;
      two_mask = (sids_two == 2) ? 0x3F : 0x0;
      three_mask = 0x0;
      four_mask = 0x0;
    }
    if (sock_one && sock_two) {        /* SocketOne enabled, SocketTwo enabled */
      /* TODO: Compact if else spiderweb */
      if (sids_one == 1 && sids_two == 1) {
        one   = 0b100;
        two   = 0b010;
        three = 0b110;
        four  = 0b110;
        one_mask = 0x1F;
        two_mask = 0x1F;
        three_mask = 0x0;
        four_mask = 0x0;
      }
      if (sids_one == 2 && sids_two == 1) {
        one   = 0b100;
        two   = 0b100;
        three = 0b010;
        four  = 0b110;
        one_mask = 0x1F;
        two_mask = 0x3F;
        three_mask = 0x1F;
        four_mask = 0x0;
      }
      if (sids_one == 1 && sids_two == 2) {
        one   = 0b100;
        two   = 0b010;
        three = 0b010;
        four  = 0b110;
        one_mask = 0x1F;
        two_mask = 0x1F;
        three_mask = 0x3F;
        four_mask = 0x0;
      }
      if (sids_one == 2 && sids_two == 2) {
        one   = 0b100;
        two   = 0b100;
        three = 0b010;
        four  = 0b010;
        one_mask = 0x1F;
        two_mask = 0x3F;
        three_mask = 0x1F;
        four_mask = 0x3F;
      }
    }
  }

  printf("[SOCK_ONE EN] %s [SOCK_TWO EN] %s [ACT_AS_ONE] %s\n[NO SIDS] [SOCK_ONE] #%d [SOCK_TWO] #%d [TOTAL] #%d\n",
    (sock_one ? truefalse[0] : truefalse[1]),
    (sock_two ? truefalse[0] : truefalse[1]),
    (act_as_one ? truefalse[0] : truefalse[1]),
    sids_one, sids_two, numsids);

  printf("[BUS CONFIG]\n[ONE]   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[TWO]   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[THREE] %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[FOUR]  %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n",
    one, PRINTF_BYTE_TO_BINARY_INT8(one),
    two, PRINTF_BYTE_TO_BINARY_INT8(two),
    three, PRINTF_BYTE_TO_BINARY_INT8(three),
    four, PRINTF_BYTE_TO_BINARY_INT8(four));

  printf("[CONFIG] [SOCKET ONE] %s as %s\n",
    enabled[usbsid_config.socketOne.enabled],
    socket[usbsid_config.socketOne.dualsid]);
  printf("[CONFIG] [SOCKET ONE] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketOne.chiptype],
    clonetypes[usbsid_config.socketOne.clonetype]);
  printf("[CONFIG] [SOCKET ONE] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketOne.sid1type],
    sidtypes[usbsid_config.socketOne.sid2type]);
  printf("[CONFIG] [SOCKET TWO] %s as %s\n",
    enabled[usbsid_config.socketTwo.enabled],
    socket[usbsid_config.socketTwo.dualsid]);
  printf("[CONFIG] [SOCKET TWO] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketTwo.chiptype],
    clonetypes[usbsid_config.socketTwo.clonetype]);
  printf("[CONFIG] [SOCKET TWO] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketTwo.sid1type],
    sidtypes[usbsid_config.socketTwo.sid2type]);
  printf("[CONFIG] [SOCKET TWO AS ONE] %s\n",
    truefalse[usbsid_config.socketTwo.act_as_one]);
}

/* -----SIDKICK-pico----- */

void skpico_config_mode(int debug)
{
  uint8_t skpicobuff[3] = {0};
  memcpy(skpicobuff, &init_configmode, 3);
  skpicobuff[1] = (skpicobuff[1] + base_address);
  write_chars(skpicobuff, 3);  /* Start or extend config mode */
  if (debug == 1) {
    printf("[%s][W]%02X $%02X:%02X\n", __func__, skpicobuff[0], skpicobuff[1], skpicobuff[2]);
  }
}

void skpico_extend_config_mode(int debug)
{
  uint8_t skpicobuff[3] = {0};
  memcpy(skpicobuff, &config_extend, 3);
  skpicobuff[1] = (skpicobuff[1] + base_address);
  write_chars(skpicobuff, 3);  /* Exit config mode */
  if (debug == 1) {
    printf("[%s][W]%02X $%02X:%02X\n", __func__, skpicobuff[0], skpicobuff[1], skpicobuff[2]);
  }
}

void skpico_end_config_mode(int debug)
{
  uint8_t skpicobuff[3] = {0};
  memcpy(skpicobuff, &config_exit, 3);
  skpicobuff[1] = (skpicobuff[1] + base_address);
  write_chars(skpicobuff, 3);  /* Exit config mode */
  if (debug == 1) {
    printf("[%s][W]%02X $%02X:%02X\n", __func__, skpicobuff[0], skpicobuff[1], skpicobuff[2]);
  }
}

void skpico_update_config(int debug)
{
  uint8_t skpicobuff[3] = {0};
  memcpy(skpicobuff, &config_update, 3);
  skpicobuff[1] = (skpicobuff[1] + base_address);
  write_chars(skpicobuff, 3);  /* Update config, doesn't save */
  if (debug == 1) {
    printf("[%s][W]%02X $%02X:%02X\n", __func__, skpicobuff[0], skpicobuff[1], skpicobuff[2]);
  }
}

void skpico_save_config(int debug)
{
  uint8_t skpicobuff[3] = {0};
  memcpy(skpicobuff, &config_writeupdate, 3);
  skpicobuff[1] = (skpicobuff[1] + base_address);
  write_chars(skpicobuff, 3);  /* Update & save config */
  if (debug == 1) {
    printf("[%s][W]%02X $%02X:%02X\n", __func__, skpicobuff[0], skpicobuff[1], skpicobuff[2]);
  }
}

void skpico_read_config(int debug)
{
  // memset(skpico_config, 0, count_of(skpico_config));  // Let's keep it 0xFF'd okay?
  if (debug == 1) {
    print_cfg_buffer(skpico_config, count_of(skpico_config));
  }

  skpico_config_mode(debug);

  for (int i = 0; i <= 63; ++i) {
    read_buffer[1] = (0x1d + base_address);
    int len;
    nanosleep((const struct timespec[]){{0, 1000L}}, NULL);  /* Teeny, weeny, usleepy */
    write_chars(read_buffer, 3);
    len = read_chars(read_data, count_of(read_data));
    skpico_config[i] = read_data[0];
    if (debug == 1) {
      printf("[%s][R%d]%02X $%02X:%02X\n", __func__, i, read_buffer[0], read_buffer[1], read_data[0]);
    }
  }

  skpico_extend_config_mode(debug);

  if (debug == 1) {
    print_cfg_buffer(skpico_config, count_of(skpico_config));
  }

  printf("[PRINT CFG SETTINGS START]\n");
  for (size_t i = 0; i < 64; i++) {
    if (i >= 4 && i <= 7) continue;
    if (i >= 13 && i <= 56) continue;
    if (i == 62 || i == 63) continue;
    if (i == 0 || i == 8) {
      printf("[%02ld] %s: %02X ~ %s\n", i, config_names[i], skpico_config[i], (skpico_config[i] < s_sid_types) ? (char*)sid_types[skpico_config[i]] : (char*)error_type[0]);
      continue;
    }
    if (i == 10) {
      printf("[%02ld] %s: %02X ~ %s\n", i, config_names[i], skpico_config[i], (skpico_config[i] < s_sid2_address) ? (char*)sid2_address[skpico_config[i]] : (char*)error_type[0]);
      continue;
    }
    if (i == 59) {
      printf("[%02ld] %s: %02X ~ %s\n", i, config_names[i], skpico_config[i], (skpico_config[i] < s_clock_speed) ? (char*)clock_speed[skpico_config[i]] : (char*)error_type[0]);
      continue;
    }
    printf("[%02ld] %s: %02X\n", i, config_names[i], skpico_config[i]);
  }
  printf("[PRINT CFG SETTINGS END]\n");

  //return;
}

void skpico_read_version(int debug)
{
  uint8_t skpico_version_result[SKPICO_VER_SIZE] = {0};
  char skpico_version[SKPICO_VER_SIZE] = {0};
  uint8_t skpicobuff[3] = {0, 0, 0};
  int len;

  char skpico_correction = 0x60;  /* { 0x70, 0x69, 0x63, 0x6F } */

  skpico_config_mode(debug);
  skpico_extend_config_mode(debug);

  for (int i = 0; i < SKPICO_VER_SIZE; i++) {

    skpicobuff[1] = (0x1E + base_address);
    skpicobuff[2] = 0xE0 + i;
    write_chars(skpicobuff, 3);
    if (debug) printf("[%s][W]%02X $%02X:%02X\n", __func__, skpicobuff[0], skpicobuff[1], skpicobuff[2]);

    read_buffer[1] = (0x1D + base_address);
    write_chars(read_buffer, 3);
    len = read_chars(read_data, count_of(read_data));
    if (debug) printf("[%s][R]%02X $%02X:%02X\n", __func__, read_buffer[0], read_buffer[1], read_data[0]);
    skpico_version_result[i] = read_data[0];
    if (i >= 2 && i <= 5) skpico_version_result[i] |= skpico_correction;
  }

  if (debug) print_cfg_buffer(skpico_version_result, count_of(skpico_version_result));
  memcpy(&skpico_version[0], &skpico_version_result, count_of(skpico_version_result) - 1);
  if (skpico_version[0] != 0) printf("[CONFIG] SIDKICK-pico version is: %.36s\n", skpico_version);
}

void skpico_write_config(int debug)
{
  uint8_t skpicobuff[3] = {0};
  memcpy(skpicobuff, &start_config_write, 3);
  skpicobuff[1] = (skpicobuff[1] + base_address);
  write_chars(skpicobuff, 3);
  if (debug == 1) {
    printf("[%s][W]%02X $%02X:%02X\n", __func__, skpicobuff[0], skpicobuff[1], skpicobuff[2]);
  }

  for (int i = 0; i < 64 ; i++) {
    uint8_t skpicobuff[3] = {0};
    memcpy(skpicobuff, &config_write, 3);
    skpicobuff[1] = (skpicobuff[1] + base_address);
    skpicobuff[2] = skpico_config[i];  /* config value */
    write_chars(skpicobuff, 3);
    if (debug == 1) {
      printf("[%s][W%d]%02X $%02X:%02X\n", __func__, i, skpicobuff[0], skpicobuff[1], skpicobuff[2]);
    }
  }
}

/* -----SIDKICK-pico----- */

void print_help_skpico(void)
{
  printf("--------------------------------------------------------------------------------------------------------------------\n");
  printf("USBSID-Pico SIDKICK-pico configtool\n");
  printf("This tool should work with any SKPico PCB and firmware version, but comes without any guarantees\n");
  printf("Use at your own risk!\n");
  printf("\n");
  printf("IMPORTANT! For socket 2 to work the USBSID-Pico needs to be in 2x, 3x or 4x SID mode\n");
  printf("\n");
  printf("Usage:\n");
  printf("$ cfg_usbsid -skpico [options]\n");
  printf("--[OPTIONS]---------------------------------------------------------------------------------------------------------\n");
  printf("  -h,       --help              : Show this help message\n");
  printf("  -d,       --debug             : Prints all read write debug information\n");
  printf("  --default-config              : Resets the SKPico to default configuration\n");
  printf("  -sock     --socket            : Set the USBSID-Pico socket to use, defaults to socket 1\n");
  printf("  -addr     --base-address      : Set the USBSID-Pico base address to use\n");
  printf("  (-sock and -addr are mutually exclusive, you can choose only one)\n");
  printf("  -r,       --read              : Read and print SIDKICK-pico configuration\n");
  printf("  -w,       --write             : Write single config item to SIDKICK-pico (will read the current config first)\n");
  printf("--[MANUAL CONFIGURATION]--------------------------------------------------------------------------------------------\n");
  printf("  All the following options require '-w'\n");
  printf("  -s1       --sidone            : Change SID type to:\n");
  printf("                                  0: %s 1: %s 2: %s 3: %s 4: %s\n",
         sid_types[0], sid_types[1], sid_types[2], sid_types[3], sid_types[4]);
  printf("  -s1vol    --sidone-volume     : Change volume from 0 to 15\n");
  printf("  -s1db     --sidone-digiboost  : Change digiboost from 0 to 15\n");
  printf("  -s2       --sidtwo            : Change SID type to:\n");
  printf("                                  0: %s 1: %s 2: %s 3: %s 4: %s\n",
         sid_types[0], sid_types[1], sid_types[2], sid_types[3], sid_types[4]);
  printf("  -s2vol    --sidtwo-volume     : Change volume from 0 to 15\n");
  printf("  -s2db     --sidtwo-digiboost  : Change digiboost from 0 to 15\n");
  printf("  -io       --iopina5a8         : Change IO pin setting (A5 for USBSID-Pico) to:\n");
  printf("                                  0: %s 1: %s 2: %s 3: %s 4: %s 5: %s\n",
         sid2_address[0], sid2_address[1], sid2_address[2], sid2_address[3], sid2_address[4], sid2_address[5]);
  printf("  -clock    --clockspeed        : Change clockspeed to:\n");
  printf("                                  0: %s 1: %s 2: %s\n",
         clock_speed[0], clock_speed[1], clock_speed[2]);
  printf("  -dd       --digidetect        : Change digidetect to on or off\n");
  printf("--[NOT CONFIGURABLE]------------------------------------------------------------------------------------------------\n");
  printf("  SID panning ~ defaults to 7, approx the middle\n");
  printf("  SID balance ~ defaults to 7, approx the middle\n");
  printf("--------------------------------------------------------------------------------------------------------------------\n");
}

void config_skpico(int argc, char **argv)
{
  if (argc <= 2) {
    printf("Please supply atleast 1 option\n");
    print_help_skpico();
    goto exit;
  }
  if (!strcmp(argv[2], "-h") || !strcmp(argv[2], "--help") || !strcmp(argv[2], "-")) {
    print_help_skpico();
    goto exit;
  }
  int debug = 0;
  for (int param_count = 2; param_count < argc; param_count++) {
    /* -sock and -addr are mutually exclusive! */
    if (!strcmp(argv[param_count], "-sock") || !strcmp(argv[param_count], "--socket")) {
      param_count++;
      sid_socket = atoi(argv[param_count]);
      int socket_base = (sid_socket == 1) ? 0 : (sid_socket == 2) ? 1 : 0;
      base_address = (socket_base * 0x40);
    }
    if (!strcmp(argv[param_count], "-addr") || !strcmp(argv[param_count], "--base-address")) {
      param_count++;
      uint8_t addr = strtol(argv[param_count], NULL, 16);
      if (addr == 0x0 || addr == 0x20 || addr == 0x40 || addr == 0x60) {
        base_address = addr;
        printf("SIDKICK-pico base address 0x%02X selected\n", base_address);
      } else {
        printf("Error, incorrect base address 0x%02X (%u)!\n", addr, addr);
        goto exit;
      }
    }
    if (!strcmp(argv[param_count], "-d") || !strcmp(argv[param_count], "--debug")) {
      debug = 1;
    }
    if (!strcmp(argv[param_count], "--default-config")) {
      memcpy(skpico_config, skpico_default_config, 64);
      printf("Start writing default config\n");
      skpico_config_mode(debug);
      skpico_write_config(debug);

      printf("Start saving default config\n");
      sleep(0.5);
      skpico_save_config(debug);

      printf("Extend config mode\n");
      skpico_extend_config_mode(debug);

      sleep(1);
      printf("Start reading config\n");
      skpico_read_config(debug);
      skpico_end_config_mode(debug);
    }
  }

  printf("Using USBSID-Pico socket %d with base address 0x%02X\n", sid_socket, base_address);

  for (int param_count = 2; param_count < argc; param_count++) {
    if (!strcmp(argv[param_count], "-r") || !strcmp(argv[param_count], "--read")) {
      param_count++;
      printf("Sending reset SIDs command\n");
      write_command(RESET_SID);
      printf("Waiting a second or two for SKPico MCU to settle\n");
      sleep(2);
      printf("Read config\n");
      skpico_read_config(debug);
      skpico_end_config_mode(debug);
      write_command(RESET_SID);
      return;
    }
    if (!strcmp(argv[param_count], "-rv") || !strcmp(argv[param_count], "--read-version")) {
      param_count++;
      printf("Read version\n");
      skpico_read_version(debug);
      skpico_end_config_mode(debug);
      write_command(RESET_SID);
      return;
    }
    if (!strcmp(argv[param_count], "-w") || !strcmp(argv[param_count], "--write")) {
      param_count++;
      printf("Write config ~ read current config first\n");
      skpico_read_config(debug);
      for (int param_count = 1; param_count < argc; param_count++) {
        if (!strcmp(argv[param_count], "-s1") || !strcmp(argv[param_count], "--sidone")) {
          param_count++;
          int sid = atoi(argv[param_count]);
          if (sid < 4) {
            printf("SID one from: %s to: %s\n", sid_types[skpico_config[0]], sid_types[sid]);
            skpico_config[0] = sid;
          }
        }
        if (!strcmp(argv[param_count], "-s2") || !strcmp(argv[param_count], "--sidtwo")) {
          param_count++;
          int sid = atoi(argv[param_count]);
          if (sid < 5) {
            printf("SID two from: %s to: %s\n", sid_types[skpico_config[8]], sid_types[sid]);
            skpico_config[8] = sid;
          }
        }
        if (!strcmp(argv[param_count], "-s1vol") || !strcmp(argv[param_count], "--sidone-volume")) {
          param_count++;
          int vol = atoi(argv[param_count]);
          if (vol >= 0 && vol <= 14) {
            printf("SID one volume from: %d to: %d\n", skpico_config[3], vol);
            skpico_config[3] = vol;
          }
        }
        if (!strcmp(argv[param_count], "-s2vol") || !strcmp(argv[param_count], "--sidtwo-volume")) {
          param_count++;
          int vol = atoi(argv[param_count]);
          if (vol >= 0 && vol <= 14) {
            printf("SID two volume from: %d to: %d\n", skpico_config[11], vol);
            skpico_config[11] = vol;
          }
        }
        if (!strcmp(argv[param_count], "-s1db") || !strcmp(argv[param_count], "--sidone-digiboost")) {
          param_count++;
          int db = atoi(argv[param_count]);
          if (db >= 0 && db <= 14) {
            printf("SID one digiboost from: %d to: %d\n", skpico_config[1], db);
            skpico_config[1] = db;
          }
        }
        if (!strcmp(argv[param_count], "-s2db") || !strcmp(argv[param_count], "--sidtwo-digiboost")) {
          param_count++;
          int db = atoi(argv[param_count]);
          if (db >= 0 && db <= 14) {
            printf("SID two digiboost from: %d to: %d\n", skpico_config[9], db);
            skpico_config[9] = db;
          }
        }
        if (!strcmp(argv[param_count], "-io") || !strcmp(argv[param_count], "--iopina5a8")) {
          param_count++;
          int io = atoi(argv[param_count]);
          if (io < 6) {
            printf("SKPico SID 2 IO pin from: %s to: %s\n", sid2_address[skpico_config[10]], sid2_address[io]);
            skpico_config[10] = io;
          }
        }
        if (!strcmp(argv[param_count], "-clock") || !strcmp(argv[param_count], "--clockspeed")) {
          param_count++;
          int clk = atoi(argv[param_count]);
          if (clk < 3) {
            printf("SKPico clockspeed from: %s to: %s\n", clock_speed[skpico_config[59]], clock_speed[clk]);
            skpico_config[59] = clk;
          }
        }
        if (!strcmp(argv[param_count], "-dd") || !strcmp(argv[param_count], "--digidetect")) {
          param_count++;
          int digidetect = atoi(argv[param_count]);
          if (digidetect <= 1 ) {
            printf("Digidetect from: %s to: %s\n", enabled[skpico_config[61]], enabled[digidetect]);
            skpico_config[61] = digidetect;
          }
        }
      }
      // Default & and fallback
      if (skpico_config[2] != 1) skpico_config[2] =   1;  // register read
      skpico_config[12] =  7;  // panning
      skpico_config[58] =  7;  // balance
      skpico_config[61] =  1;  // digidetect on
      if (skpico_config[59] > 2) skpico_config[59] = 0;  // reset clockspeed to PAL if incorrect

      printf("Start writing config\n");
      skpico_extend_config_mode(debug);
      skpico_write_config(debug);

      printf("Start saving config\n");
      sleep(0.5);
      skpico_save_config(debug);

      printf("Extend config mode\n");
      skpico_extend_config_mode(debug);

      sleep(1);
      printf("Start reading config\n");
      skpico_read_config(debug);
      skpico_end_config_mode(debug);

      sleep(0.5);
      goto exit;
      break;
    }
    /* NOTICE: Undocumented as of yet due to wonky results */
    if (!strcmp(argv[param_count], "-dac") || !strcmp(argv[param_count], "--dacmode")) {
      param_count++;
      uint8_t skpicobuff[3] = {0};
      if (!strcmp(argv[param_count], "mono")) {
        printf("Set SIDKICK-pico DAC mode to mono\n");
        memcpy(skpicobuff, &set_dac_mode_mono8, 3);
        write_chars(skpicobuff, 3);
      }
      if (!strcmp(argv[param_count], "stereo")) {
        printf("Set SIDKICK-pico DAC mode to stereo\n");
        memcpy(skpicobuff, &set_dac_mode_stereo8, 3);
        write_chars(skpicobuff, 3);
      }
    }
  }
exit:;
  return;
}

/* -----USBSID-Pico------ */

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
  printf("  -reset,   --reset-sids        : Reset all SID's\n");
  printf("  --reset-sid-registers         : Reset all SID registers\n");
  printf("  -reboot,  --reboot-usp        : Reboot USBSID-Pico\n");
  printf("  -boot,    --bootloader        : Reboot USBSID-Pico to the bootloader for firmware upload\n");
  printf("  -skpico   --sidkickpico       : Enter SIDKICK-pico config mode\n");
  printf("  -config   --config-command    : Send custom config command\n");
  printf("--[TEST SIDS]--------------------------------------------------------------------------------------------------------\n");
  printf("  -sidtest N                    : Run SID test routine on available SID's\n");
  printf("                                  0: All, 1: 0x00, 2: 0x20, 3: 0x40, 4: 0x60\n");
  printf("  -stoptests                    : Interrupt and stop any running tests\n");
  printf("--[DEFAULTS]---------------------------------------------------------------------------------------------------------\n");
  printf("  -defaults,--config-defaults   : Reset USBSID-Pico config to defaults\n");
  printf("                                  Add optional positional argument `1` to reboot USBSID-Pico afterwards\n");
  printf("--[PRESETS]---------------------------------------------------------------------------------------------------------\n");
  printf("  (add '-q' before any of the preset commands for a quick change and apply the config without saving and rebooting)\n");
  printf("  -single,  --single-sid        : Socket 1 enabled @ single SID, Socket 2 disabled\n");
  printf("  -dual,    --dual-sid          : Socket 1 enabled @ single SID, Socket 2 enabled @ single SID\n");
  printf("  -duals1,  --dual-sid-socket1  : Socket 1 enabled @ dual SID, Socket 2 disabled\n");
  printf("  -duals2,  --dual-sid-socket2  : Socket 1 disabled, Socket 2 enabled @ dual SID\n");
  printf("  -triple1, --triple-sid1       : Socket 1 enabled @ dual SID, Socket 2 enabled @ single SID\n");
  printf("  -triple2, --triple-sid2       : Socket 1 enabled @ single SID, Socket 2 enabled @ dual SID\n");
  printf("  -quad,    --quad-sid          : Socket 1 enabled @ dual SID, Socket 2 enabled @ dual SID\n");
  printf("  -mirrored,--mirrored-sid      : Socket 1&2 enabled @ single SID, each socket receives the same writes\n");
  printf("--[BASICS]----------------------------------------------------------------------------------------------------------\n");
  printf("  -v,       --version           : Read and print USBSID-Pico firmware version\n");
  printf("  -r,       --read-config       : Read and print USBSID-Pico config settings\n");
  printf("  -rc,      --read-clock-speed  : Read and print USBSID-Pico SID clock speed\n");
  printf("  -rs,      --read-sock-config  : Read and print USBSID-Pico socket config settings only\n");
  printf("  -rn,      --read-num-sids     : Read and print USBSID-Pico configured number of SID's only\n");
  printf("  -dsid N,  --detect-sid-types N: Send SID autodetect command to device, returns the config as with '-r' afterwards\n");
  printf("                                  0: Cycle exact detection routine 1 (Real SIDs)\n");
  printf("                                  1: Cycle exact detection routine 2 (Clones & Real SIDs)\n");
  printf("                                  2: Detection routine (Clones)\n");
  printf("                                  3: Unsafe detection routine\n");
  printf("  -dclone,  --detect-clone-types: Send clone autodetect command to device, returns the config as with '-r' afterwards\n");
  printf("  -auto,    ----auto-detect-all : Send run autodetection routine command to device and reboot\n");
  printf("  -w,       --write-config      : Write single config item to USBSID-Pico (will read the full config first!)\n");
  printf("  -a,       --apply-config      : Apply the current config settings (from USBSID-Pico memory) that you changed with '-w'\n");
  printf("  -s,       --save-config       : Send the save config command to USBSID-Pico\n");
  printf("  -sr,      --save-reboot       : Send the save config command to USBSID-Pico and reboot it\n");
  printf("  -rl,      --reload-config     : Reload the config stored in flash, does not return anything\n");
  printf("  -sc N,    --set-clock N       : Set and apply USBSID-Pico SID clock speed\n");
  printf("                                  0: %d, 1: %d, 2: %d, 4: %d\n",
         CLOCK_DEFAULT, CLOCK_PAL, CLOCK_NTSC, CLOCK_DREAN);
  printf("  -lc N,    --lock-clockrate N  : Lock and save the clock rate from changing: True (1) False (0)\n");
  printf("  -tau,     --toggle-audio      : Toggle the mono/stereo audio switch (PCB v1.3+ only!)\n");
  printf("  -sau,     --set-audio N       : Set and save the mono/stereo audio switch (PCB v1.3+ only!)\n");
  printf("                                  0: %s, 1:%s\n", mono_stereo[0], mono_stereo[1]);
  printf("  -lau N,   --lock-audio N      : Lock and the audio switch in it's current state until reboot: True (1) False (0)\n");
  printf("--[INI FILE CONFIGURATION]------------------------------------------------------------------------------------------\n");
  printf("  -default, --default-ini       : Generate an ini file with default USBSID-Pico config named `USBSID-Pico-cfg.ini`\n");
  printf("  -export F,--export-config F   : Read config from USBSID-Pico and export it to provided ini file or default in\n");
  printf("                                  current directory when not provided\n");
  printf("  -import F,--import-config F   : Read config from provided ini file, write to USBSID-Pico, send save command\n");
  printf("                                  and read back config for a visual confirmation\n");
  printf("--[MANUAL CONFIGURATION]--------------------------------------------------------------------------------------------\n");
  printf("  All the following options require '-w'\n");
  printf("  Please do not forget to use '-a' or '-s' after writing config settings manually\n");
  printf("  this is required for the settings to have any effect after a change\n");
  printf("  -c N,     --sid-clock N       : Change SID clock to\n");
  printf("                                  0: %d, 1: %d, 2: %d, 4: %d\n",
         CLOCK_DEFAULT, CLOCK_PAL, CLOCK_NTSC, CLOCK_DREAN);
  printf("  -l N,     --lockclockrate N   : Lock the clock rate from changing: True (1) False (0)\n");
  printf("  -led N,   --led-enabled N     : LED is Enabled (1) or Disabled (0)\n");
  printf("  -lbr N,   --led-breathe N     : LED idle breathing is Enabled (1) or Disabled (0)\n");
  printf("  -rgb N,   --rgb-enabled N     : RGBLED is Enabled (1) or Disabled (0)\n");
  printf("  -rgbbr N  --rgb-breathe N     : RGBLED idle breathing is Enabled (1) or Disabled (0)\n");
  printf("  -rgbsid N,--rgb-sidtouse N    : Set the SID number the RGBLED uses (1, 2, 3 or 4)\n");
  printf("  -br N,    --rgb-brightness N  : Set the RGBLED Brightness to N (0 ~ 255)\n");
  printf("  -fm N,    --fmopl-enabled N   : FMOpl is Enabled (1) or Disabled (0)\n");
  printf("   (Requires a socket set to Clone chip and chiptype to FMOpl)\n");
  printf("  -au ,     --audio-switch N    : Set the mono/stereo audio switch (PCB v1.3+ only!)\n");
  printf("                                  0: %s, 1:%s\n", mono_stereo[0], mono_stereo[1]);
  printf("  -la N,    --lockaudio N       : Lock the audio switch from being changed: True (1) False (0)\n");
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

void config_usbsidpico(int argc, char **argv)
{
  int quickchange = 0;
  for (int param_count = 1; param_count < argc; param_count++) {
    if (!strcmp(argv[param_count], "-h") || !strcmp(argv[param_count], "--help")) {
      print_help();
      break;
    }

    if (!strcmp(argv[param_count], "-v") || !strcmp(argv[param_count], "--version")) {
      printf("Reading firmware version: ");
      read_version(USBSID_VERSION, 1);
      printf("Reading PCB version: ");
      read_version(US_PCB_VERSION, 1);
      break;
    }

    if (!strcmp(argv[param_count], "-debug") || !strcmp(argv[param_count], "--debug")) {
      debug = 1;
      continue;
    }

    if (!strcmp(argv[param_count], "-sidtest")) {
      int testno = 0;
      param_count++;
      if (param_count == argc) {
        printf("No sidnumber given, testing all available SID's!\n");
      } else {
        testno = atoi(argv[param_count++]);
      }
      const char * sidaddr[] = {"0x00","0x20","0x40","0x60"};
      printf("Starting SID test for %s%s\n", (testno == 0 ? "all SID's" : "SID at address: "), (testno == 0 ? " " : sidaddr[testno-1]));
      if (testno == 0)
        write_config_command(0x52,0,0,0,0);
      if (testno == 1)
        write_config_command(0x53,0,0,0,0);
      if (testno == 2)
        write_config_command(0x54,0,0,0,0);
      if (testno == 3)
        write_config_command(0x55,0,0,0,0);
      if (testno == 4)
        write_config_command(0x56,0,0,0,0);
      return;
    }

    if (!strcmp(argv[param_count], "-stoptests")) {
      printf("Sending stop SID test command to USBSID-Pico\n");
      write_config_command(0x59,0,0,0,0);
      return;
    }

    if (!strcmp(argv[param_count], "-defaults") || !strcmp(argv[param_count], "--config-defaults")) {
      printf("Reset USBSID-Pico config to defaults\n");
      if (argc >= param_count) {
        param_count++;
        write_config_command(RESET_CONFIG, atoi(argv[param_count]), 0 ,0 ,0);
      } else {
        write_config_command(RESET_CONFIG, 0, 0 ,0 ,0);
      }
      goto exit;
    }
    if (!strcmp(argv[param_count], "-q")) {
      quickchange = 1;
    };
    if (!strcmp(argv[param_count], "-single") || !strcmp(argv[param_count], "--single-sid")) {
      printf("Set USBSID-Pico config to single SID\n");
      write_config_command(SINGLE_SID, quickchange, 0, 0, 0);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-dual") || !strcmp(argv[param_count], "--dual-sid")) {
      printf("Set USBSID-Pico config to dual SID\n");
      write_config_command(DUAL_SID, quickchange, 0 ,0 ,0);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-duals1") || !strcmp(argv[param_count], "--dual-sid-socket1")) {
      printf("Set USBSID-Pico config to dual SID in socket one\n");
      write_config_command(DUAL_SOCKET1, quickchange, 0 ,0 ,0);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-duals2") || !strcmp(argv[param_count], "--dual-sid-socket2")) {
      printf("Set USBSID-Pico config to dual SID in socket two\n");
      write_config_command(DUAL_SOCKET2, quickchange, 0 ,0 ,0);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-triple1") || !strcmp(argv[param_count], "--triple-sid1")) {
      printf("Set USBSID-Pico config to triple SID socket one\n");
      write_config_command(TRIPLE_SID, quickchange, 0 ,0 ,0);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-triple2") || !strcmp(argv[param_count], "--triple-sid2")) {
      printf("Set USBSID-Pico config to triple SID socket two\n");
      write_config_command(TRIPLE_SID_TWO, quickchange, 0 ,0 ,0);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-quad") || !strcmp(argv[param_count], "--quad-sid")) {
      printf("Set USBSID-Pico config to quad SID\n");
      write_config_command(QUAD_SID, quickchange, 0 ,0 ,0);
      goto exit;
    }
    if (!strcmp(argv[param_count], "-mirrored") || !strcmp(argv[param_count], "--mirrored-sid")) {
      printf("Set USBSID-Pico config to single -> dual mirrored SID\n");
      write_config_command(MIRRORED_SID, quickchange, 0 ,0 ,0);
      goto exit;
    }

    if (!strcmp(argv[param_count], "-r") || !strcmp(argv[param_count], "--read-config")) {
      if (debug == 1) {
        printf("Printing default config\n");
        print_config();
      }
      printf("Reading config\n");
      read_config();
      printf("Printing config\n");
      print_config();
      for (int pc = 1; pc < argc; pc++ ) {
        if (!strcmp(argv[pc], "-d")) print_socket_config();
      }
      break;
    }
    if (!strcmp(argv[param_count], "-rc") || !strcmp(argv[param_count], "--read-clock-speed")) {
      printf("Reading SID clock speed\n");
      read_sid_clockspeed();
      printf("Printing SID clock speed\n");
      print_sid_clockspeed();
      break;
    }
    if (!strcmp(argv[param_count], "-sc") || !strcmp(argv[param_count], "--set-clock")) {
      param_count++;
      int clockrate = atoi(argv[param_count]);
      if(clockrate >= count_of(clockrates)) {
        printf("%d is not a correct clockrate option!\n", clockrate);
        goto exit;
      }
      printf("Set SID clockrate to %d\n", clockrates[clockrate]);
      write_config_command(SET_CLOCK, clockrate, 0x0, 0x0, 0x0);
      break;
    }
    if (!strcmp(argv[param_count], "-lc") || !strcmp(argv[param_count], "--lock-clockrate")) {
      param_count++;
      int lock = atoi(argv[param_count]);
      if(lock > 1) {
        printf("%d is not a correct lock clockrate option!\n", lock);
        goto exit;
      }
      printf("Locking and saving the clockrate from being changed\n");
      write_config_command(LOCK_CLOCK, lock, 0x1, 0x0, 0x0);
      break;
    }
    if (!strcmp(argv[param_count], "-rs") || !strcmp(argv[param_count], "--read-sock-config")) {
      if (debug == 1) {
        printf("Printing default socket config\n");
        print_socket_config();
      }
      printf("Reading socket config\n");
      read_socket_config();
      printf("Printing socket config\n");
      print_socket_config();
      break;
    }
    if (!strcmp(argv[param_count], "-rn") || !strcmp(argv[param_count], "--read-num-sids")) {
      printf("Reading number of SID's\n");
      write_config_command(READ_NUMSIDS, 0x0, 0x0, 0x0, 0x0);
      int len;
      len = read_chars(read_data, count_of(read_data));
      if (debug == 1) printf("Read %d byte of data, byte 0 = %02X\n", len, read_data[0]);
      printf("USBSID-Pico is configured to use %d SID's\n", read_data[0]);
      break;
    }
    if (!strcmp(argv[param_count], "-tau") || !strcmp(argv[param_count], "--toggle-audio")) {
      printf("Toggling mono/stereo audio switch\n");
      write_config_command(TOGGLE_AUDIO, 0x0, 0x0, 0x0, 0x0);
      break;
    }
    if (!strcmp(argv[param_count], "-sau") || !strcmp(argv[param_count], "--set-audio")) {
      param_count++;
      int sw = atoi(argv[param_count]);
      printf("Set mono/stereo audio switch to '%s' and save config\n", mono_stereo[sw]);
      write_config_command(SET_AUDIO, sw, 0x1, 0x0, 0x0);
      break;
    }
    if (!strcmp(argv[param_count], "-lau") || !strcmp(argv[param_count], "--lock-audio")) {
      param_count++;
      int lock = atoi(argv[param_count]);
      if(lock > 1) {
        printf("%d is not a correct lock audio switch option!\n", lock);
        goto exit;
      }
      printf("Lock audio switch from being changed set to '%s'\n", truefalse[lock]);
      write_config_command(LOCK_AUDIO, lock, 0x0, 0x0, 0x0);
      break;
    }
    if (!strcmp(argv[param_count], "-config") || !strcmp(argv[param_count], "--config-command")) {
      printf("-config requires 5 additional config commands and cannot run with any other option!\n");
      if (argc < 6) printf("You supplied %d command line arguments\n", argc);
      param_count++;  /* skip usbsid executable and -config self */
      uint8_t cmd = 0, a = 0, b = 0, c = 0, d = 0;
      if (argc <= 2) {
        printf("Unable to continue without required arguments, exiting...\n");
        exit(0);
      }
      if (argc >= 3)  cmd = strtol(argv[param_count++], NULL, 16);
      if (argc >= 4)  a = strtol(argv[param_count++], NULL, 16);
      if (argc >= 5)  b = strtol(argv[param_count++], NULL, 16);
      if (argc >= 6)  c = strtol(argv[param_count++], NULL, 16);
      if (argc >= 7)  d = strtol(argv[param_count++], NULL, 16);
      printf("Sending: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", cmd, a, b, c, d);
      write_config_command(cmd, a, b, c, d);
      break;
    }
    if (!strcmp(argv[param_count], "-command") || !strcmp(argv[param_count], "--arbitrary-command")) {
      param_count++;  /* skip usbsid executable and -config self */
      uint8_t cmd = 0, a = 0, b = 0, c = 0, d = 0, e = 0;
      if (argc <= 2) {
        printf("Unable to continue without required arguments, exiting...\n");
        exit(0);
      }
      printf("Got %d args: ", argc);
      if (argc >= 3) {
        cmd = strtol(argv[param_count++], NULL, 16);
        printf("%02x ", cmd);
      }
      if (argc >= 4) {
        a = strtol(argv[param_count++], NULL, 16);
        printf("%02x ", a);
      }
      if (argc >= 5) {
        b = strtol(argv[param_count++], NULL, 16);
        printf("%02x ",b);
      }
      if (argc >= 6) {
        c = strtol(argv[param_count++], NULL, 16);
        printf("%02x ", c);
      }
      if (argc >= 7) {
        d = strtol(argv[param_count++], NULL, 16);
        printf("%02x ", d);
      }
      if (argc >= 8) {
        e = strtol(argv[param_count++], NULL, 16);
        printf("%02x ", e);
      }
      printf("\n");

      if (cmd <= 0xF0) {
        printf("Sending: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", cmd, a, b, c, d);
        uint8_t buf[5] = { cmd, a, b, c, d };
        write_chars(buf, count_of(buf));
        if ((cmd & 0xF0) == 0x40 || cmd == 0xC4) {
          int len;
          memset(read_data, 0, count_of(read_data));
          len = read_chars(read_data, count_of(read_data));
          printf("[R %u] %02X\n", len, read_data[0]);
        }
      } else if (cmd == 0xFF) {
        printf("Sending: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", a, b, c, d, e);
        uint8_t buf[5] = { a, b, c, d, e };
        for (int i = 0; i < 50; i++)
          write_chars(buf, count_of(buf));
      } else if (cmd == 0xFE) {
        printf("Sending: 0x%02x: 0x%02x (0x%02x & 0x%02x) * 31\n", 0x80, a, b, c);
        uint8_t temp1[2] = {a, b};
        uint8_t temp2[2] = {a, c};
        uint8_t buf[63];
        buf[0] = (0x0 | 63);
        printf("[BUFFER] ");
        for (int i = 1; i < 63; i+=4) {
          memcpy(buf+i, temp1, 2);
          memcpy(buf+(i+2), temp2, 2);
          printf("$%02X:$%02X $%02X:$%02X ", buf[i], buf[i+1], buf[i+2], buf[i+3]);
        }
        printf("\n");
        write_chars(buf, count_of(buf));
      }

      break;
    }
    if (!strcmp(argv[param_count], "-dsid") || !strcmp(argv[param_count], "--detect-sid-types")) {
      int detection_routine = 0;
      if (argc != param_count+1) {
        param_count++;
        detection_routine = atoi(argv[param_count++]);
      }
      printf("Sending autodetect SID's command with routine '%u' to USBSID-Pico and reading config\n", detection_routine);
      sid_autodetect(detection_routine);
      printf("Printing config\n");
      print_config();
      break;
    }
    if (!strcmp(argv[param_count], "-dclone") || !strcmp(argv[param_count], "--detect-clone-types")) {
      printf("Sending autodetect clone command to USBSID-Pico and reading config\n");
      clone_autodetect();
      printf("Printing config\n");
      print_config();
      break;
    }
    if (!strcmp(argv[param_count], "-auto") || !strcmp(argv[param_count], "--auto-detect-all")) {
      printf("Sending start autodetection and reboot command to USBSID-Pico\n");
      run_autodetection(true);
      /* printf("Printing config\n"); */
      /* print_config(); */
      break;
    }

    if (!strcmp(argv[param_count], "-w") || !strcmp(argv[param_count], "--write-config")) {
      printf("Reading config\n");
      read_config();
      for (int pc = 1; pc < argc; pc++) {

        if (!strcmp(argv[pc], "-c") || !strcmp(argv[pc], "--sid-clock")) {
          pc++;
          int clockrate = atoi(argv[pc]);
          if(clockrate >= count_of(clockrates)) {
            printf("%d is not a correct clockrate option!\n", clockrate);
            goto exit;
          }
          printf("Set SID clockrate from %d to: %d\n", usbsid_config.clock_rate, clockrates[clockrate]);
          usbsid_config.clock_rate = clockrates[clockrate];

          for (int pcl = 1; pcl < argc; pcl++) {
            if (!strcmp(argv[pcl], "-l") || !strcmp(argv[pcl], "--lockclockrate")) {
              pcl++;
              int lock = atoi(argv[pcl]);
              if(lock > 1) {
                printf("%d is not a correct lock clockrate option!\n", lock);
                goto exit;
              }
              usbsid_config.lock_clockrate = pcl;
            }
          }
          write_config_command(SET_CONFIG, 0x0, clockspeed_n(usbsid_config.clock_rate), usbsid_config.lock_clockrate, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-au") || !strcmp(argv[pc], "--audio-switch")) {
          pc++;
          int sw = atoi(argv[pc]);
          printf("Set mono/stereo audio switch from '%s' to '%s'\n", mono_stereo[usbsid_config.stereo_en], mono_stereo[sw]);
          write_config_command(SET_CONFIG, 0xA, sw, 0x0, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-la") || !strcmp(argv[pc], "--lockaudio")) {
          pc++;
          int sw = atoi(argv[pc]);
          printf("Set lock mono/stereo audio switch from '%s' to '%s'\n", truefalse[usbsid_config.lock_audio_sw], truefalse[sw]);
          write_config_command(SET_CONFIG, 0xB, sw, 0x0, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-fm") || !strcmp(argv[pc], "--fmopl-enabled")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= count_of(enabled)) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set FMOpl from %s to: %s\n", enabled[usbsid_config.FMOpl.enabled], enabled[en]);
          usbsid_config.FMOpl.enabled = en;
          write_config_command(SET_CONFIG, 0x9, en, 0x0, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-led") || !strcmp(argv[pc], "--led-enabled")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= count_of(enabled)) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set LED from %s to: %s\n", enabled[usbsid_config.LED.enabled], enabled[en]);
          usbsid_config.LED.enabled = en;
          write_config_command(SET_CONFIG, 0x3, 0x0, en, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-lbr") || !strcmp(argv[pc], "--led-breathe")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= count_of(enabled)) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set LED idle breathing from %s to: %s\n", enabled[usbsid_config.LED.idle_breathe], enabled[en]);
          usbsid_config.LED.idle_breathe = en;
          write_config_command(SET_CONFIG, 0x3, 0x1, en, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-rgb") || !strcmp(argv[pc], "--rgb-enabled")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= count_of(enabled)) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set RGBLED from %s to: %s\n", enabled[usbsid_config.RGBLED.enabled], enabled[en]);
          usbsid_config.RGBLED.enabled = en;
          write_config_command(SET_CONFIG, 0x4, 0x0, en, 0x0);
          continue;
        }
        if (!strcmp(argv[pc], "-rgbbr") || !strcmp(argv[pc], "--rgb-breathe")) {
          pc++;
          int en = atoi(argv[pc]);
          if(en >= count_of(enabled)) {
            printf("%d is not an enable option!\n", en);
            goto exit;
          }
          printf("Set RGBLED idle breathing from %s to: %s\n", enabled[usbsid_config.RGBLED.idle_breathe], enabled[en]);
          usbsid_config.RGBLED.idle_breathe = en;
          write_config_command(SET_CONFIG, 0x4, 0x1, en, 0x0);
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
          write_config_command(SET_CONFIG, 0x4, 0x2, brightness, 0x0);
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
          write_config_command(SET_CONFIG, 0x4, 0x3, sid, 0x0);
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
              if (asone >= count_of(enabled)) {
                printf("%d is not an enable option!\n", asone);
                goto exit;
              }
              printf("Set SocketTwo act-as-one from %s to: %s\n", enabled[usbsid_config.socketTwo.act_as_one], enabled[asone]);
              usbsid_config.socketTwo.act_as_one = asone;
              write_config_command(SET_CONFIG, 0x2, 0x6, usbsid_config.socketTwo.act_as_one, 0);
              continue;
            }

            if (!strcmp(argv[p], "-en") || !strcmp(argv[p], "--enabled")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int en = atoi(argv[p]);
              if (en >= count_of(enabled)) {
                printf("%d is not an enable option!\n", en);
                goto exit;
              }
              printf("Set Socket%s enabled from %s to: %s\n", str, enabled[socket_pointer->enabled], enabled[en]);
              socket_pointer->enabled = en;
              write_config_command(SET_CONFIG, socket, 0x0, socket_pointer->enabled, 0);
              continue;
            }

            if (!strcmp(argv[p], "-d") || !strcmp(argv[p], "--dualsid")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int en = atoi(argv[p]);
              if (en >= count_of(enabled)) {
                printf("%d is not an dualsid option!\n", en);
                goto exit;
              }
              printf("Set Socket%s dualsid from %s to: %s\n", str, enabled[socket_pointer->dualsid], enabled[en]);
              socket_pointer->dualsid = en;
              write_config_command(SET_CONFIG, socket, 0x1, socket_pointer->dualsid, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-chip") || !strcmp(argv[p], "--chiptype")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= count_of(chiptypes)) {
                printf("%d is not an chiptype option!\n", t);
                goto exit;
              }
              printf("Set Socket%s chiptype from %s to: %s\n", str, chiptypes[socket_pointer->chiptype], chiptypes[t]);
              socket_pointer->chiptype = t;
              write_config_command(SET_CONFIG, socket, 0x2, socket_pointer->chiptype, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-clone") || !strcmp(argv[p], "--clonetype")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= count_of(clonetypes)) {
                printf("%d is not an clonetype option!\n", t);
                goto exit;
              }
              printf("Set Socket%s clonetype from %s to: %s\n", str, clonetypes[socket_pointer->clonetype], clonetypes[t]);
              socket_pointer->clonetype = t;
              write_config_command(SET_CONFIG, socket, 0x3, socket_pointer->clonetype, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-sid1") || !strcmp(argv[p], "--sid1type")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= count_of(sidtypes)) {
                printf("%d is not an sid1type option!\n", t);
                goto exit;
              }
              printf("Set Socket%s sid1type from %s to: %s\n", str, sidtypes[socket_pointer->sid1type], sidtypes[t]);
              socket_pointer->sid1type = t;
              write_config_command(SET_CONFIG, socket, 0x4, socket_pointer->sid1type, 0x0);
              continue;
            }

            if (!strcmp(argv[p], "-sid2") || !strcmp(argv[p], "--sid2type")) {
              p++;
              if (argv[p] == NULL) {
                printf ("No argument supplied for option '%s'\n", argv[--p]);
                goto exit;
              }
              int t = atoi(argv[p]);
              if (t >= count_of(sidtypes)) {
                printf("%d is not an sid2type option!\n", t);
                goto exit;
              }
              printf("Set Socket%s sid2type from %s to: %s\n", str, sidtypes[socket_pointer->sid2type], sidtypes[t]);
              socket_pointer->sid2type = t;
              write_config_command(SET_CONFIG, socket, 0x5, socket_pointer->sid2type, 0x0);
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
      /* printf("Sending save config command\n"); */
      /* save_config(0); */ /* Disabled, already done in write_config() */
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
      read_version(USBSID_VERSION, 0);
      // printf("Reading USBSID-Pico PCB version\n");
      // read_version(US_PCB_VERSION, 0); // TODO: Needs ini file implementation
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
    if (!strcmp(argv[param_count], "-a") || !strcmp(argv[param_count], "--apply-config")) {
      printf("Sending apply config command\n");
      apply_config();
      continue;
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
    if (!strcmp(argv[param_count], "-rl") || !strcmp(argv[param_count], "--reload-config")) {
      printf("Sending reload config from flash command\n");
      write_config_command(RELOAD_CONFIG,0x0,0x0,0x0,0x0);
      continue;
    }
    if (!strcmp(argv[param_count], "-reset") || !strcmp(argv[param_count], "--reset-sids")) {
      printf("Sending reset command\n");
      write_command(RESET_SID);
      goto exit;
    }
    if (!strcmp(argv[param_count], "--reset-sid-registers")) {
      printf("Sending reset registers command\n");
      command_buffer[1] = 1;
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
exit:;
};

/* ---------------------- */

int main(int argc, char **argv)
{
  memset(version, 0, 64);
  memset(project_version, 0, 64);
  memset(config, 0, 256);
  memset(read_data, 0, 1);
  memset(read_data_max, 0, 64);
  memset(read_data_uber, 0, 128);
  /* USBSID-Pico */
  if (argc <= 1) {
    printf("Please supply atleast 1 option\n");
    print_help();
    goto exit;
  }
  if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-") || !strcmp(argv[1], "--")) {
    print_help();
    goto exit;
  }
  /* SIDKICK-pico */
  if (!strcmp(argv[1], "-skpico") && (argc <= 2 || !strcmp(argv[2], "-h") || !strcmp(argv[2], "--help") || !strcmp(argv[2], "-") || !strcmp(argv[2], "--"))) {
    print_help_skpico();
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

  if (!strcmp(argv[1], "-skpico") || !strcmp(argv[1], "--sidkickpico")) {

    config_skpico(argc, argv);
    goto exit;
  } else {
    config_usbsidpico(argc, argv);
    goto exit;
  }

exit:;
  if (usid_dev != -1 || usid_dev == 0) {
    usbsid_close();
  }
  return 0;
};

#pragma diagnostic pop
