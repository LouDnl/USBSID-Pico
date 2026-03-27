/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_pdsid.h
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

#ifndef _USBSID_SID_PDSID_H_
#define _USBSID_SID_PDSID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


#define PDSID_P  0x50 /* 'P' */
#define PDSID_D  0x44 /* 'D' */
#define PDSID_ID 0x53 /* 'S' */
#define PDREG_P  0x1d
#define PDREG_D  0x1e
#define PDREG_S  0x1f


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_PDSID_H_ */
