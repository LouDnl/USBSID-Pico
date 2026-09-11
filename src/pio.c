/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * pio.c
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
#include <config.h>
#include <logging.h>
#include <gpio_defs.h>
#include <gpio.h>
#include <dma.h>
#include <pio.h>
#include <bus.h>
#include <sid.h>


/* locals */
const PIO bus_pio = pio0;
const PIO clkcnt_pio = pio1;
volatile uint sm_control = 0, offset_control = 0; /* pio0 */
volatile uint sm_data = 0, offset_data = 0;       /* pio0 */
volatile uint sm_clock = 0, offset_clock = 0;     /* pio0 */
volatile uint sm_delay = 0, offset_delay = 0;     /* pio0 */
volatile uint sm_clkcnt = 0, offset_clkcnt = 0;   /* pio1 */
volatile float sidclock_frequency = 0.0, busclock_frequency = 0.0;

/* Shiny things */
#if defined(PICO_DEFAULT_LED_PIN)
PIO led_pio = pio1;
uint sm_pwmled, offset_pwmled;
#if defined(USE_RGB)  /* No RGB LED on _w Pico's */
uint sm_rgbled, offset_rgbled;
#endif /* USE_RGB */
#endif /* PICO_DEFAULT_LED_PIN */


/**
 * @brief Set the up vu statemachines and dma, runs only once
 *
 * @note sm_pwmled and sm_rgbled are never released
 *
 */
void setup_vu(void)
{
#if defined(PICO_DEFAULT_LED_PIN)  /* Cannot use VU on PicoW :( */
  { /* PWM led */
    offset_pwmled = pio_add_program(led_pio, &vu_program);
    sm_pwmled = 1;  /* PIO1 SM1 */
    pio_sm_claim(led_pio, sm_pwmled);
    pio_gpio_init(led_pio, BUILTIN_LED);
    pio_sm_set_consecutive_pindirs(led_pio, sm_pwmled, BUILTIN_LED, 1, true);
    pio_sm_config c_ledpwm = vu_program_get_default_config(offset_pwmled);
    sm_config_set_sideset_pins(&c_ledpwm, BUILTIN_LED);
    pio_sm_init(led_pio, sm_pwmled, offset_pwmled, &c_ledpwm);
    pio_sm_set_enabled(led_pio, sm_pwmled, false);
    pio_sm_put(led_pio, sm_pwmled, 65534);  /* VU_MAX = 65534 */
    pio_sm_exec(led_pio, sm_pwmled, pio_encode_pull(false, false));
    pio_sm_exec(led_pio, sm_pwmled, pio_encode_out(pio_isr, 32));
    pio_sm_set_enabled(led_pio, sm_pwmled, true);
  }
#if defined(USE_RGB)  /* No RGB LED on _w Pico's */
  { /* Init RGB */
    gpio_set_drive_strength(WS2812_PIN, GPIO_DRIVE_STRENGTH_2MA);
    offset_rgbled = pio_add_program(led_pio, &vu_rgb_program);
    sm_rgbled = 2;  /* PIO1 SM2 */
    pio_sm_claim(led_pio, sm_rgbled);
    pio_gpio_init(led_pio, WS2812_PIN);
    pio_sm_set_consecutive_pindirs(led_pio, sm_rgbled, WS2812_PIN, 1, true);
    pio_sm_config c_rgbled = vu_rgb_program_get_default_config(offset_rgbled);
    sm_config_set_sideset_pins(&c_rgbled, WS2812_PIN);
    /* RGBW LED ? 32 : 24 */
    sm_config_set_out_shift(&c_rgbled, false, true, 24);
    sm_config_set_fifo_join(&c_rgbled, PIO_FIFO_JOIN_TX);
    float freq = 800000;
    int cycles_per_bit = vu_rgb_T1 + vu_rgb_T2 + vu_rgb_T3;
    float div = clock_get_hz(clk_sys) / (freq * cycles_per_bit);
    sm_config_set_clkdiv(&c_rgbled, div);
    pio_sm_init(led_pio, sm_rgbled, offset_rgbled, &c_rgbled);
    pio_sm_set_enabled(led_pio, sm_rgbled, true);
  }
#endif /* USE_RGB */
  setup_vu_dma();
#elif defined(CYW43_WL_GPIO_LED_PIN)
#if !defined(USE_NET)
  /* For Pico W devices we need to initialise the driver etc, unless
   * USE_NET is defined, this owns the cyw43_arch_init() call.
   - see the guard in net_wifi.c / bluetooth.c's setup_bluetooth() */
  if (cyw43_arch_init()) {
    usERR("cyw43_arch_init() failed, onboard LED will not work\n");
  }
  /* Ask the wifi "driver" to set the GPIO on or off */
  cyw43_arch_gpio_put(BUILTIN_LED, usbsid_config.LED.enabled);
#endif /* !USE_NET */
#endif /* CYW43_WL_GPIO_LED_PIN */
  return;
}

/**
 * @brief Set the up pio c64 bus
 *
 */
void setup_piobus(void)
{
  uint32_t pico_hz = clock_get_hz(clk_sys);
  busclock_frequency = (float)pico_hz / (usbsid_config.clock_rate * 32) / 2;  /* Clock frequency is 8 times the SID clock */

  usNFO("\n");
  usPIO("BUS Clock initialisation\n");
  usPIO("  Pico Clock @ %luMHz\n",
    (pico_hz / 1000 / 1000));
  usPIO("  BUS clock divisor = %.2f\n",
    busclock_frequency);
  usPIO("  BUS Clock @ %.2f\n",
    (float)pico_hz / busclock_frequency / 2);
  usPIO("  C64 SID Clock = %d\n",
    (int)usbsid_config.clock_rate);
  stdio_flush();

  { /* control bus */
    sm_control = 1;  /* PIO0 SM1 */
    pio_sm_claim(bus_pio, sm_control);
    offset_control = pio_add_program(bus_pio, &bus_control_program);
    for (uint i = RW; i < CS2 + 1; ++i)
      pio_gpio_init(bus_pio, i);
    pio_sm_config c_control = bus_control_program_get_default_config(offset_control);
    sm_config_set_out_pins(&c_control, RW, 3);
    sm_config_set_in_pins(&c_control, D0);
    sm_config_set_jmp_pin(&c_control, RW);
    sm_config_set_clkdiv(&c_control, busclock_frequency);
    pio_sm_init(bus_pio, sm_control, offset_control, &c_control);
    pio_sm_set_enabled(bus_pio, sm_control, true);
  }

  { /* databus */
    sm_data = 2;  /* PIO0 SM2 */
    pio_sm_claim(bus_pio, sm_data);
    offset_data = pio_add_program(bus_pio, &data_bus_program);
    for (uint i = D0; i < A5 + 1; ++i) {
      pio_gpio_init(bus_pio, i);
    }
    pio_sm_config c_data = data_bus_program_get_default_config(offset_data);
    pio_sm_set_pindirs_with_mask(bus_pio, sm_data, PIO_PINDIRMASK, PIO_PINDIRMASK);  /* WORKING */
    sm_config_set_out_pins(&c_data, D0, A5 + 1);
    sm_config_set_fifo_join(&c_data, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c_data, busclock_frequency);
    pio_sm_init(bus_pio, sm_data, offset_data, &c_data);
    pio_sm_set_enabled(bus_pio, sm_data, true);
  }

  { /* delay cycle counter */
    sm_delay = 3;  /* PIO0 SM3 */
    pio_sm_claim(bus_pio, sm_delay);
    offset_delay = pio_add_program(bus_pio, &delay_timer_program);
    pio_sm_config c_delay = delay_timer_program_get_default_config(offset_delay);
    sm_config_set_fifo_join(&c_delay, PIO_FIFO_JOIN_TX);
    pio_sm_init(bus_pio, sm_delay, offset_delay, &c_delay);
    pio_sm_set_enabled(bus_pio, sm_delay, true);
  }

  { /* cycle counter */
    sm_clkcnt = 3;  /* PIO1 SM3 */
    pio_sm_claim(clkcnt_pio, sm_clkcnt);
    offset_clkcnt = pio_add_program(clkcnt_pio, &cycle_counter_program);
    pio_sm_config clkcnt_delay = cycle_counter_program_get_default_config(offset_clkcnt);
    sm_config_set_in_pins(&clkcnt_delay, PHI1);  /* Use PHI1 as input */
    sm_config_set_fifo_join(&clkcnt_delay, PIO_FIFO_JOIN_RX);
    pio_sm_init(clkcnt_pio, sm_clkcnt, offset_clkcnt, &clkcnt_delay);
    pio_sm_set_enabled(clkcnt_pio, sm_clkcnt, true);
  }
  return;
}

/**
 * @brief Clear bus PIO's from any lingering data
 *
 */
void clear_bus_fifos(void)
{
  pio_sm_clear_fifos(bus_pio, sm_clock);
  pio_sm_clear_fifos(bus_pio, sm_control);
  pio_sm_clear_fifos(bus_pio, sm_data);
  pio_sm_clear_fifos(bus_pio, sm_delay);
  /* NOTE: sm_clkcnt lives on clkcnt_pio (pio1), passing bus_pio here
     cleared bus_pio SM3 (sm_delay) a second time and never touched the
     cycle counter fifo at all */
  pio_sm_clear_fifos(clkcnt_pio, sm_clkcnt);
  return;
}

/**
 * @brief Restart and re-synchronise the bus PIO statemachines
 *
 * Restarts the control, data and delay statemachines (leaving the PHI1
 * clock statemachine untouched to avoid glitching the SID clock) and, on
 * RP2350, also synchronises the clock dividers. Clears the stale bus
 * handshake IRQ flags afterwards, and unless called at boot also clears
 * the bus fifos and resyncs the bus program counters via bus_resync().
 *
 * @param bool at_boot
 */
void sync_pios(bool at_boot)
{ /* Sync PIO's */
  usNFO("\n");
#if PICO_PIO_VERSION == 0
  usPIO("Restarting PIO's (Pico & Pico_w)\n");
  /* NOTE: `pio_sm_restart` takes a statemachine number and not a mask,
     the previous `pio_sm_restart(bus_pio, 0b1111)` wrote CTRL bit 19,
     which is reserved, so nothing was ever restarted here.
     The PHI1 clock statemachine (SM0) is left out on purpose, restarting
     it glitches the SID clock */
  pio_restart_sm_mask(bus_pio, 0b1110);
#elif PICO_PIO_VERSION > 0  /* NOTE: rp2350 only */
  usPIO("Synchronise PIO's (Pico2 & Pico2_w)\n");
  /* stdio_flush is required here because pio_clkdiv_restart_sm_multi_mask
     (RP2350-only) briefly disrupts UART interrupt handling mid-transmission.
     Flush ensures buffer empty before PIO operations.*/
  stdio_flush();
  pio_clkdiv_restart_sm_multi_mask(bus_pio, 0, 0b1111, 0);
  // pio_clkdiv_restart_sm_multi_mask(clkcnt_pio, 0, 0b0011, 0); /* TODO: SYNC COUNTER PIO WITH BUS PIO */
  pio_clkdiv_restart_sm_multi_mask(clkcnt_pio, 0, 0b1000, 0);
  /* A clock divider restart does not clear the waiting-on-irq or stall
     state of a statemachine, so do that seperately (bus SM's only) */
  pio_restart_sm_mask(bus_pio, 0b1110);
#endif
  /* A statemachine restart leaves the irq flags untouched, clear both
     bus handshake flags so no stale token survives the sync */
  bus_pio->irq = ((1u << PIO_IRQ0) | (1u << PIO_IRQ1));
  if __us_likely(!at_boot) {
    clear_bus_fifos();
    /* A restart does not reset the program counters either, so put the
       bus statemachines back at the start of their programs */
    bus_resync();
  };
  return;
}

/**
 * @brief Recalculate and re-apply the bus and SID clock dividers from the current config
 *
 * Reads clk_sys, derives the bus and SID clock frequencies from
 * usbsid_config.clock_rate, and pushes the new clock dividers to the
 * clock, control, data, delay and cycle counter statemachines.
 */
void restart_bus_clocks(void)
{
  usNFO("\n");
  usPIO("Re-initialise clocks\n");
  uint32_t pico_hz = clock_get_hz(clk_sys);
  busclock_frequency = (float)pico_hz / (usbsid_config.clock_rate * 32) / 2;  /* Clock frequency is 8 times the SID clock */
  sidclock_frequency = (float)pico_hz / usbsid_config.clock_rate / 2;
  pio_sm_set_clkdiv(bus_pio, sm_clock, sidclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_control, busclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_data, busclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_delay, busclock_frequency);
  pio_sm_set_clkdiv(clkcnt_pio, sm_clkcnt, busclock_frequency);

  usPIO("  Pico Clock @ %luMHz\n",
    (pico_hz / 1000 / 1000));
  usPIO("  BUS clock divisor = %.2f\n",
    busclock_frequency);
  usPIO("  BUS Clock @ %.2f\n",
    ((float)pico_hz / busclock_frequency / 2));
  usPIO("  SID clock divisor = %.2f\n",
    sidclock_frequency);
  usPIO("  SID Clock @ %.2f\n",
    ((float)pico_hz / sidclock_frequency / 2));
  usPIO("  C64 SID Clock = %d\n",
    (int)usbsid_config.clock_rate);
  return;
}

/**
 * @brief Disable and release the cycle counter, delay, databus and control bus statemachines
 *
 * Disables each statemachine, removes its PIO program and unclaims the
 * statemachine slot. Does not touch the PHI1 clock statemachine.
 */
void stop_pios(void)
{
  /* disable counter */
  pio_sm_set_enabled(clkcnt_pio, sm_clkcnt, false);
  pio_remove_program(clkcnt_pio, &cycle_counter_program, offset_clkcnt);
  pio_sm_unclaim(clkcnt_pio, sm_clkcnt);
  /* disable delay */
  pio_sm_set_enabled(bus_pio, sm_delay, false);
  pio_remove_program(bus_pio, &delay_timer_program, offset_delay);
  pio_sm_unclaim(bus_pio, sm_delay);
  /* disable databus */
  pio_sm_set_enabled(bus_pio, sm_data, false);
  pio_remove_program(bus_pio, &data_bus_program, offset_data);
  pio_sm_unclaim(bus_pio, sm_data);
  /* disable control bus */
  pio_sm_set_enabled(bus_pio, sm_control, false);
  pio_remove_program(bus_pio, &bus_control_program, offset_control);
  pio_sm_unclaim(bus_pio, sm_control);
  return;
}

/**
 * @brief Init nMHz square wave output
 *
 * @note local function, sm_clock gets claimed and never released
 *
 */
static void init_sidclock(void)
{
  uint32_t pico_hz = clock_get_hz(clk_sys);
  sidclock_frequency = (float)pico_hz / usbsid_config.clock_rate / 2;

  usNFO("\n");
  usPIO("SID Clock initialisation\n");
  usPIO("  Pico Clock @ %luMHz\n",
    (pico_hz / 1000 / 1000));
  usPIO("  SID clock divisor = %.2f\n",
    sidclock_frequency);
  usPIO("  SID Clock @ %.2f\n",
    ((float)pico_hz / sidclock_frequency / 2));
  usPIO("  C64 SID Clock = %d\n",
    (int)usbsid_config.clock_rate);
  offset_clock = pio_add_program(bus_pio, &clock_program);
  sm_clock = 0;  /* PIO0 SM0 */
  pio_sm_claim(bus_pio, sm_clock);
  clock_program_init(bus_pio, sm_clock, offset_clock, PHI1, sidclock_frequency);

  return;
}

/**
 * @brief Start verification, detect and init sequence of SID clock
 *
 * Verifies the configured clock rate is in bounds, then on PCB version
 * 1.0 detects whether an external crystal is driving PHI1: if not, PHI1
 * is disabled as a GPIO and the internal SID clock is initialised via
 * init_sidclock(); if an external clock is detected, external_clock is
 * set and clock_rate falls back to CLOCK_DEFAULT (1MHz). On any other
 * PCB version, PHI1 is disabled as a GPIO and the internal clock is
 * always initialised.
 */
void setup_sidclock(void)
{
  /* Verify the clockrare in the config is not out of bounds */
  verify_clockrate();

  /* Run only if PCB version 1.0 */
  if (PCB_VERSION_INT == 10) {
    /* Detect optional external crystal */
    if __us_likely(detect_clocksignal() == 0) {
      usbsid_config.external_clock = false;
      gpio_deinit(PHI1); /* Disable PHI1 as gpio */
      init_sidclock();
    } else {  /* Do nothing gpio acts as input detection */
      usbsid_config.external_clock = true;
      usbsid_config.clock_rate = CLOCK_DEFAULT;  /* Always fallback to 1MHz */
    }
  } else {
    usbsid_config.external_clock = false;
    gpio_deinit(PHI1); /* Disable PHI1 as gpio */
#if 0 && PICO_RP2350 && !USE_PIO_UART /* Only on rp2350 for testing and when not using PIO Uart */
    // gpio_deinit(PHI1); /* Disable PHI1 as gpio */
    gpio_deinit(PHI2); /* Disable PHI2 as gpio */
#endif
    init_sidclock();
  }

}

/**
 * @brief De-init nMHz square wave output
 *
 * NOTE: The sidclock should actually never be disabled
 */
static void __us_deprecated deinit_sidclock(void)
{
  usPIO("SID Clock deinitialise\n");
  clock_program_deinit(bus_pio, sm_clock, offset_clock, clock_program);

  return;
}
