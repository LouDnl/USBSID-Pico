/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * uart.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode
 * from the original file: pico-examples/pio/uart_rx/uart_rx.c
 * Licensing conditions from the above named source automatically apply to this
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
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

#ifdef USE_PIO_UART
#if PICO_RP2350

#include "globals.h"
#include "uart.h"
#include "logging.h"


/* usbsid.c */
extern uint8_t uart_buffer[64];
extern int usbdata;
extern char ntype, dtype, uart;
extern bool offload_ledrunner;

/* gpio.c */
extern uint16_t cycled_delay_operation(uint16_t cycles);
extern uint8_t cycled_read_operation(uint8_t address, uint16_t cycles);
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern uint16_t cycled_delayed_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern void reset_sid(void);

/* Locals */
static PIO uart_pio = pio2;
static uint sm_uartrx;
static int8_t pioirq_uartrx;
static queue_t fifo_uartrx;
static uint offset_uartrx;

static size_t bytes_per_rxpacket = 8; /* Initial size = init packet */
static size_t bytes_rxed = 0; /* Counter for incoming bytes */

/* Pre declarations */
static void async_worker_func(async_context_t *async_context, async_when_pending_worker_t *worker);
/* An async context is notified by the irq to "do some work" */
static async_context_threadsafe_background_t async_context;
static async_when_pending_worker_t worker = { .do_work = async_worker_func };


static inline void uart_rx_program_init(uint pin, uint baud)
{
  pio_sm_set_consecutive_pindirs(uart_pio, sm_uartrx, pin, 1, false);
  pio_gpio_init(uart_pio, pin);
  gpio_pull_up(pin);

  pio_sm_config uartrx_config = uart_rx_program_get_default_config(offset_uartrx);
  sm_config_set_in_pins(&uartrx_config, pin); /* for WAIT, IN */
  sm_config_set_jmp_pin(&uartrx_config, pin); /* for JMP */
  /* Shift to right, autopush disabled */
  sm_config_set_in_shift(&uartrx_config, true, false, 32);
  /* Deeper FIFO as we're not doing any TX */
  sm_config_set_fifo_join(&uartrx_config, PIO_FIFO_JOIN_RX);
  /* SM transmits 1 bit per 8 execution cycles. */
  float div = (float)clock_get_hz(clk_sys) / (8 * baud);
  sm_config_set_clkdiv(&uartrx_config, div);
  /* Initialize and enable statemachine */
  pio_sm_init(uart_pio, sm_uartrx, offset_uartrx, &uartrx_config);
  pio_sm_set_enabled(uart_pio, sm_uartrx, true);
  return;
}

static inline char uart_rx_program_getc(void)
{
  /* 8-bit read from the uppermost byte of the FIFO, as data is left-justified */
  io_rw_8 *rxfifo_shift = (io_rw_8*)&uart_pio->rxf[sm_uartrx] + 3;
  while (pio_sm_is_rx_fifo_empty(uart_pio, sm_uartrx))
    tight_loop_contents();
  return (char)*rxfifo_shift;
}

static void pio_irq_func(void)
{
  while(!pio_sm_is_rx_fifo_empty(uart_pio, sm_uartrx)) {
    char rxdata = uart_rx_program_getc();
    if (!queue_try_add(&fifo_uartrx, &rxdata)) {
      panic("fifo_uartrx full");
    }
  }
  /* Tell the async worker that there are some characters waiting for us */
  async_context_set_work_pending(&async_context.core, &worker);
  return;
}

static void async_worker_func(__unused async_context_t *async_context, __unused async_when_pending_worker_t *worker)
{ /* TODO: Finish */
  usbdata = 1;
  dtype = uart;
  if (offload_ledrunner == false) bytes_rxed = 0;
  offload_ledrunner = true;
  while(!queue_is_empty(&fifo_uartrx)) {
    if (!queue_try_remove(&fifo_uartrx, &uart_buffer[bytes_rxed])) {
      panic("fifo_uartrx empty");
    } else {
      // usDBG("[%d/%d] %02X $%02X:%02X\n",i,bytes_per_rxpacket,uart_buffer[0],uart_buffer[1],uart_buffer[2]);
      if ((uart_buffer[0] == 0xFF && uart_buffer[1] == 0xFF)
          || (uart_buffer[1] == 0xFF && uart_buffer[2] == 0xFF)
          || (uart_buffer[2] == 0xFF && uart_buffer[3] == 0xFF)) {
        usDBG("[UART CONFIG] RESET\n");
        memset(uart_buffer, 0, 8);
        bytes_per_rxpacket = 8;
        bytes_rxed = 0;
        usbdata = 0;
        dtype = ntype;
        offload_ledrunner = false;
        /* reset_sid(); */
        return;
      }
      if ((bytes_per_rxpacket == 2) && (bytes_rxed == (bytes_per_rxpacket - 1))) {
        cycled_write_operation(uart_buffer[0], uart_buffer[1], 6);
        memset(uart_buffer, 0, 2);
        bytes_rxed = 0;
      } else if ((bytes_per_rxpacket == 4) && (bytes_rxed == (bytes_per_rxpacket - 1))) {
        cycled_write_operation(uart_buffer[0], uart_buffer[1], (uart_buffer[2] << 8 | uart_buffer[3]));
        memset(uart_buffer, 0, 4);
        bytes_rxed = 0;
      } else if ((bytes_per_rxpacket == 8) && (bytes_rxed == (bytes_per_rxpacket - 1))) {
        /* ISSUE: At the moment a restart is required to reset back to packet size 8 */
        if (uart_buffer[0] == 0xFF
          && uart_buffer[1] == 0xEE
          && uart_buffer[2] == 0xDD
          && uart_buffer[5] == 0xDD
          && uart_buffer[6] == 0xEE
          && uart_buffer[7] == 0xFF) { /* Receiving initiator packet */
          bytes_per_rxpacket = (size_t)(uart_buffer[3] << 8 | uart_buffer[4]);
          usDBG("[UART CONFIG] BYTES PER PACKET SET TO %d\n", bytes_per_rxpacket);
          memset(uart_buffer, 0, 8);
          bytes_rxed = 0;
        }
      } else if (bytes_rxed <= (bytes_per_rxpacket - 1)) {
        bytes_rxed++;
      }
    }
  }
  return;
}

static void init_async(void)
{
  /* Setup an async context and worker to perform work when needed */
  if (!async_context_threadsafe_background_init_with_defaults(&async_context)) {
      panic("failed to setup context");
  }
  async_context_add_when_pending_worker(&async_context.core, &worker);
  return;
}

void init_uart(void)
{
  /* create a queue so the irq can save the data somewhere */
  queue_init(&fifo_uartrx, 1, UART_FIFO_SIZE);

  /* Setup an async context and worker to perform work when needed */
  init_async();

  /* This will find a free pio and state machine for our program and load it for us */
  /* We use pio_claim_free_sm_and_add_program_for_gpio_range (for_gpio_range variant) */
  /* so we will get a PIO instance suitable for addressing gpios >= 32 if needed and supported by the hardware */
  bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&uart_rx_program, &uart_pio, &sm_uartrx, &offset_uartrx, PIOUART_RX, 1, true);
  hard_assert(success);

  /* Init uart rx program */
  uart_rx_program_init(PIOUART_RX, FFIN_FAST_BAUD_RATE);

  /* Find a free irq */
  pioirq_uartrx = pio_get_irq_num(uart_pio, 0);
  if (irq_get_exclusive_handler(pioirq_uartrx)) {
    pioirq_uartrx++;
    if (irq_get_exclusive_handler(pioirq_uartrx)) {
      panic("All IRQs are in use");
    }
  }

  /* Enable interrupt */
  irq_add_shared_handler(pioirq_uartrx, pio_irq_func, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY); /* Add a shared IRQ handler */
  irq_set_enabled(pioirq_uartrx, true); /* Enable the IRQ */
  const uint irq_index = pioirq_uartrx - pio_get_irq_num(uart_pio, 0); /* Get index of the IRQ */
  pio_set_irqn_source_enabled(uart_pio, irq_index, pio_get_rx_fifo_not_empty_interrupt_source(sm_uartrx), true); /* Set uart_pio to tell us when the FIFO is NOT empty */

  return;
}

void deinit_uart(void)
{
  // /* Echo characters received from PIO to the console */
  // while (counter < MAX_COUNTER || work_done) {
  //     // Note that we could just sleep here as we're using "threadsafe_background" that uses a low priority interrupt
  //     // But if we changed to use a "polling" context that wouldn't work. The following works for both types of context.
  //     // When using "threadsafe_background" the poll does nothing. This loop is just preventing main from exiting!
  //     work_done = false;
  //     async_context_poll(&async_context.core);
  //     async_context_wait_for_work_ms(&async_context.core, 2000);
  // }

  /* Disable interrupt */
  const uint irq_index = pioirq_uartrx - pio_get_irq_num(uart_pio, 0); /* Get index of the IRQ */
  pio_set_irqn_source_enabled(uart_pio, irq_index, pio_get_rx_fifo_not_empty_interrupt_source(sm_uartrx), false);
  irq_set_enabled(pioirq_uartrx, false);
  irq_remove_handler(pioirq_uartrx, pio_irq_func);

  /* This will free resources and unload our program */
  pio_remove_program_and_unclaim_sm(&uart_rx_program, uart_pio, sm_uartrx, offset_uartrx);

  async_context_remove_when_pending_worker(&async_context.core, &worker);
  async_context_deinit(&async_context.core);
  queue_free(&fifo_uartrx);

  return;
}

#endif /* PICO_RP2350 */
#endif /* USE_PIO_UART */
