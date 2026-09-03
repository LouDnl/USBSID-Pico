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
 #include <midi_arp_table.h>
 #include <midi_fmopl.h>
 #include <midi_handler.h>


/* Custom commands */
enum {
  SYSEX_TOGGLE_AUDIO = 0x01,
  /* Trigger-only, no payload - the MIDI blob save/load/reset
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
  /* The same LOAD/DUMP/DATA shape as the SID patch commands just
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
  /* GoatTracker/SidWizard-style arp/chord table authoring, same
   * LOAD/DUMP/DATA shape as the SID (0x20-0x22) and FMOpl (0x23-0x25) patch
   * commands - by now a proven, repeatable pattern. Own frame size (36
   * nibbles: 16 signed-byte offsets + loop_start + step_count), aimed at
   * arp_tables[]/midi_arp_table_t (midi_arp_table.h) instead of a patch
   * struct - entirely new commands, no shared frame with anything above to
   * version or collide with. */
  SYSEX_MIDI_ARPTABLE_LOAD = 0x27,
  SYSEX_MIDI_ARPTABLE_DUMP = 0x28,  /* request: device -> host */
  SYSEX_MIDI_ARPTABLE_DATA = 0x29,  /* response: device -> host, and what LOAD expects as input */
  /* The inverse of LOAD: capture a channel's current live state into a
   * patch slot instead of writing data the host already has. No payload -
   * buffer[3] = channel, buffer[4] = patch index, everything else is read
   * straight off live device state (midi_handler_capture_patch()/
   * midi_fmopl_capture_patch()). RAM only, same as LOAD; SYSEX_MIDI_SAVE
   * persists it. */
  SYSEX_MIDI_PATCH_SAVE  = 0x2A,
  SYSEX_FMOPL_PATCH_SAVE = 0x2B,
};


/* Every midi_patch_t field sent as two 7-bit-safe nibble bytes (0-15 each),
 * filter_cutoff (the one 16 bit field) as four - simple and obviously
 * correct over squeezing 8 bits into 7, which a mis-implemented packer
 * could get wrong in ways that are hard to notice from raw hex. 17 fields,
 * 16 of them 1 byte (32 nibbles) plus filter_cutoff (4 nibbles) = 36
 * nibble bytes per patch, comfortably inside the 64 byte SysEx receive
 * buffer (midimachine.streambuffer, midi.c) alongside the framing around
 * it.
 *
 * 6 more 1-byte fields appended at the tail (lfo2_wave/rate/
 * depth/dest, unison_enabled, unison_detune) -> +12 nibbles = 48. Appended,
 * not interleaved, so PATCH_SYSEX_MIN_SIZE (derived from this constant)
 * naturally refuses an old 36-nibble dump instead of misreading its
 * trailing bytes as these new fields - see midi_patch.h's own comment on
 * midi_patch_t for the full non-breaking rule this follows.*/
#define PATCH_SYSEX_NIBBLES 48

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
  encode_byte(p->bend_range, out); out += 2;
  encode_byte(p->lfo2_wave, out); out += 2;
  encode_byte(p->lfo2_rate, out); out += 2;
  encode_byte(p->lfo2_depth, out); out += 2;
  encode_byte(p->lfo2_dest, out); out += 2;
  encode_byte(p->unison_enabled, out); out += 2;
  encode_byte(p->unison_detune, out);
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
  p->bend_range = decode_byte(in); in += 2;
  p->lfo2_wave = decode_byte(in); in += 2;
  p->lfo2_rate = decode_byte(in); in += 2;
  p->lfo2_depth = decode_byte(in); in += 2;
  p->lfo2_dest = decode_byte(in); in += 2;
  p->unison_enabled = decode_byte(in); in += 2;
  p->unison_detune = decode_byte(in);
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

/**
 * @brief Handle SYSEX_MIDI_PATCH_SAVE: capture a channel's live state into a patch slot
 *
 * Inverse of LOAD - no payload, just addressing. buffer[3] = channel,
 * buffer[4] = patch index. Delegates the actual field-by-field capture to
 * midi_handler_capture_patch() (midi_handler.c), which mirrors
 * apply_patch_to_channel() in reverse. RAM only; SYSEX_MIDI_SAVE persists it.
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_patch_save(uint8_t *buffer, int size)
{
  if (size < 6) {
    usERR("[SYSEX] SYSEX_MIDI_PATCH_SAVE: message too short (%d < 6)\n", size);
    return;
  }
  uint8_t channel = buffer[3];
  uint8_t patch_index = buffer[4];
  if (channel >= MAX_CHANNELS) {
    usERR("[SYSEX] SYSEX_MIDI_PATCH_SAVE: channel %d out of range (max %d)\n", channel, MAX_CHANNELS - 1);
    return;
  }
  if (patch_index >= MIDI_PATCH_COUNT) {
    usERR("[SYSEX] SYSEX_MIDI_PATCH_SAVE: patch %d out of range (max %d)\n", patch_index, MIDI_PATCH_COUNT - 1);
    return;
  }
  midi_handler_capture_patch(channel, patch_index);
  usCFG("[SYSEX] SYSEX_MIDI_PATCH_SAVE: channel %d captured into patch %d (RAM, SYSEX_MIDI_SAVE to persist it)\n",
        channel, patch_index);
  return;
}


/* --- FMOpl patch authoring, same shape as the SID patch commands
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

/**
 * @brief Handle SYSEX_FMOPL_PATCH_SAVE: capture a channel's currently-selected
 *        instrument into another patch slot
 *
 * No payload, just addressing. buffer[3] = channel, buffer[4] = patch index.
 * Delegates to midi_fmopl_capture_patch() (midi_fmopl.c) - see that
 * function's own doc comment for why this is a plain instrument duplicate
 * today, not yet a true live-tweaks capture (no per-operator live editing
 * exists to capture). RAM only; SYSEX_MIDI_SAVE persists it.
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_fmopl_patch_save(uint8_t *buffer, int size)
{
  if (size < 6) {
    usERR("[SYSEX] SYSEX_FMOPL_PATCH_SAVE: message too short (%d < 6)\n", size);
    return;
  }
  uint8_t channel = buffer[3];
  uint8_t patch_index = buffer[4];
  if (channel >= MAX_CHANNELS) {
    usERR("[SYSEX] SYSEX_FMOPL_PATCH_SAVE: channel %d out of range (max %d)\n", channel, MAX_CHANNELS - 1);
    return;
  }
  if (patch_index >= MIDI_FMOPL_PATCH_COUNT) {
    usERR("[SYSEX] SYSEX_FMOPL_PATCH_SAVE: patch %d out of range (max %d)\n", patch_index, MIDI_FMOPL_PATCH_COUNT - 1);
    return;
  }
  midi_fmopl_capture_patch(channel, patch_index);
  usCFG("[SYSEX] SYSEX_FMOPL_PATCH_SAVE: channel %d captured into patch %d (RAM, SYSEX_MIDI_SAVE to persist it)\n",
        channel, patch_index);
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


/* --- Arp table authoring, same shape as the SID/FMOpl patch
 * commands above, aimed at arp_tables[]/midi_arp_table_t instead. 16
 * offsets (as signed bytes, this file's own encode_byte()/decode_byte()
 * reinterpreting the int8_t bit pattern as a uint8_t) plus loop_start plus
 * step_count, each 1 byte -> 2 nibbles = 36 nibbles per table. */
#define ARPTABLE_SYSEX_NIBBLES 36

/**
 * @brief Pack a midi_arp_table_t into ARPTABLE_SYSEX_NIBBLES nibble bytes
 *
 * @param const midi_arp_table_t * t
 * @param uint8_t * out
 */
static void pack_arp_table(const midi_arp_table_t *t, uint8_t *out)
{
  for (uint8_t i = 0; i < MIDI_ARP_TABLE_STEPS; i++) {
    encode_byte((uint8_t)t->offsets[i], out); out += 2;
  }
  encode_byte(t->loop_start, out); out += 2;
  encode_byte(t->step_count, out);
  return;
}

/**
 * @brief The inverse of pack_arp_table()
 *
 * Also clamps step_count to MIDI_ARP_TABLE_STEPS and resets loop_start to
 * 0 when it falls outside the table's decoded step_count, so a malformed
 * or hand-crafted frame cannot make arp_advance_table() (midi_handler.c)
 * index out of bounds.
 *
 * @param const uint8_t * in
 * @param midi_arp_table_t * t
 */
static void unpack_arp_table(const uint8_t *in, midi_arp_table_t *t)
{
  for (uint8_t i = 0; i < MIDI_ARP_TABLE_STEPS; i++) {
    t->offsets[i] = (int8_t)decode_byte(in); in += 2;
  }
  t->loop_start = decode_byte(in); in += 2;
  t->step_count = decode_byte(in);
  /* Defensive clamp: a malformed or hand-crafted frame's step_count/
   * loop_start must not let arp_advance_table() (midi_handler.c) index
   * outside offsets[MIDI_ARP_TABLE_STEPS], or read a loop point past its
   * own step_count. */
  if (t->step_count > MIDI_ARP_TABLE_STEPS) t->step_count = MIDI_ARP_TABLE_STEPS;
  if (t->step_count > 0 && t->loop_start >= t->step_count) t->loop_start = 0;
  return;
}

#define ARPTABLE_SYSEX_MIN_SIZE (4 + ARPTABLE_SYSEX_NIBBLES + 1)

/**
 * @brief Handle SYSEX_MIDI_ARPTABLE_LOAD: write one arp/chord table into RAM
 *
 * Validates the message length and table index, then unpacks the nibble
 * payload at buffer[4..] into arp_tables[table_index]. RAM only; does not
 * persist to flash (SYSEX_MIDI_SAVE does that).
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_arptable_load(uint8_t *buffer, int size)
{
  if (size < ARPTABLE_SYSEX_MIN_SIZE) {
    usERR("[SYSEX] SYSEX_MIDI_ARPTABLE_LOAD: message too short (%d < %d)\n", size, ARPTABLE_SYSEX_MIN_SIZE);
    return;
  }
  uint8_t table_index = buffer[3];
  if (table_index >= MIDI_ARP_TABLE_COUNT) {
    usERR("[SYSEX] SYSEX_MIDI_ARPTABLE_LOAD: table %d out of range (max %d)\n", table_index, MIDI_ARP_TABLE_COUNT - 1);
    return;
  }
  unpack_arp_table(&buffer[4], &arp_tables[table_index]);
  usCFG("[SYSEX] SYSEX_MIDI_ARPTABLE_LOAD: table %d updated in RAM (SYSEX_MIDI_SAVE to persist it)\n", table_index);
  return;
}

/**
 * @brief Handle SYSEX_MIDI_ARPTABLE_DUMP: send one arp/chord table back over MIDI OUT
 *
 * Validates the table index, packs arp_tables[table_index] into a
 * SYSEX_MIDI_ARPTABLE_DATA SysEx message, and writes it out on MIDI_CABLE.
 *
 * @param uint8_t * buffer
 * @param int size
 */
static void handle_arptable_dump(uint8_t *buffer, int size)
{
  if (size < 4) {
    usERR("[SYSEX] SYSEX_MIDI_ARPTABLE_DUMP: message too short\n");
    return;
  }
  uint8_t table_index = buffer[3];
  if (table_index >= MIDI_ARP_TABLE_COUNT) {
    usERR("[SYSEX] SYSEX_MIDI_ARPTABLE_DUMP: table %d out of range (max %d)\n", table_index, MIDI_ARP_TABLE_COUNT - 1);
    return;
  }
  uint8_t out[4 + ARPTABLE_SYSEX_NIBBLES + 1];
  out[0] = 0xF0;
  out[1] = 0x50;
  out[2] = SYSEX_MIDI_ARPTABLE_DATA;
  out[3] = table_index;
  pack_arp_table(&arp_tables[table_index], &out[4]);
  out[4 + ARPTABLE_SYSEX_NIBBLES] = 0xF7;
  tud_midi_stream_write(MIDI_CABLE, out, sizeof(out));
  usCFG("[SYSEX] SYSEX_MIDI_ARPTABLE_DUMP: sent table %d\n", table_index);
  return;
}


/**
 * @brief Dispatch a USBSID custom SysEx command (buffer[2] = command id)
 *
 * Handles TOGGLE_AUDIO (routed through handle_config_request), the MIDI
 * config SAVE/LOAD/RESET triggers, the LOAD/DUMP command pairs for SID
 * patches, FMOpl patches and arp tables, the SAVE command pair that captures
 * a channel's live state into a SID or FMOpl patch slot, plus the FMOpl
 * clock override. Ignored while the bus is in reset state or MIDI is
 * disabled in config.
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
    case SYSEX_MIDI_ARPTABLE_LOAD:
      handle_arptable_load(buffer, size);
      break;
    case SYSEX_MIDI_ARPTABLE_DUMP:
      handle_arptable_dump(buffer, size);
      break;
    case SYSEX_MIDI_PATCH_SAVE:
      handle_patch_save(buffer, size);
      break;
    case SYSEX_FMOPL_PATCH_SAVE:
      handle_fmopl_patch_save(buffer, size);
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
