/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * tusb_config.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
  extern "C" {
#endif


//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

#define CFG_TUSB_MCU OPT_MCU_RP2040

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED  /* Boo! Can't use HIGH speed 😢 */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | BOARD_TUD_MAX_SPEED)  /* High speed with TinyUSB is not available for rp2040*/
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0 /* 3 == VERBOSE LOGGING */
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION  __attribute__ ((section(".usb_ram")))
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif

#define CFG_TUD_TASK_QUEUE_SZ   100  /* WHAT DOES THIS BUTTON DO!? */

//------------- CLASS -------------//
#ifdef USB_PRINTF
#define CFG_TUD_CDC              2
#else
#define CFG_TUD_CDC              1
#endif
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             1
#define CFG_TUD_VENDOR           1

// CDC Endpoint transfer buffer size, more is faster
#define CFG_TUD_CDC_EP_BUFSIZE    64  // Even at 512KB only 64KB will be used because of TUD_OPT_FULL_SPEED, Pico is only USB2.0

// CDC FIFO size of TX and RX
#define CFG_TUD_CDC_RX_BUFSIZE    64  // Even at 512KB only 64KB will be used because of TUD_OPT_FULL_SPEED, Pico is only USB2.0
#define CFG_TUD_CDC_TX_BUFSIZE    64  // Even at 512KB only 64KB will be used because of TUD_OPT_FULL_SPEED, Pico is only USB2.0

// MIDI FIFO size of TX and RX
#define CFG_TUD_MIDI_RX_BUFSIZE   64  // Even at 512KB only 64KB will be used because of TUD_OPT_FULL_SPEED, Pico is only USB2.0
#define CFG_TUD_MIDI_TX_BUFSIZE   64  // Even at 512KB only 64KB will be used because of TUD_OPT_FULL_SPEED, Pico is only USB2.0

// Vendor FIFO size of TX and RX
// If not configured vendor endpoints will not be buffered
#define CFG_TUD_VENDOR_RX_BUFSIZE 0  /* Set to 0 to disable buffering (the fifo is not used) */
#define CFG_TUD_VENDOR_TX_BUFSIZE 0  /* Set to 0 to disable buffering (the fifo is not used) */


#ifdef __cplusplus
  }
#endif

#endif /* _TUSB_CONFIG_H_ */
