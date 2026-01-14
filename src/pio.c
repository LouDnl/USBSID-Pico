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

#include "globals.h"

#include "config.h"
#include "pio.h"

#include "logging.h"
#include "sid.h"


/* gpio.c */
extern int detect_clocksignal(void);

/* config.c */
extern void verify_clockrate(void);
extern Config usbsid_config;
extern RuntimeCFG cfg;
extern const char* pcb_version;

/* dma.c */
extern void setup_vu_dma(void);

/* locals */
PIO bus_pio = pio0;
PIO clkcnt_pio = pio1;
uint sm_control, offset_control; /* pio0 */
uint sm_data, offset_data;       /* pio0 */
uint sm_clock, offset_clock;     /* pio0 */
uint sm_delay, offset_delay;     /* pio0 */
uint sm_clkcnt, offset_clkcnt;   /* pio1 */
float sidclock_frequency, busclock_frequency;

/* Clock cycle counter */
#if 0 && PICO_RP2350 && !USE_PIO_UART /* Only on rp2350 for testing and when not using PIO Uart */
PIO sid2_pio = pio2;
uint sm_clock2, offset_clock2;
#endif

/* Shiny things */
#if defined(PICO_DEFAULT_LED_PIN)
PIO led_pio = pio1;
uint sm_pwmled, offset_pwmled;
#if defined(USE_RGB)  /* No RGB LED on _w Pico's */
uint sm_rgbled, offset_rgbled;
#endif
#endif


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
  #endif
  setup_vu_dma();
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  /* For Pico W devices we need to initialise the driver etc */
  cyw43_arch_init();
  /* Ask the wifi "driver" to set the GPIO on or off */
  cyw43_arch_gpio_put(BUILTIN_LED, usbsid_config.LED.enabled);
  #endif
  return;
}

void setup_piobus(void)
{
  uint32_t pico_hz = clock_get_hz(clk_sys);
  busclock_frequency = (float)pico_hz / (usbsid_config.clock_rate * 32) / 2;  /* Clock frequency is 8 times the SID clock */

  CFG("[BUS CLK INIT] START\n");
  CFG("[PI CLK]@%luMHz [DIV]@%.2f [BUS CLK]@%.2f [CFG SID CLK]%d\n",
     (pico_hz / 1000 / 1000),
     busclock_frequency,
     ((float)pico_hz / busclock_frequency / 2),
     (int)usbsid_config.clock_rate);

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

  { /* delay counter */
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
  CFG("[BUS CLK INIT] FINISHED\n");
  return;
}

void sync_pios(bool at_boot) // TODO: SYNC COUNTER PIO WITH BUS PIO
{ /* Sync PIO's */
  #if PICO_PIO_VERSION == 0
  CFG("[RESTART PIOS] Pico & Pico_w\n");
  pio_sm_restart(bus_pio, 0b1111);
  #elif PICO_PIO_VERSION > 0  // NOTE: rp2350 only
  CFG("[SYNC PIOS] Pico2\n");
  pio_clkdiv_restart_sm_multi_mask(bus_pio, 0, 0b1111, 0);
  // pio_clkdiv_restart_sm_multi_mask(clkcnt_pio, 0, 0b0011, 0);
  pio_clkdiv_restart_sm_multi_mask(clkcnt_pio, 0, 0b1000, 0);
  #endif
  if __us_likely(!at_boot) {
    pio_sm_clear_fifos(bus_pio, sm_clock); // TODO: TEST!
    pio_sm_clear_fifos(bus_pio, sm_control);
    pio_sm_clear_fifos(bus_pio, sm_data);
    pio_sm_clear_fifos(bus_pio, sm_delay);
  };
  return;
}

void restart_bus_clocks(void)
{
  CFG("[CLK RE-INIT] START\n");
  uint32_t pico_hz = clock_get_hz(clk_sys);
  busclock_frequency = (float)pico_hz / (usbsid_config.clock_rate * 32) / 2;  /* Clock frequency is 8 times the SID clock */
  sidclock_frequency = (float)pico_hz / usbsid_config.clock_rate / 2;
  pio_sm_set_clkdiv(bus_pio, sm_clock, sidclock_frequency);
#if 0 && PICO_RP2350 && !USE_PIO_UART /* Only on rp2350 for testing and when not using PIO Uart */
  pio_sm_set_clkdiv(sid2_pio, sm_clock2, sidclock_frequency);
#endif
  pio_sm_set_clkdiv(clkcnt_pio, sm_clkcnt, busclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_control, busclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_data, busclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_delay, busclock_frequency);

  CFG("[PI CLK]@%luMHz [DIV]@%.2f [BUS CLK]@%.2f [CFG SID CLK]%d\n",
    (pico_hz / 1000 / 1000),
    busclock_frequency,
    ((float)pico_hz / busclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  CFG("[PI CLK]@%luMHz [DIV]@%.2f [SID CLK]@%.2f [CFG SID CLK]%d\n",
    (pico_hz / 1000 / 1000),
    sidclock_frequency,
    ((float)pico_hz / sidclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  CFG("[CLK RE-INIT] FINISHED\n");
  return;
}

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

/* Init nMHz square wave output */
void init_sidclock(void)
{
  uint32_t pico_hz = clock_get_hz(clk_sys);
  sidclock_frequency = (float)pico_hz / usbsid_config.clock_rate / 2;

  CFG("[SID CLK INIT] START\n");
  CFG("[PI CLK]@%luMHz [DIV]@%.2f [SID CLK]@%.2f [CFG SID CLK]%d\n",
    (pico_hz / 1000 / 1000),
    sidclock_frequency,
    ((float)pico_hz / sidclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  offset_clock = pio_add_program(bus_pio, &clock_program);
  sm_clock = 0;  /* PIO0 SM0 */
  pio_sm_claim(bus_pio, sm_clock);
  clock_program_init(bus_pio, sm_clock, offset_clock, PHI1, sidclock_frequency);
  CFG("[SID CLK INIT] FINISHED\n");

/* DISABLED */
#if 0 && PICO_RP2350 && !USE_PIO_UART /* Only on rp2350 for testing and when not using PIO Uart */
  CFG("[SID CLK2 INIT] START\n");
  CFG("[PI CLK]@%luMHz [DIV]@%.2f [SID CLK]@%.2f [CFG SID CLK]%d\n",
    (pico_hz / 1000 / 1000),
    sidclock_frequency,
    ((float)pico_hz / sidclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  offset_clock2 = pio_add_program(sid2_pio, &clock_program);
  sm_clock2 = 0;  /* PIO2 SM0 */
  pio_sm_claim(sid2_pio, sm_clock2);
  clock_program_init(sid2_pio, sm_clock2, offset_clock2, PHI2, sidclock_frequency);
  // clock_program_init(sid2_pio, sm_clock2, offset_clock2, PHI2, sidclock_frequency);
  CFG("[SID CLK2 INIT] FINISHED\n");
#endif
  return;
}

/* Start verification, detect and init sequence of SID clock */
void setup_sidclock(void)
{
  /* Verify the clockrare in the config is not out of bounds */
  verify_clockrate();

  /* Run only if PCB version 1.0 */
  if ((strcmp(pcb_version, "1.0") == 0)) {
    /* Detect optional external crystal */
    if __us_likely(detect_clocksignal() == 0) {
      usbsid_config.external_clock = false;
      gpio_deinit(PHI1); /* Disable PHI1 as gpio */
      init_sidclock();
    } else {  /* Do nothing gpio acts as input detection */
      usbsid_config.external_clock = true;
      usbsid_config.clock_rate = CLOCK_DEFAULT;  /* Always 1MHz */
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

/* De-init nMHz square wave output */
void deinit_sidclock(void)
{
  CFG("[DE-INIT CLOCK]\n");
  clock_program_deinit(bus_pio, sm_clock, offset_clock, clock_program);
#if 0 && PICO_RP2350 && !USE_PIO_UART /* Only on rp2350 for testing and when not using PIO Uart */
  clock_program_deinit(sid2_pio, sm_clock2, offset_clock2, clock_program);
#endif
  return;
}
