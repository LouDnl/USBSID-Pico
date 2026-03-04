/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_detection.h
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

#ifndef _USBSID_SID_DETECTION_H_
#define _USBSID_SID_DETECTION_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>
#include <stdbool.h>

/* Project includes required for this header */
#include <usbsid_constants.h>


/* Functions from sid_detection.c */
ConfigError sid_auto_detect(bool at_boot);
ConfigError sid_auto_detect_silent(void);
ChipType    detect_chiptype_at(uint8_t base_address);
SIDType     detect_sidtype_at(uint8_t base_address, uint8_t chiptype);
bool        detect_fmopl(uint8_t base_address);
void        auto_detect_routine(void);
uint8_t     detect_sid_model(uint8_t start_addr);
uint8_t     detect_sid_version(uint8_t start_addr);
uint8_t     detect_sid_reflex(uint8_t start_addr);
uint8_t     detect_sid_version_skpico_deprecated(uint8_t start_addr);
bool        detect_pdsid(uint8_t base_address, bool silent);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_DETECTION_H_ */
