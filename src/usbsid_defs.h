/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * us_defs.h
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

#ifndef _USBSID_DEFS_H_
#define _USBSID_DEFS_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Maximum incoming and outgoing USB (CDC/WebUSB) buffer size
 * NOTE: 64 byte zero padded packets take longer to process
 *
 * Incoming Data buffer write example
 * 3 bytes minimum, 63 bytes maximum
 * Byte 0  ~ command byte (see globals.h)
 * Byte 1  ~ address byte
 * Byte 2  ~ data byte
 * Byte n+ ~ repetition of byte 1 and 2
 *
 * Incoming Data buffer with clock cycles write example
 * 5 bytes minimum, 61 bytes maximum
 * Byte 0  ~ command byte (see globals.h)
 * Byte 1  ~ address low byte
 * Byte 2  ~ data byte
 * Byte 3  ~ clock cycles high byte
 * Byte 4  ~ clock cycles low byte
 * Byte n+ ~ repetition of byte 1, 2, 3 and 4
 *
 * Incoming Command buffer example
 * 2 bytes, trailing bytes will be ignored
 * Byte 0 ~ command byte (see globals.h)
 * Byte 1 ~ optional command argument
 *
 * Incoming Config data buffer command example
 * 5 bytes, trailing bytes will be ignored
 * Byte 0 ~ command byte (see globals.h)
 * Byte 1 ~ config command
 * Byte 2 ~ struct setting e.g. socketOne or clock_rate
 * Byte 3 ~ setting entry e.g. dualsid
 * Byte 4 ~ new value
 * Byte 5 ~ reserved
 *
 */
#define MAX_BUFFER_SIZE 64

/* Socket config array
 *  0 Initiator     = 0x37
 *  1 Verification  = 0x7f
 *  2 HiByte        = socketOne enabled
 *  2 LoByte        = socketOne dualsid
 *  3 LoByte        = socketOne chipType
 *  4 HiByte        = socketOne sid1Type
 *  4 LoByte        = socketOne sid2Type
 *  5 HiByte        = socketTwo enabled
 *  5 LoByte        = socketTwo dualsid
 *  6 LoByte        = socketTwo chipType
 *  7 HiByte        = socketTwo sid1Type
 *  7 LoByte        = socketTwo sid2Type
 *  8 HiByte        = socketOne SID1 id
 *  8 LoByte        = socketOne SID2 id
 *  9 HiByte        = socketOne SID1 id
 *  9 LoByte        = socketOne SID2 id
 * 10 LoByte 0b001  = socketTwo mirrors socketOne
 * 10 LoByte 0b010  = sockets are flipped One is Two and vice versa
 * 10 LoByte 0b100  = SID addresses are mixed (quad sid only)
 * 11 Terminator    = 0xff
 */
#define SOCKET_BUFFER_SIZE 12

/* Outgoing USB (CDC/WebUSB) data buffer
 *
 * 1 byte:
 * byte 0 : value to return
 *
 */
#define BYTES_TO_SEND 1

/* C64 memory storage sizes */
#define C64_MEMORY_SIZE 0x10000 /* 64KB e.g. 65536 bytes */
#define SID_MEMORY_SIZE 0x80    /* 4 * 0x20 = 4 * 32 e.g. 128 bytes */


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_DEFS_H_ */
