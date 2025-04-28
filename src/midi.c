/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi.c
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

#include "midi.h"
#include "globals.h"
#include "config.h"
#include "sid.h"
#include "logging.h"
#include "midi_patch_cynthcart.h"


/* GPIO externals */
extern uint8_t __not_in_flash_func(cycled_write_operation)(uint8_t address, uint8_t data, uint16_t cycles);

/* ASID externals */
extern void process_sysex(uint8_t *buffer, int size);

/* Config externals */
extern Config usbsid_config;
extern int numsids;

/* Init vars */
uint8_t addr, val;
clock_rates clock_rate = CLOCK_DEFAULT;
hertz_values hertz = HZ_50;
int midi_bytes = 3;
int st[12] = { 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3 };  /* SID table */
int vt[12] = { 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2 };  /* Voice table */
int voices[12] = {0};
int voice = 0;
int curr_midi_channel;  /* For use in config.c */
// int freevoice = 0;

/* Initialize the midi handlers */
void midi_init(void)
{
  /* Clear buffers once */
  memset(midimachine.streambuffer, 0, sizeof midimachine.streambuffer);
  memset(midimachine.usbstreambuffer, 0, sizeof midimachine.usbstreambuffer);
  memset(midimachine.channelkey_states, 0, sizeof midimachine.channelkey_states);
  memset(midimachine.bank1channelgate, true, sizeof midimachine.bank1channelgate);

  /* Initial state and index */
  midimachine.bus = FREE;
  midimachine.state = IDLE;
  midimachine.index = 0;
  curr_midi_channel = 0;

  /* NOTE: Midi state is not loaded from config on init, needs LOAD_MIDI_STATE command once */
}


/* Write helper functions */

void midi_bus_operation(uint8_t a, uint8_t b)
{
  cycled_write_operation(a, b, 10);  /* 10 cycles constant */
}

void write(int channel, int sidno, int reg)
{  /* General */
  addr = (0x20 * sidno);
  midi_bus_operation((addr | reg), midimachine.channel_states[channel][sidno][reg]);
}

void write_triple(int channel, int sidno, int reg)
{
  for (int i = 0; i < 3; i++) {
    write(channel, sidno, ((i * 7) + reg));
  }
}

void write_voice(int channel, int sidno, int voice)
{  /* Voice specific */
  addr = (0x20 * sidno);
  midi_bus_operation((addr | (0x05 + voice)), midimachine.channel_states[channel][sidno][ATTDEC + voice]);  /* Attack / Decay */
  midi_bus_operation((addr | (0x06 + voice)), midimachine.channel_states[channel][sidno][SUSREL + voice]);  /* Sustain / Release */
  midi_bus_operation((addr | (0x02 + voice)), midimachine.channel_states[channel][sidno][PWMLO + voice]);   /* PW LO */
  midi_bus_operation((addr | (0x03 + voice)), midimachine.channel_states[channel][sidno][PWMHI + voice]);   /* PW HI */
}

void write_note(int channel, int sidno, int voice)
{
  addr = (0x20 * sidno);
  midi_bus_operation((addr | (0x00 + voice)), midimachine.channel_states[channel][sidno][NOTELO + voice]);
  midi_bus_operation((addr | (0x01 + voice)), midimachine.channel_states[channel][sidno][NOTEHI + voice]);
}

void write_gate(int channel, int sidno, int voice)
{
  addr = (0x20 * sidno) | (CONTR + voice);
  midi_bus_operation(addr, midimachine.channel_states[channel][sidno][CONTR + voice]);
}


/* Bank helper functions */

void bank_null(int channel, int sidno, int program)
{
  /* SID Voices */
  midimachine.channel_states[channel][sidno][PWMLO]       = patchPWL[program];
  midimachine.channel_states[channel][sidno][PWMLO + 7]   = patchPWL[program];
  midimachine.channel_states[channel][sidno][PWMLO + 14]  = patchPWL[program];
  midimachine.channel_states[channel][sidno][PWMHI]       = patchPWH[program];
  midimachine.channel_states[channel][sidno][PWMHI + 7]   = patchPWH[program];
  midimachine.channel_states[channel][sidno][PWMHI + 14]  = patchPWH[program];
  midimachine.channel_states[channel][sidno][CONTR]       = patchWave1[program];
  midimachine.channel_states[channel][sidno][CONTR + 7]   = patchWave2[program];
  midimachine.channel_states[channel][sidno][CONTR + 14]  = patchWave3[program];
  midimachine.channel_states[channel][sidno][ATTDEC]      = patchAD[program];
  midimachine.channel_states[channel][sidno][SUSREL]      = patchSR1[program];
  midimachine.channel_states[channel][sidno][ATTDEC + 7]  = patchAD[program];
  midimachine.channel_states[channel][sidno][SUSREL + 7]  = patchSR2[program];
  midimachine.channel_states[channel][sidno][ATTDEC + 14] = patchAD[program];
  midimachine.channel_states[channel][sidno][SUSREL + 14] = patchSR3[program];
  /* SID General */
  midimachine.channel_states[channel][sidno][FC_LO]       = newPatchFiltCut[program];
  midimachine.channel_states[channel][sidno][FC_HI]       = 0;
  midimachine.channel_states[channel][sidno][RESFLT]      = patchFilt[program];
  midimachine.channel_states[channel][sidno][MODVOL]      = patchVolMode[program] | patchVol[program];
  for (int sidno = 0; sidno < 4; sidno++) {
    addr = (sidno * 0x20);
    midi_bus_operation((addr | PWMLO), midimachine.channel_states[channel][sidno][PWMLO]);               /* PW LO */
    midi_bus_operation((addr | (PWMLO + 7)), midimachine.channel_states[channel][sidno][PWMLO + 7]);     /* PW LO */
    midi_bus_operation((addr | (PWMLO + 14)), midimachine.channel_states[channel][sidno][PWMLO + 14]);   /* PW LO */
    midi_bus_operation((addr | PWMHI), midimachine.channel_states[channel][sidno][PWMHI]);               /* PW HI */
    midi_bus_operation((addr | (PWMHI + 7)), midimachine.channel_states[channel][sidno][PWMHI + 7]);     /* PW HI */
    midi_bus_operation((addr | (PWMHI + 14)), midimachine.channel_states[channel][sidno][PWMHI + 14]);   /* PW HI */
    midi_bus_operation((addr | CONTR), midimachine.channel_states[channel][sidno][CONTR]);               /* Waveform */
    midi_bus_operation((addr | (CONTR + 7)), midimachine.channel_states[channel][sidno][CONTR + 7]);     /* Waveform */
    midi_bus_operation((addr | (CONTR + 14)), midimachine.channel_states[channel][sidno][CONTR + 14]);   /* Waveform */
    midi_bus_operation((addr | ATTDEC), midimachine.channel_states[channel][sidno][ATTDEC]);             /* Attack & Decay */
    midi_bus_operation((addr | SUSREL), midimachine.channel_states[channel][sidno][SUSREL]);             /* Sustain & Release */
    midi_bus_operation((addr | (ATTDEC + 7)), midimachine.channel_states[channel][sidno][ATTDEC + 7]);   /* Attack & Decay */
    midi_bus_operation((addr | (SUSREL + 7)), midimachine.channel_states[channel][sidno][SUSREL + 7]);   /* Sustain & Release */
    midi_bus_operation((addr | (ATTDEC + 14)), midimachine.channel_states[channel][sidno][ATTDEC + 14]); /* Attack & Decay */
    midi_bus_operation((addr | (SUSREL + 14)), midimachine.channel_states[channel][sidno][SUSREL + 14]); /* Sustain & Release */

    midi_bus_operation((addr | FC_LO), midimachine.channel_states[channel][sidno][FC_LO]);               /* Filtercut LO */
    midi_bus_operation((addr | FC_HI), midimachine.channel_states[channel][sidno][FC_HI]);               /* Filtercut HI */
    // midi_bus_operation((addr | RESFLT), midimachine.channel_states[channel][sidno][RESFLT]);           /* Filer */ // BUG: Causes the sound to be SILENT!?
    midi_bus_operation((addr | MODVOL), midimachine.channel_states[channel][sidno][MODVOL]);             /* Mode & Volume */
  }
}

void bank_zero_sidsetter_dualregister(int channel, int val, int hi, int lo, int himask, int lomask, int hishift)
{ /* For provided hi and lo registers of each active SID */
  for (int sid = 0; sid < numsids; sid++) {
    /* Dual Hi */
    midimachine.channel_states[channel][sid][hi] = ((val & himask) >> hishift);
    write(channel, sid, hi);  /* HI REG */
    /* Dual Lo */
    midimachine.channel_states[channel][sid][lo] = (val & lomask);
    write(channel, sid, lo);  /* LO REG */
  }
}

void bank_one_sidsetter_dualregister(int channel, int sidno, int val, int hi, int lo, int himask, int lomask, int hishift)
{ /* For provided hi and lo registers of provided SID */
  /* Dual Hi */
  midimachine.channel_states[channel][sidno][hi] = ((val & himask) >> hishift);
  write(channel, sidno, hi);
  /* Dual Lo */
  midimachine.channel_states[channel][sidno][lo] = (val & lomask);
  write(channel, sidno, lo);
}

void bank_zero_voicesetter_dualregister(int channel, int val, int hi, int lo, int himask, int lomask, int hishift)
{ /* For provided hi and lo registers of each voice of each active SID */
  for (int sid = 0; sid < numsids; sid ++) {
    for (int i = 0; i < 3; i++) {
      /* Dual Hi */
      midimachine.channel_states[channel][sid][(i * 7) + hi] = ((val & himask) >> hishift);
      /* Dual Lo */
      midimachine.channel_states[channel][sid][(i * 7) + lo] = (val & lomask);
    }
    write_triple(channel, sid, hi);  /* HI REG */
    write_triple(channel, sid, lo);  /* LO REG */
  }
}

void bank_one_voicesetter_dualregister(int channel, int sidno, int voiceno, int val, int hi, int lo, int himask, int lomask, int hishift)
{ /* For provided hi and lo registers of provided voice of provided SID */
  /* Dual Hi */
  midimachine.channel_states[channel][sidno][(voiceno * 7) + hi] = ((val & himask) >> hishift);
  write(channel, sidno, ((voiceno * 7) + hi));
  /* Dual Lo */
  midimachine.channel_states[channel][sidno][(voiceno * 7) + lo] = (val & lomask);
  write(channel, sidno, ((voiceno * 7) + lo));
}

void bank_zero_sidsetter_singleregister(int channel, int val, int reg, int mask)
{ /* For provided register of each voice of each active SID with provided mask */
  for (int sid = 0; sid < numsids; sid ++) {
    uint8_t keep_state;
    keep_state = midimachine.channel_states[channel][sid][reg];
    keep_state &= mask;
    val = keep_state | val;
    midimachine.channel_states[channel][sid][reg] = val;
    write(channel, sid, reg);
  }
}

void bank_one_sidsetter_singleregister(int channel, int sidno, int val, int reg, int mask)
{ /* For provided register of provided voice of provided SID with provided mask */
  uint8_t keep_state;
  keep_state = midimachine.channel_states[channel][sidno][reg];
  keep_state &= mask;
  val = keep_state | val;
  midimachine.channel_states[channel][sidno][reg] = val;
  write(channel, sidno, reg);
}

void bank_one_voicesetter_singleregister(int channel, int sidno, int voiceno, int val, int reg, int mask)
{ /* For provided register of provided voice of provided SID with provided mask */
  uint8_t keep_state;
  keep_state = midimachine.channel_states[channel][sidno][((voiceno * 7) + reg)];
  keep_state &= mask;
  val = keep_state | val;
  midimachine.channel_states[channel][sidno][((voiceno * 7) + reg)] = val;
  write(channel, sidno, ((voiceno * 7) + reg));
}

void bank_zero_voicetoggle(int channel, int val, int reg, uint8_t toggle)
{ /* For provided register of each voice of each active SID for provided toggle bit */
  for (int sid = 0; sid < numsids; sid++) {
    for (int i = 0; i < 3; i++) {
      uint8_t keep_state;
      keep_state = midimachine.channel_states[channel][sid][(i * 7) + reg];
      val = val == 1 ? keep_state |= toggle : keep_state & ~(toggle);
      midimachine.channel_states[channel][sid][(i * 7) + reg] = val;
    }
    write_triple(channel, sid, reg);
  }
}

void bank_one_voicetoggle(int channel, int sidno, int voiceno, int val, int reg, uint8_t toggle)
{ /* For provided register of provided voice of provided SID for provided toggle bit */
  uint8_t keep_state;
  keep_state = midimachine.channel_states[channel][sidno][(voiceno * 7) + reg];
  val = val == 1 ? keep_state |= toggle : keep_state & ~(toggle);
  midimachine.channel_states[channel][sidno][(voiceno * 7) + reg] = val;
  write(channel, sidno, (voiceno * 7) + reg);
}

// void bank_zero_sidsetter_single(int channel, int val, int reg, int mask)
// {
//   for (int sid = 0; sid < numsids; sid++) {
//     midimachine.channel_states[channel][sid][reg] = val;
//     write(channel, sid, reg);
//   }
// }

// void bank_one_sidsetter_single(int channel, int sidno, int voiceno, int val, int reg, int mask)
// {
//   midimachine.channel_states[channel][sidno][reg] = val;
//   write(channel, sidno, reg);
// }

// void bank_zero_voicesetter_single(int channel, int val, int hi, int lo, int himask, int lomask, int hishift)
// {
//   // for (int sid = 0; sid < numsids; sid ++) {  /* For each active SID */
//   //   for (int i = 0; i < 3; i++) {  /* For each voice per SID */
//   //     /* Dual Hi */
//   //     midimachine.channel_states[channel][sid][(i * 7) + hi] = ((val & himask) >> hishift);
//   //     /* Dual Lo */
//   //     midimachine.channel_states[channel][sid][(i * 7) + lo] = (val & lomask);
//   //   }
//   //   write_triple(channel, sid, hi);  /* HI REG */
//   //   write_triple(channel, sid, lo);  /* LO REG */
//   // }
// }

// void bank_one_voicesetter_single(int channel, int sidno, int voiceno, int val, int hi, int lo, int himask, int lomask, int hishift)
// {
//   // /* Dual Hi */
//   // midimachine.channel_states[channel][sidno][(voiceno * 7) + hi] = ((val & himask) >> hishift);
//   // write(channel, sidno, ((voiceno * 7) + hi));
//   // /* Dual Lo */
//   // midimachine.channel_states[channel][sidno][(voiceno * 7) + lo] = (val & lomask);
//   // write(channel, sidno, ((voiceno * 7) + lo));
// }

// void bank_zero_voicesetter_toggle_single_withkeep(int channel, int val, int reg, uint8_t toggle)
// {
//   // (void)mask;
//   for (int sid = 0; sid < numsids; sid ++) {  /* For each active SID */
//     for (int i = 0; i < 3; i++) {  /* For each voice per SID */
//       // midimachine.channel_states[channel][sid][reg * i] =
//       // ((midimachine.channel_states[channel][sid][reg] & R_NIBBLE) ^ toggle);
//       uint8_t keep_state;
//       keep_state = midimachine.channel_states[channel][sid][(i * 7) + reg];
//       /* val = val == 1 ? keep_state |= toggle : keep_state ^ toggle; */
//       val = val == 1 ? keep_state |= toggle : keep_state & ~(toggle);
//       midimachine.channel_states[channel][sid][(i * 7) + reg] = val;
//       // write(channel, sid, (reg * i));
//     }
//     write_triple(channel, sid, reg);
//   }
// }

// // void bank_one_toggle_single(int channel, int sidno, int voiceno, int val, int reg, uint8_t toggle)
// void bank_one_voicesetter_toggle_single_withkeep(int channel, int sidno, int voiceno, int val, int reg, uint8_t toggle)
// {
//   uint8_t keep_state;
//   keep_state = midimachine.channel_states[channel][sidno][reg];
//   val = val == 1 ? keep_state |= toggle : keep_state & ~(toggle);
//   midimachine.channel_states[channel][sidno][reg] = val;
//   write(channel, sidno, reg);
// }

/* Bank noteon ~ noteoff functions */

void bank_null_off(int channel, int sidno)
{
  // sidno = midimachine.channelkey_states[channel][sidno][N_KEYS];
  if (midimachine.channelkey_states[channel][sidno][N_KEYS] != 0) midimachine.channelkey_states[channel][sidno][N_KEYS]--;
  addr |= (midimachine.channelkey_states[channel][sidno][N_KEYS] * 0x20);
  if (midimachine.channelkey_states[channel][sidno][N_KEYS] < 0) midimachine.channelkey_states[channel][sidno][N_KEYS] = 0;
  /* Control ~ Gate bit off */
  midimachine.channel_states[channel][sidno][CONTR] = (midimachine.channel_states[channel][sidno][CONTR] & 0xFE);
  midimachine.channel_states[channel][sidno][CONTR + 7] = (midimachine.channel_states[channel][sidno][CONTR + 7] & 0xFE);
  midimachine.channel_states[channel][sidno][CONTR + 14] = (midimachine.channel_states[channel][sidno][CONTR + 17] & 0xFE);
  write_triple(channel, sidno, CONTR);
}

void bank_null_on(int channel, int sidno, uint8_t Flo, uint8_t Fhi)
{
  // sidno = midimachine.channelkey_states[channel][sidno][N_KEYS];
  int val1, val2, val3;
  if (midimachine.channelkey_states[channel][sidno][N_KEYS] < 0) midimachine.channelkey_states[channel][sidno][N_KEYS] = 0;
  addr |= midimachine.channelkey_states[channel][sidno][N_KEYS] <= 4 ? (midimachine.channelkey_states[channel][sidno][N_KEYS] * 0x20) : 0x0;
  /* Control ~ Gate bit on */
  val1 = midimachine.channel_states[channel][sidno][CONTR] & 0xFE;
  val1 |= 0x1;
  val2 = midimachine.channel_states[channel][sidno][CONTR + 7] & 0xFE;
  val2 |= 0x1;
  val3 = midimachine.channel_states[channel][sidno][CONTR + 14] & 0xFE;
  val3 |= 0x1;
  midimachine.channel_states[channel][sidno][CONTR] = val1;
  midimachine.channel_states[channel][sidno][CONTR + 7] = val2;
  midimachine.channel_states[channel][sidno][CONTR + 14] = val3;
  write_triple(channel, sidno, CONTR);
  /* Note Lo & Hi */
  midimachine.channel_states[channel][sidno][NOTELO] = Flo;
  midimachine.channel_states[channel][sidno][NOTELO + 7] = Flo;
  midimachine.channel_states[channel][sidno][NOTELO + 14] = Flo;
  midimachine.channel_states[channel][sidno][NOTEHI] = Fhi;
  midimachine.channel_states[channel][sidno][NOTEHI + 7] = Fhi;
  midimachine.channel_states[channel][sidno][NOTEHI + 14] = Fhi;
  write_triple(channel, sidno, NOTELO);
  write_triple(channel, sidno, NOTEHI);
  midimachine.channelkey_states[channel][sidno][N_KEYS]++;
}

void bank_zero_off(int channel, int sidno, uint8_t note)
{
  // if (midimachine.channelkey_states[channel][sidno][N_KEYS] < 0) midimachine.channelkey_states[channel][sidno][N_KEYS] = 0;
  // midimachine.channelkey_states[channel][sidno][V1_ACTIVE] == 1 && midimachine.channelkey_notestates[channel][sidno][note_index] == 1
  // ? voice = 0, midimachine.channelkey_states[channel][sidno][V1_ACTIVE] = 0, midimachine.channelkey_notestates[channel][sidno][note_index] = 0
  // : midimachine.channelkey_states[channel][sidno][V2_ACTIVE] == 1 && midimachine.channelkey_notestates[channel][sidno][note_index] == 2
  // ? voice = 1, midimachine.channelkey_states[channel][sidno][V2_ACTIVE] = 0, midimachine.channelkey_notestates[channel][sidno][note_index] = 0
  // : midimachine.channelkey_states[channel][sidno][V3_ACTIVE] == 1 && midimachine.channelkey_notestates[channel][sidno][note_index] == 3
  // ? voice = 2, midimachine.channelkey_states[channel][sidno][V3_ACTIVE] = 0, midimachine.channelkey_notestates[channel][sidno][note_index] = 0
  // : 0; /* ISSUE: EVERY NOTE AFTER 3 KEYS GOES TO VOICE 1 */

  // if (midimachine.channelkey_states[channel][sidno][N_KEYS] < 0) midimachine.channelkey_states[channel][sidno][N_KEYS] = 0;

  for (int i = 0; i < ((numsids * 3) - 1); i++) {
    // if (voices[i] == buffer[1]) {
    if (voices[i] == note) {
      voice = i;
      voices[i] = 0;
      // freevoice = voice;
    }
  }
  sidno = abs(voice / 3);
  midimachine.channel_states[channel][sidno][CONTR + (vt[voice] * 7)] = (midimachine.channel_states[channel][sidno][CONTR + (vt[voice] * 7)] & 0xFE);
  if (midimachine.channelkey_states[channel][sidno][N_KEYS] != 0) midimachine.channelkey_states[channel][sidno][N_KEYS]--;
  write_gate(channel, sidno, (vt[voice] * 7));
  voices[voice] = 0;
  for (int i = 0; i < ((numsids * 3) - 1); i++) {
    if (voices[i] == 0) {
      voice = i;
      break;
    }
  }
  // voice = voice == 2 ? voice : voice--;
  if (voice != 0) voice--;
}

void bank_zero_on(int channel, int sidno, uint8_t note, uint8_t Flo, uint8_t Fhi)
{
  // midimachine.channelkey_states[channel][sidno][V1_ACTIVE] == 0 && midimachine.channelkey_notestates[channel][sidno][note_index] != 1
  //   ? voice = 0, midimachine.channelkey_states[channel][sidno][V1_ACTIVE] = 1, midimachine.channelkey_notestates[channel][sidno][note_index] = 1
  //   : midimachine.channelkey_states[channel][sidno][V2_ACTIVE] == 0 && midimachine.channelkey_notestates[channel][sidno][note_index] != 2
  //   ? voice = 1, midimachine.channelkey_states[channel][sidno][V2_ACTIVE] = 1, midimachine.channelkey_notestates[channel][sidno][note_index] = 2
  //   : midimachine.channelkey_states[channel][sidno][V3_ACTIVE] == 0 && midimachine.channelkey_notestates[channel][sidno][note_index] != 3
  //   ? voice = 2, midimachine.channelkey_states[channel][sidno][V3_ACTIVE] = 1, midimachine.channelkey_notestates[channel][sidno][note_index] = 3
  //   : 0;
  // for (int i = 0; i < 3; i++) {
  //   if (voices[i] == 0) voice = i;
  //   break;
  // }
  // voices[voice] = buffer[1];
  voices[voice] = note;
  sidno = abs(voice / 3);
  midimachine.channelkey_states[channel][sidno][N_KEYS]++;
  /* write(channel, sid, MODVOL); */
  /* write_voice(channel, sid, (voice * 7)); */
  /* Note Lo & Hi */
  midimachine.channel_states[channel][sidno][NOTELO + (vt[voice] * 7)] = Flo;
  midimachine.channel_states[channel][sidno][NOTEHI + (vt[voice] * 7)] = Fhi;
  write_note(channel, sidno, (vt[voice] * 7));
  /* Control ~ Gate bit on */
  val = midimachine.channel_states[channel][sidno][CONTR + (vt[voice] * 7)] & 0xFE;
  val |= 0x1;
  midimachine.channel_states[channel][sidno][CONTR + (vt[voice] * 7)] = val;
  write_gate(channel, sidno, (vt[voice] * 7));
  // prevvoice = voice;
  // if (voice < 3) voice++;
  if (voice < ((numsids * 3) - 1)) voice++;
  if (voice > ((numsids * 3) - 1)) voice = 0;
}

void bank_one_off(int channel, int sidno, int voiceno, uint8_t note)
{
  if (midimachine.bank1channelgate[channel]) {
    midimachine.channel_states[channel][sidno][CONTR + (voiceno * 7)] = (midimachine.channel_states[channel][sidno][CONTR + (voiceno * 7)] & 0xFE);
    write_gate(channel, sidno, (voiceno * 7));
  }
  voices[voiceno] = 0;
}

void bank_one_on(int channel, int sidno, int voiceno, uint8_t note, uint8_t Flo, uint8_t Fhi)
{
  // printf("%d %d %d\n", channel, sidno, voiceno);
  /* Keepstate */
  voices[voice] = note;
  /* Note Lo & Hi */
  midimachine.channel_states[channel][sidno][NOTELO + (voiceno * 7)] = Flo;
  midimachine.channel_states[channel][sidno][NOTEHI + (voiceno * 7)] = Fhi;
  write_note(channel, sidno, (voiceno * 7));
  /* Control ~ Gate bit on */
  if (midimachine.bank1channelgate[channel]) {
    int val;
    val = midimachine.channel_states[channel][sidno][CONTR + (voiceno * 7)] & 0xFE;
    val |= 0x1;
    midimachine.channel_states[channel][sidno][CONTR + (voiceno * 7)] = val;
    write_gate(channel, sidno, (voiceno * 7));
  }
}

/* Helpers */

long map_to_onoff(long in)
{
  return map(in, ZERO, MIDI_CC_MAX, ZERO, ONE);
}


/* Midi processing */

void process_midi(uint8_t *buffer, int size)
{
  uint8_t note_index = buffer[1];
  uint16_t frequency = musical_scale_values[note_index];
  uint8_t Flo, Fhi;
  Flo = (frequency & VOICE_FREQLO);
  Fhi = ((frequency >> 8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> 8));

  int /* in, */ out, volume, channel, voiceno, sidno, bank, program;
  curr_midi_channel = channel = (buffer[0] & R_NIBBLE);  /* 1 -> 16 ~ 0x0 -> 0xF */
  bank = midimachine.channelbank[channel];  /* Set Bank */
  program = midimachine.channelprogram[channel]; /* Set Program */
  voiceno = vt[(int)channel];  /* Sets the sid voice according the the channel we're on, max voices is 12 with 4x SID */ // TODO: Limit at max voices
  sidno = (bank == 1) ? st[(int)channel] : 0;  /* Sets the SID number according to the voice we're on */
  volume = midimachine.channel_states[channel][sidno][MODVOL] & R_NIBBLE;
  // addr = 0x0;  /* Set starting address */
  /* TODO: NEEDED? */
  midimachine.channelkey_states[channel][sidno][N_KEYS] = midimachine.channelkey_states[channel][sidno][N_KEYS] < 0
    ? midimachine.channelkey_states[channel][sidno][N_KEYS] = 0
    : midimachine.channelkey_states[channel][sidno][N_KEYS];
  int voicecount = 0;
  for (int i = 0; i < ((numsids * 3) - 1); i++) {
    if (voices[i] == 0) {
      voicecount += 1; ;
    }
  }
  if (voicecount == ((numsids * 3) - 1) && midimachine.channelkey_states[channel][sidno][N_KEYS] != 0) midimachine.channelkey_states[channel][sidno][N_KEYS] = 0;
// TODO:
// Add clock sync
// Any other missing links
// TODO:
// Reorder the items below into preset CC's and custom CC's
  /* Start Status byte handling */
  // printf("%d %d %d %d %d %d %d\n", bank, channel, curr_midi_channel, voiceno, sidno, (bank == 1), st[voiceno]);
  switch (buffer[0]) {
    case 0x80 ... 0x8F:  /* Channel 0~16 Note Off */
      bank = midimachine.channelbank[channel];
      switch (bank) {
        case 0:  /* Bank 0 */
          bank_zero_off(channel, sidno, buffer[1]);
          break;
        case 1:  /* Bank 1 */
          bank_one_off(channel, sidno, voiceno, buffer[1]);
          break;
        case 9:  /* Bank 9 */
          bank_null_off(channel, sidno);
        default:
          break;
      }
      break;
    case 0x90 ... 0x9F:  /* Channel 0~16 Note On */
      bank = midimachine.channelbank[channel];
      switch (bank) {
        case 0:  /* Bank 0 */
          bank_zero_on(channel, sidno, buffer[1], Flo, Fhi);
          break;
        case 1:  /* Bank 1 */
          bank_one_on(channel, sidno, voiceno, buffer[1], Flo, Fhi);
          break;
        case 9:  /* Bank 9 */
          bank_null_on(channel, sidno, Flo, Fhi);
        default:
          break;
      }
      break;
    case 0xA0 ... 0xAF:  /* Channel 0~16 Polyphonic Key Pressure (Aftertouch) */
      break;
    case 0xB0 ... 0xBF:  /* Channel 0~16 Control/Mode Change */
      switch (bank) { /* QUESTION: Should this be/stay bank independent!? */
        case 0:  /* Bank 0 ~ Polyfonic stacking */
        case 1:  /* Bank 1 ~ Single voices */
        case 9:  /* Bank 9 ~ Cynthcart */
          switch (buffer[1]) { /* MSB or LSB for Control/Mode Change */
            /* Bank ~ Patch select */
            case CC_BMSB:  /* Bank Select MSB */
              /* Ignore ~ We have no more banks, only 1 bank with 16 programs */
              break;
            case CC_BLSB:  /* Bank Select LSB */
              bank = midimachine.channelbank[channel] = buffer[2];  /* Set Bank */
              break;
            /* Per SID settings */
            case CC_VOL:   /* Set Master Volume */
              volume = map(buffer[2], 0, MIDI_CC_MAX, 0, 15);
              if (bank == 0) bank_zero_sidsetter_singleregister(channel, volume, MODVOL, L_NIBBLE);
              if (bank == 1) bank_one_sidsetter_singleregister(channel, sidno, volume, MODVOL, L_NIBBLE);
              break;
            case CC_FFC:   /* Filter Frequency Cutoff */
              out = map(buffer[2], 0, MIDI_CC_MAX, 0, 2047);
              if (bank == 0) bank_zero_sidsetter_dualregister(channel, out, FC_HI, FC_LO, F_MASK_HI, F_MASK_LO, SHIFT_3); /* writes all available sids */
              if (bank == 1) bank_one_sidsetter_dualregister(channel, sidno, out, FC_HI, FC_LO, F_MASK_HI, F_MASK_LO, SHIFT_3); /* writes a single sid */
              // for (int sid = 0; sid < numsids; sid ++) {
              //   val = (out & 0x7);
              //   midimachine.channel_states[channel][sid][FC_LO] = val;
              //   val = (out & 0x7F8) >> 3;
              //   midimachine.channel_states[channel][sid][FC_HI] = val;
              //   write(channel, sid, FC_LO);
              //   write(channel, sid, FC_HI);
              // }
              break;
            case CC_RES:   /* Filter resonance */
              out = map(buffer[2], 0, MIDI_CC_MAX, 0, 15);
              if (bank == 0) bank_zero_sidsetter_singleregister(channel, (out << 4), RESFLT, R_NIBBLE);
              if (bank == 1) bank_one_sidsetter_singleregister(channel, sidno, (out << 4), RESFLT, R_NIBBLE);
              // mask = R_NIBBLE;  /* Inverted */
              // for (int sid = 0; sid < numsids; sid ++) {
              //   midimachine.channel_states[channel][sid][RESFLT] = (out << 4) | (midimachine.channel_states[channel][sid][RESFLT] & mask);
              //   write(channel, sid, RESFLT);
              // }
              break;
            case CC_3OFF:  /* Voice 3 disconnect */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, MODVOL, BIT_7);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, MODVOL, BIT_7);
              // for (int sid = 0; sid < numsids; sid ++) {
              //   keep_state = midimachine.channel_states[channel][sid][MODVOL];
              //   val = out == 1 ? keep_state |= 0x80 : keep_state ^ 0x80;
              //   midimachine.channel_states[channel][sid][MODVOL] = val;
              //   write(channel, sid, MODVOL);
              // }
              /* write(channel, sidno, MODVOL); */
              break;
            case CC_FLT1:  /* Filter channel 1 */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, RESFLT, BIT_0);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, RESFLT, BIT_0);
              break;
            case CC_FLT2:  /* Filter channel 2 */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, RESFLT, BIT_1);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, RESFLT, BIT_1);
              break;
            case CC_FLT3:  /* Filter channel 3 */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, RESFLT, BIT_2);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, RESFLT, BIT_2);
              break;
            case CC_FLTE:  /* Filter external */
              out = map_to_onoff(buffer[2]);
              // uint8_t fltchannel = buffer[1] == CC_FLT1 ? BIT_0
              //   : buffer[1] == CC_FLT2 ? BIT_1
              //   : buffer[1] == CC_FLT3 ? BIT_2
              //   : buffer[1] == CC_FLTE ? BIT_3
              //   : 0; // <<- fallback
              if (bank == 0) bank_zero_voicetoggle(channel, out, RESFLT, BIT_3);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, RESFLT, BIT_3);
              // for (int sid = 0; sid < numsids; sid++) {
              //   fltres_state = midimachine.channel_states[channel][sid][RESFLT];
              //   val = out == 1 ? fltres_state |= fltchannel : fltres_state ^ fltchannel;
              //   midimachine.channel_states[channel][sid][RESFLT] = val;
              //   write(channel, sid, RESFLT);
              // }
              /* write(channel, sidno, RESFLT); */
              break;
            case CC_HPF:   /* High pass */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, MODVOL, BIT_6);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, MODVOL, BIT_6);
              // for (int sid = 0; sid < numsids; sid ++) {
              //   keep_state = midimachine.channel_states[channel][sid][MODVOL];
              //   val = out == 1 ? keep_state |= 0x40 : keep_state ^ 0x40;
              //   midimachine.channel_states[channel][sid][MODVOL] = val;
              //   write(channel, sid, MODVOL);
              // }
              /* write(channel, sidno, MODVOL); */
              break;
            case CC_BPF:   /* Band pass */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, MODVOL, BIT_5);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, MODVOL, BIT_5);
              // for (int sid = 0; sid < numsids; sid ++) {
              //   keep_state = midimachine.channel_states[channel][sid][MODVOL];
              //   val = out == 1 ? keep_state |= 0x20 : keep_state ^ 0x20;
              //   midimachine.channel_states[channel][sid][MODVOL] = val;
              //   write(channel, sid, MODVOL);
              // }
              /* write(channel, sidno, MODVOL); */
              break;
            case CC_LPF:   /* Low pass */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, MODVOL, BIT_4);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, MODVOL, BIT_4);
              // for (int sid = 0; sid < numsids; sid ++) {
              //   keep_state = midimachine.channel_states[channel][sid][MODVOL];
              //   val = out == 1 ? keep_state |= 0x10 : keep_state ^ 0x10;
              //   midimachine.channel_states[channel][sid][MODVOL] = val;
              //   write(channel, sid, MODVOL);
              // }
              /* write(channel, sidno, MODVOL); */
              break;
            /* Per voice settings */
            case CC_PWM:   /* Pulse Width Modulation (Modulation wheel) */
              out = map(buffer[2], 0, MIDI_CC_MAX, 0, TRIPLE_NIBBLE);
              /* Pulse width Hi & Lo*/
              if (bank == 0) bank_zero_voicesetter_dualregister(channel, out, PWMHI, PWMLO, NIBBLE_3, BYTE, SHIFT_8); /* writes all 3 voices of a sid */
              if (bank == 1) bank_one_voicesetter_dualregister(channel, sidno, voiceno, out, PWMHI, PWMLO, NIBBLE_3, BYTE, SHIFT_8); /* writes a single voice */
              break;
            case CC_NOTE:  /* Note frequency */  // NOTE: NOT AVAILABLE FOR BANK ZERO FOR NOW
              uint8_t b1_note_index = buffer[2];
              uint16_t b1_frequency = musical_scale_values[b1_note_index];
              uint8_t b1_Flo, b1_Fhi;
              b1_Flo = (b1_frequency & VOICE_FREQLO);
              b1_Fhi = ((b1_frequency >> 8) >= VOICE_FREQHI ? VOICE_FREQHI : (b1_frequency >> 8));
              /* Keepstate */
              voices[voice] = buffer[2];
              /* Note Lo & Hi */
              midimachine.channel_states[channel][sidno][NOTELO + (voiceno * 7)] = b1_Flo;
              midimachine.channel_states[channel][sidno][NOTEHI + (voiceno * 7)] = b1_Fhi;
              write_note(channel, sidno, (voiceno * 7));
              break;
            case CC_NOIS:  /* Noise waveform */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_7);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_7);
              break;
            case CC_PULS:  /* Pulse waveform */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_6);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_6);
              break;
            case CC_SAWT:  /* Sawtooth waveform */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_5);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_5);
              break;
            case CC_TRIA:  /* Triangle waveform */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_4);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_4);
              break;
            case CC_TEST:  /* Test bit */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_3);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_3);
              break;
            case CC_RMOD:  /* Ring modulator bit */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_2);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_2);
              // for (int sid = 0; sid < numsids; sid ++) {
              //   midimachine.channel_states[channel][sid][CONTR] = ((midimachine.channel_states[channel][sid][CONTR] & R_NIBBLE) ^ 0x4);
              //   midimachine.channel_states[channel][sid][CONTR + 7] = ((midimachine.channel_states[channel][sid][CONTR + 7] & R_NIBBLE) ^ 0x4);
              //   midimachine.channel_states[channel][sid][CONTR + 14] = ((midimachine.channel_states[channel][sid][CONTR + 14] & R_NIBBLE) ^ 0x4);
              //   write_triple(channel, sid, CONTR);
              // }
              /* write_triple(channel, sidno, CONTR); */
              break;
            case CC_SYNC:  /* Sync bit */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_1);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_1);
              // for (int sid = 0; sid < numsids; sid ++) {
              //   midimachine.channel_states[channel][sid][CONTR] = ((midimachine.channel_states[channel][sid][CONTR] & R_NIBBLE) ^ 0x2);
              //   midimachine.channel_states[channel][sid][CONTR + 7] = ((midimachine.channel_states[channel][sid][CONTR + 7] & R_NIBBLE) ^ 0x2);
              //   midimachine.channel_states[channel][sid][CONTR + 14] = ((midimachine.channel_states[channel][sid][CONTR + 14] & R_NIBBLE) ^ 0x2);
              //   write_triple(channel, sid, CONTR);
              // }
              /* write_triple(channel, sidno, CONTR); */
              break;
            case CC_GATE:  /* Gate bit */
              out = map_to_onoff(buffer[2]);
              if (bank == 0) bank_zero_voicetoggle(channel, out, CONTR, BIT_0);
              if (bank == 1) bank_one_voicetoggle(channel, sidno, voiceno, out, CONTR, BIT_0);
              break;
            case CC_GTEN:  /* Custom setting for handling gate on and gate off in noteon and noteoff */
              out = map_to_onoff(buffer[2]);
              midimachine.bank1channelgate[channel] = (bool)out;
              break;
            case CC_ATT:   /* Attack */  // NOTE: NOT AVAILABLE FOR BANK ZERO FOR NOW
              out = map(buffer[2], 0, MIDI_CC_MAX, 0, 15);
              bank_one_voicesetter_singleregister(channel, sidno, voiceno, (out << 4), ATTDEC, R_NIBBLE);
              // mask = R_NIBBLE; /* Inverted */
              // for (int sid = 0; sid < numsids; sid ++) {
              //   for (int i = 0; i < 3; i++) {
              //     midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] = (out << 4) | (midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] & mask);
              //   }
              //   write_triple(channel, sid, ATTDEC);
              // }
              /* write_triple(channel, sidno, ATTDEC); */
              break;
            case CC_DEL:   /* Decay */  // NOTE: NOT AVAILABLE FOR BANK ZERO FOR NOW
              out = map(buffer[2], 0, MIDI_CC_MAX, 0, 15);
              bank_one_voicesetter_singleregister(channel, sidno, voiceno, out, ATTDEC, L_NIBBLE);
              // out = map(in, 0, 127, 0, 15);
              // mask = L_NIBBLE;  /* Inverted */
              // for (int sid = 0; sid < numsids; sid ++) {
              //   for (int i = 0; i < 3; i++) {
              //     midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] = (midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] & mask) | out;
              //   }
              //   write_triple(channel, sid, ATTDEC);
              // }
              /* write_triple(channel, sidno, ATTDEC); */
              break;
            case CC_SUS:   /* Sustain */  // NOTE: NOT AVAILABLE FOR BANK ZERO FOR NOW
              out = map(buffer[2], 0, MIDI_CC_MAX, 0, 15);
              bank_one_voicesetter_singleregister(channel, sidno, voiceno, (out << 4), SUSREL, R_NIBBLE);
              // mask = R_NIBBLE;  /* Inverted */
              // for (int sid = 0; sid < numsids; sid ++) {
              //   for (int i = 0; i < 3; i++) {
              //     midimachine.channel_states[channel][sid][(i * 7) + SUSREL] = (out << 4) | (midimachine.channel_states[channel][sid][(i * 7) + SUSREL] & mask);
              //   }
              //   write_triple(channel, sid, SUSREL);
              // }
              /* write_triple(channel, sidno, SUSREL); */
              break;
            case CC_REL:   /* Release */  // NOTE: NOT AVAILABLE FOR BANK ZERO FOR NOW
              out = map(buffer[2], 0, MIDI_CC_MAX, 0, 15);
              bank_one_voicesetter_singleregister(channel, sidno, voiceno, out, SUSREL, L_NIBBLE);
              // mask = L_NIBBLE;  /* Inverted */
              // for (int sid = 0; sid < numsids; sid ++) {
              //   for (int i = 0; i < 3; i++) {
              //     midimachine.channel_states[channel][sid][(i * 7) + SUSREL] = (midimachine.channel_states[channel][sid][(i * 7) + SUSREL] & mask) | out;
              //   }
              //   write_triple(channel, sid, SUSREL);
              // }
              /* write_triple(channel, sidno, SUSREL); */
              break;
            default:
              break;
          }
          break;
        // case 9:  /* Bank 9 ~ Cynthcart */
        default:
          break;
      }
      break;
    case 0xC0 ... 0xCF:  /* Channel 0~16 Program change */
      program = midimachine.channelprogram[channel] = buffer[1];  /* Set Program */
      // bank = midimachine.channelbank[channel];
      switch (bank) {
        case 0:  /* Bank 0 ~ Polyfonic stacking */
          switch (program) {
            case 0:  /* Noise */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                  midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x80);
                }
                write_triple(channel, sid, CONTR);
              }
              break;
            case 1:  /* Pulse */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x40);
                }
                write_triple(channel, sid, CONTR);
               }
              break;
            case 2:  /* Sawtooth */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                  midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x20);
                }
                write_triple(channel, sid, CONTR);
              }
              break;
            case 3:  /* Triangle */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                  midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x10);
                }
                write_triple(channel, sid, CONTR);
              }
              break;
          };
          break;
        case 1:  /* Bank 1 ~ Single voices */
          switch (program) {
            case 0:  /* Noise */
              midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] = ((midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] & R_NIBBLE) | 0x80);
              write(channel, sidno, CONTR);
              break;
            case 1:  /* Pulse */
              midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] = ((midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] & R_NIBBLE) | 0x40);
              write(channel, sidno, CONTR);
              break;
            case 2:  /* Sawtooth */
              midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] = ((midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] & R_NIBBLE) | 0x20);
              write(channel, sidno, CONTR);
              break;
            case 3:  /* Triangle */
              midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] = ((midimachine.channel_states[channel][sidno][(voiceno * 7) + CONTR] & R_NIBBLE) | 0x10);
              write(channel, sidno, CONTR);
              break;
          }
          break;
        case 9:  /* Patches */
          for (int n = 1; n <= numsids; n++) {
            bank_null(channel, (n - 1), program);
          }
        default:
          break;
      };
      break;
    case 0xD0 ... 0xDF:  /* Channel 0~16 Pressure (After-touch) */
      break;
    case 0xE0 ... 0xEF:  /* Channel 0~16 Pitch Bend Change */
      break;
    default:
      break;
  };
  if (midimachine.channelkey_states[channel][sidno][N_KEYS] == 0) voice = 0;

  MVDBG("[N]%d[V%d]%02x%02x%02x[1][$%04x][$%04x][$%02x][$%02x][$%02x][2][$%04x][$%04x][$%02x][$%02x][$%02x][3][$%04x][$%04x][$%02x][$%02x][$%02x][R][$%02x%01x][$%02x][$%02x]\r\n",
    midimachine.channelkey_states[channel][sidno][N_KEYS], // N
    voice,voices[0],voices[1],voices[2], //V
    (midimachine.channel_states[channel][sidno][NOTEHI] << 8 | midimachine.channel_states[channel][sidno][NOTELO]), // 0&1
    (midimachine.channel_states[channel][sidno][PWMHI] << 8 | midimachine.channel_states[channel][sidno][PWMLO]),  // 2&3
    midimachine.channel_states[channel][sidno][CONTR],  // 4
    midimachine.channel_states[channel][sidno][ATTDEC],  // 5
    midimachine.channel_states[channel][sidno][SUSREL],  // 6

    (midimachine.channel_states[channel][sidno][NOTEHI + 7] << 8 | midimachine.channel_states[channel][sidno][NOTELO + 7]), // 0&1
    (midimachine.channel_states[channel][sidno][PWMHI + 7] << 8 | midimachine.channel_states[channel][sidno][PWMLO + 7]),  // 2&3
    midimachine.channel_states[channel][sidno][CONTR + 7],  // 4
    midimachine.channel_states[channel][sidno][ATTDEC + 7],  // 5
    midimachine.channel_states[channel][sidno][SUSREL + 7],  // 6

    (midimachine.channel_states[channel][sidno][NOTEHI + 14] << 8 | midimachine.channel_states[channel][sidno][NOTELO + 14]), // 0&1
    (midimachine.channel_states[channel][sidno][PWMHI + 14] << 8 | midimachine.channel_states[channel][sidno][PWMLO + 14]),  // 2&3
    midimachine.channel_states[channel][sidno][CONTR + 14],  // 4
    midimachine.channel_states[channel][sidno][ATTDEC + 14],  // 5
    midimachine.channel_states[channel][sidno][SUSREL + 14],  // 6

    midimachine.channel_states[channel][sidno][FC_HI],  // 15
    midimachine.channel_states[channel][sidno][FC_LO],  // 16
    midimachine.channel_states[channel][sidno][RESFLT],  // 17
    midimachine.channel_states[channel][sidno][MODVOL]);  // 18
}

/* Processes a 1 byte incoming midi buffer
 * Figures out if we're receiving midi or sysex */
void midi_buffer_task(uint8_t buffer)
{
  if (midimachine.index != 0) {
    if (midimachine.type != SYSEX) MIDBG(" [B%d]$%02x#%03d", midimachine.index, buffer, buffer);
  }

  if (buffer & 0x80) { /* Handle start byte */
    switch (buffer) {
      /* System Exclusive */
      case 0xF0:  /* System Exclusive Start */
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          dtype = asid; /* Set data type to ASID */
          midimachine.state = RECEIVING;
          midimachine.type = SYSEX;
          midimachine.bus = CLAIMED;
          midimachine.index = 0;
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
        }
        break;
      case 0xF7:  /* System Exclusive End of SysEx (EOX) */
        if (midimachine.bus == CLAIMED && midimachine.type == SYSEX) {
          dtype = asid; /* Set data type to ASID */
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
          process_sysex(midimachine.streambuffer, midimachine.index);
          midimachine.bus = FREE;
          midimachine.type = NONE;
          midimachine.state = IDLE;
        }
        break;
      case 0xF1:  /* System Exclusive MIDI Time Code Qtr. Frame */
      case 0xF2:  /* System Exclusive Song Position Pointer */
      case 0xF3:  /* System Exclusive Song Select (Song #) */
      case 0xF4:  /* System Exclusive Undefined (Reserved) */
      case 0xF5:  /* System Exclusive Undefined (Reserved) */
      case 0xF6:  /* System Exclusive Tune request */
      case 0xF8:  /* System Exclusive Timing clock */
      case 0xF9:  /* System Exclusive Undefined (Reserved) */
      case 0xFA:  /* System Exclusive Start */
      case 0xFB:  /* System Exclusive Continue */
      case 0xFC:  /* System Exclusive Stop */
      case 0xFD:  /* System Exclusive Undefined (Reserved) */
      case 0xFE:  /* System Exclusive Active Sensing */
      case 0xFF:  /* System Exclusive System Reset */
        break;
      /* Midi 2 Bytes per message */
      case 0xC0 ... 0xCF:  /* Channel 0~16 Program (Patch) change */
      case 0xD0 ... 0xDF:  /* Channel 0~16 Pressure (After-touch) */
        dtype = midi; /* Set data type to midi */
        midi_bytes = 2;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          if (midimachine.index == 0) MIDBG("[M][B%d]$%02x#%03d", midimachine.index, buffer, buffer);
          midimachine.type = MIDI;
          midimachine.state = RECEIVING;
          midimachine.bus = CLAIMED;
          midimachine.index = 0;
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
        }
        break;
      /* Midi 3 Bytes per message */
      case 0x80 ... 0x8F:  /* Channel 0~16 Note Off */
      case 0x90 ... 0x9F:  /* Channel 0~16 Note On */
      case 0xA0 ... 0xAF:  /* Channel 0~16 Polyphonic Key Pressure (Aftertouch) */
      case 0xB0 ... 0xBF:  /* Channel 0~16 Control/Mode Change */
      case 0xE0 ... 0xEF:  /* Channel 0~16 Pitch Bend Change */
        dtype = midi; /* Set data type to midi */
        midi_bytes = 3;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          if (midimachine.index == 0) MIDBG("[M][B%d]$%02x#%03d", midimachine.index, buffer, buffer);
          midimachine.type = MIDI;
          midimachine.state = RECEIVING;
          midimachine.bus = CLAIMED;
          midimachine.index = 0;
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
        }
        break;
      default:
        break;
    }
  } else { /* Handle continuing byte stream */
    if (midimachine.state == RECEIVING) {
      if (midimachine.index < count_of(midimachine.streambuffer)) {
        /* Add midi data to the buffer ~ SysEx & Midi */
        midimachine.streambuffer[midimachine.index++] = buffer;
        /* Handle midi 2 & 3 byte buffer */
        if (midimachine.type == MIDI) {
          /* if (midimachine.streambuffer[0] >= 0x80 || midimachine.streambuffer[0] <= 0xEF) { */
            if (midimachine.index == midi_bytes) {
              MIDBG("\n");
              dtype = midi; /* Set data type to midi */
              process_midi(midimachine.streambuffer, midimachine.index);
              midimachine.index = 0;
              midimachine.state = IDLE;
              midimachine.bus = FREE;
              midimachine.type = NONE;
            }
        }
      } else {
        /* Buffer is full, receiving to much data too handle, wait for message to end */
        midimachine.state = WAITING_FOR_END;
        MIDBG("[EXCESS][IDX]%02d %02x \n", midimachine.index, buffer);
      }
    } else if (midimachine.state == WAITING_FOR_END) {
      /* Consuming SysEx messages, nothing else to do */
      MIDBG("[EXCESS][IDX]%02d %02x \n", midimachine.index, buffer);
    }
  }
}

void process_stream(uint8_t *buffer, size_t size)
{ /* ISSUE: Processing the stream byte by byte makes it more prone to latency */
  int n = 0;
  while (1) {
    midi_buffer_task(buffer[n++]);
    if (n == size) return;
  }
}
