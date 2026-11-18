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

#include <midi.h>
#include <globals.h>
#include <config.h>
#include <sid.h>
#include <logging.h>
#include <midi_handler.h>
#include <midi_defs.h>
#include <midi_queue.h>
#include <sysex.h>

#if defined(ONBOARD_EMULATOR)
#include <usbsid.h> /* emulator variables */
#include <sid_player.h> /* emulator variables */
#include <c64app_embedded.h> /* Cynthcart, SID SYNTH ~ USBSID-Player */
/* Read by the emulated MC68B50 ACIA on core1 (player-repo src/cart/MC68B50.cpp) */
queue_t c64midi_queue;
#define C64MIDI_QUEUE_ENTRIES 512 /* A SID SYNTH Editor patch burst is ~200 bytes */
#endif /* ONBOARD_EMULATOR */


/* MIDI state machine (declared extern in midi.h) */
midi_machine midimachine;

/* Compile-time defaults, the CC map's starting point at boot until
 * midi_config_load() (midi_config.c) overwrites it from flash, if valid. */
const midi_ccvalues midi_ccvalues_defaults = MIDI_DEFAULT_CCVALUES_INIT;

/* USB-MIDI 1.0 Code Index Number (low nibble of byte 0 in a 4-byte event
 * packet). High nibble is cable number. Only one embedded cable exists
 * today (TUD_MIDI_DESCRIPTOR, usb_descriptors.c). It's always 0. */
typedef enum {
  CIN_MISC          = 0x0, /* reserved, unused */
  CIN_CABLE_EVENT   = 0x1, /* reserved, unused */
  CIN_SYSCOM_2BYTE  = 0x2, /* 2 byte system common, e.g. Song Select */
  CIN_SYSCOM_3BYTE  = 0x3, /* 3 byte system common, e.g. Song Position */
  CIN_SYSEX_START   = 0x4, /* SysEx starts or continues, 3 data bytes */
  CIN_SYSEX_END1    = 0x5, /* SysEx ends with 1 data byte */
  CIN_SYSEX_END2    = 0x6, /* SysEx ends with 2 data bytes */
  CIN_SYSEX_END3    = 0x7, /* SysEx ends with 3 data bytes */
  CIN_NOTE_OFF      = 0x8,
  CIN_NOTE_ON       = 0x9,
  CIN_POLY_AT       = 0xA,
  CIN_CC            = 0xB,
  CIN_PROG_CHG      = 0xC,
  CIN_CHAN_AT       = 0xD,
  CIN_PITCH_BEND    = 0xE,
  CIN_1BYTE_DATA    = 0xF, /* single byte, mostly System Real-Time */
} usb_midi_cin;

/* MIDI clock: 24 PPQN, BPM estimated from an EWMA of pulse intervals,
 * published via midi_clock_bpm_x100()/midi_clock_present() (midi.h) for
 * the core1 engine (arp/LFO sync). Runs on core0. Engine only reads, never
 * writes: no lock needed. */
static volatile uint32_t clock_pulse_count   = 0;      /* 0..23 within the current quarter note */
static volatile uint32_t clock_total_pulses  = 0;      /* monotonic, for clock-synced division counting */
static volatile uint64_t clock_last_pulse_us = 0;
static volatile uint32_t clock_bpm_x100      = 12000;  /* 120.00 BPM, fixed point *100, until pulses say otherwise */
static volatile bool     clock_running       = false;

/* A pulse more than half a second old is treated as "no clock". The
 * engine can fall back to its own free-running rate instead of a rate
 * estimate frozen from whenever a DAW last sent clock. */
#define MIDI_CLOCK_PRESENT_TIMEOUT_US 500000ull


/**
 * @brief Initialise the MIDI state machine and start the buffer processor
 *
 * Runs on core0 before core1 is released: nothing races the queue init.
 */
void midi_init(void)
{
  usNFO("\n");
  usMIDI("Init\n");

  /* Set initial stream state and index */
  midimachine.bus = FREE;
  midimachine.state = IDLE;
  midimachine.index = 0;
  midimachine.midi_bytes = 3;
  midimachine.last_status = 0;

  /* Clear stream buffers once */
  memset(midimachine.streambuffer, 0, sizeof midimachine.streambuffer);

  /* Runs on core0 before core1 is released to its main loop: there is no
   * consumer racing this yet */
  midi_queue_init();

  #if defined(ONBOARD_EMULATOR)
  /* Allocated once, never freed: the ACIA on core1 may still poll it while core0 stops the emulator */
  queue_init(&c64midi_queue, sizeof(c64midi_queue_entry_t), C64MIDI_QUEUE_ENTRIES);
  #endif /* ONBOARD_EMULATOR */

  /* Start the processor of midi buffers */
  midi_processor_init();

  return;
}

#if defined(ONBOARD_EMULATOR)
/**
 * @brief Drop every byte still waiting for the C64 MIDI app
 */
static void emulator_queue_drain(void)
{
  c64midi_queue_entry_t entry;
  while (queue_try_remove(&c64midi_queue, &entry)) {}
}

/**
 * @brief Forward the current MIDI stream buffer to the C64 MIDI app queue
 *
 * Pushes every byte of `midimachine.streambuffer` (up to `midimachine.index`)
 * onto c64midi_queue, blocking if the queue is full.
 */
static inline void handle_emulater_data(void)
{
  for (size_t e = 0; e < midimachine.index; e++) {
    c64midi_queue_entry_t cq_entry;
    cq_entry.data = midimachine.streambuffer[e];
    queue_add_blocking(&c64midi_queue, &cq_entry);
  }
  return;
}

/**
 * @brief Request emulator startup, core1 starts Midi.emulator_app on its next loop
 */
static inline void emulator_enable(void)
{
  usMIDI("[EMULATOR] Starting '%s'\n", embedded_c64app_str(usbsid_config.Midi.emulator_app));
  emulator_queue_drain();
  emulator_running = false;
  offload_ledrunner = true;
  starting_emulator = true;
  return;
}

/**
 * @brief Stop the C64 MIDI app and reset the SID
 */
static void emulator_disable(void)
{
  usMIDI("[EMULATOR] Stopping '%s'\n", embedded_c64app_str(usbsid_config.Midi.emulator_app));
  emulator_running = false;
  stopping_emulator = true;
  stop_c64app();
  offload_ledrunner = false;
  emulator_queue_drain();
  reset_sid_registers();
  return;
}

/**
 * @brief Restart the C64 MIDI app, a fresh boot of Midi.emulator_app
 */
static inline void emulator_reset(void)
{
  usMIDI("[EMULATOR] Restarting '%s'\n", embedded_c64app_str(usbsid_config.Midi.emulator_app));
  emulator_disable();
  emulator_enable();
  return;
}

/**
 * @brief Run an emulator control command from SysEx SYSEX_EMULATOR
 *
 * @param uint8_t op EMULATOR_STOP, EMULATOR_START or EMULATOR_RESTART
 * @param int app c64app_t to select first (RAM only, SAVE_CONFIG persists it), < 0 keeps the current one
 */
void emulator_command(uint8_t op, int app)
{
  if (app >= 0) {
    if (app < C64APP_COUNT) {
      usbsid_config.Midi.emulator_app = (uint8_t)app;
    } else {
      usERR("[EMULATOR] app %d out of range, max %d\n", app, C64APP_COUNT - 1);
      return;
    }
  }
  usMIDI("[EMULATOR] op %u app %u running %d\n", op, usbsid_config.Midi.emulator_app, (int)emulator_running);
  switch (op) {
    case EMULATOR_STOP:
      if (emulator_running || starting_emulator) emulator_disable();
      starting_emulator = false;
      break;
    case EMULATOR_START:
      if (!emulator_running) emulator_enable();
      break;
    case EMULATOR_RESTART:
      emulator_reset();
      break;
    default:
      break;
  }
  return;
}

/**
 * @brief Dispatch a Control Change message to the emulator enable/disable/reset handlers
 *
 * Compares the CC number in `midimachine.streambuffer[1]` against the
 * CC_CEN/CC_CDI/CC_CRE control values and calls the matching
 * emulator_enable()/emulator_disable()/emulator_reset() handler.
 */
static const void handle_emulator_cc(void)
{
  if (midimachine.streambuffer[1] == midi_ccvalues_defaults.CC_CEN) { /* control emulator enable 0x75 (117) */
    if (!emulator_running) {
      emulator_enable();
    }
  } else
  if (midimachine.streambuffer[1] == midi_ccvalues_defaults.CC_CDI) { /* control emulator disable 0x76 (118) */
    if (emulator_running) {
      emulator_disable();
    }
  } else
  if (midimachine.streambuffer[1] == midi_ccvalues_defaults.CC_CRE) { /* control emulator reset 0x77 (119) */
    if (emulator_running) {
      emulator_reset();
    }
  }
  return;
}
#endif /* ONBOARD_EMULATOR */

/**
 * @brief Process one MIDI clock pulse and update the smoothed BPM estimate
 *
 * Called on each 0xF8 System Real-Time Timing Clock byte (24 pulses per
 * quarter note). Computes the interval since the previous pulse and, if it
 * falls within a plausible tempo range (roughly 1..1000 BPM), folds it into
 * `clock_bpm_x100` via an EWMA (alpha = 1/8) that smooths jitter without
 * lagging tempo changes for more than a beat, roughly. Advances the 0..23
 * pulse counter and the monotonic total pulse count.
 */
static void handle_midi_clock(void)
{
  uint64_t now = time_us_64();
  uint64_t prev = clock_last_pulse_us;
  clock_last_pulse_us = now;

  if (prev != 0) {
    uint64_t interval = now - prev;
    /* Ignore implausible intervals (e.g. the gap across a Stop/Start, or
     * the very first pulse after boot) rather than let one bad sample
     * swing the estimate: 24 pulses/quarter note bounds any sane tempo
     * (roughly 1..1000 BPM) to a 2.5ms..2.5s interval. */
    if (interval >= 2500 && interval <= 2500000) {
      /* quarter_us = interval * 24;
       * bpm_x100   = 6,000,000,000 / quarter_us
       *            = 250,000,000 / interval */
      uint32_t inst_bpm_x100 = (uint32_t)(250000000ull / interval);
      /* EWMA, alpha = 1/8: smooths jitter without lagging tempo changes
       * for more than a beat, roughly */
      clock_bpm_x100 = (uint32_t)(((uint64_t)clock_bpm_x100 * 7 + inst_bpm_x100) / 8);
    }
  }

  clock_pulse_count++;
  if (clock_pulse_count >= 24) clock_pulse_count = 0;
  clock_total_pulses++;
  return;
}

/**
 * @brief Handle a MIDI Start (0xFA) message
 *
 * Resets the pulse phase and clears the last pulse timestamp. The following
 * clock pulse does not compute an interval against a stale value, then
 * marks the clock as running.
 */
static void handle_midi_start(void)
{
  clock_pulse_count = 0;
  clock_last_pulse_us = 0;  /* following pulse must not compute an interval against a stale timestamp */
  clock_running = true;
  return;
}

/**
 * @brief Handle a MIDI Continue (0xFB) message
 *
 * Resumes the clock without resetting the pulse phase, unlike Start. Clears
 * the last pulse timestamp. The following pulse's interval is not computed
 * against a stale value.
 */
static void handle_midi_continue(void)
{
  /* Resume without resetting phase, unlike Start */
  clock_last_pulse_us = 0;
  clock_running = true;
  return;
}

/**
 * @brief Handle a MIDI Stop (0xFC) message
 *
 * Marks the clock as not running.
 */
static void handle_midi_stop(void)
{
  clock_running = false;
  return;
}

/**
 * @brief Get the current smoothed MIDI clock tempo
 *
 * @return uint32_t tempo in BPM, fixed point times 100
 */
uint32_t midi_clock_bpm_x100(void)
{
  return clock_bpm_x100;
}

/**
 * @brief Check whether a MIDI clock Start/Continue has been received without a following Stop
 *
 * @return bool true when the clock is running
 */
bool midi_clock_running(void)
{
  return clock_running;
}

/**
 * @brief Check whether MIDI clock pulses are currently arriving
 *
 * The clock is considered present when running and the last pulse was
 * received less than MIDI_CLOCK_PRESENT_TIMEOUT_US ago, letting a caller
 * fall back to a free-running rate once a DAW stops sending clock.
 *
 * @return bool true when clock pulses are present
 */
bool midi_clock_present(void)
{
  if (!clock_running || clock_last_pulse_us == 0) return false;
  return (time_us_64() - clock_last_pulse_us) < MIDI_CLOCK_PRESENT_TIMEOUT_US;
}

/**
 * @brief Get the current pulse position within the quarter note
 *
 * @return uint32_t pulse count, 0..23 (24 PPQN)
 */
uint32_t midi_clock_pulse_count(void)
{
  return clock_pulse_count;
}

/**
 * @brief Get the monotonic MIDI clock pulse count
 *
 * @return uint32_t total pulses received since boot, for clock-synced division counting
 */
uint32_t midi_clock_total_pulses(void)
{
  return clock_total_pulses;
}

/**
 * @brief Hand a complete channel voice message off to the emulator
 *        interception, or enqueue it for the core1 MIDI engine
 *
 * Shared by the legacy byte state machine (midi_buffer_task()) and the USB
 * packet fast path (process_usb_midi_packet()). Both callers must already
 * have the message in midimachine.streambuffer with midimachine.index set
 * to its length.
 * Emulator interception stays on core0: CC 117-119 enable/disable/reset the
 * embedded C64 MIDI app (Cynthcart or SID SYNTH) and a running app gets every
 * other message. A regular MIDI message is queued for midi_engine_task() on
 * core1, gated by usbsid_config.Midi.enabled.
 * A disabled MIDI path never fills a queue nothing drains.
 */
static inline void dispatch_complete_message(void)
{
  usMCMD("\n");
  dtype = midi; /* Set data type to midi */

  #if defined(ONBOARD_EMULATOR)
  if (((midimachine.streambuffer[0] & 0xF0) == 0xB0) /* Control mode change */
    && (midimachine.streambuffer[1] >= midi_ccvalues_defaults.CC_CEN)
    && (midimachine.streambuffer[1] <= midi_ccvalues_defaults.CC_CRE)) {
      handle_emulator_cc();
  } else
  if (emulator_running) { /* Cynthcart or SID SYNTH, yeah baby yeah! */
    handle_emulater_data();
  } else {
  #endif
    if (usbsid_config.Midi.enabled) {
      midi_queue_push(midimachine.streambuffer, (uint8_t)midimachine.index);
    }
  #if defined(ONBOARD_EMULATOR)
  }
  #endif

  midimachine.index = 0;
  midimachine.state = IDLE;
  midimachine.bus = FREE;
  midimachine.type = NONE;
  return;
}

/**
 * @brief Feed one incoming byte through the legacy MIDI/SysEx byte state machine
 *
 * Classifies the stream as Real-Time, SysEx, or a channel voice message,
 * accumulates into midimachine.streambuffer, and dispatches once complete.
 * Also handles running status by re-entering with the cached last_status.
 *
 * @param uint8_t buffer, one incoming MIDI byte
 */
static inline void midi_buffer_task(uint8_t buffer)
{
  if (midimachine.index != 0) {
    if (midimachine.type != SYSEX) usMCMD(" [B%d]$%02x#%03d", midimachine.index, buffer, buffer);
  }

  /* Real-Time messages: single byte, never touch running stream state */
  if __us_unlikely(buffer >= 0xF8) {
    usMCMD("[RT] %02x\n",buffer);
    dtype = sysex; /* Set data type to SysEx */
    midimachine.last_status = 0;  /* SysEx cancels running status */
    switch (buffer) {
      case 0xF8: handle_midi_clock();    break; /* System Exclusive Timing clock */
      case 0xF9: break;                         /* System Exclusive Undefined (Reserved) */
      case 0xFA: handle_midi_start();    break; /* System Exclusive Start */
      case 0xFB: handle_midi_continue(); break; /* System Exclusive Continue */
      case 0xFC: handle_midi_stop();     break; /* System Exclusive Stop */
      case 0xFD:                                /* System Exclusive Undefined (Reserved) */
      case 0xFE: break;                         /* System Exclusive Active Sensing - reset watchdog if implemented */
      case 0xFF: midi_processor_init();  break; /* System Exclusive System Reset */
      default:   break;
    }
    return; /* return and do not fall into state machine below */
  }

  if (buffer & 0x80) { /* Handle start byte */
    switch (buffer) {
      /* System Exclusive */
      case 0xF0:  /* System Exclusive Start */
        midimachine.last_status = 0;  /* SysEx cancels running status */
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
        midimachine.last_status = 0;  /* SysEx cancels running status */
        if (midimachine.bus == CLAIMED && midimachine.type == SYSEX) {
          dtype = sysex; /* Set data type to SysEx */
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
        midimachine.last_status = 0;  /* SysEx cancels running status */
        dtype = sysex; /* Set data type to SysEx */
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
        midimachine.last_status = buffer;  /* Add cache for running status */
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
              dispatch_complete_message();
            }
        }
      } else {
        /* Buffer is full, receiving to much data too handle, wait for message to end */
        midimachine.state = WAITING_FOR_END;
        usMCMD("[EXCESS][IDX]%02d %02x \n", midimachine.index, buffer);
      }
    } else if (midimachine.state == IDLE && midimachine.last_status != 0) {
      /* Running status: re-enter as if last_status arrived fresh */
      midi_buffer_task(midimachine.last_status);  /* synthetic status byte */
      midi_buffer_task(buffer);                   /* then this data byte */
      return;
    } else if (midimachine.state == WAITING_FOR_END) {
      /* Consuming SysEx messages, nothing else to do */
      usMCMD("[EXCESS][IDX]%02d %02x \n", midimachine.index, buffer);
    }
  }
}

/**
 * @brief Load a channel voice message straight into the state machine's
 *        buffer and dispatch it, bypassing the byte-by-byte accumulation
 *
 * @param const uint8_t *buf, message bytes (status first), from the packet
 * @param uint8_t n, message length, 2 or 3
 */
static inline void dispatch_packet_message(const uint8_t *buf, uint8_t n)
{
  memcpy(midimachine.streambuffer, buf, n);
  midimachine.index = n;
  midimachine.last_status = buf[0]; /* Add cache for running status */
  dispatch_complete_message();
  return;
}

/**
 * @brief Entry point for the USB MIDI packet API
 *
 * Reads full 4-byte USB-MIDI event packets: a channel voice message
 * arrives already framed with no byte-wise state machine needed. SysEx and
 * single-byte System Real-Time messages still go through midi_buffer_task().
 *
 * @param uint8_t pkt[4], one USB-MIDI event packet
 */
void process_usb_midi_packet(uint8_t pkt[4])
{
  uint8_t cin = (pkt[0] & 0x0F);

  switch (cin) {
    case CIN_NOTE_OFF:
    case CIN_NOTE_ON:
    case CIN_POLY_AT:
    case CIN_CC:
    case CIN_PITCH_BEND:
      dispatch_packet_message(&pkt[1], 3);
      break;
    case CIN_PROG_CHG:
    case CIN_CHAN_AT:
      dispatch_packet_message(&pkt[1], 2);
      break;
    case CIN_1BYTE_DATA:
      /* Realtime (>= 0xF8) and other single-byte system messages both
       * arrive this way. The midi_buffer_task() already dispatches both */
      midi_buffer_task(pkt[1]);
      break;
    case CIN_SYSEX_START:
      process_stream(&pkt[1], 3);
      break;
    case CIN_SYSEX_END1:
      process_stream(&pkt[1], 1);
      break;
    case CIN_SYSEX_END2:
      process_stream(&pkt[1], 2);
      break;
    case CIN_SYSEX_END3:
      process_stream(&pkt[1], 3);
      break;
    case CIN_SYSCOM_2BYTE:
      process_stream(&pkt[1], 2);
      break;
    case CIN_SYSCOM_3BYTE:
      process_stream(&pkt[1], 3);
      break;
    case CIN_MISC:
    case CIN_CABLE_EVENT:
    default:
      /* Reserved and unused per USB-MIDI 1.0. TinyUSB's own stream reader
       * skips these the same way */
      break;
  }
  return;
}

/**
 * @brief Feed a buffer of MIDI bytes through midi_buffer_task() one byte at a time
 *
 * @note Processing byte by byte makes this more prone to latency than a
 *       block-based approach
 *
 * @param uint8_t *buffer, bytes to process
 * @param size_t size, number of bytes in buffer
 */
void process_stream(uint8_t *buffer, size_t size)
{ /* ISSUE: Processing the stream byte by byte makes it more prone to latency */
#if 0
  usMCMD("[S] ");
  for (size_t i = 0; i < size; i++) { usMCMD("%02x ", buffer[i]); }
  usMCMD("\n");
#endif
  size_t n = 0;
  while (1) {
    midi_buffer_task(buffer[n++]);
    if (n == size) return;
  }
}
