/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2 (RP2350) based board for
 * interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * usbsid.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
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

#ifndef _USBSID_H_
#define _USBSID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>

/* Pico libs */
#include "pico/stdlib.h"
#include "pico/types.h"       /* absolute_time_t */
#include "pico/multicore.h"   /* Multicore */
#include "pico/sem.h"         /* Semaphores */
#include "pico/util/queue.h"  /* Inter core queue */

/* Hardware api's */
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "hardware/structs/sio.h"  /* Pico SIO structs */

/* Reboot type logging */
#if PICO_RP2040
#include "hardware/regs/vreg_and_chip_reset.h"
#else
#include "hardware/regs/powman.h"
#endif
#include "hardware/vreg.h"

/* TinyUSB libs */
#include "bsp/board_api.h"   /* Tiny USB Board Porting API */
#include "tusb.h"            /* Tiny USB stack */
#include "tusb_config.h"     /* Tiny USB configuration */


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

/* Outgoing USB (CDC/WebUSB) data buffer
 *
 * 1 byte:
 * byte 0 : value to return
 *
 */
#define BYTES_TO_SEND 1


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_H_ */
