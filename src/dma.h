/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * dma.h
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

#ifndef _USBSID_DMA_H_
#define _USBSID_DMA_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>


/* DMA channel indices from dma.c */
extern int dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay;
extern int dma_counter;
#if PICO_RP2040
extern int dma_counter_chain;
#endif
extern volatile uint32_t cycle_count_word;

/* LED DMA channels (non-WiFi boards only) */
#if defined(PICO_DEFAULT_LED_PIN)
extern int dma_pwmled;
extern volatile int pwm_value;
#if defined(USE_RGB)
extern int dma_rgbled;
extern volatile uint32_t rgb_value;
#endif
#endif

/* Functions from dma.c */
void setup_dmachannels(void);
void setup_vu_dma(void);
void stop_dma_channels(void);
void start_dma_channels(void);
void unclaim_dma_channels(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_DMA_H_ */
