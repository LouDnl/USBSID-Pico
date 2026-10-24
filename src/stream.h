/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * stream.h
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

#ifndef _USBSID_STREAM_H_
#define _USBSID_STREAM_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#ifdef USE_SID_STREAM

/* Default includes */
#include <stdint.h>
#include <stdbool.h>

/* TinyUSB */
#include "tusb.h"


/* SID memory stream on its own vendor interface (STREAM_ITF_NUM)
 *
 * Control: vendor requests on EP0, any recipient, wIndex is ignored
 *   STREAM_REQUEST_START  OUT, wLength 0 (defaults) or 4:
 *                         [0-1] rate Hz LE (0 = default), [2] SID mask
 *                         (bit n = SID n+1, 0 = all), [3] reserved,
 *                         ignored
 *   STREAM_REQUEST_STOP   no data stage
 *   STREAM_REQUEST_INFO   IN, 16 bytes, see stream_info()
 *
 * Interface: own TinyUSB class driver (usbd_app_driver_get_cb), the USB
 * IRQ starts the following frame on transfer complete. SET_INTERFACE resets
 * the endpoints to DATA0 and stops the stream.
 *
 * Data: one 63 byte packet per enabled SID per tick on the bulk IN
 * endpoint, SID order, all frames of a tick queued at once. The sequence
 * advances on every elapsed tick: a gap means ticks missed (core0 busy) or
 * dropped (frame ring full while the host does not read), both counted as
 * dropped. 63 bytes: a short packet, no zero length packet
 *   [0]      STREAM_FRAME_SID
 *   [1]      bits 0-1 SID index, bit 6 bus owned by a non USB owner,
 *            other bits 0
 *   [2-3]    tick sequence LE, shared by all frames of one tick, a gap
 *            means skipped ticks
 *   [4-7]    PHI1 cycle counter at snapshot LE
 *   [8-11]   main interface packets processed LE
 *   [12-43]  32 SID registers: $00-$18 last written value ($00-$18 of a
 *            clone also last read value), $19-$1C last read value
 *   [44-47]  reserved, 0
 *   [48]     session, incremented by every STREAM_REQUEST_START
 *   [49-62]  reserved, 0
 */
enum {
  STREAM_REQUEST_START = 0x10,
  STREAM_REQUEST_STOP  = 0x11,
  STREAM_REQUEST_INFO  = 0x12,
};

#define STREAM_FORMAT_VERSION 1
#define STREAM_FRAME_SIZE     63
#define STREAM_FRAME_SID      0x53
#define STREAM_START_LEN      4
#define STREAM_INFO_LEN       16
#define STREAM_DEFAULT_RATE   50
#define STREAM_MAX_RATE       1000

/* frame[1] flag bits */
#define STREAM_FLAG_OTHER_OWNER 0x40


/* Variables from stream.c */
extern volatile uint32_t stream_rx_count;

/* Functions from stream.c */
bool stream_is_request(tusb_control_request_t const * request);
bool stream_control_xfer(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request);
void stream_stop(void);
void stream_task(void);

#endif /* USE_SID_STREAM */


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_STREAM_H_ */
