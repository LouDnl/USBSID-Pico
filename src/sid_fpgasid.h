/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_fpgasid.h
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

#ifndef _USBSID_SID_FPGASID_H_
#define _USBSID_SID_FPGASID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


#define FPGASID_IDENTIFIER 0xF51D

static const char __in_flash("fpgasid_vars") *slots[2] = { "A", "B" };
static const char __in_flash("fpgasid_vars") *extinsource[4] = { "analog input", "disabled", "other SID", "digifix (8580)" };
static const char __in_flash("fpgasid_vars") *readback[4] = { "bitrotation 6581", "always read value", "always read $00", "bitrotation 8580" };
static const char __in_flash("fpgasid_vars") *regdelay[2] = { "6581", "8580" };
static const char __in_flash("fpgasid_vars") *mixedwave[2] = { "6581", "8580" };
static const char __in_flash("fpgasid_vars") *crunchydac[2] = { "on (6581)", "off (8580)" };
static const char __in_flash("fpgasid_vars") *filtermode[2] = { "6581", "8580" };
static const char __in_flash("fpgasid_vars") *outputmode[2] = { "dual output over SID1 & SID2 channels -> stereo", "dual output over SID1 channel -> mono mix" };
static const char __in_flash("fpgasid_vars") *sid2addr[5] = { "$D400 ", "$DE00 ", "$D500 ", "$D420 ", "" };

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_FPGASID_H_ */
