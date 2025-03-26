/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * globals.h
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

#ifndef _USBSIDGLOBALS_H_
#define _USBSIDGLOBALS_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdint.h>
#include <stdlib.h>

#include "macros.h"


/* Compile time variable settings */
#ifndef MAGIC_SMOKE
#define MAGIC_SMOKE 19700101  /* DATEOFRELEASE */
#endif
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.4.1-BETA.20250326"  /* Must be the same as in CMakeLists.txt */
#endif
#ifndef PCB_VERSION
#define PCB_VERSION 1.0
#endif

#ifndef USBSID_MAX_SIDS
#define USBSID_MAX_SIDS 4  /* 2x Real or up to 4 with Clones */
#endif

/* USBSID command byte */
enum
{
  PACKET_TYPE  = 0xC0,  /* 0b11000000 ~ 192  */
  BYTE_MASK    = 0x3F,  /*   0b111111 ~  63  */
  COMMAND_MASK = 0x1F,  /*    0b11111 ~  31  */

  /* BYTE 0 - top 2 bits */
  WRITE        =   0,   /*        0b0 ~ 0x00 */
  READ         =   1,   /*        0b1 ~ 0x40 */
  CYCLED_WRITE =   2,   /*       0b10 ~ 0x80 */
  COMMAND      =   3,   /*       0b11 ~ 0xC0 */
  /* BYTE 0 - lower 6 bits for byte count */
  /* BYTE 0 - lower 5 bits for Commands */
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

  /* INTERNAL BUS COMMANDS */
  G_PAUSE      = 2,
  G_CLEAR_BUS  = 3,

  /* WEBSUSB ADDITIONAL COMMANDS */
  WEBUSB_COMMAND  = 0xFF,
  WEBUSB_RESET    = 0x15,
  WEBUSB_CONTINUE = 0x16,
};

/* USB data type */
extern char dtype, ntype, cdc, asid, midi, wusb;

/* WebUSB globals */
#define URL  "/usbsid.loudai.nl"
enum
{
  VENDOR_REQUEST_WEBUSB = 1,
  VENDOR_REQUEST_MICROSOFT = 2
};
extern uint8_t const desc_ms_os_20[];


#ifdef __cplusplus
  }
#endif

#endif /* _USBSIDGLOBALS_H_ */
