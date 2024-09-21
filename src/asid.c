/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * asid.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
 *
 * Copyright (c) 2024 LouD
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

#include "midi.h"
#include "asid.h"
#include "usbsid.h"

#ifdef ASID_DEBUG
#define ASDBG(...) printf(__VA_ARGS__)
#else
#define ASDBG(...) ((void)0)
#endif

void handle_asid_message(uint8_t sid ,uint8_t* buffer, int size)
{
  (void)size;  /* unused at the moment */

  unsigned int reg = 0;
  for (uint8_t mask = 0; mask < 4; mask++) {  /* no more then 4 masks */
    for (uint8_t bit = 0; bit < 7; bit++) {  /* each packet has 7 bits ~ stoopid midi */
      if (buffer[mask + 3] & (1 << bit)) {  /* 3 byte message, skip 3 each round and add the bit */
        uint8_t register_value = buffer[reg + 11];  /* get the value to write from the buffer */
        if(buffer[mask + 7] & (1 << bit)) {  /* if anything higher then 0 */
          register_value |= 0x80;  /* the register_value needs its 8th MSB bit */
        }
        uint8_t address = asid_sid_registers[mask * 7 + bit];
        handle_asidbuffer_task((address |= sid), register_value);
        reg++;
      }
    }
  }
}

void decode_asid_message(uint8_t* buffer, int size)
{
  switch(buffer[2]) {
    case 0x4C:
      /* Play start */
      midimachine.bus = CLAIMED;
      break;
    case 0x4D:
      /* Play stop */
      midimachine.bus = FREE;
      break;
    case 0x4F:
      /* Display characters */
      break;
    case 0x4E:
      handle_asid_message(0, buffer, size);
      break;
    case 0x50:
      handle_asid_message(32, buffer, size);
      break;
    case 0x51:
      handle_asid_message(64, buffer, size);
      break;
    case 0x60:
      midimachine.fmopl = 1;
      #if defined(SIDTYPE1)
      /* Not implemented yet */
      #endif
    default:
      break;
  }
}

void process_sysex(uint8_t* buffer, int size) // NOTE: UNNESCESSARY OVERHEAD IF ASID ONLY
{
  if (buffer[1] == 0x2D) {  /* 0x2D = ASID sysex message */
    decode_asid_message(buffer, size);
  }  // room for other sysex protocols
}