/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2 (RP2350) based board for
 * interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
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

#include "midi.h"
#include "asid.h"
#include "globals.h"
#include "logging.h"
#include "stdbool.h"

/* Config externals */
extern int fmopl_sid;
extern bool fmopl_enabled;

/* GPIO externals */
extern void __not_in_flash_func(cycled_bus_operation)(uint8_t address, uint8_t data, uint16_t cycles);
extern void pause_sid(void);
extern void reset_sid(void);

/* Well, it does what it does */
void handle_asid_sidmessage(uint8_t sid, uint8_t* buffer, int size)
{
	(void)size;  /* Stop calling me fat, I'm just big boned! */
  unsigned int reg = 0;
  for (uint8_t mask = 0; mask < 4; mask++) {  /* no more then 4 masks */
    for (uint8_t bit = 0; bit < 7; bit++) {  /* each packet has 7 bits ~ stoopid midi */
      if (buffer[mask + 3] & (1 << bit)) {  /* 3 byte message, skip 3 each round and add the bit */
        uint8_t register_value = buffer[reg + 11];  /* get the value to write from the buffer */
        if(buffer[mask + 7] & (1 << bit)) {  /* if anything higher then 0 */
          register_value |= 0x80;  /* the register_value needs its 8th MSB bit */
        }
        uint8_t address = asid_sid_registers[mask * 7 + bit];
        dtype = asid;  /* Set data type to asid */
        /* Pico 2 requires at least 10 cycles between writes
         * or it will be too damn fast! So we do this for other
         * Pico's too */
        cycled_bus_operation((address |= sid), register_value, 10);
        reg++;
      }
    }
  }
}

void handle_asid_fmoplmessage(uint8_t* buffer)
{
	uint8_t ndata_in_buffer = (buffer[0] + 1) << 1;
	uint8_t nmask_bytes = (ndata_in_buffer - 1) / 7 + 1;
	uint8_t data_index = nmask_bytes + 1;
	uint8_t data, field;
	uint8_t asid_fm_register_index = 0;

	static uint8_t fm_registers[MAX_FM_REG_PAIRS * 2];

	for (uint8_t mask = 0; mask < nmask_bytes; mask++) {
		field = 0x01;
		for (uint8_t bit = 0; (bit < 7) && (asid_fm_register_index < ndata_in_buffer); bit++) {
			data = buffer[data_index++];
			if ((buffer[1 + mask] & field) == field) {
				data += 0x80;
			}
			fm_registers[asid_fm_register_index++] = data;
			field <<= 1;
		}
	}
  uint8_t addr = ((fmopl_sid << 5) - 0x20);
	for (uint8_t reg = 0; reg < asid_fm_register_index; reg++) {
    dtype = asid;  /* Set data type to asid */
		/* Pico 2 requires at least 10 cycles between writes
     * or it will be too damn fast! So we do this for other
     * Pico's too */
    if((reg % 2 == 0)) {
      cycled_bus_operation((addr | OPL_REG_ADDRESS), fm_registers[reg], 10);
		} else {
      cycled_bus_operation((addr | OPL_REG_DATA), fm_registers[reg], 10);
		}
	}
	midimachine.fmopl = 0;
	return;
}

/* Spy vs Spy ? */
void decode_asid_message(uint8_t* buffer, int size)
{
  switch(buffer[2]) {
    case 0x4C:  /* Play start */
      midimachine.bus = CLAIMED;
      break;
    case 0x4D:  /* Play stop */
      reset_sid();
      pause_sid();
      midimachine.bus = FREE;
      break;
    case 0x4F:  /* Display characters */
      break;
    case 0x4E:  /* SID 1 */
      handle_asid_sidmessage(0, buffer, size);
      break;
    case 0x50:  /* SID 2 */
      handle_asid_sidmessage(32, buffer, size);
      break;
    case 0x51:  /* SID 3 */
      handle_asid_sidmessage(64, buffer, size);
      break;
    case 0x52:  /* SID 4 */
      handle_asid_sidmessage(96, buffer, size);
      break;
    case 0x60:  /* FMOpl */
      if (fmopl_enabled) {  /* Only if FMOpl is enabled, drop otherwise */
        midimachine.fmopl = 1;
        handle_asid_fmoplmessage(&buffer[3]);  /* Skip first 3 bytes */
      };
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
      decode_asid_message(buffer, size);
      break;
    default:
      break;
  }
	return;
}
