/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sysex.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Huge thanks to Thomas Jansson for all his ASID improvement work (https://github.com/thomasj)
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
 *
 * Any licensing conditions from either of the above named sources automatically
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

 #include "stdbool.h"
 #include "config.h"
 #include "globals.h"
 #include "logging.h"


/* Vu */
extern uint16_t vu;

/* ASID */
extern void decode_asid_message(uint8_t* buffer, int size);

/* Config */
void handle_config_request(uint8_t * buffer);

/* Custom commands */
enum {
  SYSEX_TOGGLE_AUDIO = 0x01,
};


/* Spy vs Spy ? */
void decode_sysex_command(uint8_t* buffer, int size)
{
  uint8_t config_buffer[5] = {0};

  switch(buffer[2]) {
    case SYSEX_TOGGLE_AUDIO:  /* Toggle mono / stereo */
      config_buffer[0] = TOGGLE_AUDIO;
      handle_config_request(config_buffer);
      break;
    default:
      break;
  }
  return;
}

/* Is it ? */
void process_sysex(uint8_t* buffer, int size)
{
  switch(buffer[1]) {
    case 0x2D:  /* 0x2D = ASID sysex message */
      dtype = asid;  /* Set data type to ASID */
      vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
      decode_asid_message(buffer, size);
      break;
    case 0x50:  /* The 80's baby */
      dtype = sysex;  /* Set data type to ASID */
      decode_sysex_command(buffer, size);
      break;
    default:
      break;
  }
  return;
}
