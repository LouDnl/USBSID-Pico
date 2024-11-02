/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * usbsid.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
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
#include "pico/types.h"     /* absolute_time_t */
#include "pico/multicore.h" /* Multicore */
#include "pico/sem.h"       /* Semaphores */
#include "pico/mem_ops.h"   /* Optimized memory handling functions string.h replacement */

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
#if __has_include("bsp/board_api.h") /* Needed to account for update in tinyUSB */
#include "bsp/board_api.h"
#else
#include "bsp/board.h"       /* Tiny USB Boards Abstraction Layer */
#endif
#include "tusb.h"            /* Tiny USB stack */
#include "tusb_config.h"     /* Tiny USB configuration */

/* PIO */
#include "clock.pio.h"       /* Square wave generator */
#if defined(USE_RGB)
#include "ws2812.pio.h"      /* RGB led handler */
#endif


/* Maximum incoming and outgoing buffer size
 *
 * NOTE: 64 byte zero padded packets take longer to process
 */
#define MAX_BUFFER_SIZE 64

/* Incoming USB (CDC/WebUSB) data buffer
 *
 * 3 bytes:
 * Byte 0 ~ command byte
 * Byte 1 ~ address byte
 * Byte 2 ~ data byte
 *
 */
#define BYTES_EXPECTED 3

/* Backwards compatible incoming USB (CDC/WebUSB) data buffer
 *
 * 4 bytes:
 * Byte 0 ~ command byte
 * Byte 1 ~ address high byte
 * Byte 2 ~ address low byte
 * Byte 3 ~ data byte
 *
 */
#define BACKWARD_BYTES 4

/* Incoming Config data buffer
  *
  * 5 bytes:
  * Byte 0 ~ command
  * Byte 1 ~ struct setting e.g. socketOne or clock_rate
  * Byte 2 ~ setting entry e.g. dualsid
  * Byte 3 ~ new value
  * Byte 4 ~ reserved
  *
  */
#define CONFIG_BYTES 5

/* Outgoing USB (CDC/WebUSB) data buffer
 *
 * 1 byte:
 * byte 0 : value to return
 *
 */
#define BYTES_TO_SEND 1

/* LED breathe levels */
enum
{
  ALWAYS_OFF = 99999,
  ALWAYS_ON = 0,
  CHECK_INTV = 100,
  MAX_CHECKS = 200,  /* 200 checks times 100ms == 20 seconds */
  BREATHE_INTV = 1,
  BREATHE_STEP = 100,
  VUE_MAX = 65534,
  HZ_MAX = 40
};


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_H_ */
