/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * emodure_emulator.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Any licensing conditions from the above named source automatically
 * apply to this code
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

#ifndef _EMUDORE_EMULATOR_H_
#define _EMUDORE_EMULATOR_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Functions from emodure_emulator.c */
void start_cynthcart(void);
void stop_cynthcart(void);
unsigned int run_cynthcart(void);
void set_logging(int logid);
void unset_logging(int logid);


#ifdef __cplusplus
  }
#endif

#endif /* _EMUDORE_EMULATOR_H_ */
