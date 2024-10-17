/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
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
 * Copyright (c) 2024 LouD
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
#include "midipatches.h"


/* GPIO externals */
extern uint8_t __not_in_flash_func(bus_operation)(uint8_t command, uint8_t address, uint8_t data);

/* ASID externals */
extern void process_sysex(uint8_t *buffer, int size);

/* Util externals */
extern long map(long x, long in_min, long in_max, long out_min, long out_max);

/* Config externals */
extern Config usbsid_config;
extern int numsids;

/* Init vars */
uint8_t addr, val;
clock_rates clock_rate = CLOCK_DEFAULT;
hertz_values hertz = HZ_50;
int midi_bytes = 3;

void midi_bus_operation(uint8_t a, uint8_t b);

void midi_init(void)
{
  /* Clear buffers once */
  __builtin_memset(midimachine.readbuffer, 0, sizeof midimachine.readbuffer);
  __builtin_memset(midimachine.streambuffer, 0, sizeof midimachine.streambuffer);
  __builtin_memset(midimachine.channelkey_states, 0, sizeof midimachine.channelkey_states);

  /* Initial state and index */
  midimachine.bus = FREE;
  midimachine.state = IDLE;
  midimachine.index = 0;
  curr_midi_channel = 0;

  /* NOTE: Midi state is not loaded from config on init, needs LOAD_MIDI_STATE command once */
}

void midi_bus_operation(uint8_t a, uint8_t b)
{
  bus_operation(0x10, a, b);
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
int vt[12] = { 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2 };
int voices[12] = {0};
int voice = 0;
int curr_midi_channel;
// int freevoice = 0;
void process_midi(uint8_t *buffer, int size)
{
  // for (int n = 0; n <= size; n++) MIDBG(" [B%d]$%02x#%d ", n, buffer[n]);
  uint8_t note_index = buffer[1];
  /* uint8_t note_velocity = buffer[2]; */
  uint16_t frequency = musical_scale_values[note_index];
  uint8_t Flo, Fhi;
  Flo = (frequency & VOICE_FREQLO);
  Fhi = ((frequency >> 8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> 8));

  int in, out, val, volume, /* add, */ channel/* , voice_start */;
  uint8_t mask, modevol_state, /* voice_state, */ keep_state;
  uint16_t fltres_state;

  int sidno = 0;  // NOTICE: TEMPORARY

  // int val1, val2, val3;
  curr_midi_channel = channel = (buffer[0] & R_NIBBLE); /* 1 -> 16 ~ 0x0 -> 0xF */
  volume = midimachine.channel_states[channel][sidno][MODVOL] & R_NIBBLE;
  addr = 0x0;
  /* int voice; */

  // voice = voice < 0 ? 0 : voice > 2 ? 2 : voice;
  // voice = freevoice >= 0 && freevoice <= 2 ? freevoice : voice;
  // for (int i = 0; i < 3; i++) {
  //     if (voices[i] != 0) voicecount++;
  // }
  // if (voicecount > 2) voice = 0;
  midimachine.channelkey_states[channel][sidno][N_KEYS] = midimachine.channelkey_states[channel][sidno][N_KEYS] < 0
    ? midimachine.channelkey_states[channel][sidno][N_KEYS] = 0
    : midimachine.channelkey_states[channel][sidno][N_KEYS];
  int voicecount = 0;
  for (int i = 0; i < ((numsids * 3) - 1); i++) {
    if (voices[i] == 0) {
      voicecount += 1; ;
    }
  }
  // if (voicecount > 0 && voicecount <= (numsids * 3) && midimachine.channelkey_states[channel][sidno][N_KEYS] != 0) midimachine.channelkey_states[channel][sidno][N_KEYS] = 0;
  if (voicecount == ((numsids * 3) - 1) && midimachine.channelkey_states[channel][sidno][N_KEYS] != 0) midimachine.channelkey_states[channel][sidno][N_KEYS] = 0;
  int bank, program;
  switch (buffer[0]) {
    case 0x80 ... 0x8F:  /* Channel 0~16 Note Off */
      bank = midimachine.channelbank[channel];
      switch (bank) {
        case 0:
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
              if (voices[i] == buffer[1]) {
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
            // if (voice != 0) voice--;
          break;
        case 9:
          bank_null_off(channel, sidno);
        default:
          break;
      }
      break;
    case 0x90 ... 0x9F:  /* Channel 0~16 Note On */
      bank = midimachine.channelbank[channel];
      switch (bank) {
        case 0:
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
          voices[voice] = buffer[1];
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
          break;
        case 9:
          bank_null_on(channel, sidno, Flo, Fhi);
        default:
          break;
      }
      break;
    case 0xA0 ... 0xAF:  /* Channel 0~16 Polyphonic Key Pressure (Aftertouch) */
      break;
    case 0xB0 ... 0xBF:  /* Channel 0~16 Control/Mode Change */
      switch (buffer[1]) { /* MSB or LSB for Control/Mode Change */
        /* Bank ~ Patch select */
        case 0x00:  /* 0  ~ Bank Select MSB */
          /* Ignore ~ only 1 bank for now */
          break;
        case 0x20:  /* 20 ~ Bank Select LSB */
          midimachine.channelbank[channel] = buffer[2];
          break;
        /* After touch something? */
        case 0x01:  /* 1  ~ Modulation wheel */
        /* Voice settings */
        case 0x07:  /* 7  ~ Set Master Volume */
          in = buffer[2];
          volume = map(in, 0, 127, 0, 15);
          mask = L_NIBBLE; /* Inverted */
          for (int sid = 0; sid < numsids; sid ++) {
            modevol_state = midimachine.channel_states[channel][sid][MODVOL];
            keep_state = (modevol_state & mask);
            val = keep_state | volume;
            midimachine.channel_states[channel][sid][MODVOL] = val;
            write(channel, sid, MODVOL);
          }
          /* write(channel, sidno, MODVOL); */
          break;
        case 0x10:  /* 16 ~ Low pass */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          for (int sid = 0; sid < numsids; sid ++) {
            keep_state = midimachine.channel_states[channel][sid][MODVOL];
            val = out == 1 ? keep_state |= 0x10 : keep_state ^ 0x10;
            midimachine.channel_states[channel][sid][MODVOL] = val;
            write(channel, sid, MODVOL);
          }
          /* write(channel, sidno, MODVOL); */
          break;
        case 0x11:  /* 17 ~ Band pass */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          for (int sid = 0; sid < numsids; sid ++) {
            keep_state = midimachine.channel_states[channel][sid][MODVOL];
            val = out == 1 ? keep_state |= 0x20 : keep_state ^ 0x20;
            midimachine.channel_states[channel][sid][MODVOL] = val;
            write(channel, sid, MODVOL);
          }
          /* write(channel, sidno, MODVOL); */
          break;
        case 0x12:  /* 18 ~ High pass */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          for (int sid = 0; sid < numsids; sid ++) {
            keep_state = midimachine.channel_states[channel][sid][MODVOL];
            val = out == 1 ? keep_state |= 0x40 : keep_state ^ 0x40;
            midimachine.channel_states[channel][sid][MODVOL] = val;
            write(channel, sid, MODVOL);
          }
          /* write(channel, sidno, MODVOL); */
          break;
        case 0x13:  /* 19 ~ Voice 3 disconnect */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          for (int sid = 0; sid < numsids; sid ++) {
            keep_state = midimachine.channel_states[channel][sid][MODVOL];
            val = out == 1 ? keep_state |= 0x80 : keep_state ^ 0x80;
            midimachine.channel_states[channel][sid][MODVOL] = val;
            write(channel, sid, MODVOL);
          }
          /* write(channel, sidno, MODVOL); */
          break;
        case 0x14:  /* 20 ~ Filter channel 1 */
        case 0x15:  /* 21 ~ Filter channel 2 */
        case 0x16:  /* 22 ~ Filter channel 3 */
        case 0x17:  /* 23 ~ Filter external */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          uint8_t fltchannel = buffer[1] == 0x14 ? 0x1
            : buffer[1] == 0x15 ? 0x2
            : buffer[1] == 0x16 ? 0x4
            : buffer[1] == 0x17 ? 0x8
            : 0; // <<- fallback
          for (int sid = 0; sid < numsids; sid ++) {
            fltres_state = midimachine.channel_states[channel][sid][RESFLT];
            val = out == 1 ? fltres_state |= fltchannel : fltres_state ^ fltchannel;
            midimachine.channel_states[channel][sid][RESFLT] = val;
            write(channel, sid, RESFLT);
          }
          /* write(channel, sidno, RESFLT); */
          break;
        case 0x42:  /* 66 ~ Ring modulator */
          for (int sid = 0; sid < numsids; sid ++) {
            midimachine.channel_states[channel][sid][CONTR] = ((midimachine.channel_states[channel][sid][CONTR] & R_NIBBLE) ^ 0x4);
            midimachine.channel_states[channel][sid][CONTR + 7] = ((midimachine.channel_states[channel][sid][CONTR + 7] & R_NIBBLE) ^ 0x4);
            midimachine.channel_states[channel][sid][CONTR + 14] = ((midimachine.channel_states[channel][sid][CONTR + 14] & R_NIBBLE) ^ 0x4);
            write_triple(channel, sid, CONTR);
          }
          /* write_triple(channel, sidno, CONTR); */
          break;
        case 0x43:  /* 67 ~ Sync */
          for (int sid = 0; sid < numsids; sid ++) {
            midimachine.channel_states[channel][sid][CONTR] = ((midimachine.channel_states[channel][sid][CONTR] & R_NIBBLE) ^ 0x2);
            midimachine.channel_states[channel][sid][CONTR + 7] = ((midimachine.channel_states[channel][sid][CONTR + 7] & R_NIBBLE) ^ 0x2);
            midimachine.channel_states[channel][sid][CONTR + 14] = ((midimachine.channel_states[channel][sid][CONTR + 14] & R_NIBBLE) ^ 0x2);
            write_triple(channel, sid, CONTR);
          }
          /* write_triple(channel, sidno, CONTR); */
          break;
        case 0x49:  /* 73 ~ Attack */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = R_NIBBLE; /* Inverted */
          for (int sid = 0; sid < numsids; sid ++) {
            for (int i = 0; i < 3; i++) {
              midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] = (out << 4) | (midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] & mask);
            }
            write_triple(channel, sid, ATTDEC);
          }
          /* write_triple(channel, sidno, ATTDEC); */
          break;
        case 0x4B:  /* 75 ~ Decay */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = L_NIBBLE;  /* Inverted */
          for (int sid = 0; sid < numsids; sid ++) {
            for (int i = 0; i < 3; i++) {
              midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] = (midimachine.channel_states[channel][sid][(i * 7) + ATTDEC] & mask) | out;
            }
            write_triple(channel, sid, ATTDEC);
          }
          /* write_triple(channel, sidno, ATTDEC); */
          break;
        case 0x40:  /* 64 ~ Sustain */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = R_NIBBLE;  /* Inverted */
          for (int sid = 0; sid < numsids; sid ++) {
            for (int i = 0; i < 3; i++) {
              midimachine.channel_states[channel][sid][(i * 7) + SUSREL] = (out << 4) | (midimachine.channel_states[channel][sid][(i * 7) + SUSREL] & mask);
            }
            write_triple(channel, sid, SUSREL);
          }
          /* write_triple(channel, sidno, SUSREL); */
          break;
        case 0x48:  /* 72 ~ Release */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = L_NIBBLE;  /* Inverted */
          for (int sid = 0; sid < numsids; sid ++) {
            for (int i = 0; i < 3; i++) {
              midimachine.channel_states[channel][sid][(i * 7) + SUSREL] = (midimachine.channel_states[channel][sid][(i * 7) + SUSREL] & mask) | out;
            }
            write_triple(channel, sid, SUSREL);
          }
          /* write_triple(channel, sidno, SUSREL); */
          break;
        case 0x46:  /* 70 ~ Filter resonance */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = R_NIBBLE;  /* Inverted */
          for (int sid = 0; sid < numsids; sid ++) {
            midimachine.channel_states[channel][sid][RESFLT] = (out << 4) | (midimachine.channel_states[channel][sid][RESFLT] & mask);
            write(channel, sid, RESFLT);
          }
          /* write(channel, sidno, RESFLT); */
          break;
        case 0x47:  /* 71 ~ Filter */
          in = buffer[2];
          out = map(in, 0, 127, 0, 2047);
          for (int sid = 0; sid < numsids; sid ++) {
            val = (out & 0x7);
            midimachine.channel_states[channel][sid][FC_LO] = val;
            val = (out & 0x7F8) >> 3;
            midimachine.channel_states[channel][sid][FC_HI] = val;
              write(channel, sid, FC_LO);
            write(channel, sid, FC_HI);
          }
          /* write(channel, sidno, FC_LO); */
          /* write(channel, sidno, FC_HI); */
          break;
        case 0x4A:  /* 74 ~ Unused */
          break;
        case 0x4C:  /* 76 ~ Pulse Width */
          in = buffer[2];
          out = map(in, 0, 0x7F, 0, 0xFFF);
          /* Pulse width Hi */
          // midimachine.sid_states[sidno][PWMHI + voice_start] = ((out & NIBBLE_3) >> 8);
          /* Pulse width Lo */
          // midimachine.sid_states[sidno][PWMLO + voice_start] = (out & BYTE);
          // midi_bus_operation((addr | 0x02), midimachine.sid_states[sidno][PWMLO + voice_start]);  /* PW LO */
          // midi_bus_operation((addr | 0x03), midimachine.sid_states[sidno][PWMHI + voice_start]);  /* PW HI */
          for (int sid = 0; sid < numsids; sid ++) {
            for (int i = 0; i < 3; i++) {
              midimachine.channel_states[channel][sid][(i * 7) + PWMHI] = ((out & NIBBLE_3) >> 8);
              midimachine.channel_states[channel][sid][(i * 7) + PWMLO] = (out & BYTE);
            }
            write_triple(channel, sid, PWMHI);
            write_triple(channel, sid, PWMLO);
          }
          /* write_triple(channel, sidno, PWMHI); */
          /* write_triple(channel, sidno, PWMLO); */
          break;
        default:
          break;
      }
      break;
    case 0xC0 ... 0xCF:  /* Channel 0~16 Program change */
      program = midimachine.channelprogram[channel] = buffer[1];
      bank = midimachine.channelbank[channel];
      switch (bank) {
        case 0:  /* 3VoiceSID */
          switch (program) {
            case 0:  /* Noise */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                  midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x80);
                }
                write_triple(channel, sid, CONTR);
              }
              /* write_triple(channel, sidno, CONTR); */
              break;
            case 1:  /* Pulse */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x40);
                }
                write_triple(channel, sid, CONTR);
               }
              /* write_triple(channel, sidno, CONTR); */
              break;
            case 2:  /* Sawtooth */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                  midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x20);
                }
                write_triple(channel, sid, CONTR);
              }
              /* write_triple(channel, sidno, CONTR); */
              break;
            case 3:  /* Triangle */
              for (int sid = 0; sid < numsids; sid ++) {
                for (int i = 0; i < 3; i++) {
                  midimachine.channel_states[channel][sid][(i * 7) + CONTR] = ((midimachine.channel_states[channel][sid][(i * 7) + CONTR] & R_NIBBLE) | 0x10);
                }
                write_triple(channel, sid, CONTR);
              }
              /* write_triple(channel, sidno, CONTR); */
              break;
          };
          break;
        case 9:
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
  // MVDBG("[S]%d[N]%d[V~%d][V%d]%02x%02x%02x %02x%02x%02x  %02x%02x%02x  %02x%02x%02x\r\n",
  //     sidno, midimachine.channelkey_states[channel][sidno][N_KEYS], // N
  //     voice, vt[voice], voices[0],voices[1],voices[2], //V
  //     voices[3],voices[4],voices[5], //V
  //     voices[6],voices[7],voices[8], //V
  //     voices[9],voices[10],voices[11] //V
  //   );
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

void process_buffer(uint8_t buffer)
{/* ISSUE: Processing the stream byte by byte makes it prone to latency */
  /* if (midimachine.index == 0) MIDBG("\r[M][B%d]$%02x#%d", midimachine.index, buffer, buffer); */
  if (midimachine.index != 0) MIDBG(" [B%d]$%02x#%d", midimachine.index, buffer, buffer);

  if (buffer & 0x80)
  {
    switch (buffer) {
      /* System Exclusive */
      case 0xF0:  /* System Exclusive Start */
        if (midimachine.bus != CLAIMED) {
          midimachine.type = SYSEX;
          midimachine.state = RECEIVING;
          midimachine.bus = CLAIMED;
          midimachine.index = 0;
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
        }
        break;
      case 0xF7:  /* System Exclusive End of SysEx (EOX) */
        midimachine.streambuffer[midimachine.index] = buffer;
        process_sysex(midimachine.streambuffer, midimachine.index);
        midimachine.index = 0;
        midimachine.state = IDLE;
        midimachine.bus = FREE;
        midimachine.type = NONE;
        /* MIDBG("[E]\r\n"); */
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
        midi_bytes = 2;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          if (midimachine.index == 0) MIDBG("[M][B%d]$%02x#%d", midimachine.index, buffer, buffer);
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
        midi_bytes = 3;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          if (midimachine.index == 0) MIDBG("[M][B%d]$%02x#%d", midimachine.index, buffer, buffer);
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
  }
  else
  {
    if (midimachine.state == RECEIVING) {
      if (midimachine.index < sizeof(midimachine.streambuffer) / sizeof(*midimachine.streambuffer)) {
        /* Add midi data to the buffer ~ SysEx & Midi */
        midimachine.streambuffer[midimachine.index++] = buffer;
        /* if (midimachine.type == SYSEX) MIDBG("[S]$%02x", buffer); */
        /* Handle 3 byte midi buffer */
        if (midimachine.type == MIDI) {
          if (midimachine.streambuffer[0] >= 0x80 || midimachine.streambuffer[0] <= 0xEF) {
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
        }
      } else {
        /* Buffer is full, receiving to much data too handle, wait for message to end */
        midimachine.state = WAITING_FOR_END;
      }
    } else if (midimachine.state == WAITING_FOR_END) {
      /* Consuming SysEx messages, nothing else to do */
    }
  }
}
