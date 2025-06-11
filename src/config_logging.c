/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config_logging.c
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


#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "globals.h"
#include "config.h"
#include "gpio.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* Config */
extern Config usbsid_config;
extern RuntimeCFG cfg;
extern char *project_version;
extern char *pcb_version;

/* String constants for logging */
const char *sidtypes[5] = { "UNKNOWN", "N/A", "MOS8580", "MOS6581", "FMOpl" };
const char *chiptypes[2] = { "Real", "Clone" };
const char *clonetypes[6] = { "Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID" };
const char *int_ext[2] = { "Internal", "External" };
const char *enabled[2] = { "Disabled", "Enabled" };
const char *true_false[2] = { "False", "True" };
const char *single_dual[2] = { "Dual SID", "Single SID" };
const char *mono_stereo[2] = { "Mono", "Stereo" };


void print_cfg(const uint8_t *buf, size_t len)
{
  CFG("[PRINT CFG START]\n");
  for (size_t i = 0; i < len; ++i) {
    if (i == 0)
      CFG("[R%03d] ", i);
    CFG("%02x", buf[i]);
    if (i == (len - 1)) {
      CFG("\n");
    } else if ((i != 0) && (i % 16 == 15)) {
      CFG("\n[R%03d] ", i);
    } else {
      CFG(" ");
    }
  }
  CFG("[PRINT CFG END]\n");

  return;
}

void print_cfg_addr(void)
{
  CFG("\n");
  CFG("[CONFIG] [XIP_BASE]0x%X (%u)\n\
[CONFIG] [PICO_FLASH_SIZE_BYTES]0x%X (%u)\n\
[CONFIG] [FLASH_PAGE_SIZE]0x%X (%u)\n\
[CONFIG] [FLASH_SECTOR_SIZE]0x%X (%u)\n\
[CONFIG] [ADDR_PERSISTENT_BASE_ADDR]0x%X (%u)\n\
[CONFIG] [FLASH_PERSISTENT_OFFSET]0x%X (%u)\n\
[CONFIG] [FLASH_PERSISTENT_SIZE]0x%X (%u)\n\
[CONFIG] [FLASH_CONFIG_OFFSET]0x%X (%u)\n\
[CONFIG] [XIP_BASE + FLASH_CONFIG_OFFSET]0x%X (%u)\n\
[CONFIG] [CONFIG_SIZE]0x%X (%u)\n\
[CONFIG] [SIZEOF_CONFIG]0x%X (%u)\n",
    XIP_BASE, XIP_BASE, PICO_FLASH_SIZE_BYTES, PICO_FLASH_SIZE_BYTES,
    FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE,
    ADDR_PERSISTENT_BASE_ADDR, ADDR_PERSISTENT_BASE_ADDR,
    FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_SIZE, FLASH_PERSISTENT_SIZE,
    FLASH_CONFIG_OFFSET, FLASH_CONFIG_OFFSET,
    (XIP_BASE + FLASH_CONFIG_OFFSET), (XIP_BASE + FLASH_CONFIG_OFFSET),
    CONFIG_SIZE, CONFIG_SIZE, sizeof(Config), sizeof(Config));
  return;
}

void print_pico_features(void)
{
  CFG("\n");  /* new line before the actual pico features are printed */
  CFG("[START PRINT PICO DEFAULT FEATURES]\n");

  CFG("PICO_PIO_VERSION = %d\n", PICO_PIO_VERSION);  /* pio.h */
  #if defined(PICO_DEFAULT_LED_PIN)
  CFG("PICO_DEFAULT_LED_PIN = %d\n", PICO_DEFAULT_LED_PIN);  /* pico*.h */
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  CFG("CYW43_WL_GPIO_LED_PIN = %d\n", CYW43_WL_GPIO_LED_PIN);  /* pico*.h */
  #endif
  CFG("LED_PWM = %d\n", LED_PWM);  /* config.h */

  CFG("[END PRINT PICO DEFAULT FEATURES]\n");
  return;
}

void print_config_settings(void)
{
  CFG("\n");  /* new line before the actual settings are printed */
  CFG("[START PRINT CONFIG SETTINGS]\n");

  CFG("[USBSID PCB VERSION] %s\n", pcb_version);
  CFG("[USBSID FIRMWARE VERSION] %s\n", project_version);

  CFG("[CLOCK] %s @%d\n",
    int_ext[(int)usbsid_config.external_clock],
    (int)usbsid_config.clock_rate);
  CFG("[CLOCK RATE LOCKED] %s\n",
    true_false[(int)usbsid_config.lock_clockrate]);
  CFG("[RASTER RATE] @%d\n",
    (int)usbsid_config.raster_rate);

  CFG("[SOCKET ONE] %s as %s\n",
    enabled[(int)usbsid_config.socketOne.enabled],
    ((int)usbsid_config.socketOne.dualsid == 1 ? single_dual[0] : single_dual[1]));
  CFG("[SOCKET ONE] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketOne.chiptype],
    clonetypes[usbsid_config.socketOne.clonetype]);
  CFG("[SOCKET ONE] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketOne.sid1.type],
    sidtypes[usbsid_config.socketOne.sid2.type]);
  CFG("[SOCKET ONE] SID 1 ID: %d, SID 2 ID: %d\n",
    usbsid_config.socketOne.sid1.id,
    usbsid_config.socketOne.sid2.id);
  CFG("[SOCKET ONE] SID 1 ADDR: 0x%02X, SID 2 ADDR: 0x%02X\n",
    usbsid_config.socketOne.sid1.addr,
    usbsid_config.socketOne.sid2.addr);
  CFG("[SOCKET TWO] %s as %s\n",
    enabled[(int)usbsid_config.socketTwo.enabled],
    ((int)usbsid_config.socketTwo.dualsid == 1 ? single_dual[0] : single_dual[1]));
  CFG("[SOCKET TWO] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketTwo.chiptype],
    clonetypes[usbsid_config.socketTwo.clonetype]);
  CFG("[SOCKET TWO] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketTwo.sid1.type],
    sidtypes[usbsid_config.socketTwo.sid2.type]);
  CFG("[SOCKET ONE] SID 1 ID: %d, SID 2 ID: %d\n",
    usbsid_config.socketTwo.sid1.id,
    usbsid_config.socketTwo.sid2.id);
  CFG("[SOCKET ONE] SID 1 ADDR: 0x%02X, SID 2 ADDR: 0x%02X\n",
    usbsid_config.socketTwo.sid1.addr,
    usbsid_config.socketTwo.sid2.addr);
  CFG("[SOCKET TWO AS ONE (MIRRORED)] %s\n",
    true_false[(int)usbsid_config.mirrored]);

  CFG("[LED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.LED.enabled],
    true_false[(int)usbsid_config.LED.idle_breathe]);
  CFG("[RGBLED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.RGBLED.enabled],
    true_false[(int)usbsid_config.RGBLED.idle_breathe]);
  CFG("[RGBLED SIDTOUSE] %d\n",
    (int)usbsid_config.RGBLED.sid_to_use);
  CFG("[RGBLED BRIGHTNESS] %d\n",
    (int)usbsid_config.RGBLED.brightness);

  CFG("[CDC] %s\n",
    enabled[(int)usbsid_config.Cdc.enabled]);
  CFG("[WebUSB] %s\n",
    enabled[(int)usbsid_config.WebUSB.enabled]);
  CFG("[Asid] %s\n",
    enabled[(int)usbsid_config.Asid.enabled]);
  CFG("[Midi] %s\n",
    enabled[(int)usbsid_config.Midi.enabled]);

  CFG("[FMOpl] %s\n",
    enabled[(int)usbsid_config.FMOpl.enabled]);
  CFG("[FMOpl] SIDno %d\n",
    usbsid_config.FMOpl.sidno);

  #if defined(HAS_AUDIOSWITCH)
  CFG("[AUDIO_SWITCH] %s\n",
    mono_stereo[(int)usbsid_config.stereo_en]);
  #endif

  CFG("[END PRINT CONFIG SETTINGS]\n");
  return;
}

void print_socket_config(void)
{
  CFG("\n");  /* new line before the actual socket settings are printed */
  CFG("[START PRINT SOCKET CONFIG]\n");

  CFG("SOCK_ONE EN: %s SOCK_TWO EN: %s\nACT_AS_ONE: %s\nNO SIDS\nSOCK_ONE #%d\nSOCK_TWO #%d\nTOTAL #%d\n",
    true_false[cfg.sock_one], true_false[cfg.sock_two], true_false[cfg.mirrored],
    cfg.sids_one, cfg.sids_two, cfg.numsids);

  CFG("[END PRINT SOCKET CONFIG]\n");
  return;
}

void print_bus_config(void)
{
  CFG("\n");  /* new line before the actual bus config is printed */
  CFG("[START PRINT BUS CONFIG]\n");

  CFG("BUS\nONE:   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\nTWO:   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\nTHREE: %02x 0b"PRINTF_BINARY_PATTERN_INT8"\nFOUR:  %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n",
    cfg.one, PRINTF_BYTE_TO_BINARY_INT8(cfg.one),
    cfg.two, PRINTF_BYTE_TO_BINARY_INT8(cfg.two),
    cfg.three, PRINTF_BYTE_TO_BINARY_INT8(cfg.three),
    cfg.four, PRINTF_BYTE_TO_BINARY_INT8(cfg.four));
  CFG("ADDRESS MASKS\n");
  CFG("MASK_ONE:   0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.one_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.one_mask));
  CFG("MASK_TWO:   0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.two_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.two_mask));
  CFG("MASK_THREE: 0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.three_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.three_mask));
  CFG("MASK_FOUR:  0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", cfg.four_mask, PRINTF_BYTE_TO_BINARY_INT8(cfg.four_mask));

  CFG("[END PRINT BUS CONFIG]\n");
  return;
}
