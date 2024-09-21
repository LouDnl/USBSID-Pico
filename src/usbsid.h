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

// #define _GNU_SOURCE

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

/* Pico libs */
#include "pico/stdlib.h"
#include "pico/types.h"            /* absolute_time_t */
// #define __STDC_FORMAT_MACROS
// #include <inttypes.h>
#include "pico/multicore.h"        /* Multicore */
#include "pico/sem.h"              /* Semaphores */
#include "hardware/pio.h"          /* Programmable I/O (PIO) API */
#include "hardware/dma.h"
#include "hardware/irq.h"          /* Hardware interrupt handling */
// #include "hardware/adc.h"          /* Analog to Digital Converter (ADC) API */
#include "hardware/pwm.h"          /* Hardware Pulse Width Modulation (PWM) API */
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"  /* Pico SIO structs */

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
#include "bus_control.pio.h" /* Busje komt zo! */
#if defined(USE_RGB)
#include "ws2812.pio.h"      /* RGB led handler */
#endif

#include "logging.h"
#include "mcu.h"   /* rp2040 mcu */
#include "gpio.h"  /* GPIO configuration */

#ifdef __cplusplus
    extern "C" {
#endif

/* SIDTYPE ~ Defaults to 0
 *
 * 0: Single SID config
 *    SID socket 1 filled with either of
 *    - MOS6581/MOS6582/MOS8580
 *    - SIDKickPico
 *    - SwinSID
 *    - any other single SID hardware emulator
 *    - NOTE: Will use the second SID socket simultaniously
 * 1: Single SID config (dual optional with 1x SIDKickPico)
 *    SID socket 1 filled with either of
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    - MOS6581/MOS6582/MOS8580
 *    - SwinSID
 *    - any other single SID hardware emulator
 * 2: Dual SID config
 *    SID sockets 1 & 2 filled with either of
 *    - MOS6581/MOS6582/MOS8580
 *    - SIDKickPico
 *    - SwinSID
 *    - any other single SID hardware emulator
 * 3: Triple SID config with 1x SIDKickPico and other
 *    SID socket 1 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    SID socket 2 filled with either of
 *    - MOS6581/MOS6582/MOS8580
 *    - SIDKickPico
 *    - SwinSID
 *    - any other single SID hardware emulator
 * 4: Quad SID config with 2x SIDKickPico
 *    SID socket 1 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    SID socket 2 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 * 5: Quad SID mixed config with 2x SIDKickPico
 *    SID socket 1 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    - Acts as SID 1 & 3
 *    SID socket 2 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    - Acts as SID 2 & 4
 *
 */
#define SIDTYPE0 0
#define SIDTYPE1 1
#define SIDTYPE2 2
#define SIDTYPE3 3
#define SIDTYPE4 4
#define SIDTYPE5 5

/* Default config for SID type */
#ifndef SIDTYPE
#define SIDTYPE SIDTYPE0
#endif

/* SID type masks for GPIO */
#define SIDUMASK 0xFF00
#define SIDLMASK 0xFF
#if SIDTYPE == SIDTYPE0
#define NUMSIDS  1
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0x0
#define SID2MASK 0x0
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE1
#define NUMSIDS  2
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 ~ OPTIONAL */
#define SID2MASK 0x3F
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE2
#define NUMSIDS  2
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD440  /* SID 2 - socket 2 */
#define SID2MASK 0x5F
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE3
#define NUMSIDS  3
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 */
#define SID2MASK 0x3F
#define SID3ADDR 0xD440  /* SID 3 - socket 2 */
#define SID3MASK 0x5F
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE4  /* SKPico only */
#define NUMSIDS  4
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 */
#define SID2MASK 0x3F
#define SID3ADDR 0xD440  /* SID 3 - socket 2 */
#define SID3MASK 0x5F
#define SID4ADDR 0xD460  /* SID 4 - socket 2 */
#define SID4MASK 0x7F
#elif SIDTYPE == SIDTYPE5  /* SKPico only */
#define NUMSIDS  4
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD440  /* SID 2 - socket 2 */
#define SID2MASK 0x5F
#define SID3ADDR 0xD420  /* SID 3 - socket 1 */
#define SID3MASK 0x3F
#define SID4ADDR 0xD460  /* SID 4 - socket 2 */
#define SID4MASK 0x7F
#endif

#define BYTES_EXPECTED 4

#if defined(DEBUG_TIMING)
uint64_t t1, t2, t3, t4 = 0;
uint64_t wdiff, wwdiff, rdiff, rrdiff = 0;
uint8_t debug_n = 0;
#endif

/* Global vars */

enum
{
  WRITE = 0,
  READ,
  PAUSE,
  RESET_SID,
  RESET_USB,
  CLEAR_BUS
};

enum  /* LED breathe levels */
{
  ALWAYS_OFF = 99999,
  ALWAYS_ON = 0,
  BREATHE_ON = 1,
  BREATHE_STEP = 100,
  VUE_MAX = 65534,
  HZ_MAX = 40
};

static uint8_t sid_memory[(0x20 * NUMSIDS)];
static uint16_t vue;
static uint32_t breathe_interval_ms = BREATHE_ON;
static uint32_t cdcread = 0, cdcwrite = 0;
static intptr_t usb_connected = 0, usbdata = 0, pwm_value = 0, updown = 1;
static uintptr_t addr, val;
static char dtype = 'E', cdc = 'C', asid = 'A', midi = 'M', wusb = 'W';
static int sid_pause = 0;

/* WebUSB Vars */

enum
{
  VENDOR_REQUEST_WEBUSB = 1,
  VENDOR_REQUEST_MICROSOFT = 2
};

extern uint8_t const desc_ms_os_20[];

#define URL  "github.com/LouDnl/USBSID-Pico"

static const tusb_desc_webusb_url_t desc_url =
{
  .bLength         = 3 + sizeof(URL) - 1,
  .bDescriptorType = 3, // WEBUSB URL type
  .bScheme         = 1, // 0: http, 1: https
  .url             = URL
};

static bool web_serial_connected = false;


/* Buffers */

/* Incoming USB data buffer
 *
 * 4 bytes
 * Byte 0 ~ command byte
 * Byte 1 ~ address range byte
 * Byte 2 ~ address destination byte
 * Byte 3 ~ data byte
 *
 */
static uint8_t buffer[4];

/* Outgoing USB data buffer
 *
 * 1 byte:
 * byte 0 : value to return
 */
static uint8_t data[1];


/* Setup */

/* Initialize debug logging if enabled
 * WARNING: any debug logging other then SID memory
 * _WILL_ slow down SID play
 */
void init_logging(void);
/* Init 1MHz square wave output */
void square_wave(void);
/* Detect clock signal */
int detect_clock(void);


/* Buffer tasks */

/* Process received cdc data */
void __not_in_flash_func(handle_buffer_task)(void);
/* Process received Sysex ASID data */
// void handle_asidbuffer_task(uint8_t a, uint8_t d);


/* IO Tasks */

/* Read from host to device */
void __not_in_flash_func(read_task)(void);
/* Write from device to host */
void __not_in_flash_func(write_task)(void);


/* LED tasks */

/* It goes up and it goes down */
void led_vuemeter_task(void);
/* Mouth breather! */
void led_breathe_task(void);


/* Supporting functions */

/* Address conversion */
uint16_t __not_in_flash_func(sid_address)(uint16_t addr);
/* Map n in range x to range y
 * Source: https://stackoverflow.com/a/61000581 */
long map(long x, long in_min, long in_max, long out_min, long out_max);


/* Device callbacks */

/* Invoked when device is mounted */
void tud_mount_cb(void);
/* Invoked when device is unmounted */
void tud_umount_cb(void);
/* Invoked when usb bus is resumed */
void tud_suspend_cb(bool remote_wakeup_en);
/* Invoked when usb bus is suspended
 * remote_wakeup_en : if host allow us  to perform remote wakeup
 * Within 7ms, device must draw an average of current less than 2.5 mA from bus
 */
void tud_resume_cb(void);


/* CDC callbacks */

/* Invoked when new data is received from tud_task() */
void tud_cdc_rx_cb(uint8_t itf);
/* Invoked when received `wanted_char` */
void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char);
/* Invoked when last rx transfer is completed */
void tud_cdc_tx_complete_cb(uint8_t itf);
/* Invoked when line state DTR & RTS are changed via SET_CONTROL_LINE_STATE */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts);
/* Invoked when line coding is change via SET_LINE_CODING */
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding);
/* Invoked when received send break */
void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms);


/* WebUSB Vendor callbacks */

/* Invoked when received control request with VENDOR TYPE */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);

#ifdef __cplusplus
    }
#endif

#endif /* _USBSID_H_ */
