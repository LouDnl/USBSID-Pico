/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * uart.h
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

#ifndef _USBSID_UART_H_
#define _USBSID_UART_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#ifdef USE_PIO_UART


/* Default includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pico libs */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/async_context_threadsafe_background.h"

/* Pico hardware api's */
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/uart.h"

/* PIO */
#include "uart_rx.pio.h"


/* What am I doing here? */
#define UART_FIFO_SIZE 65535 /* TODO: Verify size */


#endif /* USE_PIO_UART */

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_UART_H_ */
