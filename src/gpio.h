/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * gpio.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024 LouD
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

#ifndef _SIDGPIO_H_
#define _SIDGPIO_H_
#pragma once


#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/types.h"
#include "hardware/structs/sio.h"  /* Pico SIO structs */
#include "hardware/pwm.h"          /* Hardware Pulse Width Modulation (PWM) API */


/* Uart */
#define TX 16    /* uart 0 tx */
#define RX 17    /* uart 0 rx */

/* IO pins */
#define RES 18   /* Reset */
#define RW  19   /* Read/ Write enable */
#define CS  20   /* Chip Select for 1 or 1 & 2 with SKPico */
#define CS2 21   /* Chip Select for 2 or 3 & 4 with SKPico */
#define PHI 22   /* Pico 1Mhz PWM out ~ External Clock In */

/* Address pins ~ output only */
#define A0 8
#define A1 9
#define A2 10
#define A3 11
#define A4 12
#define A5 13  /* $D420+ or FM on SKPico */
#define A8 14  /* $D500. $DE00, $DF00 or external CS on SKPico */

/* Data pins ~ output/input */
#define D0 0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7

/* Other */
#define BUILTIN_LED PICO_DEFAULT_LED_PIN  /* 25 */
#define WS2812_PIN 23

/* Unused */
#define NIL1 15
#define NIL2 26
#define NIL3 27
#define NIL4 28

#define bPIN(i) ( 1 << i )
#define bADDR 8
#define bDATA 0

#define BUS_PINMASK      0x3FFFFF  /* 0b00000000001111111111111111111111 21 GPIO pins */
#define ADDRPINS_PINMASK 0x7F00    /* 0b0111111100000000 */
#define DATAPINS_PINMASK 0xFF      /* 0b0000000011111111 */
#define INIT_MASK (bPIN(RW) | bPIN(CS) | bPIN(CS2) | bPIN(RES))
#define CS_MASK (bPIN(CS) | bPIN(CS2))
#define RW_MASK bPIN(RW)
#define CSRW_MASK (bPIN(RW) | bPIN(CS) | bPIN(CS2))

#define A8_MASK    0x100  /* 0b100000000 */
#define A5_MASK    0x20   /*  0b00100000 */

#define ADDR_MASK  (0x1F | A5_MASK)   /*  0b00111111 */
#define DATA_MASK  0xFF               /*  0b11111111 */

/* The following 4 lines are a direct copy (var naming changed) from frenetic his SKPico code */
#define bPHI bPIN( PHI )
#define VIC_HALF_CYCLE( b )	( !( (b) & bPHI ) )  /* low */
#define CPU_HALF_CYCLE( b )	(  ( (b) & bPHI ) )  /* high */
#define WAIT_FOR_VIC_HALF_CYCLE { do { b = *BUSState; } while ( !( VIC_HALF_CYCLE( b ) ) ); }  /* while !0 == Falling edge*/
#define WAIT_FOR_CPU_HALF_CYCLE { do { b = *BUSState; } while ( !( CPU_HALF_CYCLE( b ) ) ); }  /* while !1 == Rising edge*/
/* https://github.com/frntc/SIDKick-pico */

/* Pins 0->20 (except TX and RX) to sio and output */
void initPins(void);
/* Clears and de-inits all pins */
void resetPins(void);
/* Sets the address and databus pins to LOW */
void clearBus(void);
/* Pauses any operation by cetting CS high */
void pauseSID(void);
/* Reset the SID and clears the bus */
void resetSID(void);
/* Enable the SID by setting RES to HIGH */
/* Enable the SID by setting RW to LOW and CS & RES to HIGH */
void enableSID(void);
/* Disable the SID by setting RES to LOW */
/* Disable the SID by setting RW & RES to LOW and CS to HIGH*/
void disableSID(void);
/* Read  val at SID addr hex value */
uint8_t readSID(uint16_t addr);
/* Write val to sid at addr hex value */
void writeSID(uint16_t addr, uint8_t val);

#ifdef __cplusplus
 }
#endif

#endif /* _SIDGPIO_H_ */
