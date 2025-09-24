/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * interface.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Any licensing conditions from the above named source automatically
 * apply to this code
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

#ifndef _USBSID_INTERFACE_H_
#define _USBSID_INTERFACE_H_

#include <stdint.h> /* C type include */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ONBOARD_EMULATOR
void start_emudore_cynthcart(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  bool run_continuously
);
bool stop_emudore(void);

unsigned int run_emulation_cycle(void);
unsigned int run_specified_cycle(
  bool cpu, bool cia1, bool cia2,
  bool vic, bool io,   bool cart);

bool is_looping(void);
bool disable_looping(void);

void logging_enable(int logid);
void logging_disable(int logid);

#ifdef ONBOARD_SIDPLAYER
void start_emudore_sidtuneplayer(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  size_t binsize_, bool run_continuously
);
void start_emudore_prgtuneplayer(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  size_t binsize_, bool run_continuously
);
#endif /* ONBOARD_SIDPLAYER */
#endif /* ONBOARD_EMULATOR */

#ifdef __cplusplus
}
#endif


#endif /* _USBSID_INTERFACE_H_ */
