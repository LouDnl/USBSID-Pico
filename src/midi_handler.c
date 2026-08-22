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
static void midi_bus_operation(uint8_t a, uint8_t b)
{
  cycled_write_operation(a, b, 0);  /* 0 cycles constant for fast writing */
  return;
}

static inline uint32_t midi_clockrate(void)
{
  /* The configured clock lives on usbsid_config, not on the runtime cfg */
  return (usbsid_config.clock_rate == 0 ? (uint32_t)CLOCK_DEFAULT : usbsid_config.clock_rate);
}

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
  usNFO("[MIDI] Note table built for %u Hz\n", rate);
  return;
}

/* Cheap enough to run on every incoming message and keeps the table honest
 * without config.c having to call into the MIDI handler on a clock change */
static inline void check_note_table(void)
{
  if __us_unlikely(note_table_clock != midi_clockrate()) build_note_table();
  return;
}

/* Bit / nibble helpers, generalised to a target byte pointer.
 * A target is either a live sid_memory[] location (written straight to the
 * bus by the caller afterwards) or a channel's per-voice register template
 * (pushed to every held voice by propagate_reg() afterwards). Neither of
 * these helpers writes the bus itself. */
static inline void set_bit_at(uint8_t *target, int bit)
{
  *target = (uint8_t)(*target | bit);
  return;
}
static inline void unset_bit_at(uint8_t *target, int bit)
{
  *target = (uint8_t)(*target & ~bit);
  return;
}
static inline void toggle_bit_at(uint8_t *target, int bit)
{
  *target = (uint8_t)((~(*target & bit) & bit) | (*target & ~bit));
  return;
}
static inline void handle_bit_at(uint8_t *target, int bit, uint8_t cc_value)
{
  switch (cc_value) {
    case 127: set_bit_at(target, bit); break;
    case 0:   unset_bit_at(target, bit); break;
    default:  toggle_bit_at(target, bit); break;
  }
  return;
}
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
 * Replaces Phase 0's poly_propagate(), which walked the 3 voices of one
 * fixed active SID. This walks the whole pool and matches on ownership
 * instead, since a channel's held voices can now be spread across SIDs and
 * come and go per note rather than sitting at fixed positions.
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
 * The single point where every source that wants to move a voice's pitch
 * converges: the portamento glide position (midi_voice_cur_pitch(), stepped
 * toward its target once per tick by midi_tick()), the owning channel's
 * current pitch-bend offset (bend_x256, updated immediately on a bend
 * message), and, if the caller is applying one, an LFO offset on top. All
 * three are expressed in the same note-index*256 fixed point space so they
 * simply add. Skips the bus write when the computed register value already
 * matches sid_memory[], which is the tick's "flush only changed registers"
 * requirement for free, using sid_memory[] itself as the cache instead of
 * a second "last written" value to keep in sync.
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

/* Manual gate toggle. Deliberately not part of the template: a held drone
 * gated open manually should not be silently re-gated by the next CC that
 * happens to touch CONTR on the same voice. */
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

/* CC_NOTE: manual pitch entry. A one-shot nudge on whatever the channel is
 * currently holding, snapped in (no glide) through the same pitch machinery
 * as everything else, so a subsequent tick's LFO/portamento pass does not
 * silently overwrite it with a stale target. */
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

static void set_filtercutoff(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  uint16_t cutoff = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, CUTOFF_MAX);
  /* Remembered so the LFO-CUTOFF tick has a stable centre to modulate
   * around instead of the live register, which it also writes. */
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
 * @brief Apply a stored patch to a channel: Program Change's whole job
 *
 * Loads the patch's per-voice template fields into the channel (so the
 * next note-on, and every voice the channel already holds via
 * propagate_reg(), reflects it immediately - exactly what a live CC change
 * to the same fields would do), writes filter cutoff/resonance/routing
 * straight to every SID in the channel's mask (chip-wide registers, same
 * as CC_FFC/CC_RES/CC_FLT1-3/CC_FLTE), and sets the lfo/arp fields plus bend_range.
 * Deliberately does not touch anything Program Change has no business
 * touching: voice_mask, poly_limit, steal_mode, transpose, sid/voice
 * overrides, or any live runtime state (arp_held[], lfo_phase, ...) - a
 * patch is a timbre, not a routing change.
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

  ch->filter_cutoff = p->filter_cutoff;
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

  ch->lfo_wave    = p->lfo_wave;
  ch->lfo_rate    = p->lfo_rate;
  ch->lfo_depth   = p->lfo_depth;
  ch->lfo_dest    = p->lfo_dest;
  ch->arp_mode    = p->arp_mode;
  ch->arp_rate    = p->arp_rate;
  ch->arp_octaves = p->arp_octaves;
  ch->bend_range  = p->bend_range;
  ch->patch       = patch_index;

  usNFO("[PATCH] ch%d -> patch %d\n", channel, patch_index);
  return;
}

/* --- CC_SID1-4 / CC_VCE1-3: manual override that narrows the configured
 *     voice_mask at runtime, without altering the configured mask itself.
 *     Value 0 clears the override; anything else selects it, so an old
 *     controller sending only "select" (typically 127) behaves exactly as
 *     it always has. --------------------------------------------------- */

static void set_sid_override(uint8_t channel, uint8_t cc, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (value == 0) { ch->sid_override = MIDI_OVERRIDE_NONE; return; }
  if (cc == CC.CC_SID1) ch->sid_override = 0;
  else if (cc == CC.CC_SID2) ch->sid_override = 1;
  else if (cc == CC.CC_SID3) ch->sid_override = 2;
  else if (cc == CC.CC_SID4) ch->sid_override = 3;
  usMVCE("[SID OVERRIDE] ch%d -> %d\n", channel, ch->sid_override);
  return;
}

static void set_voice_override(uint8_t channel, uint8_t cc, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (value == 0) { ch->voice_override = MIDI_OVERRIDE_NONE; return; }
  if (cc == CC.CC_VCE1) ch->voice_override = 0;
  else if (cc == CC.CC_VCE2) ch->voice_override = 1;
  else if (cc == CC.CC_VCE3) ch->voice_override = 2;
  usMVCE("[VOICE OVERRIDE] ch%d -> %d\n", channel, ch->voice_override);
  return;
}

/* --- LFO and portamento CCs. Arpeggiator CCs are further down, next to
 *     the arpeggiator's own functions, since enabling/disabling it needs
 *     note_off() which is not declared yet at this point in the file. --- */

static void set_lfo_wave(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_wave = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, MIDI_LFO_TRI, MIDI_LFO_SH);
  return;
}
static void set_lfo_rate(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_rate = value;
  return;
}
static void set_lfo_depth(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_depth = value;
  return;
}
static void set_lfo_dest(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].lfo_dest = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, MIDI_LFO_DEST_PITCH, MIDI_LFO_DEST_CUTOFF);
  return;
}
static void set_porta_time(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].porta_time = value;
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
    usNFO("[%s] ch%d From %d To %d\n", toggles[i].name, channel, was, now);
    return;
  }

  /* CC_CVCE/CC_CSID/CC_LVCE/CC_LSID: the old "copy/link voice or SID"
   * family existed to copy a manually selected active voice's timbre onto
   * its siblings. The flat pool has no single active voice: every channel
   * already carries one template that propagate_reg() pushes to every
   * voice it holds on every CC change. There is nothing left for these to
   * do. Kept bound, as documented no-ops, so the CC numbers stay reserved
   * and a controller still sending them gets a log line instead of
   * silently landing on whatever handler claims the number next. */
  if (cc == CC.CC_CVCE || cc == CC.CC_CSID || cc == CC.CC_LVCE || cc == CC.CC_LSID) {
    usNFO("[MIDI][CC] %d is deprecated, superseded by automatic per-channel voice templates\n", cc);
    return;
  }

  if (cc == CC.CC_SPLY) {
    /* Lighter than CC_MONO/CC_POLY below: changes polyphony without
     * silencing what the channel is already holding. */
    uint8_t full = mask_popcount(midi_channel_effective_mask(channel));
    bool was_poly = (ch->poly_limit > 1);
    bool now_poly = (value == 127 ? true : value == 0 ? false : !was_poly);
    ch->poly_limit = (uint8_t)(now_poly ? full : 1);
    usNFO("[CC_SPLY] ch%d poly_limit -> %d\n", channel, ch->poly_limit);
    return;
  }

  if (cc == CC.CC_MONO || cc == CC.CC_POLY) {
    uint8_t full = mask_popcount(midi_channel_effective_mask(channel));
    ch->poly_limit = (cc == CC.CC_MONO) ? 1 : full;
    usNFO("[%s] ch%d poly_limit -> %d, silencing this channel's held notes\n",
          (cc == CC.CC_MONO ? "CC_MONO" : "CC_POLY"), channel, ch->poly_limit);
    release_channel_voices(channel);
    return;
  }

  return;
}

/* Pool-wide: CC_ASOF (All Sound Off), CC_RACT (Reset All Controllers) and
 * CC_ANOF (All Notes Off) are device-wide panic buttons, not scoped to the
 * sending channel, so this clears every voice on the whole pool regardless
 * of which channel owns it, always forcing the gate off. */
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
  midi_fmopl_all_notes_off();  /* covers any FMOpl-targeted channel too, TODO 14 */
  return;
}

/* Channel-scoped: used by CC_MONO/CC_POLY to give a clean slate on a mode
 * switch without silencing any other channel. */
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

static void handle_aftertouch(uint8_t channel, uint8_t pressure)
{
  /* TODO 14: not wired for FMOpl yet - both branches below write straight
   * to SID registers via channel_sid_bases(), which has no notion of "skip
   * the FMOpl slot", so this channel is excluded here instead. */
  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) return;

  midi_channel_cfg_t *ch = &midi_channels[channel];
  midi_aftertouch_target_t t = (midi_aftertouch_target_t)ch->at_target;
  switch (t) {
    case MIDI_AT_FILTER:  set_filtercutoff(channel, CC.CC_FFC, pressure); break; /* expressive performance tool */
    case MIDI_AT_VOLUME:  set_modevolume(channel, CC.CC_VOL, pressure);   break; /* tremolo */
    case MIDI_AT_VIBRATO:
      /* Now reachable: pressure drives LFO depth directly. lfo_dest is left
       * as whatever CC_LFOT last set; a player wanting real vibrato sets it
       * to pitch first, same as they would set up any other LFO target. */
      ch->lfo_depth = pressure;
      break;
  }
  return;
}

static void handle_velocity(uint8_t channel, uint8_t slot, uint8_t velocity)
{
  if (velocity == 0) return;
  /* Default is no scaling. See Phase 0: the old default wrote velocity into
   * the master volume nibble on every note, stomping CC 7. */
  if (!(midi_channels[channel].flags & MIDI_CH_VELOCITY_MODE)) return;

  /* High velocity = short decay (punchy); low velocity = long decay (soft) */
  uint8_t vel_dec = MAP(velocity, 1, 127, 14, 0);
  uint8_t addr = (uint8_t)(midi_voice_sidbase(slot) + midi_voice_regbase(slot) + ATTDEC);
  set_nibble_at(&sid_memory[addr], vel_dec, L_NIBBLE);
  midi_bus_operation(addr, sid_memory[addr]);
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
 * @brief Advance one channel's LFO phase by one tick and return its
 *        depth-scaled output
 *
 * @param midi_channel_cfg_t *ch
 * @return int16_t -1000..+1000 (per-mille), already scaled by lfo_depth
 */
static int16_t lfo_step(midi_channel_cfg_t *ch)
{
  /* rate: CC 0-127 -> 0.1-20.0 Hz (tenths of Hz), free-running - LFO rate is
   * not clock-synced, unlike the arpeggiator, since a wobbling vibrato
   * locked to tempo is not how anyone actually wants a synth LFO to behave */
  uint16_t rate_dHz = (uint16_t)MAP(ch->lfo_rate, 0, 127, 1, 200);
  uint32_t inc = ((uint32_t)65536u * rate_dHz) / 10000u;  /* phase step per 1kHz tick */
  uint16_t old_phase = ch->lfo_phase;
  ch->lfo_phase = (uint16_t)(ch->lfo_phase + inc);
  bool wrapped = (ch->lfo_phase < old_phase);

  int16_t raw;
  switch (ch->lfo_wave) {
    case MIDI_LFO_TRI:
      raw = (ch->lfo_phase < 32768)
        ? (int16_t)(((int32_t)ch->lfo_phase * 2000 / 32768) - 1000)
        : (int16_t)(1000 - (((int32_t)ch->lfo_phase - 32768) * 2000 / 32768));
      break;
    case MIDI_LFO_SAW:
      raw = (int16_t)(((int32_t)ch->lfo_phase * 2000 / 65536) - 1000);
      break;
    case MIDI_LFO_SQUARE:
      raw = (ch->lfo_phase < 32768) ? -1000 : 1000;
      break;
    case MIDI_LFO_SH:
      if (wrapped) {
        /* Cheap LCG, reseeded per channel at init so channels do not lock
         * step. A new value is drawn once per cycle, held for the rest of
         * it - that is the "hold" half of sample & hold. */
        ch->lfo_sh_seed = ch->lfo_sh_seed * 1103515245u + 12345u;
        ch->lfo_sh_value = (int16_t)(((ch->lfo_sh_seed >> 16) % 2001)) - 1000;
      }
      raw = ch->lfo_sh_value;
      break;
    default:
      raw = 0;
      break;
  }

  return (int16_t)(((int32_t)raw * ch->lfo_depth) / 127);
}

/**
 * @brief Apply an LFO offset to PWM, on every voice `channel` holds
 *
 * Unlike pitch, PWM has no per-channel "current bend" to stack on top of:
 * the offset is simply added to the channel's own tmpl_pwmlo/tmpl_pwmhi
 * (the CC-set base) and written straight to the live register, skipped if
 * unchanged from what is already there.
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
 * Modulates around `filter_cutoff` (the last CC_FFC value), not around the
 * live register, since the live register is what this function itself
 * writes - reading it back as the "base" would let the offset accumulate
 * every tick instead of oscillating around a fixed centre.
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

static void note_on(uint8_t channel, uint8_t raw_note, uint8_t velocity)
{
  /* TODO 14: a channel targeting the FMOpl chip never touches the SID pool
   * at all - entirely separate note path, see midi_fmopl.c. */
  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
    midi_fmopl_note_on(channel, raw_note, velocity);
    return;
  }

  uint8_t slot = midi_voice_alloc(channel, raw_note, velocity);
  if (slot == MIDI_VOICE_NONE) return;  /* channel disabled (empty mask), or steal refused */

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
  midi_voice_set_pitch(slot, start_x256, target_x256);
  ch->last_target_x256 = target_x256;

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

static void note_off(uint8_t channel, uint8_t raw_note, uint8_t velocity)
{
  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
    midi_fmopl_note_off(channel, raw_note, velocity);
    return;
  }

  uint8_t slot = midi_voice_find(channel, raw_note);
  if (slot == MIDI_VOICE_NONE) return;

  handle_velocity(channel, slot, velocity);

  if (midi_channels[channel].flags & MIDI_CH_AUTO_GATE) {
    uint8_t addr = (uint8_t)(midi_voice_sidbase(slot) + midi_voice_regbase(slot) + CONTR);
    unset_bit_at(&sid_memory[addr], BIT_0);
    midi_bus_operation(addr, sid_memory[addr]);
  }

  /* No release-phase tracking yet (that is Phase 4's VOICE_RELEASING
   * state): the slot is free to be reallocated the instant a new note
   * needs it, which can cut a SID's own hardware envelope release short if
   * reused too soon. The old model had no timed release tracking either;
   * this is a documented limitation, not a regression. */
  midi_voice_release(slot);
  return;
}

static void pitch_notefrequency(uint8_t channel, uint8_t lo, uint8_t hi)
{
  /* 14 bit, LSB first, both bytes are 7 bit. Centre is 0x2000, full is 0x3FFF.
   * Stores the offset on the channel (write_voice_pitch() reads it), rather
   * than computing a one-off frequency here, so it composes correctly with
   * whatever the tick is doing for portamento/LFO on the same voices - a
   * bend message is no longer the only thing allowed to move a voice's
   * pitch. Still writes immediately for responsiveness rather than waiting
   * for the next tick; applies to every voice the channel currently holds,
   * matching the MIDI spec (channel pitch bend affects every sounding note
   * on that channel), unlike the old single "active voice" model. */
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

static void set_arp_mode(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].arp_mode = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, MIDI_ARP_UP, MIDI_ARP_AS_PLAYED);
  return;
}
static void set_arp_rate(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  midi_channels[channel].arp_rate = value;
  return;
}
static void set_arp_octaves(uint8_t channel, uint8_t cc, uint8_t value)
{
  (void)cc;
  /* Capped at 3 extra octaves: a held chord repeated 4 times over is
   * already close to the top of what 12 semitones*4 leaves headroom for
   * before note_index clamping in write_voice_pitch() starts flattening
   * the top of the pattern against SCALE_MAX. */
  midi_channels[channel].arp_octaves = (uint8_t)MAP(value, MIN_VAL, MIDI_CC_MAX, 0, 3);
  return;
}
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
    /* Leaving arp mode: silence whatever it was sounding and clear the
     * held pattern, so a stray key still physically held does not resume
     * arpeggiating the moment the mode is switched back on. */
    if (ch->arp_voice_slot != MIDI_VOICE_NONE) {
      uint8_t prev_note = midi_voice_note(ch->arp_voice_slot);
      note_off(channel, prev_note, 0);
      ch->arp_voice_slot = MIDI_VOICE_NONE;
    }
    ch->arp_held_count = 0;
    ch->flags = (uint8_t)(ch->flags & (uint8_t)~MIDI_CH_ARP_ENABLED);
  }
  usNFO("[CC_ARPE] ch%d arpeggiator %s\n", channel, now ? "on" : "off");
  return;
}

/**
 * @brief Step the arpeggiator one position and gate the resulting note
 *
 * The expanded pattern is `held_count * (arp_octaves + 1)` long: the held
 * notes in playing order, repeated once per extra octave. Index arithmetic
 * (`idx % held_count`, `idx / held_count`) maps a position in that expanded
 * pattern back to which held note and which octave, so MIDI_ARP_UP simply
 * counting 0..pattern_len-1 automatically plays the chord once per octave
 * before wrapping - the classic arpeggiator octave-repeat shape.
 */
static void arp_advance(uint8_t channel)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  if (ch->arp_held_count == 0) return;

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
 * Per channel: step the arpeggiator, advance the LFO and apply it to
 * whichever destination it targets, and step portamento for every voice
 * the channel holds. Pitch (portamento and/or LFO-pitch) is written through
 * write_voice_pitch(), which no-ops the bus write when nothing actually
 * changed, so an idle channel with LFO depth 0 and no glide in progress
 * costs nothing beyond the loop overhead.
 */
void midi_tick(void)
{
  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    midi_channel_cfg_t *ch = &midi_channels[c];

    /* Arpeggiator may allocate/release a voice; run before the pitch pass
     * below so a freshly stepped note gets this same tick's LFO/portamento
     * applied rather than waiting a full tick period. */
    arp_tick(c);

    bool lfo_pitch  = (ch->lfo_depth > 0 && ch->lfo_dest == MIDI_LFO_DEST_PITCH);
    bool lfo_pwm    = (ch->lfo_depth > 0 && ch->lfo_dest == MIDI_LFO_DEST_PWM);
    bool lfo_cutoff = (ch->lfo_depth > 0 && ch->lfo_dest == MIDI_LFO_DEST_CUTOFF);
    int32_t lfo_pitch_x256 = 0;

    if (lfo_pitch || lfo_pwm || lfo_cutoff) {
      int16_t raw = lfo_step(ch);  /* advances phase once per channel per tick, regardless of dest */
      if (lfo_pitch)  lfo_pitch_x256 = ((int32_t)raw * MIDI_LFO_PITCH_MAX_SEMITONES * 256) / 1000;
      if (lfo_pwm)    apply_lfo_pwm(c, raw);
      if (lfo_cutoff) apply_lfo_cutoff(c, raw);
    }

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
 * This used to be an `assert()`. `-DNDEBUG` deletes that, so on every shipped
 * build a duplicate CC number silently overwrote the earlier binding and the
 * shadowed handler became unreachable, while an asserts-on build panicked at
 * boot. A plain runtime check behaves the same in both.
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
    usNFO("[MIDI][CC] %s is 0x%02X, out of range, not registered\n", name, cc);
    return;
  }
  if __us_unlikely(cc_func_ptr_array[cc] != NULL) {
    usNFO("[MIDI][CC] %s wants CC %d which is already bound, refusing\n", name, cc);
    return;
  }
  cc_func_ptr_array[cc] = f_ptr;
  return;
}

/* Passes the CC name through so a refusal names the culprit */
#define ASSIGN_CC(field, fn)  assign_func_ptr(#field, CC.field, fn)

void midi_cc_init(void)
{
  for (int i = 0; i < 128; i++) {
    cc_func_ptr_array[i] = NULL;
  }

  /* Handler settings */
  ASSIGN_CC(CC_GTEN, set_handler);
  ASSIGN_CC(CC_SPLY, set_handler);
  ASSIGN_CC(CC_CVCE, set_handler);
  ASSIGN_CC(CC_CSID, set_handler);
  ASSIGN_CC(CC_LVCE, set_handler);
  ASSIGN_CC(CC_LSID, set_handler);
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

  /* Modulation and timing (Phase 4) */
  ASSIGN_CC(CC_LFOW, set_lfo_wave);
  ASSIGN_CC(CC_LFOR, set_lfo_rate);
  ASSIGN_CC(CC_LFOD, set_lfo_depth);
  ASSIGN_CC(CC_LFOT, set_lfo_dest);
  ASSIGN_CC(CC_PORT, set_porta_time);
  ASSIGN_CC(CC_ARPM, set_arp_mode);
  ASSIGN_CC(CC_ARPR, set_arp_rate);
  ASSIGN_CC(CC_ARPO, set_arp_octaves);
  ASSIGN_CC(CC_ARPE, set_arp_enable);

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
 * MODVOL's volume nibble has no default of its own: `reset_sid()` (sid.c)
 * zeroes the whole `sid_memory[]` shadow at boot, and the only thing that
 * ever writes MODVOL afterward is CC_VOL/3OFF/HPF/BPF/LPF. Without this, a
 * MIDI note-on can be entirely correct - right frequency, right gate, right
 * envelope - and still be inaudible, because the chip's master volume is
 * silent until a CC_VOL message happens to arrive. ASID and the SID player
 * never hit this because they always stream every register a tune's own
 * player routine touches, volume included, every frame; MIDI has nothing
 * equivalent to lean on. Filter routing bits (the same register's high
 * nibble) are left at 0 - no voice is routed through the filter by
 * default, so there is nothing filter-related to default here.
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

void midi_processor_init(void)
{
  usNFO("[MIDI] Handler init\n");

  memcpy(&CC, &midi_ccvalues_defaults, sizeof(midi_ccvalues));

  midi_config_init();
  midi_voice_init();
  midi_patch_init();
  midi_fmopl_init();  /* TODO 14, no-ops harmlessly if no FMOpl chip is configured */
  midi_cc_init();
  build_note_table();
  apply_default_volume();

  /* Compiled-in defaults are set above unconditionally, cheaply, every
   * time; midi_config_load() then overwrites them with whatever was last
   * saved to flash, if anything was. Without this, SYSEX_MIDI_SAVE/
   * LOAD_MIDI_STATE's flash writes were never actually reachable again -
   * nothing else ever reads them back, so "saving" persisted nothing a
   * reboot would ever see. Same "defaults, then let flash override" shape
   * as Config's own boot sequence (default_config() then load_config(),
   * usbsid.c). This also runs on a MIDI System Reset (0xFF), the other
   * caller of midi_processor_init(): a real synth's power-up state is
   * exactly what this should also restore to, not a blank factory reset
   * every time a controller happens to send 0xFF. */
  midi_config_load();

  return;
}

static void handle_control_change(uint8_t channel, uint8_t *buffer, int size)
{
  (void)size;
  uint8_t cc = buffer[0], value = buffer[1];

  /* CC_FMEN must reach every channel regardless of its current
   * MIDI_CH_TARGET_FMOPL state - it is the CC that sets that state. Handled
   * here directly rather than through cc_func_ptr_array: midi_fmopl_set_target()
   * takes (channel, value), not the (channel, cc, value) shape every other
   * handler in that table uses, and giving it its own two-line adapter just
   * to fit the table isn't worth it for one CC. */
  if (cc == CC.CC_FMEN) { midi_fmopl_set_target(channel, value); return; }

  if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
    /* CC_VOL and CC_PWM (mod wheel) are wired live for an FMOpl-targeted
     * channel - every other SID-specific CC (waveform bits, ADSR, filter,
     * LFO, arp, ...) still has no OPL equivalent and is silently ignored
     * rather than misdirected onto the chip as a bogus SID register write. */
    if (cc == CC.CC_VOL) midi_fmopl_set_volume(channel, value);
    else if (cc == CC.CC_PWM) midi_fmopl_set_mod_wheel(channel, value);
    return;
  }

  if (cc_func_ptr_array[cc] != NULL) {
    cc_func_ptr_array[cc](channel, cc, value);
  }
  return;
}

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
      /* TODO 14: FMOpl-targeted channels bypass the arpeggiator entirely for
       * now (note_on()/note_off() themselves redirect to midi_fmopl.c) -
       * arp+FMOpl together is simply not implemented yet, checked first so
       * it never falls into the arp branch below. */
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
      /* Only 0-31 map to a patch (MIDI_PATCH_COUNT). 32-127 do nothing:
       * Bank Select (CC_BMSB/CC_BLSB) is not wired, deliberately - with
       * only 32 patches, program number alone already reaches every one
       * of them, so a bank concept has nothing to select yet. */
      if (midi_channels[channel].flags & MIDI_CH_TARGET_FMOPL) {
        /* Separate 32-slot patch set (TODO 14/15, MIDI_FMOPL_PATCH_COUNT,
         * midi_fmopl.h) - same size and same range-checked-not-wrapped
         * convention as MIDI_PATCH_COUNT above, just a different array;
         * midi_fmopl_program_change() does its own range check. */
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

void midi_handler_get_ccmap(midi_ccvalues *out)
{
  memcpy(out, &CC, sizeof(midi_ccvalues));
  return;
}

void midi_handler_set_ccmap(const midi_ccvalues *in)
{
  memcpy(&CC, in, sizeof(midi_ccvalues));
  return;
}
