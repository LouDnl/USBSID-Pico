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

#ifdef MIDI_DEBUG
#define MIDBG(...) printf(__VA_ARGS__)
#else
#define MIDBG(...) ((void)0)
#endif

#ifdef IOMIDI_DEBUG
#define IMIDBG(...) printf(__VA_ARGS__)
#else
#define IMIDBG(...) ((void)0)
#endif

clock_rates clock_rate = CLOCK_DEFAULT;
hertz_values hertz = HZ_50;
int midi_bytes = 3;

void midi_init(void)
{
  /* Clear buffers once */
  memset(midimachine.readbuffer, 0, sizeof midimachine.readbuffer);
  // memset(midimachine.packetbuffer, 0, sizeof midimachine.packetbuffer);
  memset(midimachine.streambuffer, 0, sizeof midimachine.streambuffer);
  memset(midimachine.sid_states, 0, sizeof midimachine.sid_states);
  /* Initial state and index */
  midimachine.bus = FREE;
  midimachine.state = IDLE;
  midimachine.index = 0;
  // writeSID((addr | 0x04), ((midimachine.sid_states[sidno][4] & 0xFE) | 0x1));
  for (int i = 0; i < 4; i++)
  {
    midimachine.sid_states[i][31] = 0;
  }
}

void process_midi(uint8_t *buffer, int size)
{
  MIDBG("[D0]#%d$%02x [D1]#%d$%02x [D2]$%02x ", buffer[0], buffer[0], buffer[1], buffer[1], buffer[2]);
  uint8_t note_index = buffer[1];
  uint8_t note_velocity = buffer[2];
  uint16_t frequency = musical_scale_values[note_index];
  double a, b, c, d, e, f, g, h;
  int freq, reg01;
  uint16_t calculated_freq;

  a = buffer[1];
  b = a / 12;
  // uint16_t calculated_freq = round(pow((16.35 * 2), (note_index / 12)));
  c = pow(2, b);
  d = 16.35 * c;
  e = round(d);
  freq = e;
  f = pow(2, 24);
  g = f / clock_rate;
  h = freq * g;
  reg01 = round(h);
  calculated_freq = round(h);

  // MIDBG("[S]%d [C]0x%02x [D1]0x%02x [D2]0x%02x [F]0x%x [CF]0x%x [FR]%d [R1]%d [A]%f [B]%f [C]%f [D]%f [E]%f [F]%f [G]%f [H]%f\r\n",
  //       size, buffer[0], buffer[1], buffer[2], // s c d1 d2
  //       frequency, calculated_freq, freq, reg01,            // f cf
  //       a, b, c, d, e, f, g, h);
  uint8_t Flo, Fhi, Flo1, Fhi1;
  Flo = (frequency & 0xFF);
  Fhi = ((frequency >> 8) >= 0xFF ? 0xFF : (frequency >> 8));
  Flo1 = (calculated_freq & 0xFF);
  Fhi1 = ((calculated_freq >> 8) >= 0xFF ? 0xFF : (calculated_freq >> 8));

  // writeSID(0xD418, 0xE);
  int in, out, val, volume, add, sidno, voice;
  uint8_t mask, modevol_state, voice_state, keep_state;
  uint16_t pulsewidth, pw_state, filter, filter_state, fltres_state;
  add = (buffer[0] & 0xF);
  switch (add) {
    case 0:
      voice = 0;
      sidno = 0;
      break;
    case 1:
      voice = 7;
      sidno = 0;
      break;
    case 2:
      voice = 14;
      sidno = 0;
      break;
    case 3 ... 5: /* TODO: FINISH VOICES FOR SID 2, 3 & 4 */
      sidno = 1;
      break;
    case 6 ... 8:
      sidno = 2;
      break;
    case 9 ... 11:
      sidno = 3;
      break;
    default:
      sidno = 0;
      break;
  }
  addr = (0xD400 | (0x20 * sidno));

  switch (buffer[0]) {
    case 0x90 ... 0x93:  /* 144-147 ~ Note On ~ Voice 1 */
      // midimachine.sid_states[sidno][0] = Flo;
      // midimachine.sid_states[sidno][1] = Fhi;

      // adding up does not work for dual tones
      // midimachine.sid_states[sidno][0] = onoff == 1 ? midimachine.sid_states[sidno][0] + Flo : Flo;
      // midimachine.sid_states[sidno][1] = onoff == 1 ? midimachine.sid_states[sidno][1] + Fhi : Fhi;

      /* MIDBG("[ON] F.Lo $%02x F.Hi $%02x cF.Lo $%02x cF.Hi $%02x\r\n", Flo, Fhi, Flo1, Fhi1); */
      /* ADSR */
      // writeSID((addr | 0x05), 0x0);   /* Attack / Decay */
      // writeSID((addr | 0x06), 0xF0);  /* Sustain / Release */

      writeSID((addr | 0x18), midimachine.sid_states[sidno][24]);  /* Volume */
      // BUG
      writeSID((addr | 0x05), midimachine.sid_states[sidno][5]);   /* Attack / Decay */
      writeSID((addr | 0x06), midimachine.sid_states[sidno][6]);   /* Sustain / Release */
      writeSID((addr | 0x02), midimachine.sid_states[sidno][2]);   /* PW LO */
      writeSID((addr | 0x03), midimachine.sid_states[sidno][3]);   /* PW HI */
      /* Control ~ Gate bit on */
      if (midimachine.sid_states[sidno][31] == 0) {
      // midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xFE) | 0x1);
      val = midimachine.sid_states[sidno][4] & 0xFE;
      val |= 0x1;
      midimachine.sid_states[sidno][4] = val;
      // MIDBG("KAK %d %d %d KAK", (addr | 0x04), midimachine.sid_states[sidno][4], val);
      writeSID((addr | 0x04), midimachine.sid_states[sidno][4]);
      };
      /* writeSID((addr | 0x04), ((midimachine.sid_states[sidno][4] & 0xFE) | 0x1)); */
      /* Note Lo & Hi */
      midimachine.sid_states[sidno][0] = Flo;
      midimachine.sid_states[sidno][1] = Fhi;
      writeSID((addr | 0x0), midimachine.sid_states[sidno][0]);
      writeSID((addr | 0x01), midimachine.sid_states[sidno][1]);
      // MIDBG(" $%02x$%02x ", (addr | 0x0), (addr | 0x1));
      midimachine.sid_states[sidno][30]++;
      break;
    case 0x80 ... 0x83:  /* 128-131 ~ Note Off */
      /* MIDBG("[OFF] F.Lo $%02x F.Hi $%02x cF.Lo $%02x cF.Hi $%02x\r\n", Flo, Fhi, Flo1, Fhi1); */
      /* Note Lo & Hi */
      // writeSID((addr | 0x0), 0);
      // writeSID((addr | 0x01), 0);
      // /* Control */
      // writeSID((addr | 0x04), 0x10);
      if (midimachine.sid_states[sidno][30] != 0) midimachine.sid_states[sidno][30]--;
      if (midimachine.sid_states[sidno][30] < 0) midimachine.sid_states[sidno][30] = 0;
      if (midimachine.sid_states[sidno][30] == 0)
      {
        /* Control ~ Gate bit off */
        if (midimachine.sid_states[sidno][31] == 0) {
        midimachine.sid_states[sidno][4] = (midimachine.sid_states[sidno][4] & 0xFE);
        writeSID((addr | 0x04), midimachine.sid_states[sidno][4]);
        };
      };
      break;
    case 0xB0 ... 0xB3:
      switch (buffer[1]) {
        case 0x07:  /* Set Volume */
          in = buffer[2];
          volume = map(in, 0, 127, 0, 15);
          mask = 0xF0; /* Inverted */
          modevol_state = midimachine.sid_states[sidno][24];
          keep_state = (modevol_state & mask);
          val = keep_state | volume;
          midimachine.sid_states[sidno][24] = val;
          addr |= 0x18;
          writeSID(addr, midimachine.sid_states[sidno][24]);  /* Volume */
          // MIDBG("$%02x$%02x$%02x", modevol_state, keep_state,val);
          // writeSID(addr, val);
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          break;
        case 0x10:  /* 16 ~ Low pass */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          keep_state = midimachine.sid_states[sidno][24];
          val = out == 1 ? keep_state |= 0x10 : keep_state ^ 0x10;
          midimachine.sid_states[sidno][24] = val;
          addr |= 0x18;
          writeSID(addr, midimachine.sid_states[sidno][24]);
          // MIDBG("$%02x$%02x", keep_state, val);
          break;
        case 0x11:  /* 17 ~ Band pass */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          keep_state = midimachine.sid_states[sidno][24];
          val = out == 1 ? keep_state |= 0x20 : keep_state ^ 0x20;
          midimachine.sid_states[sidno][24] = val;
          addr |= 0x18;
          writeSID(addr, midimachine.sid_states[sidno][24]);
          // MIDBG("$%02x$%02x", keep_state, val);
          break;
        case 0x12:  /* 18 ~ High pass */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          keep_state = midimachine.sid_states[sidno][24];
          val = out == 1 ? keep_state |= 0x40 : keep_state ^ 0x40;
          midimachine.sid_states[sidno][24] = val;
          addr |= 0x18;
          writeSID(addr, midimachine.sid_states[sidno][24]);
          // MIDBG("$%02x$%02x", keep_state, val);
          break;
        // case 0x13:  /* 19 ~ Voice 3 disconnect */
        //   in = buffer[2];
        //   out = map(in, 0, 127, 0, 1);
        //   keep_state = midimachine.sid_states[sidno][24];
        //   val = out == 1 ? keep_state |= 0x80 : keep_state ^ 0x80;
        //   midimachine.sid_states[sidno][24] = val;
        //   addr |= 0x18;
        //   writeSID(addr, midimachine.sid_states[sidno][24]);
        //   // MIDBG("$%02x$%02x", keep_state, val);
        //   break;
        case 0x14:  /* 20 ~ Filter channel 1 */
        case 0x15:  /* 21 ~ Filter channel 2 */
        case 0x16:  /* 22 ~ Filter channel 3 */
        // case 0x17:  /* 23 ~ Filter external */
          in = buffer[2];
          out = map(in, 0, 127, 0, 1);
          uint8_t channel = buffer[1] == 0x14 ? 0x1 : buffer[1] == 0x15 ? 0x2
                          : buffer[1] == 0x16 ? 0x4
                          : buffer[1] == 0x17 ? 0x8
                          : 0; // <<- fallback
          fltres_state = midimachine.sid_states[sidno][23];
          // keep_state = (fltres_state & 0xFF);
          // val = (keep_state | channel);
          // val = (fltres_state | channel);
          val = out == 1 ? fltres_state |= channel : fltres_state ^ channel;
          midimachine.sid_states[sidno][23] = val;
          addr |= 0x17;
          writeSID(addr, midimachine.sid_states[sidno][23]);   /* Filter channel */
          // MIDBG("$%02x$%02x$%02x", channel, fltres_state, val);
          break;
        case 0x42:  /* 66 ~ Ring modulator */
          // MIDBG("$%02x$%02x$%02x", buffer[0], buffer[1], buffer[2]);
          /* Ring modulator */
          midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xF) ^ 0x4);
          addr |= 0x04;
          writeSID(addr, midimachine.sid_states[sidno][4]);
          break;
        case 0x43:  /* 67 ~ Sync */
          // MIDBG("$%02x$%02x$%02x", buffer[0], buffer[1], buffer[2]);
          /* Sync */
          midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xFF) ^ 0x2);
          addr |= 0x04;
          writeSID(addr, midimachine.sid_states[sidno][4]);
          break;
        case 0x49:  /* 73 ~ Attack */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF; /* Inverted */
          voice_state = midimachine.sid_states[sidno][5];
          keep_state = (voice_state & mask);
          val = (out << 4) | keep_state;
          midimachine.sid_states[sidno][5] = val;
          addr |= 0x05;
          writeSID(addr, midimachine.sid_states[sidno][5]);   /* Attack / Decay */
          // writeSID(addr, val);
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          break;
        case 0x4B:  /* 75 ~ Decay */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF0; /* Inverted */
          voice_state = midimachine.sid_states[sidno][5];
          keep_state = (voice_state & mask);
          val = (keep_state | out);
          midimachine.sid_states[sidno][5] = val;
          addr |= 0x05;
          writeSID(addr, midimachine.sid_states[sidno][5]);   /* Attack / Decay */
          // addr |= 0x05;
          // writeSID(addr, val);
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          break;
        case 0x40:  /* 64 ~ Sustain */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF; /* Inverted */
          voice_state = midimachine.sid_states[sidno][6];
          keep_state = (voice_state & mask);
          val = (out << 4) | keep_state;
          midimachine.sid_states[sidno][6] = val;
          addr |= 0x06;
          writeSID(addr, midimachine.sid_states[sidno][6]);   /* Sustain / Release */
          // writeSID(addr, val);
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          break;
        case 0x48:  /* 72 ~ Release */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF0; /* Inverted */
          voice_state = midimachine.sid_states[sidno][6];
          keep_state = (voice_state & mask);
          val = (keep_state | out);
          midimachine.sid_states[sidno][6] = val;
          addr |= 0x06;
          writeSID(addr, midimachine.sid_states[sidno][6]);   /* Sustain / Release */
          // writeSID(addr, val);
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          break;
        case 0x46:  /* 70 ~ Filter resonance */
          in = buffer[2];
          out = map(in, 0, 127, 0, 127);
          fltres_state = midimachine.sid_states[sidno][23];
          keep_state = (fltres_state & 0xF);
          val = (out << 4) | keep_state;
          midimachine.sid_states[sidno][23] = val;
          addr |= 0x17;
          writeSID(addr, midimachine.sid_states[sidno][23]);   /* Filter rsonance */
          break;
        case 0x47:  /* 71 ~ Filter */
          in = buffer[2];
          out = map(in, 0, 127, 0, 2047);
          filter_state = (midimachine.sid_states[sidno][22] << 3 | (midimachine.sid_states[sidno][21] & 0x7));
          // addr |= 0x06;
          val = (out & 0x7);
          midimachine.sid_states[sidno][21] = val;
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          val = (out & 0x7F8) >> 3;
          midimachine.sid_states[sidno][22] = val;
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          // writeSID(addr, val);
          writeSID((addr | 0x22), midimachine.sid_states[sidno][21]);   /* FC LO */
          writeSID((addr | 0x23), midimachine.sid_states[sidno][22]);   /* FC HI */
          break;
        case 0x4A:  /* 74 ~ Unused */
        //   in = buffer[2];
        //   out = map(in, 0, 127, 0, 3);
        //   uint8_t channel = out == 0 ? 1 : out == 1 ? 2
        //                                : out == 2   ? 4
        //                                : out == 3   ? 8
        //                                             : 0;
        //   fltres_state = midimachine.sid_states[sidno][23];
        //   keep_state = (fltres_state & 0xF0);
        //   val = (keep_state | channel);
        //   midimachine.sid_states[sidno][23] = val;
        //   writeSID((addr | 0x17), midimachine.sid_states[sidno][23]);   /* Filter channel */
          break;
        case 0x4C:  /* 76 ~ Pulse Width */
          in = buffer[2];
          out = map(in, 0, 127, 0, 4095);
          // mask = 0xF0; /* Inverted */
          pw_state = (midimachine.sid_states[sidno][3] << 8 | midimachine.sid_states[sidno][2]);
          // keep_state = (voice_state & mask);
          // addr |= 0x06;
          // val = (out >> 8);
          /* Pulse width Hi */
          midimachine.sid_states[sidno][3] = (out >> 8);
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          // val = (out & 0xFF);
          /* Pulse width Lo */
          midimachine.sid_states[sidno][2] = (out & 0xFF);
          // MIDBG("W@[0x%04x] (0b"PRINTF_BINARY_PATTERN_INT16") D[0x%02x] (0b"PRINTF_BINARY_PATTERN_INT8") ", addr, PRINTF_BYTE_TO_BINARY_INT16(addr), val, PRINTF_BYTE_TO_BINARY_INT8(val));
          // writeSID(addr, val);
          writeSID((addr | 0x02), midimachine.sid_states[sidno][2]);   /* PW LO */
          writeSID((addr | 0x03), midimachine.sid_states[sidno][3]);   /* PW HI */
          break;
        default:
          break;
      }
      break;
    case 0xBF:
      switch (buffer[1]) {
        case 0x6E:  // TODO: Adjust for all 3 voices per sid
            if (buffer[2] == 0x7F) {
              /* Gate bit keep */
              midimachine.sid_states[sidno][31] ^= 0x1;
              // midimachine.sid_states[sidno][31] = (midimachine.sid_states[sidno][31] ^ 0x1);
              MIDBG("%d ? ", midimachine.sid_states[sidno][31]);
              /* Control ~ Gate bit on */
              if (midimachine.sid_states[sidno][31] == 1) {
                midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xFE) | 0x1);
                writeSID((addr | 0x04), midimachine.sid_states[sidno][4]);
              };
              /* Control ~ Gate bit off */
              if (midimachine.sid_states[sidno][31] != 1) {
                midimachine.sid_states[sidno][4] = (midimachine.sid_states[sidno][4] & 0xFE);
                writeSID((addr | 0x04), midimachine.sid_states[sidno][4]);
              };
            };
            break;
        case 0x6F:
            break;
        }
      break;
    case 0xC0 ... 0xC3:
      switch (buffer[1]) {
        case 0x00:  /* Noise */
          // MIDBG("$%02x$%02x$%02x", buffer[0], buffer[1], buffer[2]);
          /* Noise */
          midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xF) | 0x80);
          addr |= 0x04;
          writeSID(addr, midimachine.sid_states[sidno][4]);
          break;
        case 0x01:  /* Pulse */
          // MIDBG("$%02x$%02x$%02x", buffer[0], buffer[1], buffer[2]);
          /* Pulse */
          midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xF) | 0x40);
          addr |= 0x04;
          writeSID(addr, midimachine.sid_states[sidno][4]);
          break;
        case 0x02:  /* Sawtooth */
          // MIDBG("$%02x$%02x$%02x", buffer[0], buffer[1], buffer[2]);
          /* Sawtooth */
          midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xF) | 0x20);
          addr |= 0x04;
          writeSID(addr, midimachine.sid_states[sidno][4]);
          break;
        case 0x03:  /* Triangle */
          // MIDBG("$%02x$%02x$%02x", buffer[0], buffer[1], buffer[2]);
          /* Triangle */
          midimachine.sid_states[sidno][4] = ((midimachine.sid_states[sidno][4] & 0xF) | 0x10);
          addr |= 0x04;
          writeSID(addr, midimachine.sid_states[sidno][4]);
          break;
        case 0x04 ... 255:
          // MIDBG("$%02x$%02x$%02x", buffer[0], buffer[1], buffer[2]);
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
  // extern uint32_t *BUSState;
  // IMIDBG("0x%llu 0b"PRINTF_BINARY_PATTERN_INT32"\r\n",*BUSState,*BUSState);
  MIDBG("[A]$%02x[N]%d[V]%d[0&1]$%04x[2&3]$%04x[4]$%02x[5]$%02x[6]$%02x[15&16]$%02x%01x[17]$%02x[18]$%02x\r\n",
    (addr & 0xFF), // A
    midimachine.sid_states[sidno][30], sidno,  // N V
    (midimachine.sid_states[sidno][1] << 8 | midimachine.sid_states[sidno][0]), // 01
    // ((Fhi1 << 8) | Flo1),  // 01
    (midimachine.sid_states[sidno][3] << 8 | midimachine.sid_states[sidno][2]),  // 23
    midimachine.sid_states[sidno][4],
    midimachine.sid_states[sidno][5],
    midimachine.sid_states[sidno][6],
    midimachine.sid_states[sidno][22],
    midimachine.sid_states[sidno][21],
    midimachine.sid_states[sidno][23],
    midimachine.sid_states[sidno][24]);
}

void process_buffer(uint8_t buffer)
{
  if (midimachine.index == 0) MIDBG("[B]%d", midi_bytes);
  if (buffer & 0x80)
  {
    switch (buffer) {
      /* System Exclusive */
      case 0xF0: // System Exclusive Start
        if (midimachine.bus != CLAIMED) {
          midimachine.type = SYSEX;
          midimachine.state = RECEIVING;
          midimachine.bus = CLAIMED;
          midimachine.index = 0;
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
        }
        break;
      case 0xF7: // System Exclusive End of SysEx (EOX)
        midimachine.streambuffer[midimachine.index] = buffer;
        process_sysex(midimachine.streambuffer, midimachine.index);
        midimachine.index = 0;
        midimachine.state = IDLE;
        midimachine.bus = FREE;
        midimachine.type = NONE;
        MIDBG("[E]\r\n");
        break;
      case 0xF1: // System Exclusive MIDI Time Code Qtr. Frame
      case 0xF2: // System Exclusive Song Position Pointer
      case 0xF3: // System Exclusive Song Select (Song #)
      case 0xF4: // System Exclusive Undefined (Reserved)
      case 0xF5: // System Exclusive Undefined (Reserved)
      case 0xF6: // System Exclusive Tune request
      case 0xF8: // System Exclusive Timing clock
      case 0xF9: // System Exclusive Undefined (Reserved)
      case 0xFA: // System Exclusive Start
      case 0xFB: // System Exclusive Continue
      case 0xFC: // System Exclusive Stop
      case 0xFD: // System Exclusive Undefined (Reserved)
      case 0xFE: // System Exclusive Active Sensing
      case 0xFF: // System Exclusive System Reset
        break;

      /* Midi */
      // case 0x0 ... 0x7F:   // Data Byte
        // break;

      case 0xC0 ... 0xCF:  // Program Phange
      case 0xD0 ... 0xDF:  // Channel Pressure (After-touch)
        midi_bytes = 2;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          midimachine.type = MIDI;
          midimachine.state = RECEIVING;
          midimachine.bus = CLAIMED;
          midimachine.index = 0;
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
        }
        break;
      case 0xB0 ... 0xBF:  // Control/Mode Change
      case 0x80 ... 0x8F:  // Note Off Event
      case 0x90 ... 0x9F:  // Note On Event
      case 0xA0 ... 0xAF:  // Polyphonic Key Pressure (Aftertouch)
      case 0xE0 ... 0xEF:  // Pitch Bend Change
        midi_bytes = 3;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          midimachine.type = MIDI;
          midimachine.state = RECEIVING;
          midimachine.bus = CLAIMED;
          midimachine.index = 0;
          midimachine.streambuffer[midimachine.index] = buffer;
          midimachine.index++;
        }
        break;

      default:
          /* MIDBG("\r\n"); */
        break;
      }
  }
  else
  {
    // MIDBG("[BM]#%d $%02x ", buffer, buffer);
    if (midimachine.state == RECEIVING) {
      if (midimachine.index < sizeof(midimachine.streambuffer) / sizeof(*midimachine.streambuffer)) {
        /* Add midi data to the buffer ~ SysEx & Midi */
        midimachine.streambuffer[midimachine.index++] = buffer;
        if (midimachine.type == SYSEX) MIDBG("[S]$%02x", buffer);
        /* Handle 3 byte midi buffer */
        if (midimachine.type == MIDI) {
          if (midimachine.streambuffer[0] >= 0x80 || midimachine.streambuffer[0] <= 0xEF) {
            if (midimachine.index == midi_bytes) {
              // MIDBG("%x %x %x %x\r\n", midimachine.streambuffer[0], midimachine.streambuffer[1], midimachine.streambuffer[2], midimachine.streambuffer[3]);
              process_midi(midimachine.streambuffer, midimachine.index);
              midimachine.index = 0;
              midimachine.state = IDLE;
              midimachine.bus = FREE;
              midimachine.type = NONE;
            }
          }
        }
      } else {
        // Buffer is full, just wait for this message to be finished
        midimachine.state = WAITING_FOR_END;
      }
    } else if (midimachine.state == WAITING_FOR_END) {
        // We are getting data even though we are just waiting for sysex end to come
        // So just consume data and do nothing...
    }
  }
}

// 20240723 ~ disabled, unused
// void process_stream(uint8_t* buffer)
// {
//   (void)buffer;
//   int i;
//   // TODO: add condition if stream [0] == 0xF0 == sysex
//   // TODO: add condition if stream [0~3] == midi
//   if (midimachine.streambuffer[0] == 0xF0)
//   {
//     MIDBG("[B] ");
//     for (i = 0; i < sizeof(midimachine.streambuffer); i++) {
//       MIDBG("%02x ", midimachine.streambuffer[i]);
//       process_buffer(midimachine.streambuffer[i]);
//       if (midimachine.streambuffer[i] == 0xF7) {
//         memset(midimachine.streambuffer, 0, sizeof midimachine.streambuffer);
//         MIDBG("\r\n");
//         break;
//       }
//     }
//   }
// }
