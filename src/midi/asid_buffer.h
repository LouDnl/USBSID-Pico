/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * asid_buffer.h
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

#ifndef _USBSID_ASID_BUFFER_H_
#define _USBSID_ASID_BUFFER_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>


/* Functions from asid_buffer.c */
uint32_t track_asid_arrival(void);
void     adjust_buffer_rate_dynamic(uint32_t target_rate);
void     reset_arrival_tracking(void);
void     ring_buffer_reset_size(void);
void     set_buffer_rate(uint16_t rate);
void     update_sid_count(uint8_t sid_num);
void     init_buffer_pio(void);
void     stop_buffer_pio(void);
void     asid_ring_write(uint8_t reg, uint8_t val, uint16_t c);
void     asid_ring_init(void);
void     asid_ring_deinit(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_ASID_BUFFER_H_ */
