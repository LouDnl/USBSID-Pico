/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_skpico.h
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

#ifndef _USBSID_SID_SKPICO_H_
#define _USBSID_SID_SKPICO_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


#define SKPICO_VER_SIZE 36
#define SKPICO_VER_LEN 20
#define SKPICO_SIG_START 20
#define SKPICO_SIG_LEN 8
#define SKPICO_FWVER_START 28
#define SKPICO_FWVER_LEN 2
#define SKPICO_DAC_START 30
#define SKPICO_DAC_LEN 1

static const uint8_t init_configmode[2] = {0x1F, 0xFF };       /* Inits and extends config mode */
static const uint8_t config_extend[2] = {0x1D, 0xFA };         /* Clear address lines and extend config mode */
static const uint8_t config_exit[2] = {0x1D, 0xFB };           /* Exit config mode without reset */
static const uint8_t config_update[2] = {0x1D, 0xFE };         /* Update configuration and exit */
static const uint8_t config_writeupdate[2] = {0x1D, 0xFF };    /* Update and write configuration and exit */
static const uint8_t config_write[2] = {0x1D, 0x0 };           /* Write complete config to memory */
static const uint8_t start_config_write[2] = {0x1E, 0x0 };     /* Write single value to config */
static const uint8_t set_dac_mode_mono8[2] = {0x1F, 0xFC };    /* Set DAC mode to mono */
static const uint8_t set_dac_mode_stereo8[2] = {0x1F, 0xFB };  /* Set DAC mode to stereo */

static const uint8_t skpico_default_config[64] = {                   /* Default SKPico config */
  0x1, 0xC, 0x1, 0xE, /* SID 1 */
  0,0,0,0, /* -- */
  0x3, 0xC, 0x0, 0xE, /* SID 2 */
  0x5,
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0,0,
  0x0, 0x7, 0x0, 0xF, 0x0,
  0,0,
};

static const char __in_flash("skpico_vars") *config_names[64] = {
  "CFG_SID1_TYPE",       //  0   //  1  // 0 ... 3  // 6581, 8580, 8580+digiboost, none
  "CFG_SID1_DIGIBOOST",  //  1   // 12  // 0 ... 15, 0x0 ... 0xF
  "CFG_REGISTER_READ",   //  2   //  1  // Always 1
  "CFG_SID1_VOLUME",     //  3   // 14  // 0 ... 14, 0x0 ... 0xE
  "","","","",           //  4 -> 7
  "CFG_SID2_TYPE",       //  8   //  3  // 0 ... 4  // 6581, 8580, 8580+digiboost, none, FMopl
  "CFG_SID2_DIGIBOOST",  //  9   // 12  // 0 ... 15, 0x0 ... 0xF
  "CFG_SID2_ADDRESS",    // 10   //  0  // 0 ... 5  // sidFlags{ CS, A5, A8, A5|A8, A8, A8 }
  "CFG_SID2_VOLUME",     // 11   // 14  // 0 ... 14, 0x0 ... 0xE
  "CFG_SID_PANNING",     // 12   //  5  // 0 ... 14, 0x0 ... 0xE
  "","","","","","",     // 13 -> 18
  "","","","","","",     // 19 -> 24
  "","","","","","",     // 25 -> 30
  "","","","","","",     // 31 -> 36
  "","","","","","",     // 37 -> 42
  "","","","","","",     // 43 -> 48
  "","","","","","",     // 49 -> 54
  "","",                 // 55 -> 56
  "CFG_TRIGGER",         // 57   //  0  // Paddles
  "CFG_SID_BALANCE",     // 58   //  7  // 0 ... 14, 0x0 ... 0xE
  "CFG_CLOCKSPEED",      // 59   //  0  // 0 ... 2  // c64clock{ 985248, 1022727, 1023440 }
  "CFG_POT_FILTER",      // 60   // 16  // Paddles
  "CFG_DIGIDETECT",      // 61   //  0  // 0 ... 1
  "","",                 // 62 -> 63
};
static const char __in_flash("skpico_vars") *sid_types[5] = {
  "6581", "8580", "8580+digiboost", "none", "FMopl"
};
static const char __in_flash("skpico_vars") *sid2_address[6] = {
  "CS", "A5", "A8", "A5|A8", "A8", "A8"
};
static const char __in_flash("skpico_vars") *clock_speed[3] = {
  "985248", "1022727", "1023440"
};
static const size_t s_config_names = count_of(config_names);
static const size_t s_sid_types = count_of(sid_types);
static const size_t s_sid2_address = count_of(sid2_address);
static const size_t s_clock_speed = count_of(clock_speed);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_SKPICO_H_ */
