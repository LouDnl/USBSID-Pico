/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * buffer.c
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


#include "globals.h"
#include "config.h"
#include "gpio.h"
#include "logging.h"
#include "asid.h"
#include "sid.h"


/* Init external vars */
extern Config usbsid_config;
extern uint8_t __not_in_flash("usbsid_buffer") sid_memory[(0x20 * 4)] __attribute__((aligned(2 * (0x20 * 4))));
extern void __no_inline_not_in_flash_func(cycled_write_operation)(uint8_t address, uint8_t data, uint16_t cycles);
extern queue_t buffer_queue;

/* Init vars */
PIO raster_pio = pio1;
static uint sm_buffer, offset_buffer;
static int8_t buffer_irq;
const int BUFFPIOIRQ = 2;
volatile const uint32_t *IRQEn = &pio1_hw->irq;

uint64_t before, after;

void __not_in_flash_func(buffer_irq_handler)(void)
{
  pio_interrupt_clear(raster_pio, BUFFPIOIRQ);

    for (size_t qpos = 0; qpos < 28; qpos++) {
      buffer_queue_entry_t b_entry;
      if (queue_try_remove(&buffer_queue, &b_entry)) {
        if (b_entry.cycles != 0xff) {
          /* Perform write including wait cycles */
          cycled_write_operation(
            b_entry.reg,
            b_entry.val,
            (b_entry.cycles + 6) /* Add and extra 6 cycles minimum to each write for LDA(2) & STA(4) */
          );
          // DBG("[QUEUE LEVEL] %04u\n", queue_get_level(&buffer_queue));
          // DBG("[BUFF %d] $%02X:%02X %u\n",
          //   qpos,
          //   b_entry.buffer_entries[qpos].reg,
          //   b_entry.buffer_entries[qpos].val,
          //   (b_entry.buffer_entries[qpos].cycles + 6));
        }
      }
    // }
  }
}

void init_buffer_pio(void)
{ /* buffer cycle counter */
  sm_buffer = 0;  /* PIO0 SM3 */
  pio_sm_claim(raster_pio, sm_buffer);
  offset_buffer = pio_add_program(raster_pio, &raster_buffer_program);
  pio_sm_config c_buffer = raster_buffer_program_get_default_config(offset_buffer);
  sm_config_set_fifo_join(&c_buffer, PIO_FIFO_JOIN_TX);
  pio_sm_init(raster_pio, sm_buffer, offset_buffer, &c_buffer);
}

void init_buffer_irq(void)
{
  buffer_irq = pis_interrupt2;
  irq_set_exclusive_handler(PIO1_IRQ_0, buffer_irq_handler);
  pio_set_irq0_source_enabled(raster_pio, buffer_irq + sm_buffer, true);
  irq_set_enabled(PIO1_IRQ_0, true);
}

void setup_buffer_handler(void)
{
  /* Init buffer PIO */
  init_buffer_pio();

  /* Init buffer IRQ */
  init_buffer_irq();

  /* Add first cycles based on configured refresh_rate to buffer counter */
  pio_sm_put(raster_pio, sm_buffer, usbsid_config.refresh_rate);
  /* Enabled the buffer PIO */
  pio_sm_set_enabled(raster_pio, sm_buffer, true);
}
