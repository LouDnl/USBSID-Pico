/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * vu.c
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

#include <math.h>
#include "globals.h"
#include "config.h"
#include "logging.h"
#include "sid.h"
#include "vu.pio.h"      /* Vu LED handler */
#if defined(USE_RGB)
#include "vu_rgb.pio.h"  /* Vu RGB led handler */
#endif


/* Init external vars */
extern Config usbsid_config;  /* usbsid.c */
#ifdef ONBOARD_EMULATOR
extern uint8_t *sid_memory;
#else
extern uint8_t sid_memory[(0x20 * 4)];  /* usbsid.c */
#endif
extern volatile int usbdata;     /* usbsid.c */
extern volatile bool offload_ledrunner;

/* GPIO externals */
extern void setup_vu(void);
extern int dma_pwmled, dma_rgbled;
volatile extern int pwm_value;
volatile extern uint32_t rgb_value;

/* LED breathe levels */
enum
{
  ALWAYS_OFF   = 99999,   /* non zero is off */
  ALWAYS_ON    = 0,       /* zero is on */
  CHECK_INTV   = 100000,  /* 100ms == 100000us */
  MAX_CHECKS   = 100,     /* 100 checks times 100ms == 10 seconds */
  BREATHE_INTV = 1000,    /* 1ms == 1000us */
  BREATHE_STEP = 100,     /* steps in brightness */
  VU_MAX       = 65534,   /* max vu brightness */
  HZ_MAX       = 40       /* No clue where I got this from 😅 but hey it works! */
};

/* (RGB)LED variables */
volatile int n_checks = 0, updown = 1;
volatile uint64_t us_now = 0;
volatile uint16_t vu = 0;
volatile uint64_t start_us = 0;

/* RGB LED */
#if defined(USE_RGB)
static double r_, g_, b_ = 0;
int _rgb = 0;
int c1, c2, c3;
unsigned char o1 = 0, o2 = 0, o3 = 0;
#endif


/* Init the RGB LED */
void init_rgb(void)
{
  #if defined(USE_RGB)
  _rgb = RANDVAL(0, 5);  /* Select random color to start with */
  r_ = g_ = b_ = 0;
  rgb_value = (UGRB_U32(r_,g_,b_) << 8u);
  dma_hw->multi_channel_trigger = (1u << dma_rgbled);
  #endif
  return;
}

/* Init the Vu */
void init_vu(void)
{
  /* Start the Vu statemachines */
  setup_vu();

  /* Init RGB LED */
  init_rgb();

  /* Apply us once on init */
  us_now = to_us_since_boot(get_absolute_time());
}


/* LED SORCERERS */

/* It goes up and it goes down */
void led_vumeter_task(void)
{ /* Only the lower 8 bits of each oscillator frequency is used
     Adding the higher 8 bits only makes de LED brighter and the Vu uglier */
  #if LED_PWM
  if (to_us_since_boot(get_absolute_time()) - start_us < usbsid_config.raster_rate) {  /* Wait time by rasterrate */
    return;  /* not enough time, not complete a raster */
  }
  start_us = to_us_since_boot(get_absolute_time());
  if (usbdata == 1 && dtype != ntype) {
    /* LED always uses SID 1 */
    double osc1, osc2, osc3;
    osc1 = (sid_memory[0x00] * 0.596f);  /* Frequency in Hz of SID1 @ $D400 Oscillator 1 */
    osc2 = (sid_memory[0x07] * 0.596f);  /* Frequency in Hz of SID1 @ $D400 Oscillator 2 */
    osc3 = (sid_memory[0x0E] * 0.596f);  /* Frequency in Hz of SID1 @ $D400 Oscillator 3 */

    vu = fabsl(((osc1 + osc2 + osc3) / 3.0f));
    vu = MAP(vu, 0, HZ_MAX, 0, VU_MAX);
    if (usbsid_config.LED.enabled) {
      pwm_value = (uint16_t)vu;
      dma_hw->multi_channel_trigger = (1u << dma_pwmled);
    }

    #if defined(USE_RGB)
    if(usbsid_config.RGBLED.enabled) {
      int sidno = (usbsid_config.RGBLED.sid_to_use - 1) * 0x20;
      rgb_value = (
        (UGRB_U32(
          RGBB((sid_memory[0x00 + sidno] * 0.596f),usbsid_config.RGBLED.brightness),
          RGBB((sid_memory[0x07 + sidno] * 0.596f),usbsid_config.RGBLED.brightness),
          RGBB((sid_memory[0x0E + sidno] * 0.596f),usbsid_config.RGBLED.brightness))
        ) << 8u);
      dma_hw->multi_channel_trigger = (1u << dma_rgbled);
    }
    #endif

    usMEM("[%c:%d][PWM]$%04x[V1]$%02X%02X$%02X%02X$%02X$%02X$%02X[V2]$%02X%02X$%02X%02X$%02X$%02X$%02X[V3]$%02X%02X$%02X%02X$%02X$%02X$%02X[FC]$%02x%02x$%02x[VOL]$%02x\n",
      dtype, usbdata, vu,
      sid_memory[0x01], sid_memory[0x00], sid_memory[0x03], sid_memory[0x02], sid_memory[0x04], sid_memory[0x05], sid_memory[0x06],
      sid_memory[0x08], sid_memory[0x07], sid_memory[0x0A], sid_memory[0x09], sid_memory[0x0B], sid_memory[0x0C], sid_memory[0x0D],
      sid_memory[0x0F], sid_memory[0x0E], sid_memory[0x11], sid_memory[0x10], sid_memory[0x12], sid_memory[0x13], sid_memory[0x14],
      sid_memory[0x16], sid_memory[0x15], sid_memory[0x17], sid_memory[0x18]);

  }
  return;
  #endif
}

/* Mouth breather! */
void led_breathe_task(void)
{
  #if LED_PWM
  if (usbdata == 0 && dtype == ntype) {
    if (to_us_since_boot(get_absolute_time()) - start_us < (uint32_t)BREATHE_INTV) {
      return;  /* not enough time since last check */
    }
    start_us = to_us_since_boot(get_absolute_time());

    if (pwm_value >= VU_MAX) {
      updown = 0;
    }
    if (pwm_value <= 0) {
      updown = 1;
      #ifdef USE_RGB
      _rgb = RANDVAL(0, 5);  /* Select random color when at 0 brightness */
      #endif
    }

    if (updown == 1 && pwm_value <= VU_MAX)
      pwm_value += BREATHE_STEP;

    if (updown == 0 && pwm_value >= 0)
      pwm_value -= BREATHE_STEP;
    if (usbsid_config.LED.enabled && usbsid_config.LED.idle_breathe) {
      dma_hw->multi_channel_trigger = (1u << dma_pwmled);
    }
    #if defined(USE_RGB)
    if (usbsid_config.RGBLED.enabled && usbsid_config.RGBLED.idle_breathe) {
      int rgb_ = MAP(pwm_value, 0, VU_MAX, 0, 31);
      r_ = (_rgb == 0 || _rgb == 3 || _rgb == 5) ? (rgb_ * 8) : 0;
      g_ = (_rgb == 1 || _rgb == 3 || _rgb == 4) ? (rgb_ * 8) : 0;
      b_ = (_rgb == 2 || _rgb == 4 || _rgb == 5) ? (rgb_ * 8) : 0;
      rgb_value = (
        (UGRB_U32(
          RGBB(r_,usbsid_config.RGBLED.brightness),
          RGBB(g_,usbsid_config.RGBLED.brightness),
          RGBB(b_,usbsid_config.RGBLED.brightness))
        ) << 8u);
      dma_hw->multi_channel_trigger = (1u << dma_rgbled);
    } else {
      rgb_value = ((UGRB_U32(0,0,0)) << 8u);
      dma_hw->multi_channel_trigger = (1u << dma_rgbled);
    }
    #endif
  }
  return;
  #endif
}

void led_runner(void)
{ /* Called from Core 1 */
  usbdata == 1 ? led_vumeter_task() : led_breathe_task();
  if (to_us_since_boot(get_absolute_time()) - us_now < CHECK_INTV) { /* 0.1 second */
    /* NOTICE: Testfix for core1 setting dtype to 0 */
    /* do nothing */
    return;
  } else {  /* Check if we need to reset the Vu to breathing */
    us_now = to_us_since_boot(get_absolute_time());
    if (vu == 0 && usbdata == 1) {
      n_checks++;
      if (n_checks >= MAX_CHECKS) { /* 100 checks */
        n_checks = 0, usbdata = 0, dtype = ntype;  /* NOTE: This sets dtype to 0 which causes buffertask write to go to default and error out with many consecutive reads from the bus */
        offload_ledrunner = false;
        /**
         * @brief Let's make sure we always reset the write order
         * when the interface is no longer mounted
         */
        extern bool tud_midi_n_mounted (uint8_t itf);
        if (!tud_midi_n_mounted(MIDI_ITF)) {
          extern void reset_asid_to_writeorder(void);
          reset_asid_to_writeorder();
        }
      }
    }
    return;
  }
}
