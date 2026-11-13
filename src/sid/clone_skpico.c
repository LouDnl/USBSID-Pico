/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * clone_skpico.c
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

#include <globals.h>
#include <usbsid_constants.h>
#include <logging.h>
#include <bus.h>
#include <config.h>
#include <config_logging.h>
#include <clone_skpico_defs.h>


/* Init local variables */
static uint8_t skpico_config[64] = {0xff};
static uint8_t skpico_version_result[32] = {0xff};
static char skpico_version[32] = {0};
static int skpico_v = 0;
static bool is_u64fw = false;

static const char __in_flash("us_vars_static") * error_type[1] = { "ERROR" };
static volatile bool sid_logging = true, sid_debug = true;


/**
 * @brief Parses and prints supplied SIDKick-pico configuration array
 *
 * @param bool is_u64fw
 * @param uint8_t *configarray
 */
static void print_skpico_configuration(bool is_u64fw, uint8_t * configarray)
{
  usNFO("\n");
  usSID("SIDKICK-pico configuration:\n");
  for (size_t i = 0; i < 64; i++) {
    if (i >= 4 && i <= 7) continue;
    if (i >= 13 && i <= 56) continue;
    if (i == 62 || i == 63) continue;
    if (i == 0 || i == 8) {
      usSID("  [%02ld] %s: %02X ~ %s\n",
        i, config_names[i], configarray[i],
        (configarray[i] < s_sid_types) ? (char*)sid_types[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    if (i == 10 && !is_u64fw) {
      usSID("  [%02ld] %s: %02X ~ %s\n",
        i, config_names[i], configarray[i],
        (configarray[i] < s_sid2_address) ? (char*)sid2_address[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    if ((i == 10 || i == 11) && is_u64fw) {
      usSID("  [%02ld] %s: %02X ~ Ultimate64\n",
        i, u64_names[i-10], configarray[i]);
      continue;
    }
    if (i == 59) {
      usSID("  [%02ld] %s: %02X ~ %s\n",
        i, config_names[i], configarray[i],
        (configarray[i] < s_clock_speed) ? (char*)clock_speed[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    usSID("  [%02ld] %s: %02X\n", i, config_names[i], configarray[i]);
  }

  return;
}

/**
 * @brief Read SIDKick-pico version at supplied address
 *
 * @param uint8_t base_address
 * @return true  is Ultimate64 firmware
 * @return false is regular firmware
 */
bool read_skpico_version(uint8_t base_address)
{
  if (sid_logging) {
    usNFO("\n");
    usSID("Reading SIDKick-pico version @ $%02x\n", base_address);
  }

  const char skpico_correction = 0x60;  /* { 0x70, 0x69, 0x63, 0x6F } */

  /* Extend config mode */
  sleep_us(10);
  cycled_write_operation((config_extend[0] + base_address), config_extend[1], 6);

  for (int i = 0; i < 32; i++) {
    cycled_write_operation((0x1e + base_address), (224 + i), 6);
    sleep_us(10);
    skpico_version_result[i] = cycled_read_operation((0x1d + base_address), 4);
    if (i >= 2 && i <= 5) skpico_version_result[i] |= skpico_correction;
  }
  skpico_v = (((skpico_version_result[8] - '0') * 10) + (skpico_version_result[9] - '0'));
  bool is_u64fw = ((skpico_version_result[11] == 0x55) ? true : false);

  /* Extend config mode */
  sleep_us(10);
  cycled_write_operation((config_extend[0] + base_address), config_extend[1], 6);

  /* Finalize version string */
  memcpy(&skpico_version[0], &skpico_version_result, 32);
  if (sid_logging) {
    usSID("  SIDKick-pico version: %.36s (skpico_v: %d is_u64fw: %s)\n",
      skpico_version, skpico_v, (is_u64fw ? "true" : "false"));
  }

  /* Clear used arrays after use */
  memset(skpico_version_result, 0xff, 32);
  memset(skpico_version, 0, 32);

  return is_u64fw;
}

static void init_skpico_configmode(uint8_t base_address)
{
  /* Enter config mode */
  cycled_write_operation((init_configmode[0] + base_address), init_configmode[1], 6);

  return;
}

static void exit_skpico_configmode(uint8_t base_address)
{
  /* Exit config mode */
  cycled_write_operation((config_exit[0] + base_address), config_exit[1], 6);

  return;
}

static void extend_skpico_configmode(uint8_t base_address)
{
  /* Extend config mode */
  cycled_write_operation((config_extend[0] + base_address), config_extend[1], 6);

  return;
}

static void update_skpico_config(uint8_t base_address)
{
  /* Update config */
  cycled_write_operation((config_update[0] + base_address), config_update[1], 6);

  return;
}

static void save_skpico_config(uint8_t base_address)
{
  /* Write config */
  cycled_write_operation((config_writeupdate[0] + base_address), config_writeupdate[1], 6);

  return;
}

static void select_skpico_configprofile(uint8_t base_address, uint8_t profile)
{
  /* Select config profile */
    if (skpico_v >= 22) {
    cycled_write_operation((select_profile[0] + base_address), profile, 6);
  } else {
    cycled_write_operation((select_profile[0] + base_address), 0, 6);
  }

  return;
}

static void read_skpico_profile_config(uint8_t base_address, uint8_t profile)
{
  /* No init here */
  select_skpico_configprofile(base_address, profile);
  /* Read config from SKPico */
  for (int i = 0; i <= 63; ++i) {
    /* sleep_us(10); */
    skpico_config[i] = cycled_read_operation((0x1d + base_address), 4);
  }

  return;
}

static void write_skpico_profile_config(uint8_t base_address, uint8_t profile)
{
  /* No init here */
  select_skpico_configprofile(base_address, profile);

  /* Write config to SKPico */
  for (int i = 0; i <= 63; ++i) {
    /* sleep_us(10); */
    cycled_write_operation((config_write[0] + base_address), skpico_config[i], 6);
  }

  return;
}

void read_skpico_configuration_noexit(uint8_t base_address, uint8_t profile)
{
  /* Clear config buffer */
  memset(skpico_config, 0xff, 64);

  /* Enter config mode */
  init_skpico_configmode(base_address);

  is_u64fw = read_skpico_version(base_address);

  /* Extend config mode */
  // sleep_us(10);
  extend_skpico_configmode(base_address);

  /* Select config profile */
  // sleep_us(10);
  // select_skpico_configprofile(base_address, profile);

  if (sid_logging) {
    usSID("\n");
    usSID("Reading SIDKick-pico configuration @ $%02x\n", base_address);
  }

  /* Extend config mode */
  // sleep_us(10);
  extend_skpico_configmode(base_address);

  /* Read config from SKPico */
  read_skpico_profile_config(base_address, profile);

  return;
}

/**
 * @brief Read configuration from a SIDKick-pico at supplied address
 *
 * @param uint8_t base_address
 * @param uint8_t profile
 */
void read_skpico_configuration(uint8_t base_address, uint8_t profile)
{
  is_u64fw = false;

  read_skpico_configuration_noexit(base_address, profile);

  /* Exit config mode */
  sleep_us(10);
  exit_skpico_configmode(base_address);

  if (sid_debug) {
    print_cfg(skpico_config, 64, false);
  }

  print_skpico_configuration(is_u64fw, skpico_config);

  return;
}

/**
 * @brief Set clockspeed for a SIDKick-pico at supplied address
 *        to match USBSID-Pico clockspeed
 *
 * @param uint8_t base_address
 */
void set_skpico_clockspeed(uint8_t base_address)
{
  uint8_t us_clkrate = return_clockrate();
  uint8_t skpico_clkrate = 0; /* PAL default */
  switch (us_clkrate) {
    case 1: /* PAL */
      skpico_clkrate = 0;
      break;
    case 2: /* NTSC */
      skpico_clkrate = 1;
      break;
    case 3 ... 4: /* DREAN & NTSC2 */
      skpico_clkrate = 2;
      break;
    case 0: /* 1MHz -> fallback to PAL */
    default: /* fallthrough */
      skpico_clkrate = 0;
      break;
  }
  skpico_config[59] = skpico_clkrate;
}

void auto_update_skpico_clock(uint8_t base_address, uint8_t profile)
{
  is_u64fw = false;
  sid_logging = false;
  sid_debug = false;

  usSID("Set SIDKICK-pico clockspeed to match C64 clock\n");

  read_skpico_configuration_noexit(base_address, profile);

  extend_skpico_configmode(base_address);

  // print_cfg(skpico_config, 64, false);

  /* Exit config mode */
  // sleep_us(10);
  // exit_skpico_configmode(base_address);

  /* Enter config mode */
  // init_skpico_configmode(base_address);

  /* Select config profile */
  // sleep_us(10);
  // select_skpico_configprofile(base_address, profile);

  /* Default & and fallback */
  if (skpico_config[2] != 1) skpico_config[2] =   1;  /* Re-enable register read */
  if (is_u64fw) skpico_config[11] =  14;  /* fmopl volume default */
  // skpico_config[12] =  7;  /* panning in the middle */
  // skpico_config[58] =  7;  /* balance in the middle */
  // skpico_config[61] =  1;  /* digidetect always on */
  // if (skpico_config[59] > 2) skpico_config[59] = 0;  /* reset clockspeed to PAL if incorrect */

  // sleep_us(10);
  extend_skpico_configmode(base_address);

  set_skpico_clockspeed(base_address);

  // sleep_us(10);
  extend_skpico_configmode(base_address);

  // sleep_us(10);
  write_skpico_profile_config(base_address, profile);

  // sleep_us(10);
  save_skpico_config(base_address);

  // sleep_us(10);
  // extend_skpico_configmode(base_address);

  // /* Select config profile */
  // sleep_us(10);
  // select_skpico_configprofile(base_address, profile);

  // sleep_us(10);
  // extend_skpico_configmode(base_address);

  /* Clear config buffer */
  // memset(skpico_config, 0xff, 64);
  // sleep_us(10);
  exit_skpico_configmode(base_address);

  /* Read config from SKPico */
  // sleep_us(10);
  // read_skpico_configuration_noexit(base_address, profile);
  // print_cfg(skpico_config, 64, false);

  /* Exit config mode */
  // sleep_us(10);
  // exit_skpico_configmode(base_address);

  sid_logging = true;
  sid_debug = true;

  // clockcycle_delay(usbsid_config.raster_rate);
  // read_skpico_configuration(base_address, profile);

  return;
}
