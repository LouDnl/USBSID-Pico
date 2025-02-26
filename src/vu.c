/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2 (RP2350) based board for
 * interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * vu.c
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
#if defined(USE_RGB)
#include "ws2812.pio.h"  /* RGB led handler */
#endif


/* Init external vars */
extern Config usbsid_config;  /* usbsid.c */
extern uint8_t __not_in_flash("usbsid_buffer") sid_memory[(0x20 * 4)] __attribute__((aligned(2 * (0x20 * 4))));  /* usbsid.c */
extern int usbdata;  /* usbsid.c */
extern int numsids;  /* config.c */

/* LED breathe levels */
enum
{
  ALWAYS_OFF = 99999,
  ALWAYS_ON = 0,
  CHECK_INTV = 100,
  MAX_CHECKS = 200,  /* 200 checks times 100ms == 20 seconds */
  BREATHE_INTV = 1,
  BREATHE_STEP = 100,
  VU_MAX = 65534,
  HZ_MAX = 40
};

/* (RGB)LED variables */
int pwm_value = 0, updown = 1;
int n_checks = 0, m_now = 0;
uint16_t vu = 0;
uint32_t start_ms = 0;
uint32_t breathe_interval_ms = BREATHE_INTV;

/* RGB LED */
#if defined(USE_RGB)
#define IS_RGBW false
PIO pio_rgb = pio1;
uint ws2812_sm, offset_ws2812;
int _rgb = 0;
double r_ = 0, g_ = 0, b_ = 0;
uint8_t br = 0;
int c1, c2, c3;
unsigned char o1 = 0, o2 = 0, o3 = 0;
const __not_in_flash("usbsid_data") unsigned char color_LUT[43][6][3] =
{
  /* Red Green Blue Yellow Cyan Purple*/
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{6, 0, 0}, {0, 6, 0}, {0, 0, 6}, {6, 6, 0}, {0, 6, 6}, {6, 0, 6}},
  {{12, 0, 0}, {0, 12, 0}, {0, 0, 12}, {12, 12, 0}, {0, 12, 12}, {12, 0, 12}},
  {{18, 0, 0}, {0, 18, 0}, {0, 0, 18}, {18, 18, 0}, {0, 18, 18}, {18, 0, 18}},
  {{24, 0, 0}, {0, 24, 0}, {0, 0, 24}, {24, 24, 0}, {0, 24, 24}, {24, 0, 24}},
  {{30, 0, 0}, {0, 30, 0}, {0, 0, 30}, {30, 30, 0}, {0, 30, 30}, {30, 0, 30}},
  {{36, 0, 0}, {0, 36, 0}, {0, 0, 36}, {36, 36, 0}, {0, 36, 36}, {36, 0, 36}},
  {{42, 0, 0}, {0, 42, 0}, {0, 0, 42}, {42, 42, 0}, {0, 42, 42}, {42, 0, 42}},
  {{48, 0, 0}, {0, 48, 0}, {0, 0, 48}, {48, 48, 0}, {0, 48, 48}, {48, 0, 48}},
  {{54, 0, 0}, {0, 54, 0}, {0, 0, 54}, {54, 54, 0}, {0, 54, 54}, {54, 0, 54}},
  {{60, 0, 0}, {0, 60, 0}, {0, 0, 60}, {60, 60, 0}, {0, 60, 60}, {60, 0, 60}},
  {{66, 0, 0}, {0, 66, 0}, {0, 0, 66}, {66, 66, 0}, {0, 66, 66}, {66, 0, 66}},
  {{72, 0, 0}, {0, 72, 0}, {0, 0, 72}, {72, 72, 0}, {0, 72, 72}, {72, 0, 72}},
  {{78, 0, 0}, {0, 78, 0}, {0, 0, 78}, {78, 78, 0}, {0, 78, 78}, {78, 0, 78}},
  {{84, 0, 0}, {0, 84, 0}, {0, 0, 84}, {84, 84, 0}, {0, 84, 84}, {84, 0, 84}},
  {{90, 0, 0}, {0, 90, 0}, {0, 0, 90}, {90, 90, 0}, {0, 90, 90}, {90, 0, 90}},
  {{96, 0, 0}, {0, 96, 0}, {0, 0, 96}, {96, 96, 0}, {0, 96, 96}, {96, 0, 96}},
  {{102, 0, 0}, {0, 102, 0}, {0, 0, 102}, {102, 102, 0}, {0, 102, 102}, {102, 0, 102}},
  {{108, 0, 0}, {0, 108, 0}, {0, 0, 108}, {108, 108, 0}, {0, 108, 108}, {108, 0, 108}},
  {{114, 0, 0}, {0, 114, 0}, {0, 0, 114}, {114, 114, 0}, {0, 114, 114}, {114, 0, 114}},
  {{120, 0, 0}, {0, 120, 0}, {0, 0, 120}, {120, 120, 0}, {0, 120, 120}, {120, 0, 120}},
  {{126, 0, 0}, {0, 126, 0}, {0, 0, 126}, {126, 126, 0}, {0, 126, 126}, {126, 0, 126}},
  {{132, 0, 0}, {0, 132, 0}, {0, 0, 132}, {132, 132, 0}, {0, 132, 132}, {132, 0, 132}},
  {{138, 0, 0}, {0, 138, 0}, {0, 0, 138}, {138, 138, 0}, {0, 138, 138}, {138, 0, 138}},
  {{144, 0, 0}, {0, 144, 0}, {0, 0, 144}, {144, 144, 0}, {0, 144, 144}, {144, 0, 144}},
  {{150, 0, 0}, {0, 150, 0}, {0, 0, 150}, {150, 150, 0}, {0, 150, 150}, {150, 0, 150}},
  {{156, 0, 0}, {0, 156, 0}, {0, 0, 156}, {156, 156, 0}, {0, 156, 156}, {156, 0, 156}},
  {{162, 0, 0}, {0, 162, 0}, {0, 0, 162}, {162, 162, 0}, {0, 162, 162}, {162, 0, 162}},
  {{168, 0, 0}, {0, 168, 0}, {0, 0, 168}, {168, 168, 0}, {0, 168, 168}, {168, 0, 168}},
  {{174, 0, 0}, {0, 174, 0}, {0, 0, 174}, {174, 174, 0}, {0, 174, 174}, {174, 0, 174}},
  {{180, 0, 0}, {0, 180, 0}, {0, 0, 180}, {180, 180, 0}, {0, 180, 180}, {180, 0, 180}},
  {{186, 0, 0}, {0, 186, 0}, {0, 0, 186}, {186, 186, 0}, {0, 186, 186}, {186, 0, 186}},
  {{192, 0, 0}, {0, 192, 0}, {0, 0, 192}, {192, 192, 0}, {0, 192, 192}, {192, 0, 192}},
  {{198, 0, 0}, {0, 198, 0}, {0, 0, 198}, {198, 198, 0}, {0, 198, 198}, {198, 0, 198}},
  {{204, 0, 0}, {0, 204, 0}, {0, 0, 204}, {204, 204, 0}, {0, 204, 204}, {204, 0, 204}},
  {{210, 0, 0}, {0, 210, 0}, {0, 0, 210}, {210, 210, 0}, {0, 210, 210}, {210, 0, 210}},
  {{216, 0, 0}, {0, 216, 0}, {0, 0, 216}, {216, 216, 0}, {0, 216, 216}, {216, 0, 216}},
  {{222, 0, 0}, {0, 222, 0}, {0, 0, 222}, {222, 222, 0}, {0, 222, 222}, {222, 0, 222}},
  {{228, 0, 0}, {0, 228, 0}, {0, 0, 228}, {228, 228, 0}, {0, 228, 228}, {228, 0, 228}},
  {{234, 0, 0}, {0, 234, 0}, {0, 0, 234}, {234, 234, 0}, {0, 234, 234}, {234, 0, 234}},
  {{240, 0, 0}, {0, 240, 0}, {0, 0, 240}, {240, 240, 0}, {0, 240, 240}, {240, 0, 240}},
  {{246, 0, 0}, {0, 246, 0}, {0, 0, 246}, {246, 246, 0}, {0, 246, 246}, {246, 0, 246}},
  {{252, 0, 0}, {0, 252, 0}, {0, 0, 252}, {252, 252, 0}, {0, 252, 252}, {252, 0, 252}}
};

void put_pixel(uint32_t pixel_grb) {
  pio_sm_put(pio_rgb, ws2812_sm, pixel_grb << 8u);
  return;
}
#endif

/* Init the RGB LED pio */
void init_rgb(void)
{
  #if defined(USE_RGB)
  _rgb = randval(0, 5);
  r_ = g_ = b_ = 0;
  offset_ws2812 = pio_add_program(pio_rgb, &ws2812_program);
  ws2812_sm = 0;  /* PIO1 SM0 */
  pio_sm_claim(pio_rgb, ws2812_sm);
  ws2812_program_init(pio_rgb, ws2812_sm, offset_ws2812, 23, 800000, IS_RGBW);
  put_pixel(urgb_u32(0,0,0));
  #endif
  return;
}

/* LED SORCERERS */

/* It goes up and it goes down */
void led_vumeter_task(void)
{
  #if LED_PWM
  if (to_ms_since_boot(get_absolute_time()) - start_ms < breathe_interval_ms) {
    return;  /* not enough time */
  }
  start_ms = to_ms_since_boot(get_absolute_time());
  if (usbdata == 1 && dtype != ntype) {
    /* LED always uses SID 1 */
    uint8_t osc1, osc2, osc3;
    osc1 = (sid_memory[0x00] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 1 */
    osc2 = (sid_memory[0x07] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 2 */
    osc3 = (sid_memory[0x0E] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 3 */

    vu = abs((osc1 + osc2 + osc3) / 3.0f);
    vu = map(vu, 0, HZ_MAX, 0, VU_MAX);
    if (usbsid_config.LED.enabled) {
      pwm_set_gpio_level(BUILTIN_LED, vu);
    }

    #if defined(USE_RGB)
    if(usbsid_config.RGBLED.enabled) {
      static int sidno;// = (usbsid_config.RGBLED.sid_to_use - 1);
      sidno = usbsid_config.RGBLED.sid_to_use;
      static double rgb_osc1, rgb_osc2, rgb_osc3;
      rgb_osc1 = (sid_memory[0x00 + ((sidno - 1) * 0x20)] * 0.596);  /* Frequency in Hz of SID2 @ $D420 Oscillator 1 */
      rgb_osc2 = (sid_memory[0x07 + ((sidno - 1) * 0x20)] * 0.596);  /* Frequency in Hz of SID2 @ $D420 Oscillator 2 */
      rgb_osc3 = (sid_memory[0x0E + ((sidno - 1) * 0x20)] * 0.596);  /* Frequency in Hz of SID2 @ $D420 Oscillator 3 */
      o1 = map(rgb_osc1, 0, 255, 0, 43);
      o2 = map(rgb_osc2, 0, 255, 0, 43);
      o3 = map(rgb_osc3, 0, 255, 0, 43);
      c1 = ((sidno % 2) == 1) ? 0 : 3;
      c2 = ((sidno % 2) == 1) ? 1 : 4;
      c3 = ((sidno % 2) == 1) ? 2 : 5;
      r_ = (color_LUT[o1][c1][0] + color_LUT[o2][c2][0] + color_LUT[o3][c3][0]);
      g_ = (color_LUT[o1][c1][1] + color_LUT[o2][c2][1] + color_LUT[o3][c3][1]);
      b_ = (color_LUT[o1][c1][2] + color_LUT[o2][c2][2] + color_LUT[o3][c3][2]);
      br = usbsid_config.RGBLED.brightness;
      put_pixel(urgb_u32(rgbb(r_,br),rgbb(g_,br),rgbb(b_,br)));

      /* Color LED debugging ~ uncomment for testing */
      /* DBG("[%c][PWM]$%04x [R]%g [G]%g [B]%g [RO1]%g [RO2]%g [RO3]%g [O1]%d [O2]%d [O3]%d [C1]%d [C2]%d [C3]%d\n",
        dtype, vu, r_, g_, b_, rgb_osc1, rgb_osc2, rgb_osc3, o1, o2, o3, c1, c2, c3); */
    }
    #endif

    MDBG("[%c:%d][PWM]$%04x[V1]$%02X%02X$%02X%02X$%02X$%02X$%02X[V2]$%02X%02X$%02X%02X$%02X$%02X$%02X[V3]$%02X%02X$%02X%02X$%02X$%02X$%02X[FC]$%02x%02x$%02x[VOL]$%02x\n",
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
    if (to_ms_since_boot(get_absolute_time()) - start_ms < breathe_interval_ms) {
      return;  /* not enough time */
    }
    start_ms = to_ms_since_boot(get_absolute_time());

    if (pwm_value >= VU_MAX) {
      updown = 0;
    }
    if (pwm_value <= 0) {
      updown = 1;
      #ifdef USE_RGB
      _rgb = randval(0, 5);
      #endif
    }

    if (updown == 1 && pwm_value <= VU_MAX)
      pwm_value += BREATHE_STEP;

    if (updown == 0 && pwm_value >= 0)
      pwm_value -= BREATHE_STEP;
    if (usbsid_config.LED.enabled && usbsid_config.LED.idle_breathe) {
      pwm_set_gpio_level(BUILTIN_LED, pwm_value);
    }
    #if defined(USE_RGB)
    if (usbsid_config.RGBLED.enabled && usbsid_config.RGBLED.idle_breathe) {
      int rgb_ = map(pwm_value, 0, VU_MAX, 0, 43);
      r_ = color_LUT[rgb_][_rgb][0];
      g_ = color_LUT[rgb_][_rgb][1];
      b_ = color_LUT[rgb_][_rgb][2];
      br = usbsid_config.RGBLED.brightness;
      put_pixel(urgb_u32(rgbb(r_,br),rgbb(g_,br),rgbb(b_,br)));
    } else {
      put_pixel(urgb_u32(0,0,0));
    }
    #endif
  }
  return;
  #endif
}

void led_runner(void)
{ /* Called from Core 1 */
  usbdata == 1 ? led_vumeter_task() : led_breathe_task();
  if (to_ms_since_boot(get_absolute_time()) - m_now < CHECK_INTV) { /* 20 seconds */
    /* NOTICE: Testfix for core1 setting dtype to 0 */
    /* do nothing */
    return;
  } else {
    m_now = to_ms_since_boot(get_absolute_time());
    if (vu == 0 && usbdata == 1) {
      n_checks++;
      if (n_checks >= MAX_CHECKS)
      {
        n_checks = 0, usbdata = 0, dtype = ntype;  /* NOTE: This sets dtype to 0 which causes buffertask write to go to default and error out with many consecutive reads from the bus */
      }
    }
    return;
  }
}
