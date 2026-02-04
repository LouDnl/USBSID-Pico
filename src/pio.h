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
#include "cycle_counter.pio.h" /* !<(O.O)>! */
#include "clock.pio.h"         /* TikTak */
#include "vu.pio.h"            /* Kiem em goan! */
#if defined(USE_RGB)
#include "vu_rgb.pio.h"        /* Ik zie regenbogen! */
#endif


/* NOTICE: PIO & Statemachine usage
 * PIO0
 * SM0: sm_clkcnt  - PHI1 SID clock (clock.pio / pio.c)
 * SM1: sm_control - Bus control write and data read (bus_control.pio / pio.c)
 * SM2: sm_data    - Data and address bus write (bus_control.pio / pio.c)
 * SM3: sm_delay   - Delay cycle counter interrupt for bus SM1 & SM2 (bus_control.pio / pio.c)
 * PIO1
 * SM0: Buffer raster cycle counter (ASID) (bus_control.pio / asid_buffer.c)
 * SM1: LED PWM control (Non Wifi boards only, unused otherwise) (vu.pio)
 * SM2: RGB LED control (as LED but for RGB Boards only, unused otherwise) (vu_rgb.pio)
 * SM3: PHI1 Clock cycle counter (cycle_counter.pio)
 * PIO2 (rp2350 only!)
 * SM0: Uart RX (uart_rx.pio / uart.c)
 * SM1:
 * SM2:
 * SM3:
 */

 /* NOTICE: PIO <-> DMA information
  * all pio statemachines have one or more corresponding DMA
  * configurations in dma.c where the corresponding DREQ is also set
  * PIO0
  * SM0: - no DMA use
  * SM1: dma_tx_control - DMA_SIZE_8  - DREQ_PIO0_TX1
  * SM1: dma_rx_data    - DMA_SIZE_8  - DREQ_PIO0_RX1
  * SM2: dma_tx_data    - DMA_SIZE_32 - DREQ_PIO0_TX2
  * SM3: dma_tx_delay   - DMA_SIZE_16 - DREQ_PIO0_TX3
  * PIO1
  * SM0: - no DMA use
  * SM1: dma_pwmled     - DMA_SIZE_32 - DREQ_PIO1_TX0
  * SM2: dma_rgbled     - DMA_SIZE_32 - DREQ_PIO1_TX1
  * SM3: dma_counter    - DMA_SIZE_32 - DREQ_PIO1_RX3 (dual channel on rp2040)
  * PIO2 (rp2350 only!)
  * SM0:
  * SM1:
  * SM2:
  * SM3:
  */


/* IRQ's */
#define PIO_IRQ0 0
#define PIO_IRQ1 1


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_PIO_H_ */
