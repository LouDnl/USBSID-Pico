/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_cloneconfig.h
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

#ifndef _USBSID_SID_CLONECONFIG_H_
#define _USBSID_SID_CLONECONFIG_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>
#include <stdbool.h>


/* Functions from sid_cloneconfig.c */
void    read_fpgasid_configuration(uint8_t base_address);
void    read_skpico_configuration(uint8_t base_address, uint8_t profile);
void    reset_switch_pdsid_type(void);
uint8_t read_pdsid_sid_type(uint8_t base_address);
bool    set_pdsid_sid_type(uint8_t base_address, uint8_t type);
void    print_backsid_filter_type(uint8_t base_address);
void    print_backsid_version(uint8_t base_address);
void    set_backsid_filter_type(uint8_t base_address, uint8_t type);
bool    detect_armsid(uint8_t base_address);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_CLONECONFIG_H_ */
