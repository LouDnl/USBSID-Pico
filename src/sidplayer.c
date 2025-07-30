/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sidplayer.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the
 * sourcecode of:
 * cRSID 1.41 by Hermit: https://csdb.dk/release/?id=248448
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
#include "cRSID/libcRSID.h"

#include "sidtunes/supremacy.h"
#include "sidtunes/afterburner.h"
#include "sidtunes/edgeofdisgrace.h"


/* config.c */
extern RuntimeCFG cfg;
extern void apply_clockrate(int n_clock, bool suspend_sids);

/* gpio.c */
extern void mute_sid(void);
extern void unmute_sid(void);
extern uint16_t __no_inline_not_in_flash_func(cycled_delay_operation)(uint16_t cycles);

/* cRSID */
typedef struct cRSID_C64instance cRSID_C64instance;
typedef struct cRSID_SIDheader   cRSID_SIDheader;
extern unsigned long long cRSID_emulateC64(cRSID_C64instance* C64);
extern cRSID_C64instance *cRSID_init(void);
extern cRSID_SIDheader* cRSID_processSIDfile (cRSID_C64instance* C64, unsigned char* filedata, int filesize);
extern void cRSID_initSIDtune(cRSID_C64instance* C64, cRSID_SIDheader* SIDheader, char subtune);
extern void cRSID_initC64(cRSID_C64instance* C64);

/* Declare variables */
bool sidplayer_init = false, sidplayer_playing = false;

/* Declare local variables */
static cRSID_C64instance *sidplayer = NULL;
static cRSID_SIDheader *sidfile_header = NULL;
static int sidfile_size = 0;
static int subtune = 0;
static const char ChannelChar[] = {'-','L','R','M'};


void run_emulator(void)
{
  cRSID_emulateC64(sidplayer);
  return;
}

static void print_tune_info(cRSID_C64instance *C64, cRSID_SIDheader *SIDheader)
{
  DBG("\n****** SID file info ******\n");

  DBG("Author: %s\n", SIDheader->Author);
  DBG("Title: %s\n", SIDheader->Title);
  DBG("Info: %s\n", SIDheader->ReleaseInfo);
  DBG("Load-address: $%4.4X, End-address:  $%4.4X, Size:%d bytes\n", C64->LoadAddress, C64->EndAddress, C64->EndAddress - C64->LoadAddress);
  DBG("Init-address: $%4.4X, Play-address: $%4.4X", C64->InitAddress, C64->PlayAddress);
  if ((SIDheader->PlayAddressH == 0) && (SIDheader->PlayAddressL == 0)) {
    DBG(", (IRQ)\n");
  } else {
    DBG("\n");
  }

  DBG("Subtune: %d (of %d)", C64->SubTune, SIDheader->SubtuneAmount);
  if (C64->RealSIDmode) {
    DBG(", RealSID");
  } else if (C64->PSIDdigiMode) {
    DBG(", PSID-digi");
  } else {
    DBG(", PSID");
  }
  DBG(", SID Version: %d\n", SIDheader->Version);

  DBG("SID1:$%4.4X,%d(%c) ", C64->SID[0].BaseAddress, C64->SID[0].ChipModel, ChannelChar[C64->SID[0].Channel]);
  if (C64->SID[1].BaseAddress) DBG("SID2:$%4.4X,%d(%c) ", C64->SID[1].BaseAddress, C64->SID[1].ChipModel, ChannelChar[C64->SID[1].Channel]);
  if (C64->SID[2].BaseAddress) DBG("SID3:$%4.4X,%d(%c) ", C64->SID[2].BaseAddress, C64->SID[2].ChipModel, ChannelChar[C64->SID[2].Channel]);
  if (C64->SID[3].BaseAddress) DBG("SID4:$%4.4X,%d(%c) ", C64->SID[3].BaseAddress, C64->SID[3].ChipModel, ChannelChar[C64->SID[3].Channel]);
  DBG("\n");

  DBG("Speed: %.1fx (player-call at every %d cycle) TimerSource: %s (%d)\n",
    (C64->VideoStandard<=1? 19656.0:17095.0) / C64->FrameCycles, C64->FrameCycles, C64->TimerSource ? "CIA" : "VIC", C64->TimerSource);
  DBG("VideoStandard: %s\n", C64->VideoStandard ? "PAL" : "NTSC");

  DBG("***************************\n\n");
  return;
}

static void set_tune_config(cRSID_C64instance *C64, cRSID_SIDheader *SIDheader)
{
  int sid_clockrate = ((C64->VideoStandard == 0) ? 2 : 1);
  apply_clockrate(sid_clockrate, true);
  switch(cfg.numsids) { /* Ensure we do not write to SID's that do not exist */
    case 1:
      if (C64->SID[1].BaseAddress) { C64->SID[1].BaseAddress=0; C64->SID[1].BasePtr = NULL; }
      if (C64->SID[2].BaseAddress) { C64->SID[2].BaseAddress=0; C64->SID[2].BasePtr = NULL; }
      if (C64->SID[3].BaseAddress) { C64->SID[3].BaseAddress=0; C64->SID[3].BasePtr = NULL; }
      break;
    case 2:
      if (C64->SID[2].BaseAddress) { C64->SID[2].BaseAddress=0; C64->SID[2].BasePtr = NULL; }
      if (C64->SID[3].BaseAddress) { C64->SID[3].BaseAddress=0; C64->SID[3].BasePtr = NULL; }
      break;
    case 3:
      if (C64->SID[3].BaseAddress) { C64->SID[3].BaseAddress=0; C64->SID[3].BasePtr = NULL; }
      break;
    case 4:
    default:
    break;
  }
  return;
}

static int init_sidplayer(uint8_t * sidfile)
{
  sidplayer = cRSID_init();
  DBG("[SIDPLAYER] Init\n");
  if (sidplayer) {
    sidfile_header = cRSID_processSIDfile(sidplayer, sidfile, sidfile_size);
    DBG("[SIDPLAYER] SID file processed\n");
  }
  if (sidfile_header) {
    cRSID_initSIDtune(sidplayer, sidfile_header, subtune);
    set_tune_config(sidplayer, sidfile_header);
    print_tune_info(sidplayer, sidfile_header);
    unmute_sid(); /* Must unmute before play start or some tunes like Spy_vs_Spy will be silent */
    DBG("[SIDPLAYER] SID file loaded and player initialized for playback\n");
    return 1;
  } else {
    DBG("[SIDPLAYER] error initializing sidplayer, unable to start playback\n");
    return 0;
  }
  return 0;
}

int load_sidtune(uint8_t * sidfile, int sidfilesize, char tuneno)
{
  if (sidfile_header)
  {
      sidfile_header = NULL;
      sidfile_size = 0;
      subtune = 1;
  }
  subtune = (tuneno == 0 ? 1 : tuneno);
  sidfile_size = sidfilesize;
  DBG("[SIDPLAYER] SID subtune: %d tuneno: %d\n", subtune, tuneno);
  DBG("[SIDPLAYER] SID size: %d\n", sidfilesize);
  DBG("[SIDPLAYER] SID sidfile @ 0x%X\n", &sidfile);
  return init_sidplayer(sidfile);
}

int load_sidtune_fromflash(int sidflashid, char tuneno)
{
  DBG("[SIDPLAYER] Load SID file from flash\n");
  switch (sidflashid) {
    case 1:
      return load_sidtune(supremacy, count_of(supremacy), tuneno);
      break;
    case 2:
      return load_sidtune(afterburner, count_of(afterburner), tuneno);
      break;
    case 3:
      return load_sidtune(edge_of_disgrace, count_of(edge_of_disgrace), tuneno);
      break;
    default:
      DBG("[SIDPLAYER] Incorrect flash ID supplied, not loading SID file\n");
      break;
  }
  return 0;
}

void reset_sidplayer(void)
{
  sidplayer_playing = false;
  if (sidplayer != NULL) {
    cRSID_initC64(sidplayer);  /* Reset */
  }
}

void next_subtune(void)
{
  if (sidplayer != NULL) {
    mute_sid();
    reset_sidplayer();  /* Reset */
    if(++subtune > sidfile_header->SubtuneAmount) { subtune = 1; } /* Make sure we wrap around no subtunes */
    cRSID_initSIDtune(sidplayer, sidfile_header, subtune);
    set_tune_config(sidplayer, sidfile_header);
    print_tune_info(sidplayer, sidfile_header);
    unmute_sid();
  }
  return;
}

void previous_subtune(void)
{
  if (sidplayer != NULL) {
    mute_sid();
    reset_sidplayer();  /* Reset */
    if(--subtune < 1) { subtune = sidfile_header->SubtuneAmount; }  /* Make sure we wrap around no subtunes */
    cRSID_initSIDtune(sidplayer, sidfile_header, subtune);
    set_tune_config(sidplayer, sidfile_header);
    print_tune_info(sidplayer, sidfile_header);
    unmute_sid();
  }
  return;
}

uint16_t playtime(void)
{
  if (sidplayer != NULL)
    return sidplayer->PlayTime;
  else
    return 0;
}
