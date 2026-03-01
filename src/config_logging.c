/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config_logging.c
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


#include "globals.h"
#include "config_constants.h"
#include "config.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* config.c */
extern const char *project_version;
extern const char *pcb_version;
extern Config usbsid_config;
extern RuntimeCFG cfg;


/**
 * @brief Print an array containing a
 *        configuration of supplied length
 *
 * @param uint8_t *buf
 * @param size_t len
 */
void print_cfg(const uint8_t *buf, size_t len, bool newline)
{
  if (newline) usNFO("\n");
  usCFG("Configuration buffer:\n");
  for (size_t i = 0; i < len; ++i) {
    if (i == 0)
      usNFO("[R%03d] ", i);
    usNFO("%02x", buf[i]);
    if (i == (len - 1)) {
      usNFO("\n");
    } else if ((i != 0) && (i % 16 == 15)) {
      usNFO("\n[R%03d] ", i);
    } else {
      usNFO(" ");
    }
  }

  return;
}

/**
 * @brief Prints configuration flash addresses
 *
 */
void print_cfg_addr(void)
{
  usNFO("\n");
  usCFG("USBSID-Pico Memory locations:\n");
  usNFO("  XIP_BASE = 0x%X (%u)\n\
  PICO_FLASH_SIZE_BYTES = 0x%X (%u)\n\
  FLASH_PAGE_SIZE = 0x%X (%u)\n\
  FLASH_SECTOR_SIZE = 0x%X (%u)\n\
  ADDR_PERSISTENT_BASE_ADDR = 0x%X (%u)\n\
  FLASH_PERSISTENT_OFFSET = 0x%X (%u)\n\
  FLASH_PERSISTENT_SIZE = 0x%X (%u)\n\
  FLASH_CONFIG_OFFSET = 0x%X (%u)\n\
  XIP_BASE + FLASH_CONFIG_OFFSET = 0x%X (%u)\n\
  CONFIG_SIZE = 0x%X (%u)\n\
  SIZEOF_CONFIG = 0x%X (%u)\n",
    XIP_BASE, XIP_BASE, PICO_FLASH_SIZE_BYTES, PICO_FLASH_SIZE_BYTES,
    FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE,
    ADDR_PERSISTENT_BASE_ADDR, ADDR_PERSISTENT_BASE_ADDR,
    FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_SIZE, FLASH_PERSISTENT_SIZE,
    FLASH_CONFIG_OFFSET, FLASH_CONFIG_OFFSET,
    (XIP_BASE + FLASH_CONFIG_OFFSET), (XIP_BASE + FLASH_CONFIG_OFFSET),
    CONFIG_SIZE, CONFIG_SIZE, sizeof(Config), sizeof(Config));
  return;
}

/**
 * @brief Prints Pico or Pico2 features
 *
 */
void print_pico_features(void)
{
  usNFO("\n");  /* new line before the actual features are printed */
  usCFG("RaspberryPi Pico features:\n");

  usCFG("  PIO version = %d\n", PICO_PIO_VERSION);  /* pio.h */
  #if defined(PICO_DEFAULT_LED_PIN)
  usCFG("  Default LED pin = %d\n", PICO_DEFAULT_LED_PIN);  /* pico*.h */
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  usCFG("  WiFi LED pin = %d\n", CYW43_WL_GPIO_LED_PIN);  /* pico*.h */
  #endif
  usCFG("  PWM on LED = %d\n", LED_PWM);  /* config.h */

  return;
}

/**
 * @brief Prints a formatted overview of the active configuration
 *
 */
void print_config_overview(void)
{
  usNFO("\n");  /* new line before the actual overview is printed */
  usCFG("Config Overview:\n");
  usCFG("  PCB version = v%s\n", pcb_version);
  usCFG("  Firmware version = v%s\n", project_version);
  usCFG("\n");
  usCFG("  %s C64 clockrate = %d\n",
    in_ext_str((int)usbsid_config.external_clock), (int)usbsid_config.clock_rate);
  usCFG("  Clock rate = %s\n",
    locked_str((int)usbsid_config.lock_clockrate));
  usCFG("  Raster rate = %d\n",
    (int)usbsid_config.raster_rate);
  usCFG("\n");
  usCFG("  Last applied preset is: %s\n",
    preset_name((int)usbsid_config.last_preset));
  usCFG("\n");
  usCFG("  Socket One is %s as %s\n",
    switch_str((int)usbsid_config.socketOne.enabled),
    dualsingle_str((int)usbsid_config.socketOne.dualsid));
  usCFG("  Chip is %s\n",
    chip_type_name((int)usbsid_config.socketOne.chiptype));
  if (usbsid_config.socketOne.enabled)
    usCFG("      %s as SID1 @ addr $%02x with id %d\n",
      sid_type_name((int)usbsid_config.socketOne.sid1.type),
      usbsid_config.socketOne.sid1.addr,
      usbsid_config.socketOne.sid1.id);
  if (usbsid_config.socketOne.enabled && usbsid_config.socketOne.dualsid)
    usCFG("      %s as SID2 @ addr $%02x with id %d\n",
      sid_type_name((int)usbsid_config.socketOne.sid2.type),
      usbsid_config.socketOne.sid2.addr,
      usbsid_config.socketOne.sid2.id);
  usCFG("  Socket Two is %s as %s\n",
    switch_str((int)usbsid_config.socketTwo.enabled),
    dualsingle_str((int)usbsid_config.socketTwo.dualsid));
  usCFG("  Chip is %s\n",
    chip_type_name((int)usbsid_config.socketTwo.chiptype));
  if (usbsid_config.socketTwo.enabled)
    usCFG("      %s as SID1 @ addr $%02x with id %d\n",
      sid_type_name((int)usbsid_config.socketTwo.sid1.type),
      usbsid_config.socketTwo.sid1.addr,
      usbsid_config.socketTwo.sid1.id);
  if (usbsid_config.socketTwo.enabled && usbsid_config.socketTwo.dualsid)
    usCFG("      %s as SID2 @ addr $%02x with id %d\n",
      sid_type_name((int)usbsid_config.socketTwo.sid2.type),
      usbsid_config.socketTwo.sid2.addr,
      usbsid_config.socketTwo.sid2.id);
  usCFG("  Mirror Socket Two to Socket One = %s\n", switch_str(usbsid_config.mirrored));
  usCFG("\n");
  usCFG("  FMOpl is %s\n",
    switch_str((int)usbsid_config.FMOpl.enabled));
  if (usbsid_config.FMOpl.enabled)
    usCFG("  FMOpl available @ SID %d\n",
      (int)usbsid_config.FMOpl.sidno);
  usCFG("\n");
#if defined(HAS_AUDIOSWITCH)
  usCFG("  Audio switch is set to %s\n",
    monostereo_str((int)usbsid_config.stereo_en));
  usCFG("  Audio switch position = %s\n",
    locked_str((int)usbsid_config.lock_audio_sw));
#endif
  usCFG("\n");
  usCFG("  Pico LED is %s, Breathing effect when idle? %s\n",
    switch_str((int)usbsid_config.LED.enabled),
    boolean_str((int)usbsid_config.LED.idle_breathe));
#ifdef USE_RGB
  usCFG("  Pico RGB LED is %s, Breathing effect when idle? %s\n",
    switch_str((int)usbsid_config.RGBLED.enabled),
    boolean_str((int)usbsid_config.RGBLED.idle_breathe));
  usCFG("  RGB LED uses SID %d\n",
    (int)usbsid_config.RGBLED.sid_to_use);
  usCFG("  RGB LED brightness = %d\n",
    (int)usbsid_config.RGBLED.brightness);
#else
  usCFG("  Pico RGB LED not supported\n");
#endif
  usCFG("\n");
  usCFG("  CDC feature = %s\n",
    switch_str((int)usbsid_config.Cdc.enabled));
  usCFG("  WebUSB feature = %s\n",
    switch_str((int)usbsid_config.WebUSB.enabled));
  usCFG("  Midi feature =  %s\n",
    switch_str((int)usbsid_config.Midi.enabled));
  usCFG("  ASID feature = %s\n",
    switch_str((int)usbsid_config.Asid.enabled));

  return;
}

/**
 * @brief Prints a summary of the active socket configuration
 *
 */
void print_config_summary(void)
{
  usNFO("\n");  /* new line before the actual summary is printed */
  usCFG("Config Summary:\n");
  usCFG("  S1: en=%d dual=%d chip=%d\n",
    usbsid_config.socketOne.enabled, usbsid_config.socketOne.dualsid, usbsid_config.socketOne.chiptype);
  usCFG("      SID1: id=%d addr=0x%02x type=%d\n",
    usbsid_config.socketOne.sid1.id, usbsid_config.socketOne.sid1.addr, usbsid_config.socketOne.sid1.type);
  usCFG("      SID2: id=%d addr=0x%02x type=%d\n",
    usbsid_config.socketOne.sid2.id, usbsid_config.socketOne.sid2.addr, usbsid_config.socketOne.sid2.type);
  usCFG("  S2: en=%d dual=%d chip=%d\n",
    usbsid_config.socketTwo.enabled, usbsid_config.socketTwo.dualsid, usbsid_config.socketTwo.chiptype);
  usCFG("      SID1: id=%d addr=0x%02x type=%d\n",
    usbsid_config.socketTwo.sid1.id, usbsid_config.socketTwo.sid1.addr, usbsid_config.socketTwo.sid1.type);
  usCFG("      SID2: id=%d addr=0x%02x type=%d\n",
    usbsid_config.socketTwo.sid2.id, usbsid_config.socketTwo.sid2.addr, usbsid_config.socketTwo.sid2.type);
  usCFG("  Mirrored: %d\n", usbsid_config.mirrored);

  return;
}

/**
 * @brief Prints a summary of the active runtime configuration
 *
 */
void print_runtime_summary(void)
{
  usNFO("\n");  /* new line before the actual summary is printed */
  usCFG("Runtime Summary:\n");
  usCFG("  Sockets: s1=%d s2=%d | s1_dual=%d s2_dual=%d | mirrored=%d\n",
    cfg.sock_one, cfg.sock_two, cfg.sock_one_dual, cfg.sock_two_dual, cfg.mirrored);
  usCFG("  SID's:   s1=%d s2=%d total=%d\n",
    cfg.sids_one, cfg.sids_two, cfg.numsids);
  usCFG("  Chips:   s1=%d s2=%d | FMOpl: en=%d sid=%d\n",
    cfg.chip_one, cfg.chip_two, cfg.fmopl_enabled, cfg.fmopl_sid);
  usCFG("  IDs:     [0,1,2,3] -> slots [%d,%d,%d,%d]\n",
    cfg.ids[0], cfg.ids[1], cfg.ids[2], cfg.ids[3]);
  usCFG("  Addr:    [0x%02x,0x%02x,0x%02x,0x%02x]\n",
    cfg.sidaddr[0], cfg.sidaddr[1], cfg.sidaddr[2], cfg.sidaddr[3]);
  usCFG("  Type:    [%d,%d,%d,%d]\n",
    cfg.sidtype[0], cfg.sidtype[1], cfg.sidtype[2], cfg.sidtype[3]);
  usCFG("  Bus:     one=0b%03b/0x%02x   two=0b%03b/0x%02x\n",
    cfg.one, cfg.one_mask, cfg.two, cfg.two_mask);
  usCFG("           three=0b%03b/0x%02x four=0b%03b/0x%02x\n",
    cfg.three, cfg.three_mask, cfg.four, cfg.four_mask);

  return;
}
