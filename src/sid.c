/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid.c
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
#include "usbsid.h"
#include "sid.h"
#include "logging.h"


/* usbsid.c */
#ifdef ONBOARD_EMULATOR
extern uint8_t *sid_memory;
#else
extern uint8_t sid_memory[(0x20 * 4)];
#endif
extern int usbdata;

/* config.c */
extern Config usbsid_config;
extern RuntimeCFG cfg;

/* vu.c */
extern uint16_t vu;

/* bus.c */
extern void clockcycle_delay(uint32_t n_cycles);
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);

/* (hot) locals */
static bool paused_state, reset_state;
static uint8_t volume_state[4] = {0};


static void log_memory(uint8_t * sid_memory)
{
  usDBG("[%c:%d][PWM]$%04x[V1]$%02X%02X$%02X%02X$%02X$%02X$%02X[V2]$%02X%02X$%02X%02X$%02X$%02X$%02X[V3]$%02X%02X$%02X%02X$%02X$%02X$%02X[FC]$%02x%02x$%02x[VOL]$%02x\n",
    dtype, usbdata, vu,
    sid_memory[0x01], sid_memory[0x00], sid_memory[0x03], sid_memory[0x02], sid_memory[0x04], sid_memory[0x05], sid_memory[0x06],
    sid_memory[0x08], sid_memory[0x07], sid_memory[0x0A], sid_memory[0x09], sid_memory[0x0B], sid_memory[0x0C], sid_memory[0x0D],
    sid_memory[0x0F], sid_memory[0x0E], sid_memory[0x11], sid_memory[0x10], sid_memory[0x12], sid_memory[0x13], sid_memory[0x14],
    sid_memory[0x16], sid_memory[0x15], sid_memory[0x17], sid_memory[0x18]);
  return;
}

void init_sid_chips(void)
{
  /* Unfinished */
  log_memory(sid_memory);
  return;
}

/**
 * @brief Set the globally used sid_memory to all zeroes
 */
void clear_sid_memory(void)
{
  memset(sid_memory, 0, count_of(sid_memory));
  return;
}

/**
 * @brief Clears the internally used volume state
 */
void clear_volume_state(void)
{
  memset(volume_state, 0, 4);
  return;
}

/**
 * @brief Set the reset state to true or false
 *
 * @param state Boolean
 */
void set_reset_state(bool state)
{
  reset_state = state;
  return;
}

/**
 * @brief Get the reset state value
 *
 */
bool get_reset_state(void)
{
  return reset_state;
}

/**
 * @brief Set the paused state to true or false
 *
 * @param state Boolean
 */
void set_paused_state(bool state)
{
  paused_state = state;
  return;
}

/**
 * @brief Get the paused state value
 *
 */
bool get_paused_state(void)
{
  return paused_state;
}

/**
 * @brief unmute all sid's
 */
void unmute_sid(void)
{
  usDBG("[UNMUTE]\n");
  /* is_muted = false; */ /* Is globally handled from usbsid.c */
  for (int i = 0; i < cfg.numsids; i++) {
    uint8_t addr = ((0x20 * i) + 0x18);
    if ((volume_state[i] & 0xF) == 0) volume_state[i] = (volume_state[i] & 0xF0) | 0x0E;
    sid_memory[addr] = volume_state[i];
    cycled_write_operation(((0x20 * i) + 0x18), volume_state[i], 0);  /* Volume back */
    usDBG("[%d] $%02X:%02X\n", i, addr, volume_state[i]);
  }
  return;
}

/**
 * @brief mute all sid's
 */
void mute_sid(void)
{
  usDBG("[MUTE]\n");
  for (int i = 0; i < cfg.numsids; i++) {
    uint8_t addr = ((0x20 * i) + 0x18);
    volume_state[i] = sid_memory[addr];
    cycled_write_operation(addr, (volume_state[i] & 0xF0), 0);  /* Volume to 0 */
    usDBG("[%d] $%02X:%02X\n", i, addr, (volume_state[i] & 0xF0));
  }
  /* is_muted = true; */ /* Is globally handled from usbsid.c */
  return;
}

void enable_sid(bool unmute)
{
  set_paused_state(false);
  // set_gpio(RES, 1);
  sPIN(RES);
  if (unmute) unmute_sid();
  return;
}

void disable_sid(void)
{
  set_paused_state(true);
  mute_sid();
  // set_gpio(CS1, 1);
  // set_gpio(CS2, 1);
  // set_gpio(RES, 0);
  sPIN(CS1);
  sPIN(CS2);
  cPIN(RES);
  return;
}

void clear_bus(int sidno)
{
  cycled_write_operation((sidno * 0x20), 0x0, 0);
  return;
}

void clear_bus_all(void)
{
  for (int sid = 0; sid < cfg.numsids; sid++) {
    clear_bus(sid);
  }
  return;
}

void pause_sid(void)
{
  // set_gpio(CS1, 1);
  // set_gpio(CS2, 1);
  sPIN(CS1);
  sPIN(CS2);
  return;
}

void pause_sid_withmute(void)
{
  usDBG("[PAUSE STATE PRE] %d\n", paused_state);
  if (!paused_state) mute_sid();
  if (paused_state) unmute_sid();
  // set_gpio(CS1, 1);
  // set_gpio(CS2, 1);
  pause_sid();
  set_paused_state(!paused_state);
  usDBG("[PAUSE STATE POST] %d\n", paused_state);
  return;
}

void reset_sid(void)
{
  set_reset_state(true);
  set_paused_state(false);
  memset(volume_state, 0, 4);
  memset(sid_memory, 0, count_of(sid_memory));
  cPIN(RES);
  if (cfg.chip_one == 0 || cfg.chip_two == 0) {
    /* 10x PHI1(02) cycles as per datasheet for REAL SIDs only */
    clockcycle_delay(10);
  }
  sPIN(RES);
  set_reset_state(false);
  return;
}

/**
 * @brief Clear SID register / reset registers
 *        6 cycle delay for each write to simulate LDA (2) + STA (4)
 * @note https://csdb.dk/forums/?roomid=11&topicid=85713&showallposts=1
 * @note thanks Wilfred for pointing this out!
 * @param int sidno
 */
void clear_sid_registers(int sidno)
{
  for (uint reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    cycled_write_operation(((sidno * 0x20) | sid_registers[reg]), 0xff, 6);
  }
  for (uint reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    cycled_write_operation(((sidno * 0x20) | sid_registers[reg]), 0x08, 6);
  }
  for (uint reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    cycled_write_operation(((sidno * 0x20) | sid_registers[reg]), 0x0, 6);
  }
  memset(sid_memory, 0, (4 * 0x20));
  return;
}

/**
 * @brief Reset registers on everyo installed SID
 * @note SIDKICK-pico v0.1 might have issues
 *
 */
void reset_sid_registers(void)
{
  set_reset_state(true);
  set_paused_state(false);
  for (int sid = 0; sid < cfg.numsids; sid++) {
    clear_sid_registers(sid);
  }
  set_reset_state(false);
  return;
}
