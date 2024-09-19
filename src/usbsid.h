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


#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/types.h"      /* absolute_time_t */
// #define __STDC_FORMAT_MACROS
// #include <inttypes.h>
#include "pico/multicore.h"  /* Multicore */
#include "hardware/pio.h"    /* Programmable I/O (PIO) API */
#include "hardware/clocks.h"
// #include "hardware/irq.h"    /* Hardware interrupt handling */
// #include "hardware/adc.h"    /* Analog to Digital Converter (ADC) API */

#include "bsp/board.h"       /* Tiny USB Boards Abstraction Layer */
#include "tusb.h"            /* Tiny USB stack */
#include "tusb_config.h"     /* Tiny USB configuration */

#include "clock.pio.h"       /* Square wave generator */
#if defined(USE_RGB)
#include "ws2812.pio.h"      /* RGB led handler */
#endif

/* Debugging enabling overrides */
/* #define USBSID_UART */
/* #define MEM_DEBUG */
/* #define USBSID_DEBUG */
/* #define ADDR_DEBUG */
/* #define USBIO_DEBUG */
/* #define ASID_DEBUG */
/* #define USBSIDGPIO_DEBUG */

#include "mcu.h"   /* rp2040 mcu */
#include "gpio.h"  /* GPIO configuration */
#include "midi.h"  /* Midi */
#include "asid.h"  /* ASID */
#include "sid.h"   /* SID */

#ifdef __cplusplus
    extern "C" {
#endif


/* SIDTYPES ~ Defaults to 0
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
#ifndef SIDTYPES
#define SIDTYPES SIDTYPE0
#endif

/* SID type masks for GPIO */
#define SIDUMASK 0xFF00
#define SIDLMASK 0xFF
#if SIDTYPES == SIDTYPE0
#define NUMSIDS  1
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0x0
#define SID2MASK 0x0
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPES == SIDTYPE1
#define NUMSIDS  2
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 ~ OPTIONAL */
#define SID2MASK 0x3F
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPES == SIDTYPE2
#define NUMSIDS  2
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD440  /* SID 2 - socket 2 */
#define SID2MASK 0x5F
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPES == SIDTYPE3
#define NUMSIDS  3
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 */
#define SID2MASK 0x3F
#define SID3ADDR 0xD440  /* SID 3 - socket 2 */
#define SID3MASK 0x5F
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPES == SIDTYPE4  /* SKPico only */
#define NUMSIDS  4
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 */
#define SID2MASK 0x3F
#define SID3ADDR 0xD440  /* SID 3 - socket 2 */
#define SID3MASK 0x5F
#define SID4ADDR 0xD460  /* SID 4 - socket 2 */
#define SID4MASK 0x7F
#elif SIDTYPES == SIDTYPE5  /* SKPico only */
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


/* Global vars */

enum
{
  WRITE = 0,
  READ,
  PAUSE,
  RESET_SID,
  RESET_USB
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

extern uint8_t memory[65536];
extern uint16_t vue;
static uint32_t breathe_interval_ms = BREATHE_ON;
static uint32_t read = 0, write = 0;
static intptr_t usbdata = 0, pwm_value = 0, updown = 1;
static uintptr_t pause, reset, rw, addr, val;
static char dtype = 'E', cdc = 'C', asid = 'A', midi = 'M', wusb = 'W';

// static uint64_t read_uS, write_uS;

#if defined(USE_RGB)  /* RGB LED */

#define IS_RGBW false
#define BRIGHTNESS 127  /* Half of max brightness */

static bool rgb_mode;
static uint ws2812_sm;
static uint8_t r_ = 0, g_ = 0, b_ = 0;
static unsigned char o1 = 0, o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0;
static const __not_in_flash( "mydata" ) unsigned char color_LUT[ 43 ][ 6 ][ 3 ] =
{
  /* Red Green Blue Yellow Cyan Purple*/
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{6, 0, 0}, {0, 6, 0}, {0, 0, 6}, {6, 6, 0}, {0, 6, 6}, {6, 0, 6}},
  {{12, 0, 0}, {0, 12, 0}, {0, 0, 12}, {12, 12, 0}, {0, 12, 12}, {12, 0, 12}},
  {{18, 0, 0}, {0, 18, 0}, {0, 0, 18}, {18, 18, 0}, {0, 18, 18}, {18, 0, 18}},
  {{24, 0, 0}, {0, 24, 0}, {0, 0, 24}, {24, 24, 0}, {0, 24, 24}, {24, 0, 24}},
  {{30, 0, 0}, {0, 30, 0}, {0, 0, 30}, {30, 30, 0}, {0, 30, 30}, {30, 0, 30}},
  {{36, 0, 0}, {0, 36, 0}, {0, 0, 36}, {36, 36, 0}, {0, 36, 36}, {36, 0, 36}},
  {{42, 0, 0}, {0, 42, 0}, {0, 0, 42}, {42, 42, 0}, {0, 42, 42}, {42, 0, 42}},
  {{48, 0, 0}, {0, 48, 0}, {0, 0, 48}, {48, 48, 0}, {0, 48, 48}, {48, 0, 48}},
  {{54, 0, 0}, {0, 54, 0}, {0, 0, 54}, {54, 54, 0}, {0, 54, 54}, {54, 0, 54}},
  {{60, 0, 0}, {0, 60, 0}, {0, 0, 60}, {60, 60, 0}, {0, 60, 60}, {60, 0, 60}},
  {{66, 0, 0}, {0, 66, 0}, {0, 0, 66}, {66, 66, 0}, {0, 66, 66}, {66, 0, 66}},
  {{72, 0, 0}, {0, 72, 0}, {0, 0, 72}, {72, 72, 0}, {0, 72, 72}, {72, 0, 72}},
  {{78, 0, 0}, {0, 78, 0}, {0, 0, 78}, {78, 78, 0}, {0, 78, 78}, {78, 0, 78}},
  {{84, 0, 0}, {0, 84, 0}, {0, 0, 84}, {84, 84, 0}, {0, 84, 84}, {84, 0, 84}},
  {{90, 0, 0}, {0, 90, 0}, {0, 0, 90}, {90, 90, 0}, {0, 90, 90}, {90, 0, 90}},
  {{96, 0, 0}, {0, 96, 0}, {0, 0, 96}, {96, 96, 0}, {0, 96, 96}, {96, 0, 96}},
  {{102, 0, 0}, {0, 102, 0}, {0, 0, 102}, {102, 102, 0}, {0, 102, 102}, {102, 0, 102}},
  {{108, 0, 0}, {0, 108, 0}, {0, 0, 108}, {108, 108, 0}, {0, 108, 108}, {108, 0, 108}},
  {{114, 0, 0}, {0, 114, 0}, {0, 0, 114}, {114, 114, 0}, {0, 114, 114}, {114, 0, 114}},
  {{120, 0, 0}, {0, 120, 0}, {0, 0, 120}, {120, 120, 0}, {0, 120, 120}, {120, 0, 120}},
  {{126, 0, 0}, {0, 126, 0}, {0, 0, 126}, {126, 126, 0}, {0, 126, 126}, {126, 0, 126}},
  {{132, 0, 0}, {0, 132, 0}, {0, 0, 132}, {132, 132, 0}, {0, 132, 132}, {132, 0, 132}},
  {{138, 0, 0}, {0, 138, 0}, {0, 0, 138}, {138, 138, 0}, {0, 138, 138}, {138, 0, 138}},
  {{144, 0, 0}, {0, 144, 0}, {0, 0, 144}, {144, 144, 0}, {0, 144, 144}, {144, 0, 144}},
  {{150, 0, 0}, {0, 150, 0}, {0, 0, 150}, {150, 150, 0}, {0, 150, 150}, {150, 0, 150}},
  {{156, 0, 0}, {0, 156, 0}, {0, 0, 156}, {156, 156, 0}, {0, 156, 156}, {156, 0, 156}},
  {{162, 0, 0}, {0, 162, 0}, {0, 0, 162}, {162, 162, 0}, {0, 162, 162}, {162, 0, 162}},
  {{168, 0, 0}, {0, 168, 0}, {0, 0, 168}, {168, 168, 0}, {0, 168, 168}, {168, 0, 168}},
  {{174, 0, 0}, {0, 174, 0}, {0, 0, 174}, {174, 174, 0}, {0, 174, 174}, {174, 0, 174}},
  {{180, 0, 0}, {0, 180, 0}, {0, 0, 180}, {180, 180, 0}, {0, 180, 180}, {180, 0, 180}},
  {{186, 0, 0}, {0, 186, 0}, {0, 0, 186}, {186, 186, 0}, {0, 186, 186}, {186, 0, 186}},
  {{192, 0, 0}, {0, 192, 0}, {0, 0, 192}, {192, 192, 0}, {0, 192, 192}, {192, 0, 192}},
  {{198, 0, 0}, {0, 198, 0}, {0, 0, 198}, {198, 198, 0}, {0, 198, 198}, {198, 0, 198}},
  {{204, 0, 0}, {0, 204, 0}, {0, 0, 204}, {204, 204, 0}, {0, 204, 204}, {204, 0, 204}},
  {{210, 0, 0}, {0, 210, 0}, {0, 0, 210}, {210, 210, 0}, {0, 210, 210}, {210, 0, 210}},
  {{216, 0, 0}, {0, 216, 0}, {0, 0, 216}, {216, 216, 0}, {0, 216, 216}, {216, 0, 216}},
  {{222, 0, 0}, {0, 222, 0}, {0, 0, 222}, {222, 222, 0}, {0, 222, 222}, {222, 0, 222}},
  {{228, 0, 0}, {0, 228, 0}, {0, 0, 228}, {228, 228, 0}, {0, 228, 228}, {228, 0, 228}},
  {{234, 0, 0}, {0, 234, 0}, {0, 0, 234}, {234, 234, 0}, {0, 234, 234}, {234, 0, 234}},
  {{240, 0, 0}, {0, 240, 0}, {0, 0, 240}, {240, 240, 0}, {0, 240, 240}, {240, 0, 240}},
  {{246, 0, 0}, {0, 246, 0}, {0, 0, 246}, {246, 246, 0}, {0, 246, 246}, {246, 0, 246}},
  {{252, 0, 0}, {0, 252, 0}, {0, 0, 252}, {252, 252, 0}, {0, 252, 252}, {252, 0, 252}}
};
#endif


/* WebUSB Vars */

enum
{
  VENDOR_REQUEST_WEBUSB = 1,
  VENDOR_REQUEST_MICROSOFT = 2
};

extern uint8_t const desc_ms_os_20[];

#define URL  "github.com/LouDnl/USBSID-Pico"

// extern const tusb_desc_webusb_url_t desc_url;

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
 * 4 byte build up MSB -> LSB:
`* Byte 0 : see enum above
 * Byte 1 : address range e.g. $D4
 * Byte 2 : address e.g. $1C
 * Byte 3 : value to write e.g. $FF
 *
 * Example MSB -> LSB:
 * 0x00D4181A
 * Byte 0 0x10 ~ 0b00010000 no reset, write mode
 * Byte 1 0xD4 ~ address range $D400 ~ $DFFF
 * Byte 2 0x18 ~ address
 * Byte 3 0x1A ~ value to write
*/
static unsigned char buffer[4];

/* Outgoing USB data buffer
 *
 * 4 bytes:
 * byte 0 : value to return
 * byte 1~3 : copies of value to return
 */
static unsigned char data[4];

/* SID read data buffer
 *
 * Exactly 1 byte with data pin values:
 * byte 0 : value read from the SID
 */
static unsigned char rdata[1];


/* Setup */

/* Initialize debug logging if enabled
 * WARNING: any debug logging other then SID memory
 * _WILL_ slow down SID play
 */
void init_logging(void);
/* Create fake 16bit memory filled with nops */
void init_memory(void);
/* Init 1MHz square wave output */
void square_wave(void);
/* Detect clock signal */
int detect_clock(void);
#if defined(USE_RGB)
/* Init the RGB LED pio */
void init_rgb(void);
/* Experimental function for detecting the SMPS
 * type on GPIO23/GPIO24 */
int detect_smps(void);
#endif


/* Buffer tasks */

/* Handle the received data */
void handle_buffer_task(unsigned char buff[4]);
/* Handle the received Sysex ASID data */
void handle_asidbuffer_task(uint8_t a, uint8_t d);


/* IO Tasks */

/* Read from host to device */
void read_task(void);
/* Write from device to host */
void write_task(void);
/* Read from host to device */
void webserial_read_task(void);
/* Write from device to host */
void webserial_write_task(void);
/* Handle incoming midi data */
void midi_task(void);


/* LED tasks */

/* It goes up and it goes down */
void led_vuemeter_task(void);
/* Mouth breather! */
void led_breathe_task(void);


/* Supporting functions */

/* Address conversion */
uint16_t sid_address(uint16_t addr);
/* Map n in range x to range y
 * Source: https://stackoverflow.com/a/61000581 */
long map(long x, long in_min, long in_max, long out_min, long out_max);
#if defined(USE_RGB)
/* Write a single RGB pixel */
void put_pixel(uint32_t pixel_grb);
/* Generate uint32_t with RGB colors */
uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b);
/* Generate RGB brightness */
uint8_t rgbb(double inp);
#endif


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

/* Invoked when received new data */
void tud_cdc_rx_cb(uint8_t itf);
/* Invoked when received `wanted_char` */
void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char);
/* Invoked when last rx transfer finished */
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
