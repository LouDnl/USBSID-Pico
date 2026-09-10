/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_player.h
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

/* SID player & Emulator flags used in usbsid.c */
#if defined(ONBOARD_SIDPLAYER)
extern volatile bool
  sidplayer_init,
  sidplayer_start,
  sidplayer_playing,
  sidplayer_stop,
  sidplayer_next,
  sidplayer_prev;
extern volatile char tuneno;
extern volatile bool is_prg;
extern volatile uint32_t playtime, maxplaytime;
/* ONBOARD_CYNTHCART */
extern volatile bool
  emulator_running,
  starting_emulator,
  stopping_emulator;
#endif /* ONBOARD_SIDPLAYER */

extern volatile bool is_sidplayerplaying(void); /* Always returns false if no sidplayer available */

extern void set_maxplaytime(uint8_t * buffer);
extern void get_playtime(void);
extern void set_mutestate(uint8_t * buffer);
extern void get_mutestate(void);
