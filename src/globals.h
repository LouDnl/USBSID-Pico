/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * globals.h
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

#ifndef _USBSIDGLOBALS_H_
#define _USBSIDGLOBALS_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdint.h>

/* Helper macro for constraining a value within a range */
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

/* USBSID command byte */
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

/* USB data type */
extern char dtype, ntype, cdc, asid, midi, wusb;

/* WebUSB globals */
#define URL  "/github.com/LouDnl/USBSID-Pico"
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
