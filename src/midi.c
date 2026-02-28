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

#include "midi.h"
#include "globals.h"
#include "config.h"
#include "sid.h"
#include "logging.h"

#include "midi_cc.h"


/* sid.c */
extern void reset_sid(void);
extern void reset_sid_registers(void);

/* midi_handler.c */
extern void midi_processor_init(void);
extern void process_midi(uint8_t *buffer, int size);

/* sysex.c */
extern void process_sysex(uint8_t *buffer, int size);

#ifdef ONBOARD_EMULATOR
/* Cynthcart ~ Emudore */
#include "pico/util/queue.h"  /* Intercore queue */
extern bool emulator_running,
  starting_emulator,
  stopping_emulator,
  offload_ledrunner;
extern bool stop_cynthcart(void);
queue_t cynthcart_queue;
#endif

/* Always boot with default CC values ~ TODO: Load from flash!? */
const midi_ccvalues midi_ccvalues_defaults = MIDI_DEFAULT_CCVALUES_INIT;


/* Initialise the midi handlers */
void midi_init(void)
{
  usNFO("\n");
  usNFO("[MIDI] Init\n");

  /* Set initial stream state and index */
  midimachine.bus = FREE;
  midimachine.state = IDLE;
  midimachine.index = 0;
  midimachine.midi_bytes = 3;

  /* Clear stream buffers once */
  memset(midimachine.streambuffer, 0, sizeof midimachine.streambuffer);
  memset(midimachine.usbstreambuffer, 0, sizeof midimachine.usbstreambuffer);

  /* Start the processor of midi buffers */
  midi_processor_init();

  return;
}

#ifdef ONBOARD_EMULATOR
void emulator_queue_init(void)
{
  /* emudore */
  queue_init(&cynthcart_queue, sizeof(cynthcart_queue_entry_t), 128); /* 128 entries as buffer */
}

inline void emulator_queue_deinit(void)
{
  /* emudore */
  queue_free(&cynthcart_queue);
  reset_sid();
  reset_sid_registers();
}

inline void handle_emulater_data(void)
{
  for (size_t e = 0; e < midimachine.index; e++) {
    /* Create queue entry */
    cynthcart_queue_entry_t cq_entry;
    /* Add byte to queue */
    cq_entry.data = midimachine.streambuffer[e];
    /* Add to queue (blocking wait if queue is full) */
    queue_add_blocking(&cynthcart_queue, &cq_entry);
  }
  return;
}

inline void emulator_enable(void)
{
  emulator_queue_init();
  emulator_running = false;
  offload_ledrunner = true;
  starting_emulator = true;
  return;
}

void emulator_disable(void)
{
  emulator_running = false;
  stopping_emulator = true;
  stop_cynthcart();
  offload_ledrunner = false;
  emulator_queue_deinit();
  return;
}

inline void emulator_reset(void)
{
  usMIDI("Emulator reset not implemented yet!\n");
  return;
}

inline void handle_emulator_cc(void)
{
  if (midimachine.streambuffer[1] == midi_ccvalues_defaults.CC_CEN) { /* control emulator enable 0x55 (85) */
    if (!emulator_running) {
      emulator_enable();
    }
  } else
  if (midimachine.streambuffer[1] == midi_ccvalues_defaults.CC_CDI) { /* control emulator disable 0x56 (86) */
    if (emulator_running) {
      emulator_disable();
    }
  } else
  if (midimachine.streambuffer[1] == midi_ccvalues_defaults.CC_CRE) { /* control emulator reset 0x57 (87) */
    if (emulator_running) {
      emulator_reset();
    }
  }
  return;
}
#endif

/* Processes a 1 byte incoming midi buffer
 * Figures out if we're receiving midi or sysex */
static inline void midi_buffer_task(uint8_t buffer)
{
  if (midimachine.index != 0) {
    if (midimachine.type != SYSEX) usMCMD(" [B%d]$%02x#%03d", midimachine.index, buffer, buffer);
  }

  if (buffer & 0x80) { /* Handle start byte */
    switch (buffer) {
      /* System Exclusive */
      case 0xF0:  /* System Exclusive Start */
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          dtype = sysex; /* Set data type to SysEx */
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
          dtype = sysex; /* Set data type to ASID */
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
        midimachine.midi_bytes = 2;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          if (midimachine.index == 0) usMCMD("[M][B%d]$%02x#%03d", midimachine.index, buffer, buffer);
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
        midimachine.midi_bytes = 3;
        if (midimachine.bus != CLAIMED && midimachine.type == NONE) {
          if (midimachine.index == 0) usMCMD("[M][B%d]$%02x#%03d", midimachine.index, buffer, buffer);
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
            if (midimachine.index == midimachine.midi_bytes) {
              usMCMD("\n");
              dtype = midi; /* Set data type to midi */

              /* Do something fancy now */
              #ifdef ONBOARD_EMULATOR
              if (((midimachine.streambuffer[0] & 0xF0) == 0xB0) /* Control mode change */
                && (midimachine.streambuffer[1] >= midi_ccvalues_defaults.CC_CEN)
                && (midimachine.streambuffer[1] <= midi_ccvalues_defaults.CC_CRE)) {
                  handle_emulator_cc();
              } else
              if (emulator_running) { /* Cynthcart, yeah baby yeah! */
                handle_emulater_data();
              }
              #else
              process_midi(midimachine.streambuffer, midimachine.index);
              #endif

              midimachine.index = 0;
              midimachine.state = IDLE;
              midimachine.bus = FREE;
              midimachine.type = NONE;
            }
        }
      } else {
        /* Buffer is full, receiving to much data too handle, wait for message to end */
        midimachine.state = WAITING_FOR_END;
        usMCMD("[EXCESS][IDX]%02d %02x \n", midimachine.index, buffer);
      }
    } else if (midimachine.state == WAITING_FOR_END) {
      /* Consuming SysEx messages, nothing else to do */
      usMCMD("[EXCESS][IDX]%02d %02x \n", midimachine.index, buffer);
    }
  }
}

void process_stream(uint8_t *buffer, size_t size)
{ /* ISSUE: Processing the stream byte by byte makes it more prone to latency */
  size_t n = 0;
  while (1) {
    midi_buffer_task(buffer[n++]);
    if (n == size) return;
  }
}
