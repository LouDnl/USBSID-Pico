/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_handler.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
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


#include "pico/multicore.h"
#include "pico/util/queue.h"

#include <math.h>

#include "globals.h"
#include "config.h"
#include "logging.h"
#include "sid.h"
#include "midi.h"
#include "midi_defaults.h"
#include "midi_ccdefaults.h"
#include "midi_patches.h"


/* Config */
extern RuntimeCFG cfg;

/* GPIO */
extern uint8_t cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern uint16_t cycled_delay_operation(uint16_t cycles);

/* Initialize variables */
extern const midi_ccvalues midi_ccvalues_defaults; /* midi.c */
midi_ccvalues CC;
RuntimeMidi RM;

Bank * bank;
Instrument * instr; /* Points to current channel instrument pointer */
Channel * channel;
uint8_t channo, insno; /* channel and instrument/program */
uint16_t bankno; /* msb 7F | lsb 7F */


static void select_program(uint8_t p)
{
  insno = p;
  channel->insno = insno;
  instr = &bank->ins[channel->insno];
  return;
}

static void select_bank(uint8_t m, uint8_t l)
{
  bankno = (m << 8 | l);
  channel->bankno = bankno;
  bank = &banks[channel->bankno];
  return;
}

static void select_channel(uint8_t c)
{
  channo = c;
  channel = &RM.channel[channo];
  return;
}

void midi_programs_init(void)
{
  bankno = channo = insno = 0;
  default_banks();

  RM.channel[channo].bankno = bankno;
  RM.channel[channo].insno = insno;

  bank = &banks[RM.channel[channo].bankno]; /* Starting bank */
  instr = &bank->ins[RM.channel[channo].insno];
}

void midi_processor_init(void)
{
  usCFG("Start Midi handler init\n");

  /* NOTE: Temporary - always init defaults */
  memcpy(&CC, &midi_ccvalues_defaults, sizeof(midi_ccvalues));

  midi_programs_init();

  usCFG("End Midi handler init\n");

}

/* Helper functions */

void midi_bus_operation(uint8_t a, uint8_t b)
{
  cycled_write_operation(a, b, 6);  /* 6 cycles constant for LDA 2 and STA 4 */
  return;
}

void process_midi(uint8_t *buffer, int size)
{
  /* TODO: is still fixed to single channel */
  /* TODO: is still fixed to single instrument(program) */
  /* TODO: add non fixed intrument bank */
  /* TODO: ignoring PulseTable jump and pw_ts */
  /* TODO: ignoring FilterTable + filters */
  /* TODO: ignoring SpeedTable */
  /* TODO: Add piano like instrument */
  /* TODO: Add polyphonic instrument */
  static int keys_pressed;
  int voices = (cfg.numsids * 3);
  uint8_t voice_address = 0x0, base_address = 0x0;
  if ((buffer[0] & 0xF0) == 0x90) {
    keys_pressed++;
    // base_address = ((keys_pressed % 3 == 0) ? (base_address + ( - 1)))op
    /* uint8_t voice_address = ((keys_pressed - 1) * 0x20); */
    uint8_t voice_address = ((keys_pressed - 1) * 0x7); /* TODO: MOVE TO NEXT SID */
    int writes = 0;
    midi_bus_operation((base_address | MODVOL), 0xE); /* TODO: TEMPORARY VOLUME :) */
    writes++;
    midi_bus_operation((voice_address + 0x05), ((instr->attack << 4) | instr->decay));
    writes++;
    midi_bus_operation((voice_address + 0x06), ((instr->sustain << 4) | instr->release));
    writes++;
    bool pulsetable = true, wavetable = true;
    int i = 0;
AGAIN:
    if (instr->has_pt && pulsetable) {
      midi_bus_operation((voice_address + 0x02), (instr->pt[i].pw_hi & 0xF));
      writes++;
      midi_bus_operation((voice_address + 0x03), instr->pt[i].pw_lo);
      writes++;
    }
    if (instr->has_wt && wavetable) {
      /* ASSUMING THIS IS AN ABSOLUTE NOTE VALUE!! PITCH_OFFSET => 0x81 */
      if (instr->wt[i].pitch_offset > 0x80) { /* Absolute note values */
        midi_bus_operation((voice_address + 0x00), NOTEHI(instr->wt[i].pitch_offset));
        writes++;
        midi_bus_operation((voice_address + 0x01), NOTELO(instr->wt[i].pitch_offset));
        writes++;
      } else { /* Additions to input note */
        /* uint8_t note_index = buffer[1]; */
        uint16_t frequency = musical_scale_values[buffer[1]];
        midi_bus_operation((voice_address + 0x00), (NOTEHI(frequency) + NOTEHI(instr->wt[i].pitch_offset)));
        writes++;
        midi_bus_operation((voice_address + 0x01), (NOTELO(frequency) + NOTELO(instr->wt[i].pitch_offset)));
        writes++;
      }
      midi_bus_operation((voice_address + 0x04), instr->wt[i].reg);
      writes++;
    }
    if (instr->pt[i].end == true) pulsetable = false;
    if (instr->wt[i].end == true) wavetable = false;
    // usDBG("%d:%d%d%d%d\n", i, pulsetable, instr->pt[i].end, wavetable, instr->wt[i].end);
    i++;
    if (pulsetable || wavetable) {
      cycled_delay_operation((uint16_t)(R_PAL - (writes * 6)));  /* One table entry each per frame ;) */
      writes = 0;
      goto AGAIN;
    }
  }
  if (((buffer[0] & 0xF0) == 0x80) && (keys_pressed > 0)) keys_pressed--;
  // usDBG("[K]%d\n", keys_pressed);
  return;
}
