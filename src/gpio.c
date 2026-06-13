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

#include <globals.h>
#include <usbsid_constants.h>
#include <config.h>
#include <gpio_defs.h>
#include <logging.h>
#include <sid.h>


#if PCB_VERSION_INT >= 15
uint32_t r_state = 0;
#endif


/**
 * @brief read gpio bus state in or out
 * @param int direction ~ 1 = out, 0 = in
 *
 * @return uint32_t the complete bus state
 */
static inline uint32_t read_bus(int direction)
{
  if (direction)
    return sio_hw->gpio_out;
  else
    return sio_hw->gpio_in;
}

/**
 * @brief Initialise VCC, VDD & Voltage control
 * default states at boot for v1.5+ boards
 *
 * 5v = On for both sockets (not independently controllable)
 * 9v = Set for both sockets (independently controllable)
 * 9v = On for both sockets (not independently controllable)
 *
 * Safe initialisation order
 * set direction -> set pin -> set output/input
 */
void init_vccvdd_control(void)
{
  /* GPIO defaults for jumperless */
#if PCB_VERSION_INT >= 15
  gpio_set_dir(HV1_SEL, GPIO_OUT);
  cPIN(HV1_SEL);  /* Clear hv1 to set 9v */
  gpio_set_function(HV1_SEL, GPIO_FUNC_SIO);

  gpio_set_dir(HV2_SEL, GPIO_OUT);
  cPIN(HV2_SEL);  /* Clear hv2 to set 9v */
  gpio_set_function(HV2_SEL, GPIO_FUNC_SIO);

  gpio_set_dir(SIDVCC_EN, GPIO_OUT);
  gpio_set_dir(SIDHV_EN, GPIO_OUT);

  /* Remember, v1.4 has a leaky 5v, this can cause desync like behaviour */
  /* Also remember that the RES pin needs to be deasserted _before_ enabling voltage!!
     Otherwise this will ALSO cause desync like behaviour */
  if (config_unacknowledged()) {
    cPIN(SIDVCC_EN);
    cPIN(SIDHV_EN);
  } else {
    cPIN(RES);       /* hold reset before powering on */
    sPIN(SIDVCC_EN);
    sPIN(SIDHV_EN);
    sleep_ms(200);   /* wait for regulator + FPGASID FPGA config */
    sPIN(RES);       /* release - FPGASID initializes cleanly */
  }
  gpio_set_function(SIDVCC_EN, GPIO_FUNC_SIO);
  gpio_set_function(SIDHV_EN, GPIO_FUNC_SIO);
#endif
}

/**
 * @brief Initialise CS1, CS2, RW & RES pin
 * states at boot
 *
 * Safe initialisation order
 * set direction -> set pin -> set output/input
 */
void init_bus_control(void)
{
  /* GPIO defaults for PIO bus */
  gpio_set_dir(RES, GPIO_OUT);
#if PCB_VERSION_INT >= 15
  cPIN(RES);  /* Deassert reset pin on boot immediately */
#else
  sPIN(RES);  /* Enable the SID directly on boot v1.3 (SIDEmu hardfaults otherwise) */
#endif
  gpio_set_function(RES, GPIO_FUNC_SIO);

  gpio_set_dir(CS1, GPIO_OUT);
  sPIN(CS1);
  gpio_set_function(CS1, GPIO_FUNC_SIO);

  gpio_set_dir(CS2, GPIO_OUT);
  sPIN(CS2);
  gpio_set_function(CS2, GPIO_FUNC_SIO);

  gpio_set_dir(RW, GPIO_OUT);
  sPIN(RW);
  gpio_set_function(RW, GPIO_FUNC_SIO);
}

/**
 * @brief Initialise audio switch state at boot
 *
 * Safe initialisation order
 * set direction -> set pin -> set output/input
 */
void init_audio_switch(void)
{
  /* GPIO defaults for audio switch */
#if PCB_VERSION_INT >= 13
  gpio_set_dir(AU_SW, GPIO_OUT);
  gpio_put(AU_SW, usbsid_config.stereo_en);  /* Default on ~ stereo */
  gpio_set_function(AU_SW, GPIO_FUNC_SIO);
#endif
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
    r |= c = (read_bus(0) & bPIN(PHI1)) >> PHI1;
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
#if PCB_VERSION_INT >= 13
  if (!usbsid_config.lock_audio_sw) {
    int audio_state = (read_bus(1) & bPIN(AU_SW)) >> AU_SW; /* Pinpoint current audio switch state */
    audio_state ^= 1;
    usCFG("Toggle audio switch to: %d (%s)\n", (int)audio_state, monostereo_str((int)audio_state));
    tPIN(AU_SW);  /* toggle mono <-> stereo */
  } else {
    usCFG("Audio switch is locked at %d (%s), toggle not applied\n",
      (int)usbsid_config.stereo_en, monostereo_str((int)usbsid_config.stereo_en));
    return;
  }
#endif
  return;
}

/**
 * @brief Set the audio switch to on or off
 * @note only applicable for PCB v1.3
 *
 * @param boolean switch state
 */
void set_audio_switch(bool state)
{ /* Set the SPST switch */
#if PCB_VERSION_INT >= 13
  if (!usbsid_config.lock_audio_sw) {
    usCFG("Set audio switch to: %d (%s)\n", (int)state, monostereo_str((int)state));
    if (state) {
      sPIN(AU_SW);       /* set   mono <-> stereo pin */
    } else cPIN(AU_SW);  /* clear mono <-> stereo pin */
  } else {
    usCFG("Audio switch is locked at %d (%s), requested change to %d (%s) ignored\n",
      (int)usbsid_config.stereo_en, monostereo_str((int)usbsid_config.stereo_en), state, monostereo_str((int)state));
    return;
  }
#else
  (void)state;
#endif
  return;
}

/**
 * @brief Set the SID 5v state
 *
 * @param bool state (false == off, true == on)
 */
void set_SID5v_state(bool state)
{
#if PCB_VERSION_INT >= 15
  r_state = (read_bus(1) & bPIN(SIDVCC_EN)) >> SIDVCC_EN;
  if (state == r_state) {
    /* usCFG("SID 5v is already at state '%s', requested: '%s', state change ignored.\n",
      (r_state ? "on" : "off"), (state ? "on" : "off")); */
    return;
  } else {
    usCFG("SID 5v from '%s' to '%s'\n",
      (r_state ? "on" : "off"), (state ? "on" : "off"));
    if (state) {
      sPIN(SIDVCC_EN);
    } else {
      cPIN(SIDVCC_EN);
    }
  }
#else
  (void)state;
#endif
  return;
}

/**
 * @brief Set the SID hv state
 *
 * @param bool state (false == off, true == on)
 */
void set_SIDhv_state(bool state)
{
#if PCB_VERSION_INT >= 15
  r_state = (read_bus(1) & bPIN(SIDHV_EN)) >> SIDHV_EN;
  if (state == r_state) {
    /* usCFG("SID hv is already at state '%s', requested: '%s', state change ignored.\n",
      (r_state ? "on" : "off"), (state ? "on" : "off")); */
    return;
  } else {
    usCFG("SID hv from '%s' to '%s'\n",
      (r_state ? "on" : "off"), (state ? "on" : "off"));
    if (state) {
      sPIN(SIDHV_EN);
    } else {
      cPIN(SIDHV_EN);
    }
  }
#else
  (void)state;
#endif
  return;
}

/**
 * @brief Set SID1 highvoltage to 9v or 12v
 *
 * @param bool state (false == 9v, true == 12v)
 */
void set_SID1_highvoltage(bool state)
{
#if PCB_VERSION_INT >= 15
  r_state = (read_bus(1) & bPIN(HV1_SEL)) >> HV1_SEL;
  if (state == r_state) {
    /* usCFG("SID1 hv is already at state '%s', requested: '%s', state change ignored.\n",
      (r_state ? "12v" : "9v"), (state ? "12v" : "9v")); */
    return;
  } else {
    usCFG("SID1 hv from '%s' to '%s'\n",
      (r_state ? "12v" : "9v"), (state ? "12v" : "9v"));
    if (state) {
      sPIN(HV1_SEL);
    } else {
      cPIN(HV1_SEL);
    }
  }
#else
  (void)state;
#endif
  return;
}

/**
 * @brief Set SID2 highvoltage to 9v or 12v
 *
 * @param bool state (false == 9v, true == 12v)
 */
void set_SID2_highvoltage(bool state)
{
#if PCB_VERSION_INT >= 15
  r_state = (read_bus(1) & bPIN(HV2_SEL)) >> HV2_SEL;
  if (state == r_state) {
    /* usCFG("SID2 hv is already at state '%s', requested: '%s', state change ignored.\n",
      (r_state ? "12v" : "9v"), (state ? "12v" : "9v")); */
    return;
  } else {
    usCFG("SID2 hv from '%s' to '%s'\n",
      (r_state ? "12v" : "9v"), (state ? "12v" : "9v"));
    if (state) {
      sPIN(HV2_SEL);
    } else {
      cPIN(HV2_SEL);
    }
  }
#else
  (void)state;
#endif
  return;
}

/**
 * @brief Get the voltage pin states
 * @note 4 bits
 * @note HV2, HV1, Hv, 5v
 *
 * @return uint8_t pin_states
 */
uint8_t get_pin_states(void)
{
#if PCB_VERSION_INT >= 15
  r_state = read_bus(1);
  /* usDBG("r_state: 0b%032b\n",r_state); */

  uint8_t state = (
    (((r_state & bPIN(HV2_SEL)) >> HV2_SEL) << 3)
    | (((r_state & bPIN(HV1_SEL)) >> HV1_SEL) << 2)
    | (((r_state & bPIN(SIDHV_EN)) >> SIDHV_EN) << 1)
    | ((r_state & bPIN(SIDVCC_EN)) >> SIDVCC_EN)
  );
  /* usDBG("PINSTATES: 0b%04b\n", state); */

  return state;
#endif
  return 0;
}

/**
 * @brief Turn on both regulators
 *
 */
void voltage_state_on(void)
{
#if PCB_VERSION_INT >= 15
  set_SID5v_state(true);
  set_SIDhv_state(true);
#endif
  return;
}

/**
 * @brief Turn off both regulators
 *
 */
void voltage_state_off(void)
{
#if PCB_VERSION_INT >= 15
  set_SID5v_state(false);
  set_SIDhv_state(false);
#endif
  return;
}

/**
 * @brief Set the base SID voltages
 * This function sets 5v and 9v to both sockets as default
 * only if not already at that state
 *
 * @param uint16_t wait_ms ~ the time to sleep after voltage change
 */
void set_base_voltages(uint16_t wait_ms)
{
#if PCB_VERSION_INT >= 15
  if (get_pin_states() != 0b0011) {
    usCFG("Setting default base voltages\n");
    cPIN(RES);
    voltage_state_off();
    set_SID1_highvoltage(false); /* Set SID1 hv to 9v */
    set_SID2_highvoltage(false); /* Set SID2 hv to 9v */
    set_SID5v_state(true); /* Enable 5v for SID chips */
    set_SIDhv_state(true); /* Enable 9v (default on boot) for SID chips */
    sleep_ms(wait_ms);
    sPIN(RES);
  } else {
    usCFG("Base voltages already at default state\n");
    cPIN(RES);
    sleep_ms(wait_ms);
    sPIN(RES);
  }
#else
  (void)wait_ms;
#endif
  return;
}
