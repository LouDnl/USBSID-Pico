/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_armsid.h
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

#ifndef _USBSID_SID_ARMSID_H_
#define _USBSID_SID_ARMSID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


#define ARMSID_A  0x1d /* 29 */
#define ARMSID_B  0x1e /* 30 */
#define ARMSID_C  0x1f /* 31 */
#define ARMSID_R1 0x1b /* 27 */
#define ARMSID_R2 0x1c /* 28 */

#define ARMSID_D1 0x53 /* 'S' */
#define ARMSID_D2 0x49 /* 'I' */
#define ARMSID_D3 0x44 /* 'D' */

#define ARMSID_ID1 0x4e /* 'N' */
#define ARMSID_ID2 0x4f /* 'O' */
#define ARM2SID_ID1 0x4c /* 'L' */
#define ARM2SID_ID2 0x52 /* 'R' */


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_ARMSID_H_ */
