/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * dma.c
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
#include "logging.h"
#include "config.h"
#include "pio.h"
#include "sid.h"


/* imports */
extern PIO bus_pio, clkcnt_pio;
extern uint sm_control, sm_data, sm_clock, sm_delay, sm_clkcnt;

/* locals */
int dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay;
int dma_clkcnt_control;
volatile uint32_t clkcnt_word;

/* Shiny things */
#if defined(PICO_DEFAULT_LED_PIN)
extern PIO led_pio;
extern uint sm_pwmled;
int dma_pwmled;
int pwm_value = 0;
#if defined(USE_RGB)  /* No RGB LED on _w Pico's */
extern uint sm_rgbled;
int dma_rgbled;
uint32_t rgb_value = 0;
#endif
#endif

void setup_dmachannels(void)
{ /* NOTE: Do not manually assign DMA channels, this causes a Panic on the PicoW */
  CFG("[DMA CHANNELS INIT] START\n");

  /* NOTICE: DMA read address is disabled for now, it is causing confirmed desync on the rp2040 (rp2350 seems to works, but needs improving) */
  /* NOTICE: DMA chaining on rp2350 causes desync with cycled writes, probably due to RP2350-E8 */
  /* NOTE: Until I find a fix for the DMA chaining with the delay counter it's disabled */

  dma_tx_control = dma_claim_unused_channel(true);
  dma_tx_data = dma_claim_unused_channel(true);
  dma_rx_data = dma_claim_unused_channel(true);
  dma_tx_delay = dma_claim_unused_channel(true);
  dma_clkcnt_control = dma_claim_unused_channel(true);

  { /* dma controlbus */
    dma_channel_config tx_config_control = dma_channel_get_default_config(dma_tx_control);
    channel_config_set_transfer_data_size(&tx_config_control, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_config_control, false);
    channel_config_set_write_increment(&tx_config_control, false);
    channel_config_set_dreq(&tx_config_control, DREQ_PIO0_TX1);
    //#if PICO_PIO_VERSION == 0  /* rp2040 only for now, see notice above */
    //channel_config_set_chain_to(&tx_config_control, dma_tx_data); /* Chain to controlbus tx */
    //#endif
    dma_channel_configure(dma_tx_control, &tx_config_control, &bus_pio->txf[sm_control], NULL, 1, false);
  }

  { /* dma tx databus */
    dma_channel_config tx_config_data = dma_channel_get_default_config(dma_tx_data);
    channel_config_set_transfer_data_size(&tx_config_data, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_config_data, false);
    channel_config_set_write_increment(&tx_config_data, false);
    channel_config_set_dreq(&tx_config_data, DREQ_PIO0_TX2);
    dma_channel_configure(dma_tx_data, &tx_config_data, &bus_pio->txf[sm_data], NULL, 1, false);
  }

  { /* dma rx databus */
    dma_channel_config rx_config_data = dma_channel_get_default_config(dma_rx_data);
    channel_config_set_transfer_data_size(&rx_config_data, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config_data, false);
    channel_config_set_write_increment(&rx_config_data, false);
    channel_config_set_dreq(&rx_config_data, DREQ_PIO0_RX1);
    dma_channel_configure(dma_rx_data, &rx_config_data, NULL, &bus_pio->rxf[sm_control], 1, false);
  }

  { /* dma delaytimerbus */
    dma_channel_config tx_config_delay = dma_channel_get_default_config(dma_tx_delay);
    channel_config_set_transfer_data_size(&tx_config_delay, DMA_SIZE_16);
    channel_config_set_read_increment(&tx_config_delay, false);
    channel_config_set_write_increment(&tx_config_delay, false);
    channel_config_set_dreq(&tx_config_delay, DREQ_PIO0_TX3);
    //#if PICO_PIO_VERSION == 0  /* rp2040 only for now, see notice above */
    //channel_config_set_chain_to(&tx_config_delay, dma_tx_control); /* Chain to controlbus tx */
    //#endif
    dma_channel_configure(dma_tx_delay, &tx_config_delay, &bus_pio->txf[sm_delay], NULL, 1, false);
  }
  { /* dma rx clock counter ~ should be an endless updating item */
    dma_channel_config clkcnt_config_data = dma_channel_get_default_config(dma_clkcnt_control);
    channel_config_set_transfer_data_size(&clkcnt_config_data, DMA_SIZE_32);
    channel_config_set_read_increment(&clkcnt_config_data, false);
    channel_config_set_write_increment(&clkcnt_config_data, false);
    channel_config_set_dreq(&clkcnt_config_data, DREQ_PIO1_RX3);
    // channel_config_set_irq_ignore(&dma_clkcnt_control, false);
    // dma_channel_configure(dma_clkcnt_control, &clkcnt_config_data, &clkcnt_word, &bus_pio->rxf[sm_clkcnt], 1, false);
    clkcnt_word = 0;
    dma_channel_configure(
      dma_clkcnt_control,
      &clkcnt_config_data,
      &clkcnt_word,
      &clkcnt_pio->rxf[sm_clkcnt],
      dma_encode_endless_transfer_count(), /* ENDLESS transfer count */ // BUG: Spinlocks the Pico
      // dma_encode_transfer_count_with_self_trigger(1), /* Self triggered transfer count */ // BUG: Spinlocks the Pico
      true /* Start transfers immediately */
      // false
    );
  }

  CFG("[DMA CHANNELS CLAIMED] C:%d TX:%d RX:%d D:%d CNT:%d\n",
    dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay, dma_clkcnt_control);

  CFG("[DMA CHANNELS INIT] FINISHED\n");
  return;
}

void setup_vu_dma(void)
{
  #if defined(PICO_DEFAULT_LED_PIN)  /* Cannot use VU on PicoW :( */
  { /* dma pwmled */
    dma_pwmled = dma_claim_unused_channel(true);
    dma_channel_config c_pwmled = dma_channel_get_default_config(dma_pwmled);
    channel_config_set_transfer_data_size(&c_pwmled, DMA_SIZE_32);
    channel_config_set_read_increment(&c_pwmled, false);
    channel_config_set_write_increment(&c_pwmled, false);
    channel_config_set_dreq(&c_pwmled, DREQ_PIO1_TX0);
    dma_channel_configure(dma_pwmled, &c_pwmled, &led_pio->txf[sm_pwmled], &pwm_value, 1, true);
  }
  #if defined(USE_RGB)  /* No RGB LED on _w Pico's */
  { /* dma RGBled */
    dma_rgbled = dma_claim_unused_channel(true);
    dma_channel_config c_rgbled = dma_channel_get_default_config(dma_rgbled);
    channel_config_set_transfer_data_size(&c_rgbled, DMA_SIZE_32);
    channel_config_set_read_increment(&c_rgbled, false);
    channel_config_set_write_increment(&c_rgbled, false);
    channel_config_set_dreq(&c_rgbled, DREQ_PIO1_TX1);
    dma_channel_configure(dma_rgbled, &c_rgbled, &led_pio->txf[sm_rgbled], &rgb_value, 1, true);
  }
  #endif
  #endif
}

/**
 * @brief returns the amount of C64 cpu clock
 * cycles counted by the counter SM and updated
 * by a continous running DMA channel
 *
 * @returns uint32_t */
uint32_t clockcycles(void)
{
  return (uint32_t)clkcnt_word;
}

/**
 * @brief delay for n PHI1 clockcycles
 * Will do a cycled delay if not rp2350
 * with cycle counter
 *
 * @param uint32_t n_cycles
 */
void clockcycle_delay(uint32_t n_cycles)
{ /* ISSUE: Will crap out if delay cycles wrap around __UINT32_MAX__ */
  if (unlikely(n_cycles == 0)) return;
  uint32_t now, end;
  now = end = clockcycles();
  // if ((end + n_cycles) > __UINT32_MAX__) // TODO: Finish
  do {
    end = clockcycles();
  } while (end < (now + n_cycles));
  return;
}

void stop_dma_channels(void)
{ // TODO: Fix and finish per RP2040-E13 and RP2350-E5
  // CFG("[STOP DMA CHANNELS]\n");
  /* Clear any Interrupt enable bits as per RP2040-E13 */
  // TODO: FINISH
  /* Atomically abort channels */
  // dma_hw->abort = (1 << dma_tx_delay) | (1 << dma_rx_data) | (1 << dma_tx_data) | (1 << dma_tx_control);
  /* Wait for all aborts to complete */
  // while (dma_hw->abort) tight_loop_contents();
  /* Check and clear any Interrupt enable bits as per RP2040-E13 */
  // TODO: FINISH
  /* Wait for channels to not be busy */
  // while (dma_hw->ch[dma_tx_delay].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  // while (dma_hw->ch[dma_rx_data].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  // while (dma_hw->ch[dma_tx_data].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  // while (dma_hw->ch[dma_tx_control].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  return;
}

void start_dma_channels(void)
{ /* NOTE: DO NOT USE, THIS STARTS A TRANSFER IN THE CURRENT CONFIG */
  /* Trigger -> start dma channels all at once */
  // CFG("[START DMA CHANNELS]\n");
  // dma_hw->multi_channel_trigger = (1 << dma_tx_delay) | (1 << dma_rx_data) | (1 << dma_tx_data) | (1 << dma_tx_control);
  return;
}

void unclaim_dma_channels(void)
{
  /* disable delay timer dma */
  dma_channel_unclaim(dma_tx_delay);
  /* disable databus rx dma */
  dma_channel_unclaim(dma_rx_data);
  /* disable databus tx dma */
  dma_channel_unclaim(dma_tx_data);
  /* disable control bus dma */
  dma_channel_unclaim(dma_tx_control);
  return;
}

// dma_hw->abort = (1 << dma_clkcnt_control);
// while (dma_hw->abort) tight_loop_contents();
// while (dma_hw->ch[dma_clkcnt_control].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
