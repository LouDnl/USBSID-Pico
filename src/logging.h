/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
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


/* Binary printing */
#include "macros.h"


#ifdef MEM_DEBUG
#define MDBG(...) printf(__VA_ARGS__)
#else
#define MDBG(...) ((void)0)
#endif

#ifdef USBSID_DEBUG
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

#ifdef USBIO_DEBUG
#define IODBG(...) printf(__VA_ARGS__)
#else
#define IODBG(...) ((void)0)
#endif

#ifdef CONFIG_DEBUG
#define CFG(...) printf(__VA_ARGS__)
#else
#define CFG(...) ((void)0)
#endif

#ifdef USBSIDGPIO_DEBUG
#define GPIODBG(...) printf(__VA_ARGS__)
#else
#define GPIODBG(...) ((void)0)
#endif

#ifdef MIDI_DEBUG
#define MIDBG(...) printf(__VA_ARGS__)
#else
#define MIDBG(...) ((void)0)
#endif

#ifdef MIDIVOICE_DEBUG
#define MVDBG(...) printf(__VA_ARGS__)
#else
#define MVDBG(...) ((void)0)
#endif


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_LOGGING_H_ */
