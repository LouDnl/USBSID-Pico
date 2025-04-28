/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * gpio.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024-2025 LouD
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

#ifndef _USBSIDGPIO_H_
#define _USBSIDGPIO_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Default includes */
#include <stdint.h>
#include <stdio.h>

/* Pico libs */
#include "pico/stdlib.h"
#include "pico/types.h"

/* Pico W devices use a GPIO on the WIFI chip for the LED,
 * so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined */
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

/* Hardware api's */
#include "hardware/gpio.h"   /* General Purpose Input/Output (GPIO) API */
#include "hardware/pio.h"    /* Programmable I/O (PIO) API */
#include "hardware/dma.h"    /* DMA Controller API */
#include "hardware/pwm.h"    /* Hardware Pulse Width Modulation (PWM) API */
#include "hardware/irq.h"    /* Hardware interrupt handling */
#include "hardware/structs/iobank0.h"

/* PIO */
#include "bus_control.pio.h" /* Busje komt zo! */
#include "clock.pio.h"       /* TikTak */
#include "vu.pio.h"          /* Kiem em goan! */
#if defined(USE_RGB)
#include "vu_rgb.pio.h"      /* Ik zie regenbogen! */
#endif

/* PIO & Statemachine usage
 * PIO0
 * SM0: SID clock
 * SM1: Control bus
 * SM2: Data bus
 * SM3: Delay timer
 * PIO1
 * SM0: RGB LED control
 * SM1: LED PWM control
 */

/* Uart */
#define BAUD_RATE 115200
#define TX 16  /* uart 0 tx */
#define RX 17  /* uart 0 rx */

/* Data bus ~ output/input */
#define D0 0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7

/* Address bus ~ output only */
#define A0 8
#define A1 9
#define A2 10
#define A3 11
#define A4 12
#define A5 13  /* $D420+ or FM on SKPico */

/* IO bus */
#define RES 18  /* Reset */
#define RW  19  /* Read/ Write enable */
#define CS1 20  /* Chip Select for 1 or 1 & 2 with SKPico */
#define CS2 21  /* Chip Select for 2 or 3 & 4 with SKPico */
#define PHI 22  /* Pico 1Mhz PWM out ~ External Clock In */

/* Audio switch (v1.3+ only) */
#if defined(HAS_AUDIOSWITCH)
#define AU_SW 15
#endif

/* LED */
#if defined(PICO_DEFAULT_LED_PIN)
#define BUILTIN_LED PICO_DEFAULT_LED_PIN  /* 25 */
#elif defined(CYW43_WL_GPIO_LED_PIN)
#define BUILTIN_LED CYW43_WL_GPIO_LED_PIN  /* Warning, No PWM available on LED! */
#endif

#if defined(USE_RGB)
#define WS2812_PIN 23  /* Only available on black clones with RGB LED onboard! */
#endif

/* Unused */
#define NIL0 14
#ifndef HAS_AUDIOSWITCH
#define NIL1 15
#endif
#define NIL2 26
#define NIL3 27
#define NIL4 28

/* Masks */
#define PIO_PINDIRMASK 0x3C3FFF  /* 0b00000000001111000011111111111111 18 GPIO pins */

/* Util */
#define bPIN(P) (1 << P)
#define sPIN(P) { sio_hw->gpio_set  = (1 << P); }
#define cPIN(P) { sio_hw->gpio_clr  = (1 << P); }
#define tPIN(P) { sio_hw->gpio_togl = (1 << P); }

/* IRQ's */
#define PIO_IRQ0 0
#define PIO_IRQ1 1


#ifdef __cplusplus
  }
#endif

#endif /* _USBSIDGPIO_H_ */
