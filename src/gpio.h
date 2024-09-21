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
#define BAUD_RATE 115200
#define TX 16    /* uart 0 tx */
#define RX 17    /* uart 0 rx */

/* Data pins ~ output/input */
#define D0 0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7

/* Address pins ~ output only */
#define A0 8
#define A1 9
#define A2 10
#define A3 11
#define A4 12
#define A5 13  /* $D420+ or FM on SKPico */

/* IO pins */
#define RES 18   /* Reset */
#define RW  19   /* Read/ Write enable */
#define CS1 20   /* Chip Select for 1 or 1 & 2 with SKPico */
#define CS2 21   /* Chip Select for 2 or 3 & 4 with SKPico */
#define PHI 22   /* Pico 1Mhz PWM out ~ External Clock In */

/* Other */
#define BUILTIN_LED PICO_DEFAULT_LED_PIN  /* 25 */
#define WS2812_PIN 23

/* Unused */
#define NIL0 14
#define NIL1 15
#define NIL2 26
#define NIL3 27
#define NIL4 28

#define bPIN(i) ( 1 << i )

#define PIO_PINDIRMASK   0x3C3FFF  /* 0b00000000001111000011111111111111 17 GPIO pins */

static PIO pio = pio0;
static uint sm_clock, offset_clock;
static uint sm_control, sm_data, offset_control, offset_data;
static int dma_tx_control, dma_tx_data, dma_rx_data;

static uint16_t control_word;
static uint32_t data_word, read_data, dir_mask;

/* Set up the bus pins for pio use */
void initPins(void);
/* Set up the pio bus */
void setupPioBus(void);
/* Sets the address and databus pins to LOW */
void clearBus(void);
/* Pauses any operation by cetting CS high */
void pauseSID(void);
/* Reset the SID and clears the bus */
void resetSID(void);
/* Enable the SID by setting Volume to 15 and RES to HIGH */
void enableSID(void);
/* Disable the SID by setting Volume to 0, RW to LOW and CS1/CS2 to HIGH */
void disableSID(void);
/* Perform a read or write bus operation */
uint8_t bus_operation(uint8_t command, uint8_t address, uint8_t data);

void initVUE(void);

#ifdef __cplusplus
 }
#endif

#endif /* _SIDGPIO_H_ */
