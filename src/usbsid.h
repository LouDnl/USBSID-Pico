/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * usbsid.h
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

#ifndef _USBSID_H_
#define _USBSID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* USB packet buffers from usbsid.c */
extern uint8_t sid_buffer[];
extern uint8_t uart_buffer[];
extern uint8_t *write_buffer_p;

/* SID register shadow memory from usbsid.c */
#if defined(ONBOARD_EMULATOR) || defined(ONBOARD_SIDPLAYER)
extern uint8_t c64memory[];
extern uint8_t *sid_memory;
#else
extern uint8_t sid_memory[];
#endif

/* USB connection state */
extern volatile int usb_connected, usbdata;
extern volatile uint32_t cdcread, cdcwrite, webread, webwrite;
extern volatile uint8_t *cdc_itf, *wusb_itf;

/* CPU and SID timing */
extern volatile double cpu_mhz, cpu_us, sid_hz, sid_mhz, sid_us;

/* Emulator flags from usbsid.c */
#if defined(ONBOARD_EMULATOR)
extern volatile bool
  emulator_running,
  starting_emulator,
  stopping_emulator;
#endif /* ONBOARD_EMULATOR */

/* SID player flags from usbsid.c */
#if defined(ONBOARD_SIDPLAYER)
extern volatile bool
  sidplayer_init,
  sidplayer_start,
  sidplayer_playing,
  sidplayer_stop,
  sidplayer_next,
  sidplayer_prev;
extern uint8_t * sidfile; /* Temporary buffer to store incoming data */
extern volatile int sidfile_size;
extern volatile char tuneno;
extern volatile bool is_prg;
#endif /* ONBOARD_SIDPLAYER */

/* Runtime flags */
extern volatile bool auto_config, offload_ledrunner;

/* Inter-core queues */
extern queue_t sidtest_queue;
extern queue_t logging_queue;

/* Functions from usbsid.c */
void cdc_write(volatile uint8_t *itf, uint32_t n);
void webserial_write(volatile uint8_t *itf, uint32_t n);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_H_ */
