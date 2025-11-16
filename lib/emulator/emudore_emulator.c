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

#ifdef ONBOARD_SIDPLAYER
// #include <supremacyprg.h>
#include <supremacy.h>
// #include <edgeofdisgrace.h>
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
extern void start_emudore_prgtuneplayer(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  size_t binsize_, bool run_continuously
);
extern void start_emudore_sidtuneplayer(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  size_t binsize_, int tuneno, bool run_continuously
);
extern unsigned int run_emulation_cycle(void);
extern unsigned int run_specified_cycle(
  bool cpu, bool cia1, bool cia2,
  bool vic, bool io,   bool cart);

extern bool stop_emudore(void);
extern bool is_looping(void);
extern bool disable_looping(void);
extern void _set_logging(void);
extern void logging_enable(int logid);
extern void logging_disable(int logid);
#endif

#ifdef ONBOARD_SIDPLAYER
extern bool isrsid(void);
/* Declare variables */
bool sidplayer_init = false, sidplayer_playing = false;

/* Declare local variables */
static int sidfile_size = 0;
static int subtune = 0;
#endif


#ifdef ONBOARD_EMULATOR
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
  start_emudore_cynthcart(basic,chargen,kernal,cynthcart,false);
  return;
}

void stop_emulator(void)
{
  stop_emudore();
  return;
}

unsigned int run_cynthcart(void)
{
  return run_specified_cycle(
    true,  // cpu
    true,  // cia1
    false, // cia2
    true,  // vic
    false, // io
    true   // cart
  );
}

#ifdef ONBOARD_SIDPLAYER
static void print_tune_info()
{
  return;
}

static void set_tune_config()
{
  return;
}

void start_sidplayer()
{ /* BUG: Still has issues */

  // size_t filesize = count_of(edgeofdisgrace/* supremacyprg */); /* 4382 */
  // do_timer_logging(dolog);
  // start_emudore_sidtuneplayer(
  //   basic,chargen,kernal,
  //   edgeofdisgrace,filesize,
  //   false /* true */);
  size_t filesize = count_of(supremacy); /* 4382 */
  start_emudore_sidtuneplayer(
    basic,chargen,kernal,
    supremacy,filesize,0,
    false /* true */);
  // size_t filesize = count_of(supremacyprg); /* 4382 */
  // start_emudore_prgtuneplayer(
  //   basic,chargen,kernal,
  //   supremacyprg,filesize,
  //   false /* true */);
}

unsigned int run_psidplayer(void)
{
  if (!isrsid()) {
    return run_specified_cycle(
      true,   // cpu
      true,   // cia1
      false,  // cia2
      false,  // vic
      false,  // io
      false   // cart
    );
  } else {
    run_emulation_cycle();
  }
}

static int init_sidplayer(uint8_t * sidfile)
{
  return 0;
}

int load_sidtune(uint8_t * sidfile, int sidfilesize, char tuneno)
{
  size_t filesize = sidfilesize;
  start_emudore_sidtuneplayer(
    basic,chargen,kernal,
    sidfile,filesize,tuneno,
    false /* true */);
  return 1;
}

int load_sidtune_fromflash(int sidflashid, char tuneno)
{
  return 0;
}

void reset_sidplayer(void)
{
  return;
}

void next_subtune(void)
{
  return;
}

void previous_subtune(void)
{
  return;
}

uint16_t playtime(void)
{
  return 0;
}
#endif /* ONBOARD_SIDPLAYER */
#endif /* ONBOARD_EMULATOR */
