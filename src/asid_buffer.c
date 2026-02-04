/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * asid_buffer.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Huge thanks to Thomas Jansson for all his ASID improvement work (https://github.com/thomasj)
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
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

#include <stdlib.h>
#include <pico/stdlib.h>
#include <pico/malloc.h>

#include "globals.h"
#include "config.h"
#include "pio.h"
#include "gpio.h"
#include "logging.h"
#include "asid.h"
#include "sid.h"


/* config.c */
extern Config usbsid_config;

/* PIO */
PIO raster_pio = pio1;
static uint sm_buffer, offset_buffer;
bool buffer_sm_started = false;
bool buffer_sm_claimed = false;

/* IRQ */
int pio_irq;
static int8_t buffer_irq;
const int BUFFPIOIRQ = 2;
bool buffer_irq_started = false;

/* Ring buffer */
static uint8_t __not_in_flash_func(ring_get)(void);
static  int __not_in_flash_func(ring_diff)(void);
uint8_t diff_size = 64;
uint16_t ring_size = 8192;
typedef struct {
  uint16_t ring_read;
  uint16_t ring_write;
  bool is_allocated;
  uint8_t * __restrict__ ringbuffer;
} ring_buffer_t;
static ring_buffer_t asid_ringbuffer;

/**
 * @brief buffer interrupt handler
 * @note writes data from the ringbuffer to the SIDs
 */
void __not_in_flash_func(buffer_irq_handler)(void)
{
  pio_interrupt_clear(raster_pio, BUFFPIOIRQ);

  /* inline extern as only used here */
  extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);

  if(asid_ringbuffer.ring_read != asid_ringbuffer.ring_write) {
    if (ring_diff() > diff_size) {
      for (size_t pos = 0; pos < 28; pos++) {
        uint8_t reg = ring_get();
        uint8_t val = ring_get();
        uint8_t c_hi = ring_get();
        uint8_t c_lo = ring_get();
        if (reg != 0xffu) {
          cycled_write_operation(reg,val,(c_hi<<8|c_lo));
        }
      }
    }
  }

  /* Interrupt cleared at end of routine
   * if play becomes irregular, irq's might be
   * misfiring and moving the clearing to the
   * beginning of the routine might help
   */
  /* pio_interrupt_clear(raster_pio, BUFFPIOIRQ); */
  return;
}

/**
 * @brief initialize the buffer irq
 * @note sets the interrupt number and handler
 * @note sets the irq source and enables the irq
 */
void init_buffer_irq(void)
{
  if (!buffer_irq_started) {
    DBG("[ASID] Enabling buffer IRQ\n");
    buffer_irq = pis_interrupt2; /* PIO_INTR_SM2_LSB hardware/pio.h */
    pio_irq = pio_get_irq_num(raster_pio, 0);
    irq_set_exclusive_handler(pio_irq, buffer_irq_handler);
    pio_set_irq0_source_enabled(raster_pio, buffer_irq + sm_buffer, true);
    irq_set_enabled(pio_irq, true);
    buffer_irq_started = true;
  }
  return;
}

/**
 * @brief initializes the raster_pio
 * @note also claims the pio statemachine
 */
void init_buffer_pio(void)
{ /* buffer cycle counter */
  sm_buffer = 0;  /* PIO1 SM0 */
  if (!buffer_sm_claimed) {
    DBG("[ASID] Starting & claiming buffer pio\n");
    pio_sm_claim(raster_pio, sm_buffer);
    offset_buffer = pio_add_program(raster_pio, &raster_buffer_program);
    pio_sm_config c_buffer = raster_buffer_program_get_default_config(offset_buffer);
    sm_config_set_fifo_join(&c_buffer, PIO_FIFO_JOIN_TX);
    pio_sm_init(raster_pio, sm_buffer, offset_buffer, &c_buffer);
    buffer_sm_claimed = true;
  }
  return;
}

/**
 * @brief enable the raster_pio statemachine
 */
void start_buffer_pio(void)
{
  if (!buffer_sm_started) {
    DBG("[ASID] Enabling buffer statemachine\n");
    pio_sm_set_enabled(raster_pio, sm_buffer, true);
    buffer_sm_started = true;
  }
  return;
}

/**
 * @brief stop the asid buffer handling
 * @note frees the irq and stops the irq_handler
 * @note disables the raster_pio statemachine
 * @note unclaims the statemadhine on the pio
 */
void stop_buffer_pio(void)
{
  if (buffer_irq_started) {
    irq_set_enabled(pio_irq, false);
    pio_set_irq0_source_enabled(raster_pio, buffer_irq + sm_buffer, false);
    irq_remove_handler(pio_irq, buffer_irq_handler);
    buffer_irq_started = false;
    DBG("[ASID] Buffer IRQ released\n");
  }
  if (buffer_sm_started) {
    pio_sm_set_enabled(raster_pio, sm_buffer, false);
    buffer_sm_started = false;
    DBG("[ASID] Buffer statemachine disabled\n");
  }
  if (buffer_sm_claimed) {
    pio_remove_program(raster_pio, &raster_buffer_program, offset_buffer);
    pio_sm_unclaim(raster_pio, sm_buffer);
    buffer_sm_claimed = false;
    DBG("[ASID] Buffer statemachine released\n");
  }
  return;
}

/**
 * @brief sets the asid buffer raster rate
 * @note the raster_rate pio has a 1 cycle overhead
 * @note this requires the rate to be corrected with
 * @note subtraction of a single cycle.
 * @note the function does this if the provided
 * @note rate is higher than zero.
 *
 * @param uint16_t rate
 */
void set_buffer_rate(uint16_t rate)
{
  uint16_t corrected_rate = ((rate > 0) ? (rate - 1) : (usbsid_config.raster_rate - 1));
  DBG("[ASID] Rate received %u Rate set %u\n", rate, corrected_rate);
  if (!buffer_irq_started) init_buffer_irq();
  pio_sm_put(raster_pio, sm_buffer, corrected_rate);
  start_buffer_pio();
  return;
}



/**
 * @brief reset the head and tail to zero
 */
static void ring_buffer_reset(void)
{
  asid_ringbuffer.ring_read = asid_ringbuffer.ring_write = 0;
  return;
}

/**
 * @brief returns true if the head is
 *        higher then the tail
 */
static bool __not_in_flash_func(ring_is_higher)(void)
{
  return (asid_ringbuffer.ring_read < asid_ringbuffer.ring_write);
}

/**
 * @brief returns the difference between head and tail
 */
static int __not_in_flash_func(ring_diff)(void)
{
  int d = (ring_is_higher() ? (asid_ringbuffer.ring_read - asid_ringbuffer.ring_write) : (asid_ringbuffer.ring_write - asid_ringbuffer.ring_read));
  return ((d < 0) ? (d * -1) : d);
}

/**
 * @brief put an item into the ringbuffer
 *
 * @param uint8_t item
 */
static void ring_put(uint8_t item)
{
  asid_ringbuffer.ringbuffer[asid_ringbuffer.ring_write] = item;
  asid_ringbuffer.ring_write = (asid_ringbuffer.ring_write + 1) % ring_size;
  return;
}

/**
 * @brief retrieves an item from the ring buffer
 */
static uint8_t __not_in_flash_func(ring_get)(void)
{
  uint8_t item = asid_ringbuffer.ringbuffer[asid_ringbuffer.ring_read];
  asid_ringbuffer.ring_read = (asid_ringbuffer.ring_read + 1) % ring_size;
  return item;
}

/**
 * @brief intialize the ring buffer
 */
void asid_ring_init(void)
{
  if (!asid_ringbuffer.is_allocated) {
    if (asid_ringbuffer.ringbuffer != NULL) { free(asid_ringbuffer.ringbuffer); }
    asid_ringbuffer.ringbuffer = (uint8_t*)calloc(ring_size, 1);
    asid_ringbuffer.is_allocated = true;
    ring_buffer_reset();
    DBG("[ASID] Ringbuffer initialized\n");
  }
  return;
}

/**
 * @brief de-intialize the ring buffer
 */
void asid_ring_deinit(void)
{
  if (asid_ringbuffer.is_allocated) {
    ring_buffer_reset();
    free(asid_ringbuffer.ringbuffer);
    asid_ringbuffer.ringbuffer = NULL;
    asid_ringbuffer.is_allocated = false;
  }
  return;
}

/**
 * @brief write data to the ringbuffer
 * @note writes 4 items per call
 *
 * @param uint8_t reg ~ SID register
 * @param uint8_t val ~ value
 * @param uint16_t c  ~ delay cycles
 */
void asid_ring_write(uint8_t reg, uint8_t val, uint16_t c)
{
  ring_put(reg);
  ring_put(val);
  ring_put((uint8_t)((c&0xff00u)>>8));
  ring_put((uint8_t)(c&0xffu));
  return;
}
