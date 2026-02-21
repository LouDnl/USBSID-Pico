/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * gpio.c
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
#include "gpio.h"
#include "logging.h"
#include "sid.h"


/* config.c */
extern Config usbsid_config;

/* config_logging.c */
extern char *mono_stereo[2];


/**
 * @brief read gpio in bus state
 *
 * @return uint32_t the complete bus state
 */
static inline uint32_t read_bus(void)
{
  return sio_hw->gpio_in;
}

/**
 * @brief Initialize default GPIO states at boot
 *
 */
void init_gpio(void)
{ /* GPIO defaults for PIO bus */
  gpio_set_dir(RES, GPIO_OUT);
  gpio_set_function(RES, GPIO_FUNC_SIO);
  cPIN(RES);  /* Make sure we do not yet enable the SID on boot! */
  gpio_init(CS1);
  gpio_init(CS2);
  gpio_init(RW);
  gpio_set_dir(CS1, GPIO_OUT);
  gpio_set_dir(CS2, GPIO_OUT);
  gpio_set_dir(RW, GPIO_OUT);
  sPIN(CS1);
  sPIN(CS2);
  sPIN(RW);
  /* GPIO defaults for audio switch */
  #if defined(HAS_AUDIOSWITCH)
  gpio_set_dir(AU_SW, GPIO_OUT);
  gpio_set_function(AU_SW, GPIO_FUNC_SIO);
  gpio_put(AU_SW, usbsid_config.stereo_en);  /* Default on ~ stereo */
  #endif
  return;
}

/**
 * @brief Detect clock signal at PHI1
 * @note only applicable for PCB v1.0
 *
 * @return int 1 for detected
 * @return int 0 for undetected
 */
int detect_clocksignal(void)
{
  usCFG("[DETECT CLOCK] START\n");
  int c = 0, r = 0;
  gpio_init(PHI1);
  gpio_set_pulls(PHI1, false, true);
  for (int i = 0; i < 20; i++) {
    r |= c = (read_bus() & bPIN(PHI1)) >> PHI1;
  }
  usCFG("[RESULT] %d: %s\n", r, (r == 0 ? "INTERNAL CLOCK" : "EXTERNAL CLOCK"));
  usCFG("[DETECT CLOCK] END\n");
  return r;  /* 1 if clock detected */
}

/**
 * @brief Toggle the audio switch on <-> off
 * @note only applicable for PCB v1.3
 *
 */
void toggle_audio_switch(void)
{ /* Toggle the SPST switch stereo <-> mono */
  #if defined(HAS_AUDIOSWITCH)
  if (!usbsid_config.lock_audio_sw) {
    int audio_state = (read_bus() & bPIN(AU_SW)) >> AU_SW; /* Pinpoint current audio switch state */
    audio_state ^= 1;
    usCFG("TOGGLE AUDIO SWITCH TO: %d (%s)\n", (int)audio_state, mono_stereo[(int)audio_state]);
    tPIN(AU_SW);  /* toggle mono <-> stereo */
  } else {
    usCFG("Audio switch is locked at %d (%s), toggle not applied\n",
      (int)usbsid_config.stereo_en, mono_stereo[(int)usbsid_config.stereo_en]);
    return;
  }
  #endif
}

/**
 * @brief Set the audio switch to on or off
 * @note only applicable for PCB v1.3
 *
 * @param boolean switch state
 */
void set_audio_switch(bool state)
{ /* Set the SPST switch */
  #if defined(HAS_AUDIOSWITCH)
  if (!usbsid_config.lock_audio_sw) {
    usCFG("SET AUDIO SWITCH TO: %d (%s)\n", (int)state, mono_stereo[(int)state]);
    if (state) {
      sPIN(AU_SW);       /* set   mono <-> stereo pin */
    } else cPIN(AU_SW);  /* clear mono <-> stereo pin */
  } else {
    usCFG("Audio switch is locked at %d (%s), requested change to %d (%s) ignored\n",
      (int)usbsid_config.stereo_en, mono_stereo[(int)usbsid_config.stereo_en], state, mono_stereo[state]);
    return;
  }
  #else
  (void)state;
  #endif
}
