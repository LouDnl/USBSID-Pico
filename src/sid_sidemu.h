/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_sidemu.h
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

#ifndef _USBSID_SID_SIDEMU_H_
#define _USBSID_SID_SIDEMU_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


#define SIDEMU_WAIT_CYCLES       38  /* 16 nops * 2 cycles + 1x rts 6 cycles == 38 cycles */

#define SIDEMU_CFG_OFFSET        0x1e
#define SIDEMU_CFG_DATA          0x1f

#define SIDEMU_OFFS_ID           0xfe
#define SIDEMU_OFFS_CMD          0xff

#define SIDEMU_CMD_1             0x42 /* #66 */
#define SIDEMU_CMD_2             0x45 /* #69 */
#define SIDEMU_CMD_3             0x60 /* #96 */

#define SIDEMU_CMD_RELOAD_CONFIG 0xfe
#define SIDEMU_CMD_SAVE_CONFIG   0xff

#define SIDEMU_BUILD_YEAR_HI     0x01
#define SIDEMU_BUILD_YEAR_LO     0x02
#define SIDEMU_BUILD_YEAR_MONTH  0x03
#define SIDEMU_BUILD_YEAR_DAY    0x04
#define SIDEMU_BUILD_YEAR_HOUR   0x05
#define SIDEMU_BUILD_YEAR_MINUTE 0x06
#define SIDEMU_BUILD_YEAR_SECOND 0x07

#define SIDEMU_BOARD_HW_ID       0x08
#define SIDEMU_BOARD_MAJOR_V     0x09
#define SIDEMU_BOARD_MINOR_V     0x0a

#define SIDEMU_MODEL_R2          0x00
#define SIDEMU_MODEL_R3          0x01
#define SIDEMU_MODEL_R4          0x02
#define SIDEMU_MODEL_R4AR        0x03
#define SIDEMU_MODEL_R5          0x04


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_SIDEMU_H_ */
