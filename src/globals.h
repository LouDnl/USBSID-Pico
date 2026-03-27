/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * globals.h
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

#ifndef _USBSID_GLOBALS_H_
#define _USBSID_GLOBALS_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>

/* Pico libs */
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "pico/types.h"       /* absolute_time_t */
#include "pico/multicore.h"   /* Multicore */
#include "pico/sem.h"         /* Semaphores */
#include "pico/util/queue.h"  /* Inter core queue */

/* Hardware api's */
#include "hardware/gpio.h"   /* General Purpose Input/Output (GPIO) API */
#include "hardware/pio.h"    /* Programmable I/O (PIO) API */
#include "hardware/dma.h"    /* DMA Controller API */
#include "hardware/pwm.h"    /* Hardware Pulse Width Modulation (PWM) API */
#include "hardware/irq.h"    /* Hardware interrupt handling */
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/sio.h"  /* Pico SIO structs */
/* Reboot type logging */
#if PICO_RP2350
#include "hardware/structs/powman.h"
#endif
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

/* Project headers to always include */
#include <macros.h>
#include <sid_defs.h>
#include <usbsid_defs.h>


/* Compile time variable settings */
#ifndef MAGIC_SMOKE
#define MAGIC_SMOKE 19700101  /* DATEOFRELEASE */
#endif
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.0.0-ALPHA.19700101"
#endif
#ifndef PCB_VERSION
#define PCB_VERSION "1.0"
#endif

#ifndef USBSID_MAX_SIDS
#define USBSID_MAX_SIDS 4  /* 2x Real or up to 4 with Clones */
#endif

#ifndef MIN_CYCLES
#define MIN_CYCLES 6  /* 10 cycles for non cycled writes when in sequence */
#endif

#ifndef USE_CDC_CALLBACK
#define USE_CDC_CALLBACK 1  /* Force CDC callback use over cdc_task */
#endif

/* Global USB definitions */
#define CDC_ITF 0
#define MIDI_ITF 0
#define WUSB_ITF 0
#define MIDI_CABLE 0

/* USB data type */
extern volatile char dtype, ntype, rtype;
extern const char cdc, asid, midi, sysex, wusb, uart;

/* WebUSB globals */
#define URL  "/usbsid.loudai.nl"
enum
{
  VENDOR_REQUEST_WEBUSB = 1,
  VENDOR_REQUEST_MICROSOFT = 2,
  VENDOR_REQUEST_USBSID = 3  /* Unofficial hack */
};
extern uint8_t const desc_ms_os_20[];

/* USBSID command byte */
enum
{
  PACKET_TYPE  = 0xC0,  /* 0b11000000 ~ 192  */
  BYTE_MASK    = 0x3F,  /*   0b111111 ~  63  */
  COMMAND_MASK = 0x1F,  /*    0b11111 ~  31  */

  /* BYTE 0 - top 2 bits */
  WRITE        =   0,   /*        0b0 ~ 0x00 */
  READ         =   1,   /*        0b1 ~ 0x40 */
  CYCLED_WRITE =   2,   /*       0b10 ~ 0x80 */
  COMMAND      =   3,   /*       0b11 ~ 0xC0 */
  /* BYTE 0 - lower 6 bits for byte count */
  /* BYTE 0 - lower 5 bits for Commands */
  CYCLED_READ  =   4,   /*      0b100 ~ 0x04 */
  DELAY_CYCLES =   5,   /*      0b101 ~ 0x05 */
  PAUSE        =  10,   /*     0b1010 ~ 0x0A */
  UNPAUSE      =  11,   /*     0b1011 ~ 0x0B */
  MUTE         =  12,   /*     0b1100 ~ 0x0C */
  UNMUTE       =  13,   /*     0b1101 ~ 0x0D */
  RESET_SID    =  14,   /*     0b1110 ~ 0x0E */
  DISABLE_SID  =  15,   /*     0b1111 ~ 0x0F */
  ENABLE_SID   =  16,   /*    0b10000 ~ 0x10 */
  CLEAR_BUS    =  17,   /*    0b10001 ~ 0x11 */
  CONFIG       =  18,   /*    0b10010 ~ 0x12 */
  RESET_MCU    =  19,   /*    0b10011 ~ 0x13 */
  BOOTLOADER   =  20,   /*    0b10100 ~ 0x14 */

  /* WEBSUSB ADDITIONAL COMMANDS */
  WEBUSB_COMMAND  = 0xFF,
  WEBUSB_RESET    = 0x15,
  WEBUSB_CONTINUE = 0x16,
  WEBUSB_NUMSIDS  = 0x39,
  WEBUSB_FMOPLSID = 0x3A,
  WEBUSB_TOGGLEAU = 0x3B,
};

/* USBSID Uart */
enum {
  BAUD_RATE = 115200,
  FFIN_FAST_BAUD_RATE = 921600,
};


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_GLOBALS_H_ */
