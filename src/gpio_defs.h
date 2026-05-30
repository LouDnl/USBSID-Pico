/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * gpio_defs.h
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

#ifndef _USBSID_GPIODEFS_H_
#define _USBSID_GPIODEFS_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Uart 0 */
#define TX 16  /* uart 0 tx */
#define RX 17  /* uart 0 rx */

/* Unable to use PIO Uart on PCB v1.5+ */
#if PCB_VERSION_INT >= 15
#ifdef USE_PIO_UART
#undef USE_PIO_UART /* Undefine the use of PIO Uart */
#endif
#endif
/* PIO Uart */
#ifdef USE_PIO_UART
#define PIOUART_TX 26
#define PIOUART_RX 27
#endif

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
#define RES  18  /* Reset */
#define RW   19  /* Read/ Write enable */
#define CS1  20  /* Chip Select for 1 or 1 & 2 with Clone SID */
#define CS2  21  /* Chip Select for 2 or 3 & 4 with Clone SID */
#define PHI1 22  /* Pico 1Mhz PWM out ~ External Clock In (v1.0 only) */

/* v0.1, v0.2, v1.0 & v1.2 additional pin configuration */
#if PCB_VERSION_INT < 13
/* Unused */
#define NIL0 14
#define NIL1 15
#ifndef USE_PIO_UART
#define NIL2 26
#define NIL3 27
#endif
#define NIL4 28
/* v1.3 additional pin configuration */
#elif PCB_VERSION_INT == 13
/* Unused */
#define NIL0  14
#define AU_SW 15
#ifndef USE_PIO_UART
#define NIL2  26
#define NIL3  27
#endif
#define NIL4 28
/* v1.5+ additional pin configuration */
#elif PCB_VERSION_INT == 15
#define SIDVCC_EN 14 /* Active high, default to _off_ with pulldown */
#define AU_SW     15 /* Default on ~ stereo */
#define SIDHV_EN  26 /* Active high, default to _on_ */
#define HV1_SEL   27 /* Active high, default to _off_ */
#define HV2_SEL   28 /* Active high, default to _off_ */
#endif
/** v1.5+ Important additional information
 *
 * GPIO 14 -> SIDVCC_EN
 * on v1.4 boards when SIDVCC_EN is off SID's will still get 5v directly from USB causing issues
 * on v1.5 boards the 5v regulator is always on and SIDVCC_EN will toggle VCC input to both SID's on or off
 *
 * GPIO 26 -> SIDHV_EN
 * on v1.4 & v1.5 boards the HV regulators are default off, SIDHV_EN will toggle VCC input to both HV regulators on or off
 *
 * GPIO 27 -> HV1_SEL
 * GPIO 28 -> HV2_SEL
 * HV1_SEL & HV2_SEL respectively toggle the HV regulators to 9v (off, default) and 12v (on)
 * HV1_SEL & HV2_SEL automatically toggles the SID filter capacitors
 * 22nF  (6582 & 8580) gets toggled off when GPIO is on and vice versa
 * 470pF (6581) is always connected and gets added to the 22nF within the % deviation margin
 * HV1_SEL & HV2_SEL (off) automatically toggle the 330k digiboost resistor for 6582 & 8580 SIDs
 * HV1_SEL & HV2_SEL (on)  automatically toggle the 1k shunt resistor for 6581 SIDs
 */


/* LED */
#if defined(PICO_DEFAULT_LED_PIN)
#define BUILTIN_LED PICO_DEFAULT_LED_PIN  /* 25 */
#elif defined(CYW43_WL_GPIO_LED_PIN)
#define BUILTIN_LED CYW43_WL_GPIO_LED_PIN  /* Warning, No PWM available on LED! */
#endif

#if defined(USE_RGB)
#define WS2812_PIN 23  /* Only available on black clones with RGB LED onboard! */
#endif

/* Masks */
#define PIO_PINDIRMASK 0x3C3FFF  /* 0b00000000001111000011111111111111 18 GPIO pins */

/* Util */
#define bPIN(P) (1 << P)
#if PICO_USE_GPIO_COPROCESSOR
// #define sPIN(P) { gpio_set_mask(1 << P); } /* gpioc_lo_out_set */
// #define cPIN(P) { gpio_clr_mask(1 << P); }
// #define tPIN(P) { gpioc_lo_out_xor(1 << P); }
#define sPIN(P) { pico_default_asm_volatile ("mcr p0, #2, %0, c0, c0" : : "r" (1ul << P)); } /* gpioc_lo_out_set */
#define cPIN(P) { pico_default_asm_volatile ("mcr p0, #3, %0, c0, c0" : : "r" (1ul << P)); } /* gpio_clr_mask */
// #if RP2350
// #define tPIN(P) { pico_default_asm_volatile ("mcr p0, #5, %0, c0, c0" : : "r" (P)); } /* gpioc_hilo_out_xor(1ull << pin); */
// #else
#define tPIN(P) { pico_default_asm_volatile ("mcr p0, #1, %0, c0, c0" : : "r" (1ul << P)); } /* gpioc_lo_out_xor */
// #endif
#else
#define sPIN(P) { sio_hw->gpio_set  = (1ul << P); }
#define cPIN(P) { sio_hw->gpio_clr  = (1ul << P); }
#define tPIN(P) { sio_hw->gpio_togl = (1ul << P); } /* BUG: This _can_ cause a hardfault on rp2350 */
#endif


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_GPIODEFS_H_ */
