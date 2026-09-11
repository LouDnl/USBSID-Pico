/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_player.c
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
#include <usbsid.h>
#include <config.h>
#include <logging.h>

/* SID player */
#if defined(ONBOARD_EMULATOR)
#include <usplayer.h>
volatile bool sidplayer_init = false;
volatile bool sidplayer_start = false;
volatile bool sidplayer_playing = false;
/**
 * @brief Check whether the onboard SID player is currently playing
 *
 * @return bool current sidplayer_playing state
 */
volatile bool is_sidplayerplaying(void) { return sidplayer_playing; };
volatile bool sidplayer_stop = false;
volatile bool sidplayer_next = false;
volatile bool sidplayer_prev = false;
volatile char tuneno = 0;
volatile bool is_prg = false; /* Default to SID file */
volatile uint32_t playtime = 0;
volatile uint32_t maxplaytime = 300000; /* 5 minutes in milliseconds */
/* Cynthcart, via USBSID-Player's MC68B50 ACIA */
#include <cynthcart_embedded.h>
volatile bool emulator_running = false;
volatile bool starting_emulator = false;
volatile bool stopping_emulator = false;
#else
/**
 * @brief Check whether the onboard SID player is currently playing
 *
 * @note Stub used when ONBOARD_EMULATOR is not compiled in; always false
 *
 * @return bool always false
 */
volatile bool is_sidplayerplaying(void) { return false; };
#endif /* ONBOARD_EMULATOR */


/**
 * @brief Set the onboard SID player maxplaytime
 *
 * Combines 4x uint8_t from the buffer into a single uint32_t and sets
 * maxplaytime: buffer[1..4] = { FF000000, 00FF0000, 0000FF00, 000000FF } -> 0xFFFFFFFF
 *
 * @param uint8_t * buffer buffer[5] = { CMD, FF000000, 00FF0000, 0000FF00, 000000FF }
 */
void set_maxplaytime(uint8_t * buffer)
{
#if defined(ONBOARD_EMULATOR)
  maxplaytime = ((buffer[1] << 24) | (buffer[2] << 16) | (buffer[3] << 8) | buffer[4]);
  double maxtimeplayed = maxplaytime / 1000.0; /* Force floating-point division by using 1000.0 */
  int minutes = (int)(maxtimeplayed / 60); /* Get total whole minutes */
  double secs = maxtimeplayed - (minutes * 60); /* Get remaining seconds with decimals */
  usCFG("Max playtime set to ms: %u, maxtimeplayed: %f. Max playtime: %02d:%05.2f\n",
    maxplaytime, maxtimeplayed, minutes, secs
  );
#endif
  return;
}

/**
 * @brief Get the onboard SID player current playtime
 *
 * Writes the uint32_t playtime cut into 4x uint8_t, i.e.
 * { FF000000, 00FF0000, 0000FF00, 000000FF }, to the requesting endpoint.
 *
 * @note Does not return anything, writes the buffer directly
 */
void get_playtime(void)
{
#if defined(ONBOARD_EMULATOR)
  if (sidplayer_playing) {
    playtime = usplayer_playtime_ms();
  }
  memset(write_buffer_p, 0, 64);
  write_buffer_p[0] = (uint8_t)((playtime >> 24) & 0xFF);
  write_buffer_p[1] = (uint8_t)((playtime >> 16) & 0xFF);
  write_buffer_p[2] = (uint8_t)((playtime >> 8) & 0xFF);
  write_buffer_p[3] = (uint8_t)(playtime & 0xFF);
  write_back_data(4);

  double timeplayed = playtime / 1000.0; /* Force floating-point division by using 1000.0 */
  int minutes = (int)(timeplayed / 60); /* Get total whole minutes */
  double secs = timeplayed - (minutes * 60); /* Get remaining seconds with decimals */
  usCFG("Playtime in ms: %u/%u, timeplayed: %f. Playtime: %02d:%05.2f\n",
    playtime, maxplaytime, timeplayed, minutes, secs
  );
#endif
  return;
}

/**
 * @brief Hold one onboard SID player voice of one SID silent while the
 *        tune keeps playing
 *
 * buffer[4] = { CMD, chip, voice, mute }: chip is 1 to 4, voice is 1 to 3,
 * mute is 0 or 1, any higher value in any field discards the command
 * completely.
 *
 * @note use chip = 0, voice = 0, mute 1 or 0 to mute or unmute all
 * @note chip = 0, voice = !0 or too high numbers are invalid combinations
 *
 * @param uint8_t * buffer
 */
void set_mutestate(uint8_t * buffer)
{
#if defined(ONBOARD_EMULATOR)
  if ((buffer[1] == 0 && buffer[2] > 1) /* all chips, no voices specified */
      || (buffer[1] > 4) /* chip */
      || (buffer[2] > 3) /* voice */
      || (buffer[3] > 1) /* mute */) {
    usWRN("Invalid chip (%u)/ voice (%u)/ mute (%u) combination!\n",
      buffer[1], buffer[2], buffer[3]);
    return;
  }

  uint8_t chip  = buffer[1];
  uint8_t voice = buffer[2];
  bool mute = buffer[3];

  if (chip == 0 && voice == 0) {
    usCFG("%s all chips & all voices\n",
      (mute ? "Muting" : "Unmuting"));
    for (int s = 1; s < 5; s++) {
      usCFG("  Chip %d\n", s);
      usplayer_set_chip_mute(s, mute);
      for (int v = 1; v < 4; v++) {
        usNFO(" Voice %d", v);
        usplayer_set_voice_mute(s, v, mute);
      }
      usNFO("\n");
    }
  } else if (voice == 0) {
    usCFG("%s chip %u\n",
      (mute ? "Muting" : "Unmuting"), chip);
      usplayer_set_chip_mute(chip, mute);
  } else {
    usCFG("%s voice %u on chip %u\n",
      (mute ? "Muting" : "Unmuting"), chip, voice);
    usplayer_set_voice_mute(chip, voice, mute);
  }
  return;
#endif
}

/**
 * @brief Get the onboard SID player mute state of all chips and voices
 *
 * Writes { uint8_t chips, uint8_t chip1, uint8_t chip2, uint8_t chip3,
 * uint8_t chip4 } to the requesting endpoint, where the chips byte packs
 * per-chip mute bits and each chipN byte packs its 3 voice mute bits
 * (0b111 = voices 3, 2, 1).
 *
 * @note does not return anything, writes the buffer directly
 */
void get_mutestate(void)
{
#if defined(ONBOARD_EMULATOR)
  uint8_t chips = 0, chip1 = 0, chip2 = 0, chip3 = 0, chip4 = 0;
  if (sidplayer_playing) {
    chips = usplayer_chip_mute();
    chip1 = usplayer_voice_mute(1);
    chip2 = usplayer_voice_mute(2);
    chip3 = usplayer_voice_mute(3);
    chip4 = usplayer_voice_mute(4);
  }
  memset(write_buffer_p, 0, 64);
  write_buffer_p[0] = chips;
  write_buffer_p[1] = chip1;
  write_buffer_p[2] = chip2;
  write_buffer_p[3] = chip3;
  write_buffer_p[4] = chip4;
  write_back_data(5);

  usCFG("SID player mute state\n");
  usCFG("  CHIPS :%04b\n", chips);
  usCFG("  CHIP 1:%03b\n", chip1);
  usCFG("  CHIP 2:%03b\n", chip2);
  usCFG("  CHIP 3:%03b\n", chip3);
  usCFG("  CHIP 4:%03b\n", chip4);
  return;
#endif
}
