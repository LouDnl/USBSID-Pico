/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_backsid.h
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

#ifndef _USBSID_SID_BACKSID_H_
#define _USBSID_SID_BACKSID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Registers */
#define BACKSID_REG  0x1b /* Register select index */
#define BACKSID_WR   0x1c /* Data write register */
#define BACKSID_RD   0x1f /* Data read register */
#define BACKSID_HS1  0x1d /* Handshake byte 1 */
#define BACKSID_HS2  0x1e /* Handshake byte 2 */
/* Handshake values */
#define BACKSID_HV1  0xb5 /* Handshake value 1 */
#define BACKSID_HV2  0x1d /* Handshake value 2 */
/* $1b Read values */
#define BACKSID_ID   0xba
/* $1b Write values */
#define BACKSID_MAG  0x00 /* Magic */
#define BACKSID_OVD  0x02 /* Overdrive */
#define BACKSID_FLT  0x03 /* Filters */
#define BACKSID_SMT  0x04 /* Smoothing */
#define BACKSID_POT  0x05 /* Pots */
#define BACKSID_MAJ  0x40 /* Major */
#define BACKSID_MIN  0x41 /* Minor */
#define BACKSID_PAT  0x42 /* Patch */
#define BACKSID_REV  0x45 /* Hardware revision */

static const char * backsid_filters[] = {
  "6581", "MIXED", "8580"
};

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_BACKSID_H_ */
