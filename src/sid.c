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

#include "pico/util/queue.h"

#include <globals.h>
#include <usbsid.h>
#include <config.h>
#include <gpio_defs.h>
#include <bus.h>
#include <vu.h>
#include <sid.h>
#include <logging.h>


/* (hot) locals */
static volatile bool paused_state, reset_state, muted_state;
static uint8_t volume_state[4] = {0};


/**
 * @brief deprecated
 *
 * @param sid_memory
 */
static void __us_deprecated log_memory(uint8_t * sid_memory)
{
  usSID("[%c:%d][PWM]$%04x[V1]$%02X%02X$%02X%02X$%02X$%02X$%02X[V2]$%02X%02X$%02X%02X$%02X$%02X$%02X[V3]$%02X%02X$%02X%02X$%02X$%02X$%02X[FC]$%02x%02x$%02x[VOL]$%02x\n",
    dtype, is_receivedata(), get_vu_value(),
    sid_memory[0x01], sid_memory[0x00], sid_memory[0x03], sid_memory[0x02], sid_memory[0x04], sid_memory[0x05], sid_memory[0x06],
    sid_memory[0x08], sid_memory[0x07], sid_memory[0x0A], sid_memory[0x09], sid_memory[0x0B], sid_memory[0x0C], sid_memory[0x0D],
    sid_memory[0x0F], sid_memory[0x0E], sid_memory[0x11], sid_memory[0x10], sid_memory[0x12], sid_memory[0x13], sid_memory[0x14],
    sid_memory[0x16], sid_memory[0x15], sid_memory[0x17], sid_memory[0x18]);
  return;
}

/**
 * @brief deprecated
 *
 */
void __us_deprecated init_sid_chips(void)
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
  /* Clear the dirt */
  memset(sid_memory, 0, SID_MEMORY_SIZE); /* Always no more then 128 bytes */
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
 * @brief Set the muted state to true or false
 *
 * @param state Boolean
 */
void set_muted_state(bool state)
{
  muted_state = state;
  return;
}

/**
 * @brief Get the muted state value
 *
 */
bool get_muted_state(void)
{
  return muted_state;
}

/**
 * @brief init all states to their defaults
 *
 */
void init_sid_states(void)
{
  clear_sid_memory();      /* Zero out SID memory space */
  clear_volume_state();    /* Volume state to default */
  set_muted_state(false);  /* not muted */
  set_reset_state(false);  /* not in reset state */
  set_paused_state(false); /* not in paused state */
}

/**
 * @brief unmute all sid's
 */
void unmute_sid(void)
{
  /* is_muted = false; */ /* Is globally handled from usbsid.c */
  for (int i = 0; i < cfg.numsids; i++) {
    uint8_t addr = ((0x20 * i) + 0x18);
    if ((volume_state[i] & 0xF) == 0) volume_state[i] = (volume_state[i] & 0xF0) | 0x0E;
    sid_memory[addr] = volume_state[i];
    cycled_write_operation(((0x20 * i) + 0x18), volume_state[i], 0);  /* Volume back */
    usSID("[%d] $%02X:%02X\n", i, addr, volume_state[i]);
  }
  return;
}

/**
 * @brief mute all sid's
 */
void mute_sid(void)
{
  for (int i = 0; i < cfg.numsids; i++) {
    uint8_t addr = ((0x20 * i) + 0x18);
    volume_state[i] = sid_memory[addr];
    cycled_write_operation(addr, (volume_state[i] & 0xF0), 0);  /* Volume to 0 */
    usSID("[%d] $%02X:%02X\n", i, addr, (volume_state[i] & 0xF0));
  }
  /* is_muted = true; */ /* Is globally handled from usbsid.c */
  return;
}

/**
 * @brief Bring the SID(s) out of reset and clear the paused state
 *
 * @param bool unmute
 */
void enable_sid(bool unmute)
{
  set_paused_state(false);
  sPIN(RES);
  if (unmute) unmute_sid();
  return;
}

/**
 * @brief Mute and de-select the SID(s), and hold them in reset
 *
 * Sets the paused state, mutes all SIDs, deasserts both chip selects and
 * pulls the reset line low.
 */
void disable_sid(void)
{
  set_paused_state(true);
  mute_sid();
  sPIN(CS1);
  sPIN(CS2);
  cPIN(RES);
  return;
}

/**
 * @brief Write zero to address 0x00 of sidno so the bus is all zeroes
 *
 * @param int sidno
 */
void clear_bus(int sidno)
{
  cycled_write_operation((sidno * 0x20), 0x0, 0);
  return;
}

/**
 * @brief Wrapper around `clear_bus` to write zeroes to each SID
 *
 */
void clear_bus_all(void)
{
  for (int sid = 0; sid < cfg.numsids; sid++) {
    clear_bus(sid);
  }
  return;
}

/**
 * @brief Deassert both chip selects, pausing SID bus activity
 */
void pause_sid(void)
{
  sPIN(CS1);
  sPIN(CS2);
  return;
}

/**
 * @brief Toggle pause state, muting/unmuting and pausing/resuming the SID(s)
 *
 * On the transition into pause, mutes first; on the transition out of
 * pause, unmutes first. Always calls pause_sid() and flips the paused
 * state afterwards.
 */
void pause_sid_withmute(void)
{
  usSID("[PAUSE STATE PRE] %d\n", get_paused_state());
  if (!get_paused_state()) mute_sid();
  if (get_paused_state()) unmute_sid();
  pause_sid();
  set_paused_state(!get_paused_state());
  usSID("[PAUSE STATE POST] %d\n", get_paused_state());
  return;
}

/**
 * @brief Reset all SIDS by pulling the reset line low for 10 clockcycles
 *
 */
void reset_sid(void)
{
  set_reset_state(true);
  set_paused_state(false);
  clear_volume_state();    /* Volume state to default */
  clear_sid_memory();      /* Zero out SID memory space */
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
 * @brief Reset/ Silence/ Mute the FMOpl at address
 *
 * @param base_address
 */
void clear_fmopl_registers_at_addr(uint8_t base_address)
{
  /* $bd = $00 ~ rhythm mode off */
  cycled_write_operation(base_address,          0xbd, 10);  /* index port */
  cycled_write_operation((base_address + 0x10), 0x00, 10);  /* data port  */
  /* $80..$95 = $0f ~ sustain level 0, release rate 15, the fastest available */
  for (uint8_t idx = 0x80; idx <= 0x95; idx++) {
    cycled_write_operation(base_address,          idx, 10);   /* index port */
    cycled_write_operation((base_address + 0x10), 0x0f, 10);  /* data port  */
  }
  /* $b0..$b8 = $00 ~ key off all nine melodic channels (bit 5 clear) */
  for (uint8_t idx = 0xb0; idx <= 0xb8; idx++) {
    cycled_write_operation(base_address,          idx, 10);   /* index port */
    cycled_write_operation((base_address + 0x10), 0x00, 10);  /* data port  */
  }
  /* $40..$55 = $3f ~ total level to max attenuation */
  for (uint8_t idx = 0x40; idx <= 0x55; idx++) {
    cycled_write_operation(base_address,          idx, 10);   /* index port */
    cycled_write_operation((base_address + 0x10), 0x3f, 10);  /* data port  */
  }
  /* $08 = $00, $01 = $00 ~ CSM/note-select off, waveform select off */
  cycled_write_operation(base_address,          0x08, 10);  /* index port */
  cycled_write_operation((base_address + 0x10), 0x00, 10);  /* data port  */
  cycled_write_operation(base_address,          0x01, 10);  /* index port */
  cycled_write_operation((base_address + 0x10), 0x00, 10);  /* data port  */

  return;
}

/**
 * @brief Clear SID register / reset registers
 *        6 cycle delay for each write to simulate LDA (2) + STA (4)
 * @note https://csdb.dk/forums/?roomid=11&topicid=85713&showallposts=1
 * @note thanks Wilfred for pointing this out!
 * @param uint8_t base_address
 */
void clear_sid_registers_at_addr(uint8_t base_address)
{
  for (uint reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    cycled_write_operation((base_address | sid_registers[reg]), 0xff, 6);
  }
  for (uint reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    cycled_write_operation((base_address | sid_registers[reg]), 0x08, 6);
  }
  for (uint reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    cycled_write_operation((base_address | sid_registers[reg]), 0x0, 6);
  }
  clear_sid_memory();  /* Zero out SID memory space */
  return;
}

/**
 * @brief Clear SID register / reset registers
 * @note wrapper function around `clear_sid_registers_at_addr`
 *       for backwards compatibility
 * @param int sidno
 */
void clear_sid_registers(int sidno)
{
  if __us_unlikely(cfg.fmopl_enabled
    && (cfg.fmopl_sid == (sidno+1))) { /* sidno + 1 because fmopl_sid is human readable */
    clear_fmopl_registers_at_addr((sidno * 0x20));
  } else {
    clear_sid_registers_at_addr((sidno * 0x20));
  }
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
