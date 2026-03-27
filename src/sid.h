/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid.h
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

#ifndef _USBSID_SID_H_
#define _USBSID_SID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>
#include <stdbool.h>


/* Global test queue struct used by sid_tests and usbsid */
typedef struct {
  void (*func)(int,char,char);
  int s;
  char t;
  char wf;
} sidtest_queue_entry_t;

/* Functions from sid.c */
void init_sid_chips(void);
void clear_sid_memory(void);
void clear_volume_state(void);
void set_reset_state(bool state);
bool get_reset_state(void);
void set_paused_state(bool state);
bool get_paused_state(void);
void set_muted_state(bool state);
bool get_muted_state(void);
void unmute_sid(void);
void mute_sid(void);
void enable_sid(bool unmute);
void disable_sid(void);
void clear_bus(int sidno);
void clear_bus_all(void);
void pause_sid(void);
void pause_sid_withmute(void);
void reset_sid(void);
void clear_sid_registers(int sidno);
void reset_sid_registers(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_H_ */
