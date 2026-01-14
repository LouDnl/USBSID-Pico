/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * pio.h
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

#ifndef _USBSID_PIO_H_
#define _USBSID_PIO_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Pico W devices use a GPIO on the WIFI chip for the LED,
 * so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined */
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif


/* PIO */
#include "bus_control.pio.h"   /* Busje komt zo! */
#include "clock_counter.pio.h" /* !<(O.O)>! */
#include "clock.pio.h"         /* TikTak */
#include "vu.pio.h"            /* Kiem em goan! */
#if defined(USE_RGB)
#include "vu_rgb.pio.h"        /* Ik zie regenbogen! */
#endif


/* PIO & Statemachine usage
 * PIO0
 * SM0: PHI1 SID clock
 * SM1: Control bus
 * SM2: Data bus
 * SM3: Delay timer
 * PIO1
 * SM0: RGB LED control
 * SM1: LED PWM control
 * SM2: Uart RX
 * SM3: PHI1 Cycle counter
 *
 * rp2350 only!
 * PIO2
 * SM0: SID2 clock
 * SM1: SID2 Control bus
 * SM2: SID2 Data bus
 * SM3: PHI2 Cycle counter
 */


/* IRQ's */
#define PIO_IRQ0 0
#define PIO_IRQ1 1


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_PIO_H_ */
