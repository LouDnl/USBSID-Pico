/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * logging.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
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

#ifndef _USBSID_LOGGING_H_
#define _USBSID_LOGGING_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif


/* Logging to USB uart */

#ifdef USB_PRINTF
/* TinyUSB libs */
#if __has_include("bsp/board_api.h") /* Needed to account for update in tinyUSB */
#include "bsp/board_api.h"
#else
#include "bsp/board.h"               /* Tiny USB Boards Abstraction Layer */
#endif
/* Sourced from:
 * https://github.com/hathach/tinyusb/blob/eca025f7143141fd2bc99a94619c62a6fd666f28/examples/dual/host_info_to_device_cdc/src/main.c
 */
#define cdc_printf(...)              \
do {                                 \
  char _tempbuf[1024];               \
  sprintf(_tempbuf, __VA_ARGS__);    \
  uint32_t count = sprintf(_tempbuf, __VA_ARGS__); \
  for (int i = 0; i < count; i ++) tud_cdc_n_write_char(1, _tempbuf[i]); \
  tud_cdc_n_write_flush(1);          \
  tud_task_ext(0, true);             \
} while(0)
#define _US_DBG(...) cdc_printf(__VA_ARGS__)
#else
#define _US_DBG(...) printf(__VA_ARGS__)
#endif


/* Logging macro's */

#ifdef MEM_DEBUG
#define MDBG(...) _US_DBG(__VA_ARGS__)
#else
#define MDBG(...) ((void)0)
#endif

#ifdef USBSID_DEBUG
#define DBG(...) _US_DBG(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

#ifdef USBIO_DEBUG
#define IODBG(...) _US_DBG(__VA_ARGS__)
#else
#define IODBG(...) ((void)0)
#endif

#ifdef CONFIG_DEBUG
#define CFG(...) _US_DBG(__VA_ARGS__)
#else
#define CFG(...) ((void)0)
#endif

#ifdef USBSIDGPIO_DEBUG
#define GPIODBG(...) _US_DBG(__VA_ARGS__)
#else
#define GPIODBG(...) ((void)0)
#endif

#ifdef MIDI_DEBUG
#define MIDBG(...) _US_DBG(__VA_ARGS__)
#else
#define MIDBG(...) ((void)0)
#endif

#ifdef MIDIVOICE_DEBUG
#define MVDBG(...) _US_DBG(__VA_ARGS__)
#else
#define MVDBG(...) ((void)0)
#endif


/* Global logging queues */

#ifdef WRITE_DEBUG
#include "pico/util/queue.h"  /* Inter core queue */
extern queue_t logging_queue;
typedef struct {
  /* Handles logging from Core 2 */
  char dtype;
  int n;
  uint8_t reg;
  uint8_t val;
  uint16_t cycles;
} writelogging_queue_entry_t;
#define write_to_q(a,b,c,d,e) \
do {                    \
  writelogging_queue_entry_t l_entry = {a,b,c,d,e}; \
  queue_try_add(&logging_queue, &l_entry); \
} while(0)
#define WRITEDBG(...) write_to_q(__VA_ARGS__)
#else
#define WRITEDBG(...) ((void)0)
#endif


#ifdef __cplusplus
}
#endif

#endif /* _USBSID_LOGGING_H_ */
