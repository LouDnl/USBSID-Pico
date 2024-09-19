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


clock_rates clock_rate = CLOCK_DEFAULT;
hertz_values hertz = HZ_50;

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
  // writeSID((addr | 0x04), ((midimachine.sid_states[0][4] & 0xFE) | 0x1));
}

void process_midi(uint8_t* buffer, int size)
{
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

  uint8_t Flo, Fhi, Flo1, Fhi1;
  Flo = (frequency & 0xFF);
  Fhi = ((frequency >> 8) >= 0xFF ? 0xFF : (frequency >> 8));
  Flo1 = (calculated_freq & 0xFF);
  Fhi1 = ((calculated_freq >> 8) >= 0xFF ? 0xFF : (calculated_freq >> 8));

  // writeSID(0xD418, 0xE);
  int in, out, val, volume, add;
  uint8_t mask, modevol_state, voice_state, keep_state;
  uint16_t pulsewidth, pw_state;
  add = (buffer[0] & 0xF);
  addr = (0xD400 | (0x20 * add));

  switch (buffer[0]) {
    case 0x90 ... 0x93: // Note On ~ Voice 1
      midimachine.sid_states[0][0] = Flo;
      midimachine.sid_states[0][1] = Fhi;
      /* ADSR */
      // writeSID((addr | 0x05), 0x0);   /* Attack / Decay */
      // writeSID((addr | 0x06), 0xF0);  /* Sustain / Release */
      writeSID((addr | 0x18), midimachine.sid_states[0][24]);  /* Volume */
      writeSID((addr | 0x05), midimachine.sid_states[0][5]);   /* Attack / Decay */
      writeSID((addr | 0x06), midimachine.sid_states[0][6]);   /* Sustain / Release */
      writeSID((addr | 0x02), midimachine.sid_states[0][2]);   /* PW LO */
      writeSID((addr | 0x03), midimachine.sid_states[0][3]);   /* PW HI */
      /* Control */
      midimachine.sid_states[0][4] = ((midimachine.sid_states[0][4] & 0xFE) | 0x1);
      writeSID((addr | 0x04), midimachine.sid_states[0][4]);
      /* writeSID((addr | 0x04), ((midimachine.sid_states[0][4] & 0xFE) | 0x1)); */
      /* Note Lo & Hi */
      writeSID((addr | 0x0), midimachine.sid_states[0][0]);
      writeSID((addr | 0x01), midimachine.sid_states[0][1]);
      break;
    case 0x80 ... 0x83: // Note Off
      /* Note Lo & Hi */
      // writeSID((addr | 0x0), 0);
      // writeSID((addr | 0x01), 0);
      // /* Control */
      // writeSID((addr | 0x04), 0x10);
      midimachine.sid_states[0][4] = (midimachine.sid_states[0][4] & 0xFE);
      writeSID((addr | 0x04), midimachine.sid_states[0][4]);
      break;
    case 0xB0 ... 0xB3:
      switch (buffer[1]) {
        case 0x07:  /* Set Volume */
          in = buffer[2];
          volume = map(in, 0, 127, 0, 15);
          mask = 0xF0; /* Inverted */
          modevol_state = midimachine.sid_states[0][24];
          keep_state = (modevol_state & mask);
          val = (keep_state << 4) | volume;
          midimachine.sid_states[0][24] = val;
          addr |= 0x18;
          // writeSID(addr, val);
          break;
        case 0x49:  /* Attack */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF; /* Inverted */
          voice_state = midimachine.sid_states[0][5];
          keep_state = (voice_state & mask);
          val = (out << 4) | keep_state;
          midimachine.sid_states[0][5] = val;
          addr |= 0x05;
          // writeSID(addr, val);
          break;
        case 0x4B:  /* Decay */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF0; /* Inverted */
          voice_state = midimachine.sid_states[0][5];
          keep_state = (voice_state & mask);
          val = (keep_state | out);
          midimachine.sid_states[0][5] = val;
          // addr |= 0x05;
          // writeSID(addr, val);
          break;
        case 0x40:  /* Sustain */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF; /* Inverted */
          voice_state = midimachine.sid_states[0][6];
          keep_state = (voice_state & mask);
          val = (out << 4) | keep_state;
          midimachine.sid_states[0][6] = val;
          addr |= 0x06;
          // writeSID(addr, val);
          break;
        case 0x48:  /* Release */
          in = buffer[2];
          out = map(in, 0, 127, 0, 15);
          mask = 0xF0; /* Inverted */
          voice_state = midimachine.sid_states[0][6];
          keep_state = (voice_state & mask);
          val = (keep_state | out);
          midimachine.sid_states[0][6] = val;
          addr |= 0x06;
          // writeSID(addr, val);
          break;
        case 0x4C:  /* Pulse Width */
          in = buffer[2];
          out = map(in, 0, 127, 0, 4095);
          // mask = 0xF0; /* Inverted */
          pw_state = (midimachine.sid_states[0][3] << 8 | midimachine.sid_states[0][2]);
          // keep_state = (voice_state & mask);
          addr |= 0x06;
          val = (out >> 8);
          midimachine.sid_states[0][3] = val;
          val = (out & 0xF);
          midimachine.sid_states[0][2] = val;
          // writeSID(addr, val);
          break;
        default:
          break;
      }
      break;
  case 0xC0 ... 0xC3:
    switch (buffer[1]) {
      case 0x00:
        /* Noise */
        midimachine.sid_states[0][4] = ((midimachine.sid_states[0][4] & 0xF) | 0x80);
        break;
      case 0x01:
        /* Pulse */
        midimachine.sid_states[0][4] = ((midimachine.sid_states[0][4] & 0xF) | 0x40);
        break;
      case 0x02:
        /* Sawtooth */
        midimachine.sid_states[0][4] = ((midimachine.sid_states[0][4] & 0xF) | 0x20);
        break;
      case 0x03:
        /* Triangle */
        midimachine.sid_states[0][4] = ((midimachine.sid_states[0][4] & 0xF) | 0x10);
        break;
      default:
        break;
      }
    break;
  default:
    break;
  }
}

void process_buffer(uint8_t buffer)
{
  if (buffer & 0x80) {
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
      case 0x80 ... 0x8F:  // Note Off Event
      case 0x90 ... 0x9F:  // Note On Event
      case 0xA0 ... 0xAF:  // Polyphonic Key Pressure (Aftertouch)
      case 0xB0 ... 0xBF:  // Control/Mode Change
      case 0xC0 ... 0xCF:  // Program Phange
      case 0xD0 ... 0xDF:  // Channel Pressure (After-touch)
      case 0xE0 ... 0xEF:  // Pitch Bend Change
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
        break;
      }
  } else {
    if (midimachine.state == RECEIVING) {
      if (midimachine.index < sizeof(midimachine.streambuffer) / sizeof(*midimachine.streambuffer)) {
        /* Add midi data to the buffer ~ SysEx & Midi */
        midimachine.streambuffer[midimachine.index++] = buffer;
        /* Handle 3 byte midi buffer */
        if (midimachine.type == MIDI) {
          if (midimachine.streambuffer[0] >= 0x80 || midimachine.streambuffer[0] <= 0xEF) {
            if (midimachine.index == 3) {
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
