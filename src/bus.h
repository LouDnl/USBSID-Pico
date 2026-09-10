/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * bus.h
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

#ifndef _USBSID_BUS_H_
#define _USBSID_BUS_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>


/* Timing-critical SID bus operations from bus.c
 * NOTE: These functions run from RAM (not flash) via __no_inline_not_in_flash_func
 */
uint16_t cycled_delay_operation(uint16_t cycles);
uint16_t cycled_delayed_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
void     cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
uint8_t  cycled_read_operation(uint8_t address, uint16_t cycles);

/* Bus ownership traffic controller for multi-transport access */
typedef enum {
  BUS_OWNER_NONE = 0,
  BUS_OWNER_USB,
  BUS_OWNER_NET,
  BUS_OWNER_PLAYER,
  BUS_OWNER_MIDI
} bus_owner_t;


/* Functions from bus.c */
void     bus_lock_init(void);
void     restart_bus(void);
int      bus_drain(void);
void     bus_resync(void);
uint32_t clockcycles(void);
void     clockcycle_delay(uint32_t n_cycles);
bool    bus_try_claim(bus_owner_t who);  /* true if `who` now owns the bus */
void    bus_touch(bus_owner_t who);      /* refresh idle timer */
void    bus_release(bus_owner_t who);
bus_owner_t bus_current_owner(void);

/* Heavy-operation exclusion, separate from the ownership arbiter above -
 * bus_current_owner() can't be reused since BUS_OWNER_USB never releases.
 * Dedicated flag set only around apply_clockrate(), sid_auto_detect() and
 * reset_sid()/reset_sid_registers(), which corrupt concurrent cyw43
 * SPI/DMA activity; see usbsid.c's core 0 loop for the matching
 * cyw43_arch_poll()/net_wifi_update() skip. */
void    bus_heavy_op_begin(void);
void    bus_heavy_op_end(void);
bool    bus_heavy_op_active(void);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_BUS_H_ */
