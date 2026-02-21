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
#include "config.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* config.c */
extern Config usbsid_config;
extern RuntimeCFG cfg;
extern char *project_version;
extern char *pcb_version;

/* String constants for logging */
const char __in_flash("usbsid_vars") *sidtypes[5] = { "UNKNOWN", "N/A", "MOS8580", "MOS6581", "FMOpl" };
const char __in_flash("usbsid_vars") *chiptypes[2] = { "Real", "Clone" };
const char __in_flash("usbsid_vars") *clonetypes[8] = { "Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID", "PDsid", "SwinSID" };
const char __in_flash("usbsid_vars") *int_ext[2] = { "Internal", "External" };
const char __in_flash("usbsid_vars") *enabled[2] = { "Disabled", "Enabled" };
const char __in_flash("usbsid_vars") *true_false[2] = { "False", "True" };
const char __in_flash("usbsid_vars") *single_dual[2] = { "Dual SID", "Single SID" };
const char __in_flash("usbsid_vars") *mono_stereo[2] = { "Mono", "Stereo" };


void print_cfg(const uint8_t *buf, size_t len)
{
  usNFO("\n");
  usCFG("[PRINT CFG START]\n");
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
  usCFG("[PRINT CFG END]\n");

  return;
}

void print_cfg_addr(void)
{
  usNFO("\n");
  usCFG("[START PRINT CONFIG MEMORY LOCATIONS]\n");
  usNFO("[XIP_BASE] 0x%X (%u)\n\
[PICO_FLASH_SIZE_BYTES] 0x%X (%u)\n\
[FLASH_PAGE_SIZE] 0x%X (%u)\n\
[FLASH_SECTOR_SIZE] 0x%X (%u)\n\
[ADDR_PERSISTENT_BASE_ADDR] 0x%X (%u)\n\
[FLASH_PERSISTENT_OFFSET] 0x%X (%u)\n\
[FLASH_PERSISTENT_SIZE] 0x%X (%u)\n\
[FLASH_CONFIG_OFFSET] 0x%X (%u)\n\
[XIP_BASE + FLASH_CONFIG_OFFSET] 0x%X (%u)\n\
[CONFIG_SIZE] 0x%X (%u)\n\
[SIZEOF_CONFIG] 0x%X (%u)\n",
    XIP_BASE, XIP_BASE, PICO_FLASH_SIZE_BYTES, PICO_FLASH_SIZE_BYTES,
    FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE,
    ADDR_PERSISTENT_BASE_ADDR, ADDR_PERSISTENT_BASE_ADDR,
    FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_SIZE, FLASH_PERSISTENT_SIZE,
    FLASH_CONFIG_OFFSET, FLASH_CONFIG_OFFSET,
    (XIP_BASE + FLASH_CONFIG_OFFSET), (XIP_BASE + FLASH_CONFIG_OFFSET),
    CONFIG_SIZE, CONFIG_SIZE, sizeof(Config), sizeof(Config));
  usCFG("[END PRINT CONFIG MEMORY LOCATIONS]\n");
  return;
}

void print_pico_features(void)
{
  usNFO("\n");
  usCFG("[START PRINT PICO DEFAULT FEATURES]\n");

  usNFO("PICO_PIO_VERSION = %d\n", PICO_PIO_VERSION);  /* pio.h */
  #if defined(PICO_DEFAULT_LED_PIN)
  usNFO("PICO_DEFAULT_LED_PIN = %d\n", PICO_DEFAULT_LED_PIN);  /* pico*.h */
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  usNFO("CYW43_WL_GPIO_LED_PIN = %d\n", CYW43_WL_GPIO_LED_PIN);  /* pico*.h */
  #endif
  usNFO("LED_PWM = %d\n", LED_PWM);  /* config.h */

  usCFG("[END PRINT PICO DEFAULT FEATURES]\n");
  return;
}

void print_config_settings(void)
{
  usNFO("\n");  /* new line before the actual settings are printed */
  usCFG("[START PRINT CONFIG SETTINGS]\n");

  usNFO("[USBSID PCB VERSION] %s\n", pcb_version);
  usNFO("[USBSID FIRMWARE VERSION] %s\n", project_version);

  usNFO("[CLOCK] %s @%d\n",
    int_ext[(int)usbsid_config.external_clock],
    (int)usbsid_config.clock_rate);
  usNFO("[CLOCK RATE LOCKED] %s\n",
    true_false[(int)usbsid_config.lock_clockrate]);
  usNFO("[RASTER RATE] @%d\n",
    (int)usbsid_config.raster_rate);

  usNFO("[SOCKET ONE] %s as %s\n",
    enabled[(int)usbsid_config.socketOne.enabled],
    ((int)usbsid_config.socketOne.dualsid == 1 ? single_dual[0] : single_dual[1]));
  usNFO("[SOCKET ONE] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketOne.chiptype],
    clonetypes[usbsid_config.socketOne.clonetype]);
  usNFO("[SOCKET ONE] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketOne.sid1.type],
    sidtypes[usbsid_config.socketOne.sid2.type]);
  usNFO("[SOCKET ONE] SID 1 ID: %d, SID 2 ID: %d\n",
    usbsid_config.socketOne.sid1.id,
    usbsid_config.socketOne.sid2.id);
  usNFO("[SOCKET ONE] SID 1 ADDR: 0x%02X, SID 2 ADDR: 0x%02X\n",
    usbsid_config.socketOne.sid1.addr,
    usbsid_config.socketOne.sid2.addr);
  usNFO("[SOCKET TWO] %s as %s\n",
    enabled[(int)usbsid_config.socketTwo.enabled],
    ((int)usbsid_config.socketTwo.dualsid == 1 ? single_dual[0] : single_dual[1]));
  usNFO("[SOCKET TWO] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketTwo.chiptype],
    clonetypes[usbsid_config.socketTwo.clonetype]);
  usNFO("[SOCKET TWO] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketTwo.sid1.type],
    sidtypes[usbsid_config.socketTwo.sid2.type]);
  usNFO("[SOCKET ONE] SID 1 ID: %d, SID 2 ID: %d\n",
    usbsid_config.socketTwo.sid1.id,
    usbsid_config.socketTwo.sid2.id);
  usNFO("[SOCKET ONE] SID 1 ADDR: 0x%02X, SID 2 ADDR: 0x%02X\n",
    usbsid_config.socketTwo.sid1.addr,
    usbsid_config.socketTwo.sid2.addr);
  usNFO("[SOCKET TWO AS ONE (MIRRORED)] %s\n",
    true_false[(int)usbsid_config.mirrored]);

  usNFO("[LED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.LED.enabled],
    true_false[(int)usbsid_config.LED.idle_breathe]);
  usNFO("[RGBLED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.RGBLED.enabled],
    true_false[(int)usbsid_config.RGBLED.idle_breathe]);
  usNFO("[RGBLED SIDTOUSE] %d\n",
    (int)usbsid_config.RGBLED.sid_to_use);
  usNFO("[RGBLED BRIGHTNESS] %d\n",
    (int)usbsid_config.RGBLED.brightness);

  usNFO("[CDC] %s\n",
    enabled[(int)usbsid_config.Cdc.enabled]);
  usNFO("[WebUSB] %s\n",
    enabled[(int)usbsid_config.WebUSB.enabled]);
  usNFO("[Asid] %s\n",
    enabled[(int)usbsid_config.Asid.enabled]);
  usNFO("[Midi] %s\n",
    enabled[(int)usbsid_config.Midi.enabled]);

  usNFO("[FMOpl] %s\n",
    enabled[(int)usbsid_config.FMOpl.enabled]);
  usNFO("[FMOpl] SIDno %d\n",
    usbsid_config.FMOpl.sidno);

  #if defined(HAS_AUDIOSWITCH)
  usNFO("[AUDIO_SWITCH] %s\n",
    mono_stereo[(int)usbsid_config.stereo_en]);
  usNFO("[AUDIO_SWITCH_LOCKED] %s\n",
    true_false[(int)usbsid_config.lock_audio_sw]);
  #endif

  usCFG("[END PRINT CONFIG SETTINGS]\n");
  return;
}

void print_socket_config(void)
{
  usNFO("\n");  /* new line before the actual socket settings are printed */
  usCFG("[START PRINT SOCKET CONFIG]\n");

  usNFO("SOCK_ONE EN: %s SOCK_TWO EN: %s\nACT_AS_ONE: %s\nNO SIDS\nSOCK_ONE #%d\nSOCK_TWO #%d\nTOTAL #%d\n",
    true_false[cfg.sock_one], true_false[cfg.sock_two], true_false[cfg.mirrored],
    cfg.sids_one, cfg.sids_two, cfg.numsids);

  usCFG("[END PRINT SOCKET CONFIG]\n");
  return;
}

void print_bus_config(void)
{
  usNFO("\n");  /* new line before the actual bus config is printed */
  usCFG("[START PRINT BUS CONFIG]\n");

  usNFO("BUS\nONE:   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\nTWO:   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\nTHREE: %02x 0b"PRINTF_BINARY_PATTERN_INT8"\nFOUR:  %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n",
    cfg.one, PRINTF_BYTE_TO_BINARY_INT8(cfg.one),
    cfg.two, PRINTF_BYTE_TO_BINARY_INT8(cfg.two),
    cfg.three, PRINTF_BYTE_TO_BINARY_INT8(cfg.three),
    cfg.four, PRINTF_BYTE_TO_BINARY_INT8(cfg.four));
  usNFO("ADDRESS MASKS\n");
  usNFO("MASK_ONE:   0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.one_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.one_mask));
  usNFO("MASK_TWO:   0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.two_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.two_mask));
  usNFO("MASK_THREE: 0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.three_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.three_mask));
  usNFO("MASK_FOUR:  0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.four_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.four_mask));

  usCFG("[END PRINT BUS CONFIG]\n");
  return;
}
