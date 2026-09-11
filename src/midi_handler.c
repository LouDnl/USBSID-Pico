/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_handler.c
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
#include <bus.h>
#include <logging.h>
#include <sid.h>
#include <sid_defs.h>
#include <midi.h>
#include <midi_defs.h>
#include <midi_config.h>
#include <midi_voice.h>
#include <midi_patch.h>
#include <midi_arp_table.h>
#include <midi_fmopl.h>
#include <midi_handler.h>


/* MIDI pitch bend is a 14 bit value sent LSB first, centred at 0x2000 and
 * running to 0x3FFF. Range is per channel (midi_channel_cfg_t.bend_range,
 * default 2 semitones); the centre point itself is protocol-fixed. */
#define MIDI_BEND_CENTRE  0x2000

/* Full-depth LFO pitch excursion, in semitones. Not user configurable
 * beyond the 0-127 depth CC scaling within this range; a vibrato deeper
 * than this is not a vibrato any more. */
#define MIDI_LFO_PITCH_MAX_SEMITONES 2

/* Clock scaled note table.
 * `musical_scale_values[]` is generated for a 1.000 MHz SID clock, but the SID
 * frequency register value is f_out * 2^24 / f_clk, so a board configured for
 * PAL (985248 Hz) plays every note about 29 cents sharp when the raw table is
 * used. Rescale once per clock change rather than per note. */
static uint16_t note_table[count_of(musical_scale_values)];
static uint32_t note_table_clock = 0;

static midi_ccvalues CC;
typedef void (*cc_handler_t)(uint8_t channel, uint8_t cc, uint8_t value);
static cc_handler_t cc_func_ptr_array[128];

/* Pre declare */
static void all_notes_off(uint8_t channel, uint8_t cc, uint8_t value);
static void release_channel_voices(uint8_t channel);

/* Internal helper functions */
/**
 * @brief Fast SID register write with no artificial cycle delay
 *
 * @param uint8_t a, register address
 * @param uint8_t b, value to write
 */
static void midi_bus_operation(uint8_t a, uint8_t b)
{
  cycled_write_operation(a, b, 0);  /* 0 cycles constant for fast writing */
  return;
}

/**
 * @brief Get the currently configured SID clock rate, falling back to the compile-time default
 *
 * @return uint32_t clock rate in Hz
 */
static inline uint32_t midi_clockrate(void)
{
  /* The configured clock lives on usbsid_config, not on the runtime cfg */
  return (usbsid_config.clock_rate == 0 ? (uint32_t)CLOCK_DEFAULT : usbsid_config.clock_rate);
}

/**
 * @brief Rebuild the clock-scaled MIDI note frequency table for the current clock rate
 *
 * Rescales musical_scale_values[] (built for a 1.000 MHz SID clock) to the
 * configured rate, rounding rather than truncating for tuning accuracy.
 */
static void build_note_table(void)
{
  uint32_t rate = midi_clockrate();
  for (size_t i = 0; i < count_of(musical_scale_values); i++) {
    /* Round rather than truncate: at the bottom of the range one register
     * step is over 5 cents, so truncating costs real tuning accuracy there. */
    uint64_t v = ((((uint64_t)musical_scale_values[i] * (uint64_t)CLOCK_DEFAULT) + (rate / 2)) / rate);
    note_table[i] = (uint16_t)(v > 0xFFFF ? 0xFFFF : v);
  }
  note_table_clock = rate;
  usMIDI("Note table built for %u Hz\n", rate);
  return;
}

/**
 * @brief Rebuild the note table if the configured clock rate has changed since it was last built
 *
 * Cheap enough to run on every incoming MIDI message, keeping the table
 * honest without config.c having to call into the MIDI handler on a
 * clock change.
 */
static inline void check_note_table(void)
{
  if __us_unlikely(note_table_clock != midi_clockrate()) build_note_table();
  return;
}

/* Bit/nibble helpers below take a target byte pointer, either a live
 * sid_memory[] location or a channel's per-voice register template
 * (pushed to held voices by propagate_reg() afterwards). None writes the
 * bus itself. */

/**
 * @brief Set the given bit(s) in a target byte
 *
 * @param uint8_t * target
 * @param int bit
 */
static inline void set_bit_at(uint8_t *target, int bit)
{
  *target = (uint8_t)(*target | bit);
  return;
}
/**
 * @brief Clear the given bit(s) in a target byte
 *
 * @param uint8_t * target
 * @param int bit
 */
static inline void unset_bit_at(uint8_t *target, int bit)
{
  *target = (uint8_t)(*target & ~bit);
  return;
}
/**
 * @brief Toggle the given bit(s) in a target byte
 *
 * @param uint8_t * target
 * @param int bit
 */
static inline void toggle_bit_at(uint8_t *target, int bit)
{
  *target = (uint8_t)((~(*target & bit) & bit) | (*target & ~bit));
  return;
}
/**
 * @brief Set, clear or toggle a bit in a target byte based on a MIDI CC value
 *
 * CC value 127 sets the bit, 0 clears it, any other value toggles it.
 *
 * @param uint8_t * target
 * @param int bit
 * @param uint8_t cc_value
 */
static inline void handle_bit_at(uint8_t *target, int bit, uint8_t cc_value)
{
  switch (cc_value) {
    case 127: set_bit_at(target, bit); break;
    case 0:   unset_bit_at(target, bit); break;
    default:  toggle_bit_at(target, bit); break;
  }
  return;
}
/**
 * @brief Write a value into one nibble of a target byte, preserving the other nibble
 *
 * nibble_to_preserve is the mask of the nibble to keep unchanged: when it
 * is R_NIBBLE (preserve the low nibble), val is shifted left 4 bits and
 * written into the high nibble; otherwise val is written into the low
 * nibble as-is.
 *
 * @param uint8_t * target
 * @param uint8_t val
 * @param int nibble_to_preserve
 */
static inline void set_nibble_at(uint8_t *target, uint8_t val, int nibble_to_preserve)
{
  uint8_t wrval = ((nibble_to_preserve == R_NIBBLE) ? (val << SHIFT_4) : val);
  *target = (uint8_t)((*target & nibble_to_preserve) | wrval);
  return;
}

/**
 * @brief Push a per-voice register value to every voice `channel` currently
 *        holds
 *
 * Walks the whole pool and matches on ownership, since a channel's held
 * voices can be spread across SIDs and come and go per note.
 *
 * @param uint8_t channel
 * @param uint8_t reg_offset, one of CONTR/ATTDEC/SUSREL/PWMLO/PWMHI
 * @param uint8_t value
 * @param bool preserve_gate, keep each voice's own live CONTR gate bit
 *        (only meaningful when reg_offset == CONTR)
 */
static void propagate_reg(uint8_t channel, uint8_t reg_offset, uint8_t value, bool preserve_gate)
{
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (midi_voice_state(i) != MIDI_VOICE_GATED || midi_voice_channel(i) != channel) continue;
    uint8_t addr = (uint8_t)(midi_voice_sidbase(i) + midi_voice_regbase(i) + reg_offset);
    uint8_t v = value;
    if (preserve_gate) v = (uint8_t)((v & (uint8_t)~BIT_0) | (sid_memory[addr] & BIT_0));
    sid_memory[addr] = v;
    midi_bus_operation(addr, v);
  }
  return;
}

/**
 * @brief Recompute and, if changed, write one voice's frequency registers
 *
 * Combines portamento glide position, the channel's pitch-bend offset, and
 * an optional LFO offset (all in note-index*256 fixed point, so they
 * simply add). Skips the bus write when unchanged, using sid_memory[]
 * itself as the "last written" cache.
 *
 * @param uint8_t slot
 * @param int32_t lfo_offset_x256, 0 if no LFO is targeting pitch right now
 */
static void write_voice_pitch(uint8_t slot, int32_t lfo_offset_x256)
{
  uint8_t channel = midi_voice_channel(slot);
  int32_t index_x256 = midi_voice_cur_pitch(slot) + midi_channels[channel].bend_x256 + lfo_offset_x256;

  int32_t whole = index_x256 >> MIDI_PITCH_FRAC_BITS;
  uint16_t frac_mask = (1u << MIDI_PITCH_FRAC_BITS) - 1;
  uint8_t frac = (uint8_t)(index_x256 & frac_mask);
  if (whole < 0) { whole = 0; frac = 0; }
  /* Clamped one below the top so `whole + 1` stays a valid interpolation partner */
  if (whole >= SCALE_MAX) { whole = SCALE_MAX - 1; frac = 0xFF; }

  uint32_t f_lo = note_table[whole];
  uint32_t f_hi = note_table[whole + 1];
  uint16_t frequency = (uint16_t)(f_lo + (((f_hi - f_lo) * frac) / 256));

  uint8_t base = (uint8_t)(midi_voice_sidbase(slot) + midi_voice_regbase(slot));
  uint8_t Flo = (uint8_t)(frequency & VOICE_FREQLO);
  uint8_t Fhi = (uint8_t)((frequency >> SHIFT_8) & 0xFF);

  if (sid_memory[base+NOTEHI] != Fhi) { sid_memory[base+NOTEHI] = Fhi; midi_bus_operation(base+NOTEHI, Fhi); }
  if (sid_memory[base+NOTELO] != Flo) { sid_memory[base+NOTELO] = Flo; midi_bus_operation(base+NOTELO, Flo); }
  return;
}

/**
 * @brief List the physical base address of every SID in `channel`'s
 *        effective mask, once each
 *
 * @param uint8_t channel
 * @param uint8_t out[MAX_SIDS]
 * @return uint8_t the number of entries written to out
 */
static uint8_t channel_sid_bases(uint8_t channel, uint8_t out[MAX_SIDS])
{
  uint16_t mask = midi_channel_effective_mask(channel);
  uint8_t n = 0;
  for (uint8_t s = 0; s < MAX_SIDS; s++) {
    uint16_t bits = (uint16_t)(((1u << MAX_VOICES) - 1) << (s * MAX_VOICES));
    if (mask & bits) out[n++] = cfg.sidaddr[cfg.ids[s]];
  }
  return n;
}

/* --- Per-voice register CCs: update the channel's template, then push it
 *     live to every voice the channel currently holds --------------------- */

/**
 * @brief Update a channel's control-register waveform/test/sync/ring-mod bit template and push it to every held voice
 *
 * Maps CC_NOIS/CC_PULS/CC_SAWT/CC_TRIA/CC_TEST/CC_RMOD/CC_SYNC to their
 * CONTR bit, updates the channel's tmpl_contr, then propagates it live
 * (preserving each voice's own gate bit).
 *
 * @param uint8_t channel
 * @param uint8_t cc
 * @param uint8_t value
 */
static void set_controlreg(uint8_t channel, uint8_t cc, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  int bit;
  if (cc == CC.CC_NOIS)      bit = BIT_7;
  else if (cc == CC.CC_PULS) bit = BIT_6;
  else if (cc == CC.CC_SAWT) bit = BIT_5;
  else if (cc == CC.CC_TRIA) bit = BIT_4;
  else if (cc == CC.CC_TEST) bit = BIT_3;
  else if (cc == CC.CC_RMOD) bit = BIT_2;
  else if (cc == CC.CC_SYNC) bit = BIT_1;
  else return;
  handle_bit_at(&ch->tmpl_contr, bit, value);
  propagate_reg(channel, CONTR, ch->tmpl_contr, true);
  return;
}

/**
 * @brief Manually set, clear or toggle the gate bit on every voice a channel currently holds
 *
 * Deliberately not part of the CONTR template: a held drone gated open
 * manually should not be silently re-gated by the next CC that happens to
 * touch CONTR on the same voice.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_gate(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (midi_voice_state(i) != MIDI_VOICE_GATED || midi_voice_channel(i) != channel) continue;
    uint8_t addr = (uint8_t)(midi_voice_sidbase(i) + midi_voice_regbase(i) + CONTR);
    handle_bit_at(&sid_memory[addr], BIT_0, value);
    midi_bus_operation(addr, sid_memory[addr]);
  }
  return;
}

/**
 * @brief Update a channel's ADSR nibble template from CC_ATT/CC_DEC/CC_SUS/CC_REL and push it to every held voice
 *
 * @param uint8_t channel
 * @param uint8_t cc
 * @param uint8_t value
 */
static void set_adsr(uint8_t channel, uint8_t cc, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  int nib = (((cc == CC.CC_ATT) || (cc == CC.CC_SUS)) ? R_NIBBLE : L_NIBBLE);
  uint8_t mapped = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
  bool is_attdec = (cc == CC.CC_ATT) || (cc == CC.CC_DEC);
  uint8_t *tmpl = is_attdec ? &ch->tmpl_attdec : &ch->tmpl_susrel;
  set_nibble_at(tmpl, mapped, nib);
  propagate_reg(channel, is_attdec ? ATTDEC : SUSREL, *tmpl, false);
  return;
}

/**
 * @brief Update a channel's pulse width template from CC_PWM and push it to every held voice
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_voicepwm(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channel_cfg_t *ch = &midi_channels[channel];
  uint16_t pwm = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, TRIPLE_NIBBLE);
  ch->tmpl_pwmlo = (uint8_t)(pwm & BYTE);
  ch->tmpl_pwmhi = (uint8_t)((pwm & NIBBLE_3) >> SHIFT_8);
  propagate_reg(channel, PWMLO, ch->tmpl_pwmlo, false);
  propagate_reg(channel, PWMHI, ch->tmpl_pwmhi, false);
  return;
}

/**
 * @brief CC_NOTE: one-shot manual pitch entry on every voice a channel currently holds
 *
 * Snapped in (no glide) through the same pitch machinery as everything
 * else (midi_voice_set_pitch() + write_voice_pitch()), so a subsequent
 * tick's LFO/portamento pass does not silently overwrite it with a stale
 * target.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_notefrequency(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  int32_t index_x256 = (int32_t)MAP(value, MIN_VAL, MIDI_CC_MAX, 0, (SCALE_MAX << MIDI_PITCH_FRAC_BITS));
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (midi_voice_state(i) != MIDI_VOICE_GATED || midi_voice_channel(i) != channel) continue;
    midi_voice_set_pitch(i, index_x256, index_x256);
    write_voice_pitch(i, 0);
  }
  return;
}

/* --- Chip-wide CCs: write straight to every SID in the channel's mask,
 *     no template needed, these registers are not per-voice --------------- */

/**
 * @brief Set filter cutoff (CC_FFC) on every SID in a channel's mask
 *
 * Remembers the value on midi_channels[channel].filter_cutoff so the
 * LFO-cutoff tick has a stable centre to modulate around.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_filtercutoff(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  uint16_t cutoff = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, CUTOFF_MAX);
  midi_channels[channel].filter_cutoff = cutoff;
  uint8_t Clo = (uint8_t)(cutoff & F_MASK_LO);
  uint8_t Chi = (uint8_t)((cutoff & F_MASK_HI) >> SHIFT_3);
  uint8_t bases[MAX_SIDS];
  uint8_t n = channel_sid_bases(channel, bases);
  for (uint8_t i = 0; i < n; i++) {
    sid_memory[bases[i]+FC_HI] = Chi; midi_bus_operation(bases[i]+FC_HI, Chi);
    sid_memory[bases[i]+FC_LO] = Clo; midi_bus_operation(bases[i]+FC_LO, Clo);
  }
  return;
}

/**
 * @brief Set filter resonance and routing (CC_RES/CC_FLT1-3/CC_FLTE) on every SID in a channel's mask
 *
 * @param uint8_t channel
 * @param uint8_t cc
 * @param uint8_t value
 */
static void set_resfilter(uint8_t channel, uint8_t cc, uint8_t value)
{
  uint8_t bases[MAX_SIDS];
  uint8_t n = channel_sid_bases(channel, bases);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t addr = (uint8_t)(bases[i] + RESFLT);
    if (cc == CC.CC_RES) {
      uint8_t mapped = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
      set_nibble_at(&sid_memory[addr], mapped, R_NIBBLE);
    } else if (cc == CC.CC_FLTE) handle_bit_at(&sid_memory[addr], BIT_3, value);
    else if (cc == CC.CC_FLT1)  handle_bit_at(&sid_memory[addr], BIT_0, value);
    else if (cc == CC.CC_FLT2)  handle_bit_at(&sid_memory[addr], BIT_1, value);
    else if (cc == CC.CC_FLT3)  handle_bit_at(&sid_memory[addr], BIT_2, value);
    midi_bus_operation(addr, sid_memory[addr]);
  }
  return;
}

/**
 * @brief Set master volume and filter mode bits (CC_VOL/CC_3OFF/CC_HPF/CC_BPF/CC_LPF) on every SID in a channel's mask
 *
 * @param uint8_t channel
 * @param uint8_t cc
 * @param uint8_t value
 */
static void set_modevolume(uint8_t channel, uint8_t cc, uint8_t value)
{
  uint8_t bases[MAX_SIDS];
  uint8_t n = channel_sid_bases(channel, bases);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t addr = (uint8_t)(bases[i] + MODVOL);
    if (cc == CC.CC_VOL) {
      uint8_t mapped = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
      set_nibble_at(&sid_memory[addr], mapped, L_NIBBLE);
    } else if (cc == CC.CC_3OFF) handle_bit_at(&sid_memory[addr], BIT_7, value);
    else if (cc == CC.CC_HPF)    handle_bit_at(&sid_memory[addr], BIT_6, value);
    else if (cc == CC.CC_BPF)    handle_bit_at(&sid_memory[addr], BIT_5, value);
    else if (cc == CC.CC_LPF)    handle_bit_at(&sid_memory[addr], BIT_4, value);
    midi_bus_operation(addr, sid_memory[addr]);
  }
  return;
}

/**
 * @brief Push a patch's filter cutoff/resonance/routing/mode to every SID
 *        in a channel's mask
 *
 * The chip-register-only half of apply_patch_to_channel(), factored out so
 * midi_processor_init() can re-sync these after a config reload without
 * re-running the template/LFO/arp portion.
 *
 * @param uint8_t channel
 * @param const midi_patch_t * p
 */
static void apply_patch_filter_hw(uint8_t channel, const midi_patch_t *p)
{
  midi_channels[channel].filter_cutoff = p->filter_cutoff;
  uint8_t Clo = (uint8_t)(p->filter_cutoff & F_MASK_LO);
  uint8_t Chi = (uint8_t)((p->filter_cutoff & F_MASK_HI) >> SHIFT_3);
  uint8_t bases[MAX_SIDS];
  uint8_t n = channel_sid_bases(channel, bases);
  for (uint8_t i = 0; i < n; i++) {
    sid_memory[bases[i]+FC_HI] = Chi; midi_bus_operation(bases[i]+FC_HI, Chi);
    sid_memory[bases[i]+FC_LO] = Clo; midi_bus_operation(bases[i]+FC_LO, Clo);
    uint8_t resflt = (uint8_t)((p->resonance << SHIFT_4) | (p->filter_routing & NIBBLE_MAX));
    sid_memory[bases[i]+RESFLT] = resflt; midi_bus_operation(bases[i]+RESFLT, resflt);
    /* A voice routed into the filter (filter_routing above) has no output
     * at all unless a filter mode (HPF/BPF/LPF, MODVOL's high nibble) is
     * also selected - real SID behaviour, not something to route around.
     * Read-modify-write so the channel's current volume (low nibble) and
     * 3off bit are preserved, not clobbered by a patch that has no opinion
     * on them. */
    uint8_t modvol = (uint8_t)((sid_memory[bases[i]+MODVOL] & (BIT_7 | NIBBLE_MAX)) | p->filter_mode);
    sid_memory[bases[i]+MODVOL] = modvol; midi_bus_operation(bases[i]+MODVOL, modvol);
  }
  return;
}

/**
 * @brief Apply a stored patch to a channel: Program Change's whole job
 *
 * Loads the patch's per-voice template/filter/lfo/arp/bend fields into the
 * channel and pushes them live. Deliberately does not touch voice_mask,
 * poly_limit, steal_mode, transpose, sid/voice overrides, or any live
 * runtime state - a patch is a timbre, not a routing change.
 *
 * @param uint8_t channel
 * @param uint8_t patch_index, must be < MIDI_PATCH_COUNT
 */
static void apply_patch_to_channel(uint8_t channel, uint8_t patch_index)
{
  const midi_patch_t *p = &midi_patches[patch_index];
  midi_channel_cfg_t *ch = &midi_channels[channel];

  ch->tmpl_contr  = p->tmpl_contr;
  ch->tmpl_attdec = p->tmpl_attdec;
  ch->tmpl_susrel = p->tmpl_susrel;
  ch->tmpl_pwmlo  = p->tmpl_pwmlo;
  ch->tmpl_pwmhi  = p->tmpl_pwmhi;
  propagate_reg(channel, CONTR,  ch->tmpl_contr,  true);
  propagate_reg(channel, ATTDEC, ch->tmpl_attdec, false);
  propagate_reg(channel, SUSREL, ch->tmpl_susrel, false);
  propagate_reg(channel, PWMLO,  ch->tmpl_pwmlo,  false);
  propagate_reg(channel, PWMHI,  ch->tmpl_pwmhi,  false);

  apply_patch_filter_hw(channel, p);

  ch->lfo_wave    = p->lfo_wave;
  ch->lfo_rate    = p->lfo_rate;
  ch->lfo_depth   = p->lfo_depth;
  ch->lfo_dest    = p->lfo_dest;
  ch->lfo2_wave   = p->lfo2_wave;
  ch->lfo2_rate   = p->lfo2_rate;
  ch->lfo2_depth  = p->lfo2_depth;
  ch->lfo2_dest   = p->lfo2_dest;
  ch->arp_mode    = p->arp_mode;
  ch->arp_rate    = p->arp_rate;
  ch->arp_octaves = p->arp_octaves;
  ch->bend_range  = p->bend_range;
  if (p->unison_enabled) ch->flags = (uint8_t)(ch->flags | MIDI_CH_UNISON);
  else                   ch->flags = (uint8_t)(ch->flags & (uint8_t)~MIDI_CH_UNISON);
  ch->unison_detune = p->unison_detune;
  ch->patch       = patch_index;

  usMIDI("[PATCH] ch%d -> patch %d\n", channel, patch_index);
  return;
}

/**
 * @brief Capture a channel's current live state into a patch slot - the
 *        inverse of apply_patch_to_channel()/Program Change
 *
 * Filter resonance/routing/mode are read from the first SID in the
 * channel's mask (chip-wide registers, not cached per-channel). RAM only;
 * caller must range-check channel and patch_index first.
 *
 * @param uint8_t channel
 * @param uint8_t patch_index, must be < MIDI_PATCH_COUNT
 */
void midi_handler_capture_patch(uint8_t channel, uint8_t patch_index)
{
  const midi_channel_cfg_t *ch = &midi_channels[channel];
  midi_patch_t *p = &midi_patches[patch_index];

  p->tmpl_contr  = ch->tmpl_contr;
  p->tmpl_attdec = ch->tmpl_attdec;
  p->tmpl_susrel = ch->tmpl_susrel;
  p->tmpl_pwmlo  = ch->tmpl_pwmlo;
  p->tmpl_pwmhi  = ch->tmpl_pwmhi;

  p->filter_cutoff = ch->filter_cutoff;

  uint8_t bases[MAX_SIDS];
  uint8_t n = channel_sid_bases(channel, bases);
  if (n > 0) {
    uint8_t resflt = sid_memory[bases[0]+RESFLT];
    p->resonance      = (uint8_t)((resflt >> SHIFT_4) & NIBBLE_MAX);
    p->filter_routing = (uint8_t)(resflt & NIBBLE_MAX);
    p->filter_mode    = (uint8_t)(sid_memory[bases[0]+MODVOL] & (BIT_4 | BIT_5 | BIT_6));
  } else {
    p->resonance = 0;
    p->filter_routing = 0;
    p->filter_mode = 0;
  }

  p->lfo_wave    = ch->lfo_wave;
  p->lfo_rate    = ch->lfo_rate;
  p->lfo_depth   = ch->lfo_depth;
  p->lfo_dest    = ch->lfo_dest;
  p->lfo2_wave   = ch->lfo2_wave;
  p->lfo2_rate   = ch->lfo2_rate;
  p->lfo2_depth  = ch->lfo2_depth;
  p->lfo2_dest   = ch->lfo2_dest;
  p->arp_mode    = ch->arp_mode;
  p->arp_rate    = ch->arp_rate;
  p->arp_octaves = ch->arp_octaves;
  p->bend_range  = ch->bend_range;
  p->unison_enabled = (ch->flags & MIDI_CH_UNISON) ? 1 : 0;
  p->unison_detune  = ch->unison_detune;

  usMIDI("[PATCH] ch%d captured -> patch %d\n", channel, patch_index);
  return;
}

/* --- CC_SID1-4 / CC_VCE1-3: manual override that narrows the configured
 *     voice_mask at runtime, without altering the configured mask itself.
 *     Value 0 clears the override; anything else selects it, so an old
 *     controller sending only "select" (typically 127) behaves exactly as
 *     it always has. --------------------------------------------------- */

/**
 * @brief Set or clear a channel's manual SID-socket override from CC_SID1-4
 *
 * Narrows the channel's configured voice_mask at runtime without altering
 * the configured mask itself. value 0 clears the override
 * (MIDI_OVERRIDE_NONE); any other value selects the SID socket matching
 * the CC number (CC_SID1..CC_SID4 -> socket 0..3).
 *
 * @param uint8_t channel
 * @param uint8_t cc
 * @param uint8_t value
 */
static void set_sid_override(uint8_t channel, uint8_t cc, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (value == 0) { ch->sid_override = MIDI_OVERRIDE_NONE; return; }
  if (cc == CC.CC_SID1) ch->sid_override = 0;
  else if (cc == CC.CC_SID2) ch->sid_override = 1;
  else if (cc == CC.CC_SID3) ch->sid_override = 2;
  else if (cc == CC.CC_SID4) ch->sid_override = 3;
  usMIDI("[SID OVERRIDE] ch%d -> %d\n", channel, ch->sid_override);
  return;
}

/**
 * @brief Set or clear a channel's manual voice-within-SID override from CC_VCE1-3
 *
 * value 0 clears the override (MIDI_OVERRIDE_NONE); any other value
 * selects the voice matching the CC number (CC_VCE1..CC_VCE3 -> voice
 * 0..2).
 *
 * @param uint8_t channel
 * @param uint8_t cc
 * @param uint8_t value
 */
static void set_voice_override(uint8_t channel, uint8_t cc, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (value == 0) { ch->voice_override = MIDI_OVERRIDE_NONE; return; }
  if (cc == CC.CC_VCE1) ch->voice_override = 0;
  else if (cc == CC.CC_VCE2) ch->voice_override = 1;
  else if (cc == CC.CC_VCE3) ch->voice_override = 2;
  usMIDI("[VOICE OVERRIDE] ch%d -> %d\n", channel, ch->voice_override);
  return;
}

/* --- LFO and portamento CCs. Arpeggiator CCs are further down, next to
 *     the arpeggiator's own functions, since enabling/disabling it needs
 *     note_off() which is not declared yet at this point in the file. --- */

/**
 * @brief Set a channel's LFO1 waveform from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo_wave(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_wave = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, MIDI_LFO_TRI, MIDI_LFO_SH);
  return;
}
/**
 * @brief Set a channel's LFO1 rate from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo_rate(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_rate = value;
  return;
}
/**
 * @brief Set a channel's LFO1 depth from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo_depth(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_depth = value;
  return;
}
/**
 * @brief Set a channel's LFO1 modulation destination (pitch/PWM/cutoff) from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo_dest(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_dest = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, MIDI_LFO_DEST_PITCH, MIDI_LFO_DEST_CUTOFF);
  return;
}
/**
 * @brief Set a channel's portamento glide time from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_porta_time(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].porta_time = value;
  return;
}

/* --- LFO 2, a second independent oscillator stacked with LFO 1 at
 *     the tick (midi_tick(), below) - same 4 CC shape as LFO 1's own. ----- */

/**
 * @brief Set a channel's LFO2 waveform from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo2_wave(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo2_wave = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, MIDI_LFO_TRI, MIDI_LFO_SH);
  return;
}
/**
 * @brief Set a channel's LFO2 rate from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo2_rate(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo2_rate = value;
  return;
}
/**
 * @brief Set a channel's LFO2 depth from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo2_depth(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo2_depth = value;
  return;
}
/**
 * @brief Set a channel's LFO2 modulation destination (pitch/PWM/cutoff) from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_lfo2_dest(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo2_dest = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, MIDI_LFO_DEST_PITCH, MIDI_LFO_DEST_CUTOFF);
  return;
}

/* --- Unison on/off and detune spread. Turning unison on does not
 *     retouch any note already sounding - like a patch change, it only
 *     governs the next note_on() (midi_handler.c). --------------------- */

/**
 * @brief Enable or disable unison mode on a channel from a CC value
 *
 * value >= 64 enables unison (MIDI_CH_UNISON), below disables it. Takes
 * effect only on the next note-on; an already-sounding note is not
 * retouched.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_unison_enable(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (value >= 64) ch->flags = (uint8_t)(ch->flags | MIDI_CH_UNISON);
  else             ch->flags = (uint8_t)(ch->flags & (uint8_t)~MIDI_CH_UNISON);
  usMIDI("[CC_UNIS] ch%d unison %s\n", channel, (ch->flags & MIDI_CH_UNISON) ? "on" : "off");
  return;
}
/**
 * @brief Set a channel's unison detune spread from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_unison_detune(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].unison_detune = value;
  return;
}

/**
 * @brief Popcount of an effective voice mask, at least 1
 */
static uint8_t mask_popcount(uint16_t mask)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) if (mask & (1u << i)) n++;
  return (n == 0 ? 1 : n);
}

/**
 * @brief Recompute every channel's poly_limit against the SID count
 *        actually present, once that count is known
 *
 * midi_config_init() runs before cfg.numsids is authoritative, so it
 * hardcodes poly_limit assuming every physical SID slot exists. On a
 * board with fewer SIDs this would leave poly_limit stuck above the
 * pool's real capacity, causing note-on to steal from other channels
 * instead of the channel's own oldest note. Called once at boot after
 * cfg.numsids settles, before the host can enumerate.
 */
void midi_config_sync_poly_limits(void)
{
  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    midi_channels[c].poly_limit = mask_popcount(midi_channel_effective_mask(c));
  }
  return;
}

/**
 * @brief Dispatch CC_GTEN, CC_VELM, CC_SPLY, CC_MONO and CC_POLY handling for a channel
 *
 * CC_GTEN/CC_VELM toggle their channel flag bits (127 forces on, 0 forces
 * off, any other value toggles). CC_SPLY changes poly_limit between 1 and
 * the channel's full voice count without silencing already-held notes.
 * CC_MONO/CC_POLY do the same but also call release_channel_voices() to
 * give the channel a clean slate on the mode switch.
 *
 * @param uint8_t channel
 * @param uint8_t cc
 * @param uint8_t value
 */
static void set_handler(uint8_t channel, uint8_t cc, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];

  const struct { uint8_t cc; uint8_t flag_bit; const char *name; } toggles[] = {
    { CC.CC_GTEN, MIDI_CH_AUTO_GATE,     "CC_GTEN" },
    { CC.CC_VELM, MIDI_CH_VELOCITY_MODE, "CC_VELM" },
  };
  for (size_t i = 0; i < count_of(toggles); i++) {
    if (cc != toggles[i].cc) continue;
    bool was = (ch->flags & toggles[i].flag_bit) != 0;
    bool now = (value == 127 ? true : value == 0 ? false : !was);
    if (now) ch->flags = (uint8_t)(ch->flags | toggles[i].flag_bit);
    else ch->flags = (uint8_t)(ch->flags & (uint8_t)~toggles[i].flag_bit);
    usMIDI("[%s] ch%d From %d To %d\n", toggles[i].name, channel, was, now);
    return;
  }

  if (cc == CC.CC_SPLY) {
    /* Lighter than CC_MONO/CC_POLY below: changes polyphony without
     * silencing what the channel is already holding. */
    uint8_t full = mask_popcount(midi_channel_effective_mask(channel));
    bool was_poly = (ch->poly_limit > 1);
    bool now_poly = (value == 127 ? true : value == 0 ? false : !was_poly);
    ch->poly_limit = (uint8_t)(now_poly ? full : 1);
    usMIDI("[CC_SPLY] ch%d poly_limit -> %d\n", channel, ch->poly_limit);
    return;
  }

  if (cc == CC.CC_MONO || cc == CC.CC_POLY) {
    uint8_t full = mask_popcount(midi_channel_effective_mask(channel));
    ch->poly_limit = (cc == CC.CC_MONO) ? 1 : full;
    usMIDI("[%s] ch%d poly_limit -> %d, silencing this channel's held notes\n",
          (cc == CC.CC_MONO ? "CC_MONO" : "CC_POLY"), channel, ch->poly_limit);
    release_channel_voices(channel);
    return;
  }

  return;
}

/**
 * @brief Handler for CC_ASOF, CC_RACT and CC_ANOF: silence and release every voice in the whole pool
 *
 * Device-wide panic buttons, not scoped to the sending channel: clears
 * every gated voice regardless of which channel owns it, always forcing
 * the gate off, and also silences any FMOpl-targeted channel via
 * midi_fmopl_all_notes_off().
 *
 * @param uint8_t channel, unused
 * @param uint8_t cc, unused
 * @param uint8_t value, unused
 */
static void all_notes_off(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)channel; (void)cc; (void)value;
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (midi_voice_state(i) != MIDI_VOICE_GATED) continue;
    uint8_t addr = (uint8_t)(midi_voice_sidbase(i) + midi_voice_regbase(i) + CONTR);
    unset_bit_at(&sid_memory[addr], BIT_0);
    midi_bus_operation(addr, sid_memory[addr]);
    midi_voice_release(i);
  }
  midi_fmopl_all_notes_off();  /* covers any FMOpl-targeted channel too (midi_fmopl.c) */
  return;
}

/**
 * @brief Silence and release every voice held by one channel
 *
 * Channel-scoped, used by CC_MONO/CC_POLY to give a clean slate on a mode
 * switch without silencing any other channel.
 *
 * @param uint8_t channel
 */
static void release_channel_voices(uint8_t channel)
{
  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (midi_voice_state(i) != MIDI_VOICE_GATED || midi_voice_channel(i) != channel) continue;
    uint8_t addr = (uint8_t)(midi_voice_sidbase(i) + midi_voice_regbase(i) + CONTR);
    unset_bit_at(&sid_memory[addr], BIT_0);
    midi_bus_operation(addr, sid_memory[addr]);
    midi_voice_release(i);
  }
  return;
}

/**
 * @brief Route channel aftertouch pressure to its configured destination
 *
 * Ignored for a channel targeting FMOpl. Otherwise applies pressure to
 * filter cutoff (CC_FFC) or master volume (CC_VOL) depending on
 * at_target, or drives LFO1 depth directly for a vibrato-style target.
 *
 * @param uint8_t channel
 * @param uint8_t pressure
 */
static void handle_aftertouch(uint8_t channel, uint8_t pressure)
{
  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) return;

  midi_channel_cfg_t *ch = &midi_channels[channel];
  midi_aftertouch_target_t t = (midi_aftertouch_target_t)ch->at_target;
  switch (t) {
    case MIDI_AT_FILTER:  set_filtercutoff(channel, CC.CC_FFC, pressure); break; /* expressive performance tool */
    case MIDI_AT_VOLUME:  set_modevolume(channel, CC.CC_VOL, pressure);   break; /* tremolo */
    case MIDI_AT_VIBRATO:
      /* Pressure drives LFO depth directly; lfo_dest is whatever CC_LFOT
       * last set, so real vibrato needs lfo_dest set to pitch first. */
      ch->lfo_depth = pressure;
      break;
  }
  return;
}

/**
 * @brief Scale a held voice's attack/decay and sustain/release nibbles by note velocity
 *
 * No-op if velocity is 0 or MIDI_CH_VELOCITY_MODE is off. High velocity
 * shortens decay (punchy), low velocity lengthens it (soft); sustain
 * scales alongside decay since sustain, not decay, controls a held SID
 * voice's loudness. Floored at 3 so the softest velocity is quiet, not
 * silent.
 *
 * @param uint8_t channel
 * @param uint8_t slot
 * @param uint8_t velocity
 */
static void handle_velocity(uint8_t channel, uint8_t slot, uint8_t velocity)
{
  if (velocity == 0) return;
  if (!(midi_channels[channel].flags & MIDI_CH_VELOCITY_MODE)) return;

  uint8_t vel_dec = MAP(velocity, 1, 127, 14, 0);
  uint8_t addr_ad = (uint8_t)(midi_voice_sidbase(slot) + midi_voice_regbase(slot) + ATTDEC);
  set_nibble_at(&sid_memory[addr_ad], vel_dec, L_NIBBLE);
  midi_bus_operation(addr_ad, sid_memory[addr_ad]);

  uint8_t vel_sus = MAP(velocity, 1, 127, 3, NIBBLE_MAX);
  uint8_t addr_sr = (uint8_t)(midi_voice_sidbase(slot) + midi_voice_regbase(slot) + SUSREL);
  set_nibble_at(&sid_memory[addr_sr], vel_sus, R_NIBBLE);
  midi_bus_operation(addr_sr, sid_memory[addr_sr]);
  return;
}

/* --- Modulation: portamento step size, and the per-channel LFO -----------
 *     Both are driven once per 1ms tick by midi_tick(). ------------------ */

/**
 * @brief Per-tick portamento step, in note-index*256 units
 *
 * `porta_time` 1 (fastest) covers a full semitone in 4 ticks (4ms,
 * effectively instant); 127 (slowest) covers one in 256 ticks (256ms), so a
 * full octave glide takes a bit over 3 seconds. Deliberately simple linear
 * mapping - tunable later without any caller needing to change.
 */
static int32_t porta_step_amount(uint8_t porta_time)
{
  if (porta_time == 0) return 256; /* not used when off; avoids a 0-length step if ever called */
  return (int32_t)MAP(porta_time, 1, 127, 64, 1);
}

/**
 * @brief Advance one LFO's phase by one tick and return its depth-scaled
 *        output
 *
 * Parameterised over an explicit (wave, rate, depth, phase, sh_seed,
 * sh_value) tuple so lfo_step()/lfo2_step() below can share one
 * implementation for LFO1 and LFO2.
 *
 * @return int16_t -1000..+1000 (per-mille), already scaled by depth
 */
static int16_t lfo_step_generic(uint8_t wave, uint8_t rate, uint8_t depth,
  uint16_t *phase, uint32_t *sh_seed, int16_t *sh_value)
{
  /* rate: CC 0-127 -> 0.1-20.0 Hz, free-running (not clock-synced like the
   * arpeggiator). */
  uint16_t rate_dHz = (uint16_t)MAP(rate, 0, 127, 1, 200);
  uint32_t inc = ((uint32_t)65536u * rate_dHz) / 10000u;  /* phase step per 1kHz tick */
  uint16_t old_phase = *phase;
  *phase = (uint16_t)(*phase + inc);
  bool wrapped = (*phase < old_phase);

  int16_t raw;
  switch (wave) {
    case MIDI_LFO_TRI:
      raw = (*phase < 32768)
        ? (int16_t)(((int32_t)*phase * 2000 / 32768) - 1000)
        : (int16_t)(1000 - (((int32_t)*phase - 32768) * 2000 / 32768));
      break;
    case MIDI_LFO_SAW:
      raw = (int16_t)(((int32_t)*phase * 2000 / 65536) - 1000);
      break;
    case MIDI_LFO_SQUARE:
      raw = (*phase < 32768) ? -1000 : 1000;
      break;
    case MIDI_LFO_SH:
      if (wrapped) {
        /* Cheap LCG, reseeded per channel at init so channels do not lock
         * step. A new value is drawn once per cycle, held for the rest of
         * it - that is the "hold" half of sample & hold. */
        *sh_seed = *sh_seed * 1103515245u + 12345u;
        *sh_value = (int16_t)(((*sh_seed >> 16) % 2001)) - 1000;
      }
      raw = *sh_value;
      break;
    default:
      raw = 0;
      break;
  }

  return (int16_t)(((int32_t)raw * depth) / 127);
}

/**
 * @brief Advance a channel's LFO1 by one tick and return its depth-scaled output
 *
 * Thin wrapper around lfo_step_generic() bound to the channel's LFO1 state.
 *
 * @param midi_channel_cfg_t * ch
 * @return int16_t -1000..+1000 (per-mille)
 */
static inline int16_t lfo_step(midi_channel_cfg_t *ch)
{
  return lfo_step_generic(ch->lfo_wave, ch->lfo_rate, ch->lfo_depth,
    &ch->lfo_phase, &ch->lfo_sh_seed, &ch->lfo_sh_value);
}

/**
 * @brief Advance a channel's LFO2 by one tick and return its depth-scaled output
 *
 * Thin wrapper around lfo_step_generic() bound to the channel's LFO2 state.
 *
 * @param midi_channel_cfg_t * ch
 * @return int16_t -1000..+1000 (per-mille)
 */
static inline int16_t lfo2_step(midi_channel_cfg_t *ch)
{
  return lfo_step_generic(ch->lfo2_wave, ch->lfo2_rate, ch->lfo2_depth,
    &ch->lfo2_phase, &ch->lfo2_sh_seed, &ch->lfo2_sh_value);
}

/**
 * @brief Apply an LFO offset to PWM, on every voice `channel` holds
 *
 * Offset adds to the channel's tmpl_pwmlo/tmpl_pwmhi base and writes
 * straight to the live register, skipped if unchanged.
 */
static void apply_lfo_pwm(uint8_t channel, int16_t raw_permille)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  int32_t base_pwm = ((int32_t)ch->tmpl_pwmhi << SHIFT_8) | ch->tmpl_pwmlo;
  int32_t offset = ((int32_t)raw_permille * TRIPLE_NIBBLE) / 2000;  /* +-half range at full excursion */
  int32_t pwm = base_pwm + offset;
  if (pwm < 0) pwm = 0;
  if (pwm > TRIPLE_NIBBLE) pwm = TRIPLE_NIBBLE;
  uint8_t Plo = (uint8_t)(pwm & BYTE);
  uint8_t Phi = (uint8_t)((pwm & NIBBLE_3) >> SHIFT_8);

  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (midi_voice_state(i) != MIDI_VOICE_GATED || midi_voice_channel(i) != channel) continue;
    uint8_t base = (uint8_t)(midi_voice_sidbase(i) + midi_voice_regbase(i));
    if (sid_memory[base+PWMLO] != Plo) { sid_memory[base+PWMLO] = Plo; midi_bus_operation(base+PWMLO, Plo); }
    if (sid_memory[base+PWMHI] != Phi) { sid_memory[base+PWMHI] = Phi; midi_bus_operation(base+PWMHI, Phi); }
  }
  return;
}

/**
 * @brief Apply an LFO offset to filter cutoff, on every SID in the
 *        channel's mask
 *
 * Modulates around `filter_cutoff` (last CC_FFC value), not the live
 * register, or the offset would accumulate every tick instead of
 * oscillating around a fixed centre.
 */
static void apply_lfo_cutoff(uint8_t channel, int16_t raw_permille)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  int32_t offset = ((int32_t)raw_permille * CUTOFF_MAX) / 2000;
  int32_t cutoff = (int32_t)ch->filter_cutoff + offset;
  if (cutoff < 0) cutoff = 0;
  if (cutoff > CUTOFF_MAX) cutoff = CUTOFF_MAX;
  uint8_t Clo = (uint8_t)(cutoff & F_MASK_LO);
  uint8_t Chi = (uint8_t)(((uint32_t)cutoff & F_MASK_HI) >> SHIFT_3);

  uint8_t bases[MAX_SIDS];
  uint8_t n = channel_sid_bases(channel, bases);
  for (uint8_t i = 0; i < n; i++) {
    if (sid_memory[bases[i]+FC_HI] != Chi) { sid_memory[bases[i]+FC_HI] = Chi; midi_bus_operation(bases[i]+FC_HI, Chi); }
    if (sid_memory[bases[i]+FC_LO] != Clo) { sid_memory[bases[i]+FC_LO] = Clo; midi_bus_operation(bases[i]+FC_LO, Clo); }
  }
  return;
}

/**
 * @brief Stamp one voice slot with the channel's current timbre, apply the
 *        given pitch, and gate it - the per-slot body of note_on()
 *
 * Factored out so a unison note-on can call this 3 times (once per
 * claimed oscillator, each with its own detuned start/target).
 */
static void note_on_slot(uint8_t channel, uint8_t slot, int32_t start_x256, int32_t target_x256, uint8_t velocity)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  midi_voice_set_pitch(slot, start_x256, target_x256);

  uint8_t base = (uint8_t)(midi_voice_sidbase(slot) + midi_voice_regbase(slot));
  write_voice_pitch(slot, 0);

  /* Stamp the channel's current timbre onto this voice, gate bit off first
   * so auto_gate (or CC_GATE, manually) is what turns it on. */
  sid_memory[base+CONTR]  = (uint8_t)(ch->tmpl_contr & (uint8_t)~BIT_0);
  midi_bus_operation(base+CONTR, sid_memory[base+CONTR]);
  sid_memory[base+ATTDEC] = ch->tmpl_attdec;
  midi_bus_operation(base+ATTDEC, sid_memory[base+ATTDEC]);
  sid_memory[base+SUSREL] = ch->tmpl_susrel;
  midi_bus_operation(base+SUSREL, sid_memory[base+SUSREL]);
  sid_memory[base+PWMLO]  = ch->tmpl_pwmlo;
  midi_bus_operation(base+PWMLO, sid_memory[base+PWMLO]);
  sid_memory[base+PWMHI]  = ch->tmpl_pwmhi;
  midi_bus_operation(base+PWMHI, sid_memory[base+PWMHI]);

  handle_velocity(channel, slot, velocity);

  if (ch->flags & MIDI_CH_AUTO_GATE) {
    sid_memory[base+CONTR] = (uint8_t)(sid_memory[base+CONTR] | BIT_0);
    midi_bus_operation(base+CONTR, sid_memory[base+CONTR]);
  }
  return;
}

/**
 * @brief Handle a MIDI note-on for a channel: allocate voice(s) and gate them
 *
 * Redirects to midi_fmopl_note_on() for a channel targeting FMOpl.
 * Applies transpose, computes the portamento glide start/target, then
 * either allocates all 3 unison oscillators (detune spread: centre/up/
 * down) or a single voice, stamping each via note_on_slot().
 *
 * @param uint8_t channel
 * @param uint8_t raw_note
 * @param uint8_t velocity
 */
static void note_on(uint8_t channel, uint8_t raw_note, uint8_t velocity)
{
  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
    midi_fmopl_note_on(channel, raw_note, velocity);
    return;
  }

  midi_channel_cfg_t *ch = &midi_channels[channel];
  int16_t ni = (int16_t)raw_note + ch->transpose;
  uint8_t note_index = (uint8_t)(ni < 0 ? 0 : ni > SCALE_MAX ? SCALE_MAX : ni);
  int32_t target_x256 = (int32_t)note_index << MIDI_PITCH_FRAC_BITS;

  /* Portamento: glide in from the last note this channel triggered, rather
   * than starting flat at the new pitch. `last_target_x256 < 0` means no
   * prior note exists yet on this channel, so the very first note never
   * glides from nowhere. */
  int32_t start_x256 = target_x256;
  if (ch->porta_time > 0 && ch->last_target_x256 >= 0) start_x256 = ch->last_target_x256;

  if (ch->flags & MIDI_CH_UNISON) {
    /* 3 real oscillators, one note. Detune is baked into each oscillator's
     * stored cur/target pitch once, here, not applied live every tick -
     * it's a constant per-oscillator offset from the struck pitch.
     * Position 0 centred, 1 up, 2 down (MBSID's single-spread-value
     * convention). */
    uint8_t slots[MIDI_VOICE_UNISON_COUNT];
    if (midi_voice_alloc_unison(channel, raw_note, velocity, slots) == MIDI_VOICE_NONE) return;
    ch->last_target_x256 = target_x256;

    int32_t spread_x256 = ((int32_t)ch->unison_detune * 256) / 255;  /* 0-255 -> 0..1 semitone x256 at full spread */
    for (uint8_t v = 0; v < MIDI_VOICE_UNISON_COUNT; v++) {
      if (slots[v] == MIDI_VOICE_NONE) continue;
      int32_t detune = (v == 1) ? spread_x256 : (v == 2) ? -spread_x256 : 0;
      note_on_slot(channel, slots[v], start_x256 + detune, target_x256 + detune, velocity);
    }
    return;
  }

  uint8_t slot = midi_voice_alloc(channel, raw_note, velocity);
  if (slot == MIDI_VOICE_NONE) return;  /* channel disabled (empty mask), or steal refused */

  ch->last_target_x256 = target_x256;
  note_on_slot(channel, slot, start_x256, target_x256, velocity);
  return;
}

/**
 * @brief Handle a MIDI note-off for a channel: release the matching voice(s)
 *
 * Redirects to midi_fmopl_note_off() for a channel targeting FMOpl.
 * Otherwise finds every voice slot matching (channel, note): 1 for an
 * ordinary note, up to 3 for a unison note. Applies velocity-scaled
 * release, ungates each voice (if MIDI_CH_AUTO_GATE is set) and frees the
 * slot. No release-phase tracking: a freed slot can be reallocated
 * immediately, which may cut a SID's own hardware envelope release short
 * if reused too soon.
 *
 * @param uint8_t channel
 * @param uint8_t raw_note
 * @param uint8_t velocity
 */
static void note_off(uint8_t channel, uint8_t raw_note, uint8_t velocity)
{
  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
    midi_fmopl_note_off(channel, raw_note, velocity);
    return;
  }

  /* midi_voice_find_all() covers both cases: 1 slot for a non-unison
   * note, all 3 for a unison one, no separate branch needed. */
  uint8_t slots[MIDI_VOICE_UNISON_COUNT];
  uint8_t n = midi_voice_find_all(channel, raw_note, slots);
  if (n == 0) return;

  for (uint8_t k = 0; k < n; k++) {
    uint8_t slot = slots[k];
    handle_velocity(channel, slot, velocity);

    if (midi_channels[channel].flags & MIDI_CH_AUTO_GATE) {
      uint8_t addr = (uint8_t)(midi_voice_sidbase(slot) + midi_voice_regbase(slot) + CONTR);
      unset_bit_at(&sid_memory[addr], BIT_0);
      midi_bus_operation(addr, sid_memory[addr]);
    }

    /* No release-phase tracking yet: the slot can be reallocated instantly,
     * which can cut a SID's hardware envelope release short if reused too
     * soon. Known limitation. */
    midi_voice_release(slot);
  }
  return;
}

/**
 * @brief Handle a MIDI pitch bend message: compute the channel's bend offset and re-apply pitch to every held voice
 *
 * 14 bit bend value, LSB first, both bytes 7 bit, centre 0x2000. Stores
 * the offset on the channel (bend_x256, read by write_voice_pitch()) so
 * it composes with whatever portamento/LFO is doing on the same voices,
 * and applies immediately to every voice the channel holds.
 *
 * @param uint8_t channel
 * @param uint8_t lo
 * @param uint8_t hi
 */
static void pitch_notefrequency(uint8_t channel, uint8_t lo, uint8_t hi)
{
  uint16_t pitch_raw = (uint16_t)(((hi & 0x7F) << SHIFT_7) | (lo & 0x7F));
  midi_channel_cfg_t *ch = &midi_channels[channel];

  int32_t delta = (int32_t)pitch_raw - MIDI_BEND_CENTRE;
  ch->bend_x256 = (delta * (int32_t)ch->bend_range * 256) / MIDI_BEND_CENTRE;

  for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
    if (midi_voice_state(i) != MIDI_VOICE_GATED || midi_voice_channel(i) != channel) continue;
    write_voice_pitch(i, 0);
  }
  return;
}

/* --- Arpeggiator ----------------------------------------------------------
 *
 * Held notes are the arp's *input pattern*, kept in midi_channel_cfg_t,
 * separate from the voice pool: at most one physical voice is ever gated
 * for the arp at a time (arp_voice_slot), regardless of how many keys are
 * held. Note-on/off on an arp-enabled channel add/remove from this pattern
 * instead of allocating a voice directly (see process_midi()'s dispatch);
 * midi_tick() -> arp_tick() steps through the pattern and gates the next
 * note by calling the ordinary note_on()/note_off(), so a stepped note
 * still gets the channel's usual template stamp, auto_gate handling, and
 * everything else a normal note gets, for free.
 * ------------------------------------------------------------------------- */

/**
 * @brief Add a held note to a channel's arpeggiator input pattern
 *
 * Ignores a note already in the pattern. Resets the step position and
 * direction when this is the first held key, so a run starts from a known
 * position rather than wherever a previous, fully-released run left it.
 *
 * @param uint8_t channel
 * @param uint8_t raw_note
 */
static void arp_note_on(uint8_t channel, uint8_t raw_note)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  for (uint8_t i = 0; i < ch->arp_held_count; i++) if (ch->arp_held[i] == raw_note) return;
  if (ch->arp_held_count < MIDI_ARP_MAX_NOTES) {
    ch->arp_held[ch->arp_held_count++] = raw_note;
  }
  if (ch->arp_held_count == 1) {
    /* First key: start the pattern from a known position rather than
     * wherever a previous, fully-released run left it. */
    ch->arp_step = 0;
    ch->arp_step_up = true;
  }
  return;
}

/**
 * @brief Remove a held note from a channel's arpeggiator input pattern
 *
 * Compacts the held-note array when the note is found. When the last held
 * key is released, silences whatever the arp was currently sounding
 * rather than leaving a note ringing with nothing left in the pattern to
 * advance it.
 *
 * @param uint8_t channel
 * @param uint8_t raw_note
 */
static void arp_note_off(uint8_t channel, uint8_t raw_note)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  for (uint8_t i = 0; i < ch->arp_held_count; i++) {
    if (ch->arp_held[i] != raw_note) continue;
    for (uint8_t j = i; (uint8_t)(j + 1) < ch->arp_held_count; j++) ch->arp_held[j] = ch->arp_held[j+1];
    ch->arp_held_count--;
    break;
  }
  if (ch->arp_held_count == 0 && ch->arp_voice_slot != MIDI_VOICE_NONE) {
    /* Last key released: silence whatever the arp was sounding rather than
     * leaving a note ringing with nothing left in the pattern to advance it. */
    uint8_t prev_note = midi_voice_note(ch->arp_voice_slot);
    note_off(channel, prev_note, 0);
    ch->arp_voice_slot = MIDI_VOICE_NONE;
  }
  return;
}

/* Arpeggiator CCs. Kept here rather than with the LFO/portamento CCs above
 * because CC_ARPE's toggle needs note_off(), not declared yet up there. */

/**
 * @brief Set a channel's arpeggiator mode from a CC value
 *
 * Uses bucket division against the full 6-mode range rather than MAP()'s
 * linear interpolation, since MAP() truncates rather than rounds and
 * would leave some modes unreachable at their intended CC value.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_arp_mode(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].arp_mode = (uint8_t)(MIDI_ARP_UP +
    ((uint16_t)value * (MIDI_ARP_TABLE - MIDI_ARP_UP + 1)) / (MIDI_CC_MAX + 1));
  return;
}
/**
 * @brief Set a channel's arpeggiator rate from a CC value
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_arp_rate(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].arp_rate = value;
  return;
}
/**
 * @brief Select which arp_tables[] slot a channel's arpeggiator uses in table mode
 *
 * Independent of arp_mode itself: selecting a table does not switch the
 * channel into table mode, CC_ARPM does that. Uses bucket division against
 * the full 16-slot range for the same reason set_arp_mode() does.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_arp_table(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].arp_table_sel = (uint8_t)(((uint16_t)value * MIDI_ARP_TABLE_COUNT) / (MIDI_CC_MAX + 1));
  return;
}
/**
 * @brief Set a channel's arpeggiator octave-repeat count from a CC value
 *
 * Capped at 3 extra octaves, close to the headroom write_voice_pitch()'s
 * SCALE_MAX clamping leaves before a repeated chord starts flattening at
 * the top of the pattern.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_arp_octaves(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].arp_octaves = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, 0, 3);
  return;
}
/**
 * @brief Enable or disable a channel's arpeggiator from a CC value
 *
 * 127 forces on, 0 forces off, any other value toggles the current state.
 * Disabling silences whatever the arp was sounding and clears the held
 * pattern, so a stray physically-held key does not resume arpeggiating
 * the moment the mode is switched back on.
 *
 * @param uint8_t channel
 * @param uint8_t cc, unused
 * @param uint8_t value
 */
static void set_arp_enable(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channel_cfg_t *ch = &midi_channels[channel];
  bool was = (ch->flags & MIDI_CH_ARP_ENABLED) != 0;
  bool now = (value == 127 ? true : value == 0 ? false : !was);
  if (now == was) return;

  if (now) {
    ch->flags = (uint8_t)(ch->flags | MIDI_CH_ARP_ENABLED);
  } else {
    if (ch->arp_voice_slot != MIDI_VOICE_NONE) {
      uint8_t prev_note = midi_voice_note(ch->arp_voice_slot);
      note_off(channel, prev_note, 0);
      ch->arp_voice_slot = MIDI_VOICE_NONE;
    }
    ch->arp_held_count = 0;
    ch->flags = (uint8_t)(ch->flags & (uint8_t)~MIDI_CH_ARP_ENABLED);
  }
  usMIDI("[CC_ARPE] ch%d arpeggiator %s\n", channel, now ? "on" : "off");
  return;
}

/**
 * @brief Step a channel through its selected arp table instead of
 *        one of the 5 fixed shapes
 *
 * Root note is the first-held key (arp_held[0]) rather than the lowest or
 * most-recently-played held note - GoatTracker's own convention, not yet
 * independently confirmed against real GoatTracker/SidWizard behaviour on
 * hardware. Deliberately ignores arp_octaves: table steps are already fully
 * authored offsets, an octave multiplier on top would be a second degree of
 * freedom the table itself already owns - out of scope for a first pass.
 */
static void arp_advance_table(uint8_t channel)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  const midi_arp_table_t *t = &arp_tables[ch->arp_table_sel];
  if (t->step_count == 0) return;  /* unauthored table: nothing to play */

  uint8_t step = ch->arp_step;
  if (step >= t->step_count) step = 0;  /* also recovers from a stale/out-of-range loop_start */

  int16_t raw_note = (int16_t)ch->arp_held[0] + (int16_t)t->offsets[step];
  if (raw_note < 0) raw_note = 0;
  if (raw_note > 127) raw_note = 127;

  uint8_t next = (uint8_t)(step + 1);
  ch->arp_step = (next >= t->step_count) ? t->loop_start : next;

  if (ch->arp_voice_slot != MIDI_VOICE_NONE) {
    uint8_t prev_note = midi_voice_note(ch->arp_voice_slot);
    note_off(channel, prev_note, 0);
  }
  note_on(channel, (uint8_t)raw_note, 100);  /* fixed velocity, same as the shape-based modes */
  ch->arp_voice_slot = midi_voice_find(channel, (uint8_t)raw_note);
  return;
}

/**
 * @brief Step the arpeggiator through one of its shape-based modes (up/down/up-down/random/as-played) and gate the resulting note
 *
 * Delegates to arp_advance_table() when arp_mode is MIDI_ARP_TABLE. The
 * expanded pattern is held_count * (arp_octaves + 1) long; idx % held_count
 * and idx / held_count map a position back to note and octave.
 *
 * @param uint8_t channel
 */
static void arp_advance(uint8_t channel)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (ch->arp_held_count == 0) return;

  if (ch->arp_mode == MIDI_ARP_TABLE) { arp_advance_table(channel); return; }

  uint8_t pattern_len = (uint8_t)(ch->arp_held_count * (ch->arp_octaves + 1));
  if (pattern_len == 0) return;

  uint8_t idx;
  switch (ch->arp_mode) {
    case MIDI_ARP_DOWN:
      idx = (uint8_t)(pattern_len - 1 - (ch->arp_step % pattern_len));
      ch->arp_step = (uint8_t)((ch->arp_step + 1) % pattern_len);
      break;
    case MIDI_ARP_UPDOWN:
      if (pattern_len == 1) {
        idx = 0;
      } else {
        idx = (uint8_t)(ch->arp_step % pattern_len);
        if (ch->arp_step_up) {
          if ((uint8_t)(ch->arp_step + 1) >= pattern_len) ch->arp_step_up = false;
          else ch->arp_step++;
        } else {
          if (ch->arp_step == 0) ch->arp_step_up = true;
          else ch->arp_step--;
        }
      }
      break;
    case MIDI_ARP_RANDOM:
      /* Reuses the same cheap LCG shape as the LFO's S&H, its own seed */
      ch->lfo_sh_seed = ch->lfo_sh_seed * 1103515245u + 12345u;
      idx = (uint8_t)((ch->lfo_sh_seed >> 16) % pattern_len);
      break;
    case MIDI_ARP_AS_PLAYED:
    case MIDI_ARP_UP:
    default:
      idx = (uint8_t)(ch->arp_step % pattern_len);
      ch->arp_step = (uint8_t)((ch->arp_step + 1) % pattern_len);
      break;
  }

  uint8_t note_slot_idx = (uint8_t)(idx % ch->arp_held_count);
  uint8_t octave_idx    = (uint8_t)(idx / ch->arp_held_count);
  int16_t raw_note = (int16_t)ch->arp_held[note_slot_idx] + (int16_t)(octave_idx * 12);
  if (raw_note > 127) raw_note = 127;

  if (ch->arp_voice_slot != MIDI_VOICE_NONE) {
    uint8_t prev_note = midi_voice_note(ch->arp_voice_slot);
    note_off(channel, prev_note, 0);
  }
  note_on(channel, (uint8_t)raw_note, 100);  /* fixed velocity: the arp step carries no velocity of its own */
  ch->arp_voice_slot = midi_voice_find(channel, (uint8_t)raw_note);
  return;
}

/**
 * @brief Decide whether this channel's arpeggiator should step this tick,
 *        and if so, step it
 *
 * Clock-synced when a MIDI clock is present (arp_rate maps to one of 8
 * coarse PPQN divisions, whole note down to a 32nd), free-running off a
 * phase accumulator identical in shape to the LFO's otherwise.
 */
static void arp_tick(uint8_t channel)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (!(ch->flags & MIDI_CH_ARP_ENABLED)) return;
  if (ch->arp_held_count == 0) return;

  if (midi_clock_present()) {
    static const uint32_t divisions[] = { 24, 12, 8, 6, 4, 3, 2, 1 };
    uint8_t idx = (uint8_t)(((uint32_t)ch->arp_rate * (count_of(divisions) - 1)) / 127);
    uint32_t div = divisions[idx];
    uint32_t total = midi_clock_total_pulses();
    if (total - ch->arp_last_step_pulses >= div) {
      ch->arp_last_step_pulses = total;
      arp_advance(channel);
    }
    return;
  }

  /* Free-running: 0.5-20.0 steps/sec */
  uint16_t rate_dHz = (uint16_t)MAP(ch->arp_rate, 0, 127, 5, 200);
  uint32_t inc = ((uint32_t)65536u * rate_dHz) / 10000u;
  uint16_t old_phase = ch->arp_phase;
  ch->arp_phase = (uint16_t)(ch->arp_phase + inc);
  if (ch->arp_phase < old_phase) arp_advance(channel);
  return;
}

/**
 * @brief The 1kHz modulation tick, called once per pass of the core1 loop
 *        once at least MIDI_TICK_PERIOD_US has elapsed (see midi_engine.c)
 *
 * Per channel: step the arpeggiator, advance each LFO and apply it to its
 * destination, and step portamento for every held voice. write_voice_pitch()
 * no-ops the bus write when nothing changed.
 */
void midi_tick(void)
{
  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    midi_channel_cfg_t *ch = &midi_channels[c];

    /* Arpeggiator may allocate/release a voice; run before the pitch pass
     * below so a freshly stepped note gets this same tick's LFO/portamento
     * applied rather than waiting a full tick period. */
    arp_tick(c);

    /* LFO1 and LFO2 are independently gated on their own depth/dest. If
     * both target the same destination, their per-mille outputs are
     * summed before the one write to it. */
    bool lfo1_pitch  = (ch->lfo_depth > 0 && ch->lfo_dest == MIDI_LFO_DEST_PITCH);
    bool lfo1_pwm    = (ch->lfo_depth > 0 && ch->lfo_dest == MIDI_LFO_DEST_PWM);
    bool lfo1_cutoff = (ch->lfo_depth > 0 && ch->lfo_dest == MIDI_LFO_DEST_CUTOFF);
    bool lfo2_pitch  = (ch->lfo2_depth > 0 && ch->lfo2_dest == MIDI_LFO_DEST_PITCH);
    bool lfo2_pwm    = (ch->lfo2_depth > 0 && ch->lfo2_dest == MIDI_LFO_DEST_PWM);
    bool lfo2_cutoff = (ch->lfo2_depth > 0 && ch->lfo2_dest == MIDI_LFO_DEST_CUTOFF);

    int32_t lfo_pitch_x256 = 0;
    int16_t pwm_permille = 0;
    int16_t cutoff_permille = 0;

    if (lfo1_pitch || lfo1_pwm || lfo1_cutoff) {
      int16_t raw = lfo_step(ch);  /* advances LFO1 phase once per tick while depth > 0 */
      if (lfo1_pitch)  lfo_pitch_x256 += ((int32_t)raw * MIDI_LFO_PITCH_MAX_SEMITONES * 256) / 1000;
      if (lfo1_pwm)    pwm_permille = (int16_t)(pwm_permille + raw);
      if (lfo1_cutoff) cutoff_permille = (int16_t)(cutoff_permille + raw);
    }
    if (lfo2_pitch || lfo2_pwm || lfo2_cutoff) {
      int16_t raw = lfo2_step(ch);  /* advances LFO2 phase once per tick while depth > 0 */
      if (lfo2_pitch)  lfo_pitch_x256 += ((int32_t)raw * MIDI_LFO_PITCH_MAX_SEMITONES * 256) / 1000;
      if (lfo2_pwm)    pwm_permille = (int16_t)(pwm_permille + raw);
      if (lfo2_cutoff) cutoff_permille = (int16_t)(cutoff_permille + raw);
    }
    bool lfo_pitch = lfo1_pitch || lfo2_pitch;
    if (lfo1_pwm    || lfo2_pwm)    apply_lfo_pwm(c, pwm_permille);
    if (lfo1_cutoff || lfo2_cutoff) apply_lfo_cutoff(c, cutoff_permille);

    for (uint8_t i = 0; i < MIDI_VOICE_COUNT; i++) {
      if (midi_voice_state(i) != MIDI_VOICE_GATED || midi_voice_channel(i) != c) continue;

      int32_t cur = midi_voice_cur_pitch(i);
      int32_t target = midi_voice_target_pitch(i);
      bool moved = false;

      if (cur != target) {
        if (ch->porta_time > 0) {
          int32_t step = porta_step_amount(ch->porta_time);
          if (cur < target) { cur += step; if (cur > target) cur = target; }
          else               { cur -= step; if (cur < target) cur = target; }
        } else {
          cur = target;  /* portamento off (or turned off mid-glide): snap */
        }
        midi_voice_set_pitch(i, cur, target);
        moved = true;
      }

      if (moved || lfo_pitch) write_voice_pitch(i, lfo_pitch ? lfo_pitch_x256 : 0);
    }
  }
  return;
}

/**
 * @brief Bind a CC number to a handler, refusing collisions at runtime
 *
 * A plain runtime check rather than assert(), so a duplicate CC binding
 * is refused the same way on both -DNDEBUG and asserts-on builds.
 *
 * @param const char *name, the CC name, for the log line only
 * @param uint8_t cc, the CC number to bind
 * @param cc_handler_t f_ptr, the handler
 */
static void assign_func_ptr(const char *name, uint8_t cc, cc_handler_t f_ptr)
{
  if __us_unlikely(cc > MIDI_CC_MAX) {
    /* Catches the 0xFF placeholders in the default map, which would otherwise
     * run off the end of cc_func_ptr_array[128] */
    usMIDI("[MIDI][CC] %s is 0x%02X, out of range, not registered\n", name, cc);
    return;
  }
  if __us_unlikely(cc_func_ptr_array[cc] != NULL) {
    usMIDI("[MIDI][CC] %s wants CC %d which is already bound, refusing\n", name, cc);
    return;
  }
  cc_func_ptr_array[cc] = f_ptr;
  return;
}

/* Passes the CC name through so a refusal names the culprit */
#define ASSIGN_CC(field, fn)  assign_func_ptr(#field, CC.field, fn)

/**
 * @brief Reset the CC dispatch table and bind every supported CC number to its handler
 *
 * Clears cc_func_ptr_array[] and registers each handler via ASSIGN_CC(),
 * grouped by handler-toggle, SID/voice override, per-voice, chip-wide,
 * modulation/timing and fixed-action CCs.
 */
void midi_cc_init(void)
{
  for (int i = 0; i < 128; i++) {
    cc_func_ptr_array[i] = NULL;
  }

  /* Handler settings */
  ASSIGN_CC(CC_GTEN, set_handler);
  ASSIGN_CC(CC_SPLY, set_handler);
  ASSIGN_CC(CC_VELM, set_handler);

  /* SID / voice mask override */
  ASSIGN_CC(CC_SID1, set_sid_override);
  ASSIGN_CC(CC_SID2, set_sid_override);
  ASSIGN_CC(CC_SID3, set_sid_override);
  ASSIGN_CC(CC_SID4, set_sid_override);
  ASSIGN_CC(CC_VCE1, set_voice_override);
  ASSIGN_CC(CC_VCE2, set_voice_override);
  ASSIGN_CC(CC_VCE3, set_voice_override);

  /* Per-voice settings */
  ASSIGN_CC(CC_NOTE, set_notefrequency);
  ASSIGN_CC(CC_PWM, set_voicepwm);
  ASSIGN_CC(CC_NOIS, set_controlreg);
  ASSIGN_CC(CC_PULS, set_controlreg);
  ASSIGN_CC(CC_SAWT, set_controlreg);
  ASSIGN_CC(CC_TRIA, set_controlreg);
  ASSIGN_CC(CC_TEST, set_controlreg);
  ASSIGN_CC(CC_RMOD, set_controlreg);
  ASSIGN_CC(CC_SYNC, set_controlreg);
  ASSIGN_CC(CC_GATE, set_gate);
  ASSIGN_CC(CC_ATT, set_adsr);
  ASSIGN_CC(CC_DEC, set_adsr);
  ASSIGN_CC(CC_SUS, set_adsr);
  ASSIGN_CC(CC_REL, set_adsr);

  /* Chip-wide settings */
  ASSIGN_CC(CC_FFC, set_filtercutoff);
  ASSIGN_CC(CC_RES, set_resfilter);
  ASSIGN_CC(CC_FLT1, set_resfilter);
  ASSIGN_CC(CC_FLT2, set_resfilter);
  ASSIGN_CC(CC_FLT3, set_resfilter);
  ASSIGN_CC(CC_FLTE, set_resfilter);
  ASSIGN_CC(CC_3OFF, set_modevolume);
  ASSIGN_CC(CC_HPF, set_modevolume);
  ASSIGN_CC(CC_BPF, set_modevolume);
  ASSIGN_CC(CC_LPF, set_modevolume);
  ASSIGN_CC(CC_VOL, set_modevolume);

  /* Modulation and timing */
  ASSIGN_CC(CC_LFOW, set_lfo_wave);
  ASSIGN_CC(CC_LFOR, set_lfo_rate);
  ASSIGN_CC(CC_LFOD, set_lfo_depth);
  ASSIGN_CC(CC_LFOT, set_lfo_dest);
  ASSIGN_CC(CC_PORT, set_porta_time);
  ASSIGN_CC(CC_LFO2W, set_lfo2_wave);
  ASSIGN_CC(CC_LFO2R, set_lfo2_rate);
  ASSIGN_CC(CC_LFO2D, set_lfo2_depth);
  ASSIGN_CC(CC_LFO2T, set_lfo2_dest);
  ASSIGN_CC(CC_UNIS, set_unison_enable);
  ASSIGN_CC(CC_UDET, set_unison_detune);
  ASSIGN_CC(CC_ARPM, set_arp_mode);
  ASSIGN_CC(CC_ARPR, set_arp_rate);
  ASSIGN_CC(CC_ARPO, set_arp_octaves);
  ASSIGN_CC(CC_ARPE, set_arp_enable);
  ASSIGN_CC(CC_ARPT, set_arp_table);

  /* Fixed Midi actions */
  ASSIGN_CC(CC_ASOF, all_notes_off);
  ASSIGN_CC(CC_RACT, all_notes_off);
  ASSIGN_CC(CC_ANOF, all_notes_off);
  ASSIGN_CC(CC_MONO, set_handler);
  ASSIGN_CC(CC_POLY, set_handler);

  return;
}

/**
 * @brief Set every configured SID's master volume to an audible default
 *
 * MODVOL's volume nibble has no default of its own: reset_sid() zeroes
 * sid_memory[] at boot, and only CC_VOL/3OFF/HPF/BPF/LPF write MODVOL
 * afterward. Without this, a correct note-on stays inaudible until a
 * CC_VOL message happens to arrive. Filter routing bits are left at 0.
 */
static void apply_default_volume(void)
{
  uint8_t n = (cfg.numsids > MAX_SIDS ? MAX_SIDS : cfg.numsids);
  for (uint8_t s = 0; s < n; s++) {
    uint8_t addr = (uint8_t)(cfg.sidaddr[cfg.ids[s]] + MODVOL);
    sid_memory[addr] = NIBBLE_MAX;  /* volume 15/15, filter routing bits 0 */
    midi_bus_operation(addr, sid_memory[addr]);
  }
  return;
}

/**
 * @brief Initialise (or reset, on MIDI System Reset) the whole MIDI subsystem to its power-up state
 *
 * Sets compiled-in defaults for CC map/channels/voices/patches/arp/FMOpl,
 * rebinds the CC dispatch table, rebuilds the note table, applies default
 * volume, then lets midi_config_load() overwrite defaults from flash.
 * Re-pushes each channel's selected patch's filter fields afterward since
 * midi_config_load() only restores RAM, not hardware registers.
 */
void midi_processor_init(void)
{
  usMIDI("Handler init\n");

  memcpy(&CC, &midi_ccvalues_defaults, sizeof(midi_ccvalues));

  midi_config_init();
  midi_voice_init();
  midi_patch_init();
  midi_arp_table_init();
  midi_fmopl_init();  /* no-ops harmlessly if no FMOpl chip is configured */
  midi_cc_init();
  build_note_table();
  apply_default_volume();
  midi_config_load();

  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    uint8_t patch_index = midi_channels[c].patch;
    if (patch_index != MIDI_PATCH_NONE) apply_patch_filter_hw(c, &midi_patches[patch_index]);
  }

  return;
}

/**
 * @brief Dispatch one Control/Mode Change message to its CC handler
 *
 * CC_FMEN is handled directly regardless of the channel's current FMOpl
 * target state, since it is the CC that sets that state. For a channel
 * already targeting FMOpl, only CC_VOL and CC_PWM are wired live; every
 * other CC is silently ignored. Otherwise dispatches through
 * cc_func_ptr_array[].
 *
 * @param uint8_t channel
 * @param uint8_t * buffer, [0]=CC number, [1]=value
 * @param int size, unused
 */
static void handle_control_change(uint8_t channel, uint8_t *buffer, int size)
{
  (void)size;
  uint8_t cc = buffer[0], value = buffer[1];

  if (cc == CC.CC_FMEN) { midi_fmopl_set_target(channel, value); return; }

  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
    if (cc == CC.CC_VOL) midi_fmopl_set_volume(channel, value);
    else if (cc == CC.CC_PWM) midi_fmopl_set_mod_wheel(channel, value);
    return;
  }

  if (cc_func_ptr_array[cc] != NULL) {
    usMIDI(" [CHANNEL] %02d [CC] 0x%02X -> %02x\n", channel, cc, value);
    cc_func_ptr_array[cc](channel, cc, value);
  }
  return;
}

/**
 * @brief Entry point for one incoming MIDI message: rebuild the note table if needed and dispatch by status byte
 *
 * Handles Control/Mode Change, Note Off, Note On (velocity 0 treated as
 * Note Off), Pitch Bend, Program Change, Channel Pressure and Polyphonic
 * Key Pressure, routing Note On/Off through the arpeggiator or FMOpl when
 * a channel is configured for either.
 *
 * @param uint8_t * buffer
 * @param int size
 */
void process_midi(uint8_t *buffer, int size)
{
  usMVCE("");
  for (int i = 0; i < size; i++) {
    usMDAT("[%d]%02x",i,buffer[i]);
  }
  usMDAT("\n");

  uint8_t channel = (uint8_t)(buffer[0] & 0xF);

  /* Keeps the note table in step with a clock change made after boot */
  check_note_table();

  switch (buffer[0] & 0xF0) { /* Channel 0~16 */
    case 0xB0:  /* Control/Mode Change */
      handle_control_change(channel, (buffer+1), (size-1));
      break;
    case 0x80:  /* Note Off ~ 3-bytes */
      /* FMOpl-targeted channels bypass the arpeggiator: arp+FMOpl
       * together is not implemented yet. */
      if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) note_off(channel, buffer[1], buffer[2]);
      else if (midi_channels[channel].flags & MIDI_CH_ARP_ENABLED) arp_note_off(channel, buffer[1]);
      else note_off(channel, buffer[1], buffer[2]);
      break;
    case 0x90:  /* Note On ~ 3-bytes */
      /* Note On with velocity 0 is a Note Off. Keyboards that use running
       * status send releases this way, so without this notes stick. */
      if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
        if (buffer[2] == 0) note_off(channel, buffer[1], 0);
        else note_on(channel, buffer[1], buffer[2]);
      } else if (midi_channels[channel].flags & MIDI_CH_ARP_ENABLED) {
        if (buffer[2] == 0) arp_note_off(channel, buffer[1]);
        else arp_note_on(channel, buffer[1]);
      } else if (buffer[2] == 0) {
        note_off(channel, buffer[1], 0);
      } else {
        note_on(channel, buffer[1], buffer[2]);
      }
      break;
    case 0xE0:  /* Pitch Bend Change ~ 3-bytes */
      if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) midi_fmopl_pitch_bend(channel, buffer[1], buffer[2]);
      else pitch_notefrequency(channel, buffer[1], buffer[2]);
      break;
    case 0xC0:  /* Program change ~ 2-bytes, buffer[1]=program number */
      /* Only 0-31 map to a patch (MIDI_PATCH_COUNT); Bank Select is not
       * wired, not needed with only 32 patches. */
      if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
        midi_fmopl_program_change(channel, buffer[1]);
      } else if (buffer[1] < MIDI_PATCH_COUNT) apply_patch_to_channel(channel, buffer[1]);
      break;
    case 0xD0:  /* Pressure (Aftertouch) ~ 2-bytes, buffer[1]=pressure */
      handle_aftertouch(channel, buffer[1]);
      break;
    case 0xA0:  /* Polyphonic Key Pressure ~ 3-bytes, buffer[1]=note, buffer[2]=pressure */
      /* Use the same channel-wide handler for now; buffer[1] note could
       * target one specific voice in a future revision. */
      handle_aftertouch(channel, buffer[2]);
      break;
    default:
      break;
  }

  return;
}

/**
 * @brief Copy the current live CC map out to the caller
 *
 * @param midi_ccvalues * out
 */
void midi_handler_get_ccmap(midi_ccvalues *out)
{
  memcpy(out, &CC, sizeof(midi_ccvalues));
  return;
}

/**
 * @brief Replace the current live CC map with the caller's
 *
 * @param const midi_ccvalues * in
 */
void midi_handler_set_ccmap(const midi_ccvalues *in)
{
  memcpy(&CC, in, sizeof(midi_ccvalues));
  return;
}
