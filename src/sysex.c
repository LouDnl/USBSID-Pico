/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sysex.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Huge thanks to Thomas Jansson for all his ASID improvement work (https://github.com/thomasj)
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

 #include <stdbool.h>

 #include <config.h>
 #include <globals.h>
 #include <sid.h>
 #include <vu.h>
 #include <asid.h>
 #include <sysex.h>
 #include <logging.h>
 #include <midi_config.h>
 #include <midi_patch.h>
 #include <midi_fmopl.h>


/* Custom commands */
enum {
  SYSEX_TOGGLE_AUDIO = 0x01,
  /* Phase 5: trigger-only, no payload - the MIDI blob save/load/reset
   * itself (magic/version/crc32/sequence, slot scan, sanitising transient
   * state) lives entirely in midi_config.c; this just calls it. Reachable
   * with a plain `amidi -S "f0 50 10 f7"` etc, so testing flash
   * persistence needs nothing beyond what midi_test.sh already uses -
   * unlike LOAD_MIDI_STATE/SAVE_MIDI_STATE/RESET_MIDI_STATE (config.c),
   * which are the raw USB config protocol, a different transport. */
  SYSEX_MIDI_SAVE  = 0x10,
  SYSEX_MIDI_LOAD  = 0x11,
  SYSEX_MIDI_RESET = 0x12,
  /* Per-patch authoring, without needing any host tool or a firmware
   * rebuild: LOAD writes one patch's data into midi_patches[] (RAM only -
   * SYSEX_MIDI_SAVE persists it, same as any other live edit). DUMP asks
   * the device to send one patch's current data back out over MIDI OUT
   * (endpoint 0x83, declared in usb_descriptors.c and otherwise still
   * unused) as a DATA message, so the same tool that authors a patch can
   * also read one back to inspect or copy it. Neither touches the bus -
   * a stored patch only reaches the chip via Program Change. */
  SYSEX_MIDI_PATCH_LOAD = 0x20,
  SYSEX_MIDI_PATCH_DUMP = 0x21,  /* request: device -> host */
  SYSEX_MIDI_PATCH_DATA = 0x22,  /* response: device -> host, and what LOAD expects as input */
  /* TODO 15: the same LOAD/DUMP/DATA shape as the SID patch commands just
   * above, aimed at fmopl_patches[]/opl_instrument_t (midi_fmopl.h) instead
   * of midi_patches[]/midi_patch_t - a different, much smaller struct, so
   * its own commands and its own nibble count rather than trying to
   * shoehorn it into 0x20-0x22's fixed 36-nibble frame. */
  SYSEX_FMOPL_PATCH_LOAD = 0x23,
  SYSEX_FMOPL_PATCH_DUMP = 0x24,  /* request: device -> host */
  SYSEX_FMOPL_PATCH_DATA = 0x25,  /* response: device -> host, and what LOAD expects as input */
  /* Debugging/tuning tool, not a real config setting yet - see
   * midi_fmopl_set_clock()'s own doc comment (midi_fmopl.h) for why this
   * exists at all: the compiled-in OPL2 clock constant is verified correct
   * against both the spec and SIDKick-pico's own source, but still measured
   * wrong on at least one real board. Payload: 3 bytes (24 bits, enough for
   * any real OPL clock) packed as 6 nibbles, big-endian; all-zero resets to
   * the compiled-in default. */
  SYSEX_FMOPL_SET_CLOCK = 0x26,
};

/* Every midi_patch_t field sent as two 7-bit-safe nibble bytes (0-15 each),
 * filter_cutoff (the one 16 bit field) as four - simple and obviously
 * correct over squeezing 8 bits into 7, which a mis-implemented packer
 * could get wrong in ways that are hard to notice from raw hex. 17 fields,
 * 16 of them 1 byte (32 nibbles) plus filter_cutoff (4 nibbles) = 36
 * nibble bytes per patch, comfortably inside the 64 byte SysEx receive
 * buffer (midimachine.streambuffer, midi.c) alongside the framing around
 * it. */
#define PATCH_SYSEX_NIBBLES 36

/**
 * @brief Split one byte into two 7-bit-safe nibble bytes, high nibble first
 *
 * @param uint8_t v
 * @param uint8_t * out
 */
static void encode_byte(uint8_t v, uint8_t *out)
{
  out[0] = (uint8_t)((v >> 4) & 0x0F);
  out[1] = (uint8_t)(v & 0x0F);
  return;
}
/**
 * @brief Recombine two nibble bytes produced by encode_byte() into one byte
 *
 * @param const uint8_t * in
 * @return uint8_t decoded byte
 */
static uint8_t decode_byte(const uint8_t *in)
{
  return (uint8_t)(((in[0] & 0x0F) << 4) | (in[1] & 0x0F));
}
/**
 * @brief Split a 16 bit value into four 7-bit-safe nibble bytes, MSB first
 *
 * @param uint16_t v
 * @param uint8_t * out
 */
static void encode_u16(uint16_t v, uint8_t *out)
{
  out[0] = (uint8_t)((v >> 12) & 0x0F);
  out[1] = (uint8_t)((v >> 8)  & 0x0F);
  out[2] = (uint8_t)((v >> 4)  & 0x0F);
  out[3] = (uint8_t)(v & 0x0F);
  return;
}
/**
 * @brief Recombine four nibble bytes produced by encode_u16() into one uint16_t
 *
 * @param const uint8_t * in
 * @return uint16_t decoded value
 */
static uint16_t decode_u16(const uint8_t *in)
{
  return (uint16_t)(((in[0] & 0x0F) << 12) | ((in[1] & 0x0F) << 8) | ((in[2] & 0x0F) << 4) | (in[3] & 0x0F));
}

/**
 * @brief Pack a patch into PATCH_SYSEX_NIBBLES nibble bytes, field order
 *        matching midi_patch_t exactly
 */
static void pack_patch(const midi_patch_t *p, uint8_t *out)
{
  encode_byte(p->tmpl_contr, out); out += 2;
  encode_byte(p->tmpl_attdec, out); out += 2;
  encode_byte(p->tmpl_susrel, out); out += 2;
  encode_byte(p->tmpl_pwmlo, out); out += 2;
  encode_byte(p->tmpl_pwmhi, out); out += 2;
  encode_u16(p->filter_cutoff, out); out += 4;
  encode_byte(p->resonance, out); out += 2;
  encode_byte(p->filter_routing, out); out += 2;
  encode_byte(p->filter_mode, out); out += 2;
  encode_byte(p->lfo_wave, out); out += 2;
  encode_byte(p->lfo_rate, out); out += 2;
  encode_byte(p->lfo_depth, out); out += 2;
  encode_byte(p->lfo_dest, out); out += 2;
  encode_byte(p->arp_mode, out); out += 2;
  encode_byte(p->arp_rate, out); out += 2;
  encode_byte(p->arp_octaves, out); out += 2;
  encode_byte(p->bend_range, out);
  return;
}

/**
 * @brief The inverse of pack_patch()
 */
static void unpack_patch(const uint8_t *in, midi_patch_t *p)
{
  p->tmpl_contr = decode_byte(in); in += 2;
  p->tmpl_attdec = decode_byte(in); in += 2;
  p->tmpl_susrel = decode_byte(in); in += 2;
  p->tmpl_pwmlo = decode_byte(in); in += 2;
  p->tmpl_pwmhi = decode_byte(in); in += 2;
  p->filter_cutoff = decode_u16(in); in += 4;
  p->resonance = decode_byte(in); in += 2;
  p->filter_routing = decode_byte(in); in += 2;
  p->filter_mode = decode_byte(in); in += 2;
  p->lfo_wave = decode_byte(in); in += 2;
  p->lfo_rate = decode_byte(in); in += 2;
  p->lfo_depth = decode_byte(in); in += 2;
  p->lfo_dest = decode_byte(in); in += 2;
  p->arp_mode = decode_byte(in); in += 2;
  p->arp_rate = decode_byte(in); in += 2;
  p->arp_octaves = decode_byte(in); in += 2;
  p->bend_range = decode_byte(in);
  return;
}

/* buffer[3] = patch index, buffer[4..4+35] = 36 nibble bytes, matching
 * SYSEX_MIDI_PATCH_LOAD's and SYSEX_MIDI_PATCH_DATA's own layout */
#define PATCH_SYSEX_MIN_SIZE (4 + PATCH_SYSEX_NIBBLES + 1)  /* + F0,50,cmd already counted by buffer[0..2]; +1 for F7 */

/**
 * @brief Handle SYSEX_MIDI_PATCH_LOAD: write one SID patch into RAM
 *
 * Validates the message length and patch index, then unpacks the nibble
 * payload at buffer[4..] into midi_patches[patch_index]. RAM only; does
 * not persist to flash (SYSEX_MIDI_SAVE does that).
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_patch_load(uint8_t *buffer, int size)
{
  if (size < PATCH_SYSEX_MIN_SIZE) {
    usERR("[SYSEX] SYSEX_MIDI_PATCH_LOAD: message too short (%d < %d)\n", size, PATCH_SYSEX_MIN_SIZE);
    return;
  }
  uint8_t patch_index = buffer[3];
  if (patch_index >= MIDI_PATCH_COUNT) {
    usERR("[SYSEX] SYSEX_MIDI_PATCH_LOAD: patch %d out of range (max %d)\n", patch_index, MIDI_PATCH_COUNT - 1);
    return;
  }
  unpack_patch(&buffer[4], &midi_patches[patch_index]);
  usCFG("[SYSEX] SYSEX_MIDI_PATCH_LOAD: patch %d updated in RAM (SYSEX_MIDI_SAVE to persist it)\n", patch_index);
  return;
}

/**
 * @brief Handle SYSEX_MIDI_PATCH_DUMP: send one SID patch back over MIDI OUT
 *
 * Validates the patch index, packs midi_patches[patch_index] into a
 * SYSEX_MIDI_PATCH_DATA SysEx message, and writes it out on MIDI_CABLE.
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_patch_dump(uint8_t *buffer, int size)
{
  if (size < 4) {
    usERR("[SYSEX] SYSEX_MIDI_PATCH_DUMP: message too short\n");
    return;
  }
  uint8_t patch_index = buffer[3];
  if (patch_index >= MIDI_PATCH_COUNT) {
    usERR("[SYSEX] SYSEX_MIDI_PATCH_DUMP: patch %d out of range (max %d)\n", patch_index, MIDI_PATCH_COUNT - 1);
    return;
  }
  uint8_t out[4 + PATCH_SYSEX_NIBBLES + 1];
  out[0] = 0xF0;
  out[1] = 0x50;
  out[2] = SYSEX_MIDI_PATCH_DATA;
  out[3] = patch_index;
  pack_patch(&midi_patches[patch_index], &out[4]);
  out[4 + PATCH_SYSEX_NIBBLES] = 0xF7;
  tud_midi_stream_write(MIDI_CABLE, out, sizeof(out));
  usCFG("[SYSEX] SYSEX_MIDI_PATCH_DUMP: sent patch %d\n", patch_index);
  return;
}


/* --- TODO 15: FMOpl patch authoring, same shape as the SID patch commands
 * above, aimed at fmopl_patches[]/opl_instrument_t instead. opl_instrument_t
 * has 11 raw bytes (op_mult[2], op_ksl_tl[2], op_ar_dr[2], op_sl_rr[2],
 * op_wave[2], feedback_algo), each sent as two nibble bytes same as
 * pack_patch()/unpack_patch() do - 22 nibbles per patch, reusing this
 * file's own encode_byte()/decode_byte(). */
#define FMOPL_SYSEX_NIBBLES 22

/**
 * @brief Pack an opl_instrument_t into FMOPL_SYSEX_NIBBLES nibble bytes
 *
 * @param const opl_instrument_t * p
 * @param uint8_t * out
 */
static void pack_opl_instrument(const opl_instrument_t *p, uint8_t *out)
{
  encode_byte(p->op_mult[0], out);    out += 2;
  encode_byte(p->op_mult[1], out);    out += 2;
  encode_byte(p->op_ksl_tl[0], out);  out += 2;
  encode_byte(p->op_ksl_tl[1], out);  out += 2;
  encode_byte(p->op_ar_dr[0], out);   out += 2;
  encode_byte(p->op_ar_dr[1], out);   out += 2;
  encode_byte(p->op_sl_rr[0], out);   out += 2;
  encode_byte(p->op_sl_rr[1], out);   out += 2;
  encode_byte(p->op_wave[0], out);    out += 2;
  encode_byte(p->op_wave[1], out);    out += 2;
  encode_byte(p->feedback_algo, out);
  return;
}

/**
 * @brief The inverse of pack_opl_instrument()
 *
 * @param const uint8_t * in
 * @param opl_instrument_t * p
 */
static void unpack_opl_instrument(const uint8_t *in, opl_instrument_t *p)
{
  p->op_mult[0]   = decode_byte(in); in += 2;
  p->op_mult[1]   = decode_byte(in); in += 2;
  p->op_ksl_tl[0] = decode_byte(in); in += 2;
  p->op_ksl_tl[1] = decode_byte(in); in += 2;
  p->op_ar_dr[0]  = decode_byte(in); in += 2;
  p->op_ar_dr[1]  = decode_byte(in); in += 2;
  p->op_sl_rr[0]  = decode_byte(in); in += 2;
  p->op_sl_rr[1]  = decode_byte(in); in += 2;
  p->op_wave[0]   = decode_byte(in); in += 2;
  p->op_wave[1]   = decode_byte(in); in += 2;
  p->feedback_algo = decode_byte(in);
  return;
}

#define FMOPL_PATCH_SYSEX_MIN_SIZE (4 + FMOPL_SYSEX_NIBBLES + 1)

/**
 * @brief Handle SYSEX_FMOPL_PATCH_LOAD: write one FMOpl patch into RAM
 *
 * Validates the message length and patch index, then unpacks the nibble
 * payload at buffer[4..] into fmopl_patches[patch_index].
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_fmopl_patch_load(uint8_t *buffer, int size)
{
  if (size < FMOPL_PATCH_SYSEX_MIN_SIZE) {
    usERR("[SYSEX] SYSEX_FMOPL_PATCH_LOAD: message too short (%d < %d)\n", size, FMOPL_PATCH_SYSEX_MIN_SIZE);
    return;
  }
  uint8_t patch_index = buffer[3];
  if (patch_index >= MIDI_FMOPL_PATCH_COUNT) {
    usERR("[SYSEX] SYSEX_FMOPL_PATCH_LOAD: patch %d out of range (max %d)\n", patch_index, MIDI_FMOPL_PATCH_COUNT - 1);
    return;
  }
  unpack_opl_instrument(&buffer[4], &fmopl_patches[patch_index]);
  usCFG("[SYSEX] SYSEX_FMOPL_PATCH_LOAD: patch %d updated in RAM\n", patch_index);
  return;
}

/**
 * @brief Handle SYSEX_FMOPL_PATCH_DUMP: send one FMOpl patch back over MIDI OUT
 *
 * Validates the patch index, packs fmopl_patches[patch_index] into a
 * SYSEX_FMOPL_PATCH_DATA SysEx message, and writes it out on MIDI_CABLE.
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_fmopl_patch_dump(uint8_t *buffer, int size)
{
  if (size < 4) {
    usERR("[SYSEX] SYSEX_FMOPL_PATCH_DUMP: message too short\n");
    return;
  }
  uint8_t patch_index = buffer[3];
  if (patch_index >= MIDI_FMOPL_PATCH_COUNT) {
    usERR("[SYSEX] SYSEX_FMOPL_PATCH_DUMP: patch %d out of range (max %d)\n", patch_index, MIDI_FMOPL_PATCH_COUNT - 1);
    return;
  }
  uint8_t out[4 + FMOPL_SYSEX_NIBBLES + 1];
  out[0] = 0xF0;
  out[1] = 0x50;
  out[2] = SYSEX_FMOPL_PATCH_DATA;
  out[3] = patch_index;
  pack_opl_instrument(&fmopl_patches[patch_index], &out[4]);
  out[4 + FMOPL_SYSEX_NIBBLES] = 0xF7;
  tud_midi_stream_write(MIDI_CABLE, out, sizeof(out));
  usCFG("[SYSEX] SYSEX_FMOPL_PATCH_DUMP: sent patch %d\n", patch_index);
  return;
}

/* Debugging/tuning tool - see SYSEX_FMOPL_SET_CLOCK's own comment above.
 * No index byte, unlike the patch commands: this is one global value, not
 * per-patch. 3 raw bytes (24 bits) -> 6 nibbles, big-endian, reusing this
 * file's own decode_byte(). All-zero resets to the compiled-in default. */
#define FMOPL_CLOCK_SYSEX_NIBBLES 6
#define FMOPL_CLOCK_SYSEX_MIN_SIZE (3 + FMOPL_CLOCK_SYSEX_NIBBLES + 1)

/**
 * @brief Handle SYSEX_FMOPL_SET_CLOCK: apply a runtime OPL2 clock override
 *
 * Validates the message length, decodes the 3 big-endian nibble-packed
 * bytes at buffer[3..8] into a 24 bit Hz value, and passes it to
 * midi_fmopl_set_clock(). An all-zero payload resets the compiled-in
 * default.
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_fmopl_set_clock(uint8_t *buffer, int size)
{
  if (size < FMOPL_CLOCK_SYSEX_MIN_SIZE) {
    usERR("[SYSEX] SYSEX_FMOPL_SET_CLOCK: message too short (%d < %d)\n", size, FMOPL_CLOCK_SYSEX_MIN_SIZE);
    return;
  }
  uint8_t b0 = decode_byte(&buffer[3]);
  uint8_t b1 = decode_byte(&buffer[5]);
  uint8_t b2 = decode_byte(&buffer[7]);
  uint32_t hz = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | (uint32_t)b2;
  midi_fmopl_set_clock(hz);
  usCFG("[SYSEX] SYSEX_FMOPL_SET_CLOCK: %lu Hz\n", (unsigned long)hz);
  return;
}


/**
 * @brief Spy vs Spy ?
 *
 * @param uint8_t * buffer
 * @param int size
 */
void decode_sysex_command(uint8_t * buffer, int size)
{
  if __us_unlikely(get_reset_state()) return;
  if __us_unlikely(!usbsid_config.Midi.enabled) return;

  uint8_t config_buffer[5] = {0};

  switch(buffer[2]) {
    case SYSEX_TOGGLE_AUDIO:  /* Toggle mono / stereo */
      config_buffer[0] = TOGGLE_AUDIO;
      handle_config_request(config_buffer, 5);
      break;
    case SYSEX_MIDI_SAVE:
      usCFG("[SYSEX] SYSEX_MIDI_SAVE\n");
      midi_config_save();
      break;
    case SYSEX_MIDI_LOAD:
      usCFG("[SYSEX] SYSEX_MIDI_LOAD\n");
      midi_config_load();
      break;
    case SYSEX_MIDI_RESET:
      usCFG("[SYSEX] SYSEX_MIDI_RESET\n");
      midi_config_reset();
      break;
    case SYSEX_MIDI_PATCH_LOAD:
      handle_patch_load(buffer, size);
      break;
    case SYSEX_MIDI_PATCH_DUMP:
      handle_patch_dump(buffer, size);
      break;
    case SYSEX_FMOPL_PATCH_LOAD:
      handle_fmopl_patch_load(buffer, size);
      break;
    case SYSEX_FMOPL_PATCH_DUMP:
      handle_fmopl_patch_dump(buffer, size);
      break;
    case SYSEX_FMOPL_SET_CLOCK:
      handle_fmopl_set_clock(buffer, size);
      break;
    default:
      break;
  }
  return;
}

/**
 * @brief Route an incoming SysEx message by its manufacturer id (buffer[1])
 *
 * 0x2D dispatches to the ASID decoder (decode_asid_message), 0x50 to this
 * file's own decode_sysex_command; any other id is ignored. Also sets
 * dtype so downstream logging reports the correct data source.
 *
 * @param uint8_t * buffer
 * @param int size
 */
void process_sysex(uint8_t* buffer, int size)
{
  switch(buffer[1]) {
    case 0x2D:  /* 0x2D = ASID sysex message */
      dtype = asid;  /* Set data type to ASID */
      set_vu_action(); /* Keep that shiny Vu blinking! */
      decode_asid_message(buffer, size);
      break;
    case 0x50:  /* The 80's baby */
      dtype = sysex;  /* Set data type to SysEx */
      decode_sysex_command(buffer, size);
      break;
    default:
      break;
  }
  return;
}
