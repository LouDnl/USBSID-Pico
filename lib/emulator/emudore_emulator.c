/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * emulator_emodure.c
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

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "logging.h"
#include "config.h"
#include "globals.h"

#ifdef ONBOARD_EMULATOR
#include <kernal.h>
#include <chargen.h>
#include <basic.h>
#include <cynthcart.h>
#endif

/* config.c */
extern RuntimeCFG cfg;
extern void apply_clockrate(int n_clock, bool suspend_sids);

/* gpio.c */
extern void mute_sid(void);
extern void unmute_sid(void);

#ifdef ONBOARD_EMULATOR
/* interface.h */
extern void start_emudore_cynthcart(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  bool run_continuously
);
extern bool stop_emudore(void);
extern unsigned int run_specified_cycle(
  bool callback,
  bool cpu, bool cia1, bool cia2,
  bool vic, bool io,   bool cart);
extern void _set_logging(void);
extern void logging_enable(int logid);
extern void logging_disable(int logid);

void set_logging(int logid)
{
  logging_enable(logid);
  _set_logging();
  return;
}

void unset_logging(int logid)
{
  logging_disable(logid);
  _set_logging();
  return;
}

void start_cynthcart(void)
{
  apply_clockrate(1,true); /* Fixed to PAL */
  start_emudore_cynthcart(
    basic,
    chargen,
    kernal,
    cynthcart,
    false);
  return;
}

void stop_cynthcart(void)
{
  stop_emudore();
  return;
}

unsigned int run_cynthcart(void)
{
  return run_specified_cycle(
    false, // callback
    true,  // cpu
    true,  // cia1
    false, // cia2
    true,  // vic
    false, // io
    true   // cart
  );
}

#endif /* ONBOARD_EMULATOR */
