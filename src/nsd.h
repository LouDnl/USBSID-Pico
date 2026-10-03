/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * nsd.h
 * Network SID Device protocol state machine
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

#ifndef _USBSID_NSD_H_
#define _USBSID_NSD_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>
#include <stdbool.h>

/* NSD transport abstraction. `send` runs on whichever core owns the
 * transport (WiFi and Bluetooth SPP both core 0); never called from
 * `nsd_drain_task` (core 1). */
typedef struct nsd_transport {
  void (*send)(void *ctx, const uint8_t *data, uint16_t len);
  void (*close)(void *ctx);
  void *ctx;
} nsd_transport_t;

/* NSD command IDs */
typedef enum {
  NSD_CMD_FLUSH = 0,
  NSD_CMD_TRY_SET_SID_COUNT = 1,
  NSD_CMD_MUTE = 2,
  NSD_CMD_TRY_RESET = 3,
  NSD_CMD_TRY_DELAY = 4,
  NSD_CMD_TRY_WRITE = 5,
  NSD_CMD_TRY_READ = 6,
  NSD_CMD_GET_VERSION = 7,
  NSD_CMD_TRY_SET_SAMPLING = 8,
  NSD_CMD_TRY_SET_CLOCK = 9,
  NSD_CMD_GET_CONFIG_COUNT = 10,
  NSD_CMD_GET_CONFIG_INFO = 11,
  NSD_CMD_SET_SID_POSITION = 12,
  NSD_CMD_SET_SID_LEVEL = 13,
  NSD_CMD_TRY_SET_SID_MODEL = 14,
  NSD_CMD_SET_DELAY = 15,
  NSD_CMD_SET_FADE_IN = 16,
  NSD_CMD_SET_FADE_OUT = 17,
  NSD_CMD_SET_SID_HEADER = 18,
  /* v5: 16-bit register address, up to 16 SIDs (0x0000-0x01ff). TRY_WRITE_EX
   * also takes FM OPL (0xdf00-0xdfff); TRY_READ_EX's own read target does
   * not (spec dropped it - a read there returns READ followed by 0x00, same
   * as an unconfigured SID). Its leading write-then-read entries still take
   * the full TRY_WRITE_EX range, since they reuse that command's own wire
   * format. See handle_try_write_ex()/handle_try_read_ex() in nsd.c. */
  NSD_CMD_TRY_WRITE_EX = 19,
  NSD_CMD_TRY_READ_EX = 20,
  /* Enable/disable FM OPL for the current connection (spec: sid number is
   * ignored, payload is exactly one byte, {0,1}). Which physical SID socket
   * answers as FM OPL is a board-owner decision (BOARD_FMOPL, config.c /
   * config_socket.c's set_fmopl_sidno()), not something this command
   * changes - it only gates whatever socket is already configured. See
   * nsd_fmopl_active in nsd.c. */
  NSD_CMD_TRY_SET_FM_OPL = 21
} nsd_command_t;

/* NSD response codes */
typedef enum {
  NSD_RESP_OK = 0,
  NSD_RESP_BUSY = 1,
  NSD_RESP_ERROR = 2,
  NSD_RESP_READ = 3,
  NSD_RESP_VERSION = 4,
  NSD_RESP_COUNT = 5,
  NSD_RESP_INFO = 6
} nsd_response_code_t;

/* NSD protocol version reported by GET_VERSION. 5 adds TRY_WRITE_EX/
 * TRY_READ_EX and TRY_SET_FM_OPL, see nsd_command_t above. */
#define NSD_PROTOCOL_VERSION 5

/* Backpressure thresholds, in PHI1 clock cycles of queued delay, applied to
 * the write ring in net_ring.c/.h. High/low form a hysteresis band so BUSY
 * doesn't chatter once the ring is near full. */
#define NSD_CYCLES_HIGH  (63 * 312 * 3)  /* ~59k cycles, ~60 ms PAL -> report BUSY */
#define NSD_CYCLES_LOW   (63 * 312)      /* ~20 ms PAL -> accept again (hysteresis) */
#define NSD_ENTRIES_MIN  256             /* free-entry floor -> report BUSY */

/* Function prototypes */
void     nsd_init(void);
bool     nsd_session_open(const nsd_transport_t *t);   /* false if a session is already live */
bool     nsd_session_is_active(void);
uint16_t nsd_feed(const uint8_t *data, uint16_t len);  /* returns bytes consumed */
void     nsd_session_close(void);
void     nsd_drain_task(void);                         /* core 1 */

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_NSD_H_ */
