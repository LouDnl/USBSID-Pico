/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * gpio.h
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

#ifndef _USBSID_GPIO_H_
#define _USBSID_GPIO_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Functions from gpio.c */
void    init_vccvdd_control(void);
void    init_bus_control(void);
void    init_audio_switch(void);
int     detect_clocksignal(void);
void    toggle_audio_switch(void);
void    set_audio_switch(bool state);
void    set_SID5v_state(bool state);
void    set_SIDhv_state(bool state);
void    set_SID1_highvoltage(bool state);
void    set_SID2_highvoltage(bool state);
uint8_t get_pin_states(void);
void    voltage_state_on(void);
void    voltage_state_off(void);
void    set_base_voltages(uint16_t wait_ms);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_GPIO_H_ */
