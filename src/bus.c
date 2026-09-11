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

#include <globals.h>
#include <usbsid.h>
#include <logging.h>
#include <config.h>
#include <gpio_defs.h>
#include <bus.h>
#include <pio.h>
#include <dma.h>
#include <vu.h>
#include <sid.h>


/* Direct Pio IRQ access */
#define IRQState (pio0_hw->irq)

/* Both bus handshake flags in one write (write 1 to clear) */
#define BUS_IRQ_MASK ((1u << PIO_IRQ0) | (1u << PIO_IRQ1))

/* Bounded spin for `bus_drain`. A single in-flight operation can hold the
 * pipeline for up to 65535 PHI1 cycles (~66ms @1MHz); bound set well above
 * that. */
#define BUS_DRAIN_TIMEOUT 2000000u

/* DMA bus data variables */
volatile static uint8_t control_word, read_data;
volatile static uint16_t delay_word;
volatile static uint32_t data_word, dir_mask;

/* Bus lock: guards control_word/data_word/delay_word, the four shared DMA
 * channels, and sid_memory[] - written from both cores now (core1 runs
 * ONBOARD_EMULATOR and MIDI). Hardware spinlock, not software:
 * spin_lock_blocking() disables interrupts for the few cycles it takes to
 * set the words and kick the DMA trigger. */
static spin_lock_t *bus_spinlock = NULL;

/**
 * @brief Claim and initialise the bus spinlock
 *
 * Must run on core0, before core1 is released to its main loop, i.e.
 * alongside `setup_dmachannels()` during boot. Called once.
 */
void bus_lock_init(void)
{
  int lock_num = spin_lock_claim_unused(true);
  bus_spinlock = spin_lock_init(lock_num);
  return;
}


/**
 * @brief Set the bits going to the PIO databus based on provided address
 *
 * @param uint8_t address
 * @param bool write
 */
inline static int __not_in_flash_func(set_bus_bits)(uint8_t address, bool write)
{
  /* usBUS("[BUS BITS]$%02X:%02X ", address, data); */
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
  if (get_muted_state() && ((address & 0x1F) == 0x18)) data &= 0xF0; /* Mask volume register to 0 if muted */
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
  /* usBUS("$%02X:%02X $%04X 0b%032b $%04X 0b%016b\n",
    address, data, data_word, data_word, control_word, control_word); */
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
  set_vu_action(); /* Keep that shiny Vu blinking! */
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
      usGPIO("[W]$%08x 0b%032b $%04x 0b%016b\n[R]$%08x 0b%032b\n",
        data_word, data_word,
        control_word, control_word,
        read_data, read_data);
      sid_memory[(address & 0x7F)] = read_data & 0xFF;

      set_sidwriting(false);
      return read_data & 0xFF;
  }
  /* WRITE, G_PAUSE & G_CLEAR_BUS*/
  dma_channel_wait_for_finish_blocking(dma_tx_control);
  usGPIO("[W]$%08x 0b%032b $%04x 0b%016b\n", data_word, data_word, control_word, control_word);

  set_sidwriting(false);
  return 0;
}

/**
 * @brief Wait until no cycled operation is in flight anymore
 *
 * The bus is a three statemachine pipeline that hands over work with
 * the CONTROL and DATABUS irq flags. A cycled operation is _not_
 * finished when its DMA reports done, that only means the word reached
 * the PIO fifo. Touching the irq flags or clearing the fifos while an
 * operation is still travelling through the pipeline steals a handover
 * token and leaves the delay word paired with the wrong write, which is
 * a permanent desync that only an MCU reset or `bus_resync` repairs.
 *
 * All three programs start with a blocking `pull`, so once every one of
 * them reports TXSTALL the pipeline is empty and parked at its wrap
 * target.
 *
 * @note bounded spin, a stuck pipeline returns 0 instead of hanging
 *
 * @return int 1 when drained, 0 on timeout (caller should `bus_resync`)
 */
int __no_inline_not_in_flash_func(bus_drain)(void)
{
  const uint32_t stall_mask =
    (((1u << sm_control) | (1u << sm_data) | (1u << sm_delay))
      << PIO_FDEBUG_TXSTALL_LSB) & PIO_FDEBUG_TXSTALL_BITS;

  /* The stall flags are sticky and report history, clear them first */
  bus_pio->fdebug = stall_mask;
  for (uint32_t spin = 0; spin < BUS_DRAIN_TIMEOUT; spin++) {
    if ((bus_pio->fdebug & stall_mask) == stall_mask) return 1;
    tight_loop_contents();
  }

  return 0;
}

/**
 * @brief Realign the bus PIO pipeline
 *
 * Puts the control, data and delay statemachines back at the start of
 * their programs with empty fifos and both handshake flags cleared, so
 * the next cycled operation starts from a known state.
 *
 * Required after anything that can leave the pipeline skewed, for
 * example the SID detection routines, which mix fire and forget writes
 * with cpu side irq manipulation and fifo flushes.
 *
 * @note the PHI1 clock statemachine is deliberately left alone, a
 *       restart of that one glitches the SID clock
 * @note `pio_restart_sm_mask` clears the waiting-on-irq state, stalls,
 *       shift counters and any latched exec instruction, but it does
 *       _not_ reset the program counter or the irq flags, both of which
 *       are handled explicitly below
 */
void __no_inline_not_in_flash_func(bus_resync)(void)
{
  const uint32_t sm_mask = (1u << sm_control) | (1u << sm_data) | (1u << sm_delay);

  /* Stop the bus statemachines */
  pio_set_sm_mask_enabled(bus_pio, sm_mask, false);
  /* Drop anything still queued */
  pio_sm_clear_fifos(bus_pio, sm_control);
  pio_sm_clear_fifos(bus_pio, sm_data);
  pio_sm_clear_fifos(bus_pio, sm_delay);
  /* Clear internal statemachine state */
  pio_restart_sm_mask(bus_pio, sm_mask);
  /* Clear both handshake flags */
  bus_pio->irq = BUS_IRQ_MASK;
  /* Send each statemachine back to its wrap target */
  pio_sm_exec(bus_pio, sm_control, pio_encode_jmp(offset_control + bus_control_wrap_target));
  pio_sm_exec(bus_pio, sm_data, pio_encode_jmp(offset_data + data_bus_wrap_target));
  pio_sm_exec(bus_pio, sm_delay, pio_encode_jmp(offset_delay + delay_timer_wrap_target));
  /* Clear the sticky stall flags so the next `bus_drain` starts clean */
  bus_pio->fdebug = ((sm_mask << PIO_FDEBUG_TXSTALL_LSB) & PIO_FDEBUG_TXSTALL_BITS);
  /* Restart all three in lockstep */
  pio_enable_sm_mask_in_sync(bus_pio, sm_mask);

  set_sidwriting(false);
  return;
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
  /* This function drives the handshake flags from the cpu side, so the
     pipeline has to be empty first. Clearing or write-1-to-clearing the
     flags while a cycled write is still in flight steals the handover
     from the control and data statemachines and desyncs the bus for
     good, see `bus_drain` */
  if __us_unlikely(!bus_drain()) bus_resync();
  delay_word = cycles;
  pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ0));  /* Clear the statemachine IRQ before starting */
  pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ1));  /* Clear the statemachine IRQ before starting */
  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_channel_set_trans_count(dma_tx_delay, 1, false);  /* Reset transfer count to 1 */
  __dsb();  /* ensure all config writes reach DMA controller before trigger */
  dma_hw->multi_channel_trigger = (1u << dma_tx_delay);  /* Delay cycles DMA transfer */

  for (;;) {  /* Keep mofo waiting yeah! */
    if (((IRQState & (1u << PIO_IRQ1)) >> PIO_IRQ1) != 1)
      continue;
    /* Clear the statemachine IRQ after finishing */
    IRQState = (1u << PIO_IRQ0) | (1u << PIO_IRQ1);  /* Write-1-to-Clear to clear both flags */
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
  uint32_t bus_irq = spin_lock_blocking(bus_spinlock);
  sid_memory[(address & 0x7F)] = data;
  if __us_unlikely(set_bus_bits(address, true) != 1) {
    spin_unlock(bus_spinlock, bus_irq);
    return;
  }

  pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));     /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI1));
  pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI1));
  pio_sm_put_blocking(bus_pio, sm_control, control_word);
  pio_sm_put_blocking(bus_pio, sm_data, data_word);

  set_sidwriting(false);
  spin_unlock(bus_spinlock, bus_irq);
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

  usGPIO("[WC]$%04x 0b%032b $%04x 0b%016b $%02X:%02X(%u %u)\n",
    data_word, data_word, control_word, control_word,
    address, data, cycles, delay_word);

  set_sidwriting(false);
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
  uint32_t bus_irq = spin_lock_blocking(bus_spinlock);
  sid_memory[(address & 0x7F)] = data;
  set_vu_action(); /* Keep that shiny Vu blinking! */
  if __us_unlikely(set_bus_bits(address, true) != 1) {
    spin_unlock(bus_spinlock, bus_irq);
    return 0;
  }

  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);
  __dsb();  /* ensure all config writes reach DMA controller before trigger */

  cycled_delay_operation(cycles); /* Replaces the delay DMA, held under the same lock as
                                      the rest of this write since it shares delay_word
                                      and the delay DMA channel with everything else here */
  pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));     /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI1));
  pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI1));
  dma_hw->multi_channel_trigger = (
    1u << dma_tx_control  /* Control lines RW, CS1 & CS2 DMA transfer */
    | 1u << dma_tx_data     /* Data & Address DMA transfer */
  );
  dma_channel_wait_for_finish_blocking(dma_tx_control);

  set_sidwriting(false);
  spin_unlock(bus_spinlock, bus_irq);
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
  uint32_t bus_irq = spin_lock_blocking(bus_spinlock);
  delay_word = cycles;
  sid_memory[(address & 0x7F)] = data; /* Store SID write data in SID memory */
  if (set_bus_bits(address, true) != 1) { /* Set bus bits (uses SID memory as source) */
    spin_unlock(bus_spinlock, bus_irq);
    return;
  }

  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);
  __dsb();  /* ensure all config writes reach DMA controller before trigger */
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

  usGPIO("[WC]$%04x 0b%032b $%04x 0b%016b $%02X:%02X(%u %u)\n",
    data_word, data_word, control_word, control_word,
    address, data, cycles, delay_word);

  set_sidwriting(false);
  spin_unlock(bus_spinlock, bus_irq);
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
  uint32_t bus_irq = spin_lock_blocking(bus_spinlock);
  delay_word = cycles;
  if __us_unlikely(set_bus_bits(address, false) != 1) {
    spin_unlock(bus_spinlock, bus_irq);
    return 0x00;
  }

  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);
  dma_channel_set_write_addr(dma_rx_data, &read_data, false);
  __dsb();  /* ensure all config writes reach DMA controller before trigger */
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

  set_sidwriting(false);
  uint8_t result = sid_memory[(address & 0x7F)];
  spin_unlock(bus_spinlock, bus_irq);
  return result;
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
  usBUS("Restarting BUS\n");
  /* unclaim all dma channels */
  unclaim_dma_channels();
  /* stop all pio's */
  stop_pios();
  /* start piobus */
  setup_piobus();
  /* start dma */
  setup_dmachannels();
  setup_vu_dma(); /* Unclaimed in unclaim dma channels */
  /* sync pios */
  sync_pios(false);
  usBUS("Finished restarting BUS\n");
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
 * @return uint32_t current C64 PHI1 clock cycle count
 */
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


/* The Bus Traffic Controller lives here */

/* Idle timeout in milliseconds - 750ms before owner considered inactive */
#define BUS_OWNER_IDLE_MS 750

/* Owner tracking - volatile for cross-core access */
static volatile bus_owner_t current_owner = BUS_OWNER_NONE;
static volatile uint32_t last_touch_time_ms = 0;

/**
 * @brief Try to claim the bus for a given owner
 *
 * USB always wins on claim (force flag) to preserve existing behavior.
 * Other owners must wait until idle timeout expires or explicit release.
 *
 * @param who owner attempting to claim
 * @return true if claim succeeded, false if bus held by another owner
 */
bool bus_try_claim(bus_owner_t who) {
  uint32_t irq = spin_lock_blocking(bus_spinlock);

  if (who == BUS_OWNER_USB) {
    /* USB always wins - force claim */
    current_owner = who;
    last_touch_time_ms = time_us_64() / 1000;
    spin_unlock(bus_spinlock, irq);
    return true;
  }

  if (current_owner == BUS_OWNER_NONE || current_owner == who) {
    /* Bus is free, or `who` already owns it - claim/refresh it */
    current_owner = who;
    last_touch_time_ms = time_us_64() / 1000;
    spin_unlock(bus_spinlock, irq);
    return true;
  }

  /* Check if current owner has been idle long enough */
  uint32_t now_ms = time_us_64() / 1000;
  if ((now_ms - last_touch_time_ms) > BUS_OWNER_IDLE_MS) {
    /* Idle timeout expired - steal the bus */
    current_owner = who;
    last_touch_time_ms = now_ms;
    spin_unlock(bus_spinlock, irq);
    return true;
  }

  /* Bus still held by active owner */
  spin_unlock(bus_spinlock, irq);
  return false;
}

/**
 * @brief Touch the current owner to refresh idle timer
 *
 * Call this periodically while actively using the bus.
 * Prevents automatic ownership transfer during long operations.
 *
 * @param who owner touching (must be current owner)
 */
void bus_touch(bus_owner_t who) {
  uint32_t irq = spin_lock_blocking(bus_spinlock);
  if (who == current_owner || who == BUS_OWNER_USB) {
    last_touch_time_ms = time_us_64() / 1000;
  }
  spin_unlock(bus_spinlock, irq);
}

/**
 * @brief Release the bus for the given owner
 *
 * Call this when done using the bus to allow other owners to claim it.
 *
 * @param who owner releasing the bus
 */
void bus_release(bus_owner_t who) {
  uint32_t irq = spin_lock_blocking(bus_spinlock);
  if (who == current_owner) {
    current_owner = BUS_OWNER_NONE;
  }
  spin_unlock(bus_spinlock, irq);
}

/**
 * @brief Get the current bus owner
 *
 * @return current owner, or BUS_OWNER_NONE if bus is free
 */
bus_owner_t bus_current_owner(void) {
  uint32_t irq = spin_lock_blocking(bus_spinlock);
  bus_owner_t owner = current_owner;
  spin_unlock(bus_spinlock, irq);
  return owner;
}

/* Heavy-operation exclusion - see the comment on these in bus.h. A counter
 * rather than a bool so a heavy op that itself calls another heavy op
 * (e.g. sid_auto_detect() internally touching apply_clockrate()-adjacent
 * code) can't have the inner op's end() prematurely clear the flag while
 * the outer op is still running. */
static volatile int heavy_op_depth = 0;

/**
 * @brief Enter a heavy-operation section (see bus.h)
 */
void bus_heavy_op_begin(void) {
  uint32_t irq = spin_lock_blocking(bus_spinlock);
  heavy_op_depth++;
  spin_unlock(bus_spinlock, irq);
}

/**
 * @brief Leave a heavy-operation section (see bus.h)
 */
void bus_heavy_op_end(void) {
  uint32_t irq = spin_lock_blocking(bus_spinlock);
  if (heavy_op_depth > 0) heavy_op_depth--;
  spin_unlock(bus_spinlock, irq);
}

/**
 * @brief Check whether a heavy-operation section is currently active
 *
 * @return true if a heavy operation is in progress
 */
bool bus_heavy_op_active(void) {
  uint32_t irq = spin_lock_blocking(bus_spinlock);
  bool active = heavy_op_depth > 0;
  spin_unlock(bus_spinlock, irq);
  return active;
}
