/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * bus.c
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


#include "hardware/irq.h"    /* Hardware interrupt handling */

#include "globals.h"

#include "logging.h"
#include "config.h"
#include "pio.h"
#include "sid.h"


/* usbsid.c */
#ifdef ONBOARD_EMULATOR
extern uint8_t *sid_memory;
#else
extern uint8_t sid_memory[(0x20 * 4)];
#endif

/* config.c */
extern RuntimeCFG cfg;

extern PIO bus_pio;
extern uint sm_control, sm_data, sm_clock, sm_delay;
extern int dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay;

/* vu.c */
extern uint16_t vu;

/* dma.c */
extern void setup_dmachannels(void);
extern void unclaim_dma_channels(void);
extern volatile uint32_t cycle_count_word;

/* pio.c */
extern void setup_piobus(void);
extern void sync_pios(bool at_boot);
extern void stop_pios(void);

/* globals */
volatile bool is_muted; /* Global muting state */

/* Direct Pio IRQ access */
volatile const uint32_t *IRQState = &pio0_hw->irq;

/* DMA bus data variables */
uint8_t control_word, read_data;
uint16_t delay_word;
uint32_t data_word, dir_mask;

/**
 * @brief Set the bits going to the PIO databus based on provided address
 *
 * @param uint8_t address
 * @param bool write
 */
inline static int __not_in_flash_func(set_bus_bits)(uint8_t address, bool write)
{
  /* usCFG("[BUS BITS]$%02X:%02X ", address, data); */
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  if __us_likely(write) {
    control_word = 0b111000;
    dir_mask = 0b1111111111111111;  /* Always OUT never IN */
  } else {
    data_word = dir_mask = 0;
    control_word = 0b111001;
    dir_mask |= 0b1111111100000000;
  }
  address = (address & 0x7F);
  uint8_t data = (write ? sid_memory[(address & 0x7F)] : 0x0);
  if (is_muted && ((address & 0x1F) == 0x18)) data &= 0xF0; /* Mask volume register to 0 if muted */
  switch (address) {
    case 0x00 ... 0x1F:
      if __us_unlikely(cfg.one == 0b110 || cfg.one == 0b111) return 0;
      data_word = (cfg.one_mask == 0x3f ? ((address & 0x1F) + 0x20) : (address & 0x1F)) << 8 | data;
      control_word |= cfg.one;
      break;
    case 0x20 ... 0x3F:
      if __us_unlikely(cfg.two == 0b110 || cfg.two == 0b111) return 0;
      data_word = (cfg.two_mask == 0x3f ? ((address & 0x1F) + 0x20) : address & 0x1F) << 8 | data;
      control_word |= cfg.two;
      break;
    case 0x40 ... 0x5F:
      if __us_unlikely(cfg.three == 0b110 || cfg.three == 0b111) return 0;
      /* Workaround for addresses in this range, mask doesn't work properly */
      data_word = (cfg.three_mask == 0x3f ? ((address & 0x1F) + 0x20) : (address & 0x1F)) << 8 | data;
      control_word |= cfg.three;
      break;
    case 0x60 ... 0x7F:
      if __us_unlikely(cfg.four == 0b110 || cfg.four == 0b111) return 0;
      data_word = (cfg.four_mask == 0x3f ? ((address & 0x1F) + 0x20) : (address & 0x1F)) << 8 | data;
      control_word |= cfg.four;
      break;
  }
  data_word = (dir_mask << 16) | data_word;
  // usCFG("$%02X:%02X $%04X 0b"PRINTF_BINARY_PATTERN_INT32" $%04X 0b"PRINTF_BINARY_PATTERN_INT16"\n",
  //   address, data, data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word));
  return 1;
}

/**
 * @brief Write data to or read data from the databus
 * @note uses PIO0 SM0, SM1, SM2 & SM3
 * WARNING: DEPRECATED AND NO LONGER WORKS, HERE FOR CODE HISTORY ONLY!!
 *
 * @param uint8_t command
 * @param uint8_t address
 * @param uint8_t data
 */
uint8_t __no_inline_not_in_flash_func(bus_operation)(uint8_t command, uint8_t address, uint8_t data)
{
  return 0;
  if __us_unlikely((command & 0xF0) != 0x10) {
    return 0; // Sync bit not set, ignore operation
  }
  sid_memory[(address & 0x7F)] = data;
  int sid_command = (command & 0x0F);
  bool is_read = sid_command == 0x01;
  control_word = data_word = dir_mask = 0;
  control_word = 0b110000;
  dir_mask |= (is_read ? 0b1111111100000000 : 0b1111111111111111);
  control_word |= (is_read ? 1 : 0);
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  if __us_unlikely(set_bus_bits(address, true) != 1) {
    return 0;
  }
  data_word = (dir_mask << 16) | data_word;
  switch (sid_command) {
    case WRITE:
      sid_memory[(address & 0x7F)] = data;
      pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI1));
      pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI1));
      dma_channel_set_read_addr(dma_tx_data, &data_word, true); /* Data & Address DMA transfer */
      dma_channel_set_read_addr(dma_tx_control, &control_word, true); /* Control lines RW, CS1 & CS2 DMA transfer */
      break;
    case READ:
      pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI1));
      pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI1));
      /* These are in a different order then WRITE on purpose so we actually get the read result */
      dma_channel_set_read_addr(dma_tx_control, &control_word, true); /* Control lines RW, CS1 & CS2 DMA transfer */
      dma_channel_set_read_addr(dma_tx_data, &data_word, true); /* Data & Address DMA transfer */
      read_data = 0x0;
      dma_channel_set_write_addr(dma_rx_data, &read_data, true);
      dma_channel_wait_for_finish_blocking(dma_rx_data);
      usGPIO("[W]$%08x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16"\n[R]$%08x 0b"PRINTF_BINARY_PATTERN_INT32"\n",
        data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word),
        control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word),
        read_data, PRINTF_BYTE_TO_BINARY_INT32(read_data));
      sid_memory[(address & 0x7F)] = read_data & 0xFF;
      return read_data & 0xFF;
  }
  /* WRITE, G_PAUSE & G_CLEAR_BUS*/
  dma_channel_wait_for_finish_blocking(dma_tx_control);
  usGPIO("[W]$%08x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16"\n", data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word));
  return 0;
}

/**
 * @brief Cycle delay function
 *        blocks for supplied number of cycles (65535 max)
 * @note uses DMA & PIO0 SM0 & SM3
 *
 * @param uint16_t cycles
 */
uint16_t __no_inline_not_in_flash_func(cycled_delay_operation)(uint16_t cycles)
{ /* This is a blocking function! */
  if __us_unlikely(cycles == 0) return 0; /* No point in waiting zero cycles */
  delay_word = cycles;
  pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ0));  /* Clear the statemachine IRQ before starting */
  pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ1));  /* Clear the statemachine IRQ before starting */
  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_hw->multi_channel_trigger = (1u << dma_tx_delay);  /* Delay cycle s DMA transfer */

  for (;;) {  /* Keep mofo waiting yeah! */
    if (((*IRQState & (1u << PIO_IRQ1)) >> PIO_IRQ1) != 1)
      continue;
    pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ0));  /* Clear the statemachine IRQ after finishing */
    pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ1));  /* Clear the statemachine IRQ after finishing */
    return cycles;
  }

  return 0;
}

/**
 * @brief Write data to the bus at address
 *        does not wait for the PIO write to finish
 * @note uses PIO0 SM0, SM1 & SM2
 *
 * @param uint8_t address
 * @param uint8_t data
 */
void __no_inline_not_in_flash_func(write_operation)(uint8_t address, uint8_t data)
{
  sid_memory[(address & 0x7F)] = data;
  if __us_unlikely(set_bus_bits(address, true) != 1) {
    return;
  }

  pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));     /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI1));
  pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI1));
  pio_sm_put_blocking(bus_pio, sm_control, control_word);
  pio_sm_put_blocking(bus_pio, sm_data, data_word);

  return;
}

/**
 * @brief Write data to the bus at address
 *        The PIO bus waits n cycles before the write occurs
 *        does not wait for the PIO write to finish
 * @note uses PIO0 SM0, SM1, SM2 & SM3
 *
 * @param uint8_t address
 * @param uint8_t data
 * @param uint16_t cycles
 */
void __no_inline_not_in_flash_func(cycled_write_operation_nondma)(uint8_t address, uint8_t data, uint16_t cycles)
{
  delay_word = cycles;
  sid_memory[(address & 0x7F)] = data;
  if __us_unlikely(set_bus_bits(address, true) != 1) {
    return;
  }

  pio_sm_put_blocking(bus_pio, sm_control, control_word);
  pio_sm_put_blocking(bus_pio, sm_data, data_word);
  pio_sm_put_blocking(bus_pio, sm_delay, delay_word);

  usGPIO("[WC]$%04x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16" $%02X:%02X(%u %u)\n",
    data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word),
    address, data, cycles, delay_word);
  return;
}

/**
 * @brief Write data to the bus at address
 *        The function waits n cycles before the write
 *        occurs by using `cycled_delay_operation`
 *        and then waits for the DMA to finish blocking
 * @note uses DMA & PIO0 SM0, SM1, SM2 & SM3
 *
 * @param uint8_t address
 * @param uint8_t data
 * @param uint16_t cycles
 */
uint16_t __no_inline_not_in_flash_func(cycled_delayed_write_operation)(uint8_t address, uint8_t data, uint16_t cycles)
{ /* This is a blocking function! */
  sid_memory[(address & 0x7F)] = data;
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  if __us_unlikely(set_bus_bits(address, true) != 1) {
    return 0;
  }

  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);

  cycled_delay_operation(cycles); /* Replaces the delay DMA */
  pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));     /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI1));
  pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI1));
  dma_hw->multi_channel_trigger = (
    1u << dma_tx_control  /* Control lines RW, CS1 & CS2 DMA transfer */
    | 1u << dma_tx_data     /* Data & Address DMA transfer */
  );
  dma_channel_wait_for_finish_blocking(dma_tx_control);

  return cycles;
}

/**
 * @brief Write data to the bus at address
 *        and then waits for the DMA to finish blocking
 *        The PIO bus waits n cycles before the write occurs
 * @note uses DMA & PIO0 SM0, SM1, SM2 & SM3
 *
 * @param uint8_t address
 * @param uint8_t data
 * @param uint16_t cycles
 */
void __no_inline_not_in_flash_func(cycled_write_operation)(uint8_t address, uint8_t data, uint16_t cycles)
{
  delay_word = cycles;
  sid_memory[(address & 0x7F)] = data;
  if (set_bus_bits(address, true) != 1) {
    return;
  }

  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);
  dma_hw->multi_channel_trigger = (
      1u << dma_tx_delay    /* Delay cycles DMA transfer */
  //#if PICO_PIO_VERSION > 0  /* rp2040 only for now, see notice in setup_dmachannels */
    | 1u << dma_tx_control  /* Control lines RW, CS1 & CS2 DMA transfer */
    | 1u << dma_tx_data     /* Data & Address DMA transfer */
  //#endif
  );
  /* DMA wait call
   * dma_tx_control ~ normal
   * dma_tx_data ~ not as good as control, maybe a bit more cracks
   * dma_tx_delay ~ white noise
   * neither ~ broken play
   */
  dma_channel_wait_for_finish_blocking(dma_tx_control);

  usGPIO("[WC]$%04x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16" $%02X:%02X(%u %u)\n",
    data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word),
    address, data, cycles, delay_word);
  return;
}

/**
 * @brief Read data from the bus at address
 *        and then waits for the DMA to finish blocking
 *        The PIO bus waits n cycles before the read occurs
 * @note uses DMA & PIO0 SM0, SM1, SM2 & SM3
 *
 * @param uint8_t address
 * @param uint16_t cycles
 */
uint8_t __no_inline_not_in_flash_func(cycled_read_operation)(uint8_t address, uint16_t cycles)
{
  delay_word = cycles;
  if __us_unlikely(set_bus_bits(address, false) != 1) {
    return 0x00;
  }

  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);
  dma_channel_set_write_addr(dma_rx_data, &read_data, false);
  dma_hw->multi_channel_trigger = (
      1u << dma_tx_delay    /* Delay cycles DMA transfer */
  //#if PICO_PIO_VERSION > 0  /* rp2040 only for now, see notice in setup_dmachannels */
    | 1u << dma_tx_control  /* Control lines RW, CS1 & CS2 DMA transfer */
    | 1u << dma_tx_data     /* Data & Address DMA transfer */
  //#endif
    | 1u << dma_rx_data     /* Read data DMA transfer */
  );
  dma_channel_wait_for_finish_blocking(dma_rx_data);  /* Wait for data */
  sid_memory[(address & 0x7F)] = (read_data & 0xFF);
  return (read_data & 0xFF);
}

/**
 * @brief Restart the PIO bus by unclaiming
 *        the DMA channels, stopping the PIO
 *        statemachines and then restarting
 *        everything
 *        Synchronizes the PIO statemachines
 *        afterwards
 */
void restart_bus(void)
{
  usCFG("[RESTART BUS START]\n");
  /* unclaim dma channels */
  unclaim_dma_channels();
  /* stop all pio's */
  stop_pios();
  /* start piobus */
  setup_piobus();
  /* start dma */
  setup_dmachannels();
  /* sync pios */
  sync_pios(false);
  usCFG("[RESTART BUS END]\n");
  return;
}

/**
 * @brief Returns the amount of C64 cpu clock
 *        cycles counted by the counter SM and updated
 *        by a continous running DMA channel
 *
 * @note rp2350 uses a single DMA channel and native endless transfer
 * @note rp2040 uses a two chained DMA channels for endless transfer
 *
 * @returns uint32_t */
uint32_t clockcycles(void)
{
  return (uint32_t)cycle_count_word;
}

/**
 * @brief Delay for n PHI1 clockcycles
 *        Will do a cycled delay with cycle counter
 * @note rp2350 uses a single DMA channel and native endless transfer
 * @note rp2040 uses a two chained DMA channels for endless transfer
 *
 * NOTICE: Will crap out if delay cycles wrap around __UINT32_MAX__ after ~71 minutes
 *
 * @param uint32_t n_cycles
 */
void clockcycle_delay(uint32_t n_cycles)
{ /*  */
  if __us_unlikely(n_cycles == 0) return;
  int32_t now, end;
  now = end = clockcycles();
  do {
    end = clockcycles();
  } while ((uint32_t)(end - now) < n_cycles);
  return;
}
