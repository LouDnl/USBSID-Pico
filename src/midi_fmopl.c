/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_fmopl.c
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

#include <math.h>

#include <globals.h>
#include <config.h>
#include <bus.h>
#include <sid.h>
#include <sid_defs.h>
#include <logging.h>
#include <midi_config.h>
#include <midi_fmopl.h>


/* --- OPL2 register map -----------------------------------------------------
 * Standard Yamaha YM3812 (OPL2) layout, same on every clone. The 9 hardware
 * channels do not address their two operators linearly - this offset table
 * is the well known one from every OPL2 programming reference. */
static const uint8_t OPL_OP_OFFSET[MIDI_FMOPL_CHANNELS][2] = {
  {0x00, 0x03}, {0x01, 0x04}, {0x02, 0x05},
  {0x08, 0x0B}, {0x09, 0x0C}, {0x0A, 0x0D},
  {0x10, 0x13}, {0x11, 0x14}, {0x12, 0x15},
};

#define OPL_REG_AM_VIB_EGT_KSR_MULT  0x20  /* + operator offset */
#define OPL_REG_KSL_TL               0x40  /* + operator offset */
#define OPL_REG_AR_DR                0x60  /* + operator offset */
#define OPL_REG_SL_RR                0x80  /* + operator offset */
#define OPL_REG_WAVEFORM             0xE0  /* + operator offset, needs OPL_REG_TEST bit5 set once */
#define OPL_REG_FEEDBACK_ALGO        0xC0  /* + channel index (0-8), not operator offset */
#define OPL_REG_FNUM_LO              0xA0  /* + channel index */
#define OPL_REG_KEYON_BLOCK_FNUMHI   0xB0  /* + channel index: bit5=key-on, bits4:2=block, bits1:0=Fnum hi */
#define OPL_REG_TEST                 0x01  /* bit5 = enable per-operator waveform-select registers */

#define FMOPL_CH_NONE 0xFF

/* One index/data-port pair per register write, exactly the sequence
 * clear_fmopl_registers_at_addr() (sid.c) already uses and proves works:
 * write the register number to the index port (base_address), then the
 * value to the data port (base_address + 0x10), each over
 * cycled_write_operation() - the same primitive every SID register write in
 * this firmware goes through (bus.h), not a new one invented for this. */
static inline void opl_write(uint8_t base, uint8_t reg, uint8_t value)
{
  cycled_write_operation(base, reg, 10);
  cycled_write_operation((uint8_t)(base + 0x10), value, 10);
  return;
}

static inline uint8_t fmopl_base_address(void)
{
  /* cfg.fmopl_sid is 1-based, 0 = disabled (config_socket.c) - same
   * convention clear_sid_registers() already relies on for plain SID
   * sockets (sidno * 0x20), just shifted by the -1. */
  return (uint8_t)((cfg.fmopl_sid > 0 ? (cfg.fmopl_sid - 1) : 0) * 0x20);
}

/* --- Patches (TODO 15) ------------------------------------------------
 * opl_instrument_t itself now lives in midi_fmopl.h - see that file for the
 * field reference. fmopl_patches[] is the RAM-mutable array Program Change
 * indexes into and sysex.c's new FMOpl patch LOAD/DUMP commands read and
 * write directly, same shape as midi_patches[]/midi_patch_t on the SID
 * side. */
opl_instrument_t fmopl_patches[MIDI_FMOPL_PATCH_COUNT];

/* First-pass, hand-designed-by-FM-theory starter instruments for patches
 * 0-5 - NOT claimed to reproduce any specific commercial soundfont/ROM
 * byte-for-byte, and UNVERIFIED against real hardware (see midi_fmopl.h).
 * [op0]=modulator, [op1]=carrier throughout. Patches 6-31 are left zeroed
 * by midi_fmopl_init()'s memset, for SYSEX_FMOPL_PATCH_LOAD/flash to fill
 * in later - same "inert rather than surprising" reasoning midi_patch_init()
 * uses for midi_patches[1+bank0_count .. MIDI_PATCH_COUNT-1]. */
static void midi_fmopl_patch_init(void)
{
  memset(fmopl_patches, 0, sizeof(fmopl_patches));
  static const opl_instrument_t factory[6] = {
    /* 0: Organ - both operators near-sine, unity multiple, full sustain */
    { .op_mult = {0x21, 0x21}, .op_ksl_tl = {0x1C, 0x00}, .op_ar_dr = {0xF0, 0xF0},
      .op_sl_rr = {0x0F, 0x0F}, .op_wave = {0x00, 0x00}, .feedback_algo = 0x00 },
    /* 1: Electric piano - fast attack, decaying to a lower sustain, brighter modulator */
    { .op_mult = {0x31, 0x21}, .op_ksl_tl = {0x18, 0x00}, .op_ar_dr = {0xF2, 0xD2},
      .op_sl_rr = {0x5F, 0x3F}, .op_wave = {0x01, 0x00}, .feedback_algo = 0x04 },
    /* 2: Bass - punchy decay, low sustain, low modulator multiple for a plain low end */
    { .op_mult = {0x11, 0x21}, .op_ksl_tl = {0x10, 0x00}, .op_ar_dr = {0xF3, 0xC4},
      .op_sl_rr = {0x8F, 0x4F}, .op_wave = {0x00, 0x00}, .feedback_algo = 0x02 },
    /* 3: Brass - slower attack than piano, full sustain, brighter/higher feedback for edge */
    { .op_mult = {0x21, 0x21}, .op_ksl_tl = {0x00, 0x00}, .op_ar_dr = {0xA3, 0xF0},
      .op_sl_rr = {0x0F, 0x0F}, .op_wave = {0x00, 0x00}, .feedback_algo = 0x06 },
    /* 4: Bell - high modulator multiple for an inharmonic/metallic ratio, fast decay */
    { .op_mult = {0x71, 0x21}, .op_ksl_tl = {0x00, 0x00}, .op_ar_dr = {0xF6, 0xF3},
      .op_sl_rr = {0x1F, 0x2F}, .op_wave = {0x00, 0x00}, .feedback_algo = 0x02 },
    /* 5: Pad - slow attack both operators, full sustain, sine, low feedback */
    { .op_mult = {0x21, 0x21}, .op_ksl_tl = {0x20, 0x08}, .op_ar_dr = {0x72, 0x72},
      .op_sl_rr = {0x0F, 0x0F}, .op_wave = {0x00, 0x00}, .feedback_algo = 0x00 },
  };
  for (uint8_t i = 0; i < count_of(factory); i++) fmopl_patches[i] = factory[i];
  return;
}

/* Adds volume-derived extra attenuation to an operator's authored Total
 * Level (low 6 bits of op_ksl_tl; the Key Scale Level in the top 2 bits is
 * left untouched). volume 127 = no change; volume 0 = +48 of TL's 63-step
 * range, not full silence - a channel volume of 0 arriving on an otherwise
 * live channel reads more like a controller glitch than an intentional
 * mute, and CC_ASOF/CC_ANOF already exist for real silence.
 *
 * `mod_boost` is CC_PWM (mod wheel)'s contribution, applied only to the
 * modulator operator (op0), never the carrier: a 2-op FM voice's timbre
 * brightness is set almost entirely by how loud its modulator is relative
 * to the carrier, so raising the modulator's level (lowering its
 * attenuation) as the wheel goes up is a real-time "more FM
 * intensity/brightness" control - a defensible, idiomatic use of "mod
 * wheel" for an FM voice given OPL has no filter to sweep the way a
 * subtractive synth's mod wheel often would. Pass 0 for the carrier. Both
 * this and the volume mapping are first-pass linear curves, same caveat
 * class as TODO 15's own attenuation-curve note - not tuned by ear, no
 * hardware to tune them on yet. */
static uint8_t fmopl_scale_tl(uint8_t op_ksl_tl, uint8_t volume, uint8_t mod_boost)
{
  uint8_t ksl = (uint8_t)(op_ksl_tl & 0xC0);
  int tl = op_ksl_tl & 0x3F;
  int extra = ((127 - volume) * 48) / 127;
  int boost = (mod_boost * 24) / 127;
  tl = tl + extra - boost;
  if (tl < 0) tl = 0;
  if (tl > 63) tl = 63;
  return (uint8_t)(ksl | (uint8_t)tl);
}

static void fmopl_write_instrument(uint8_t base, uint8_t slot, const opl_instrument_t *ins,
                                    uint8_t volume, uint8_t mod_wheel)
{
  const uint8_t mod_boost[2] = { mod_wheel, 0 };  /* op0=modulator, op1=carrier */
  for (uint8_t op = 0; op < 2; op++) {
    uint8_t off = OPL_OP_OFFSET[slot][op];
    opl_write(base, (uint8_t)(OPL_REG_AM_VIB_EGT_KSR_MULT + off), ins->op_mult[op]);
    opl_write(base, (uint8_t)(OPL_REG_KSL_TL + off),
              fmopl_scale_tl(ins->op_ksl_tl[op], volume, mod_boost[op]));
    opl_write(base, (uint8_t)(OPL_REG_AR_DR + off),                ins->op_ar_dr[op]);
    opl_write(base, (uint8_t)(OPL_REG_SL_RR + off),                ins->op_sl_rr[op]);
    opl_write(base, (uint8_t)(OPL_REG_WAVEFORM + off),             ins->op_wave[op]);
  }
  opl_write(base, (uint8_t)(OPL_REG_FEEDBACK_ALGO + slot), ins->feedback_algo);
  return;
}

/* --- Per-MIDI-channel state -------------------------------------------------
 * What CC_FMEN/Program Change/CC_VOL/CC_PWM (mod wheel)/pitch bend touch.
 * Everything else about an FMOpl-targeted channel (transpose, LFO, arp, ...)
 * is still not wired to OPL - see TODO 14's follow-up list - so this is
 * still a much smaller surface than midi_channel_cfg_t. `bend_range` is
 * deliberately NOT duplicated here: midi_channels[channel].bend_range
 * already exists and is channel-scoped regardless of what the channel
 * targets, reused as-is (repo/src/midi_config.h). */
typedef struct {
  uint8_t instrument;   /* index into fmopl_patches[] */
  uint8_t volume;       /* 0-127, CC_VOL - live: rewrites TL on every held voice, not just new notes */
  uint8_t mod_wheel;     /* 0-127, CC_PWM (mod wheel) - live modulator brightness, see fmopl_scale_tl() */
  double  bend_semitones; /* current pitch bend offset, +/- midi_channels[channel].bend_range */
} fmopl_channel_state_t;
static fmopl_channel_state_t fmopl_channels[MAX_CHANNELS];

/* --- The 9-slot hardware voice pool ---------------------------------------- */
typedef struct {
  uint8_t  midi_channel;  /* FMOPL_CH_NONE = free */
  uint8_t  note;
  uint8_t  instrument;    /* index into fmopl_patches[] this voice was struck with - a
                              Program Change while it's still held must not retroactively
                              change it, same as apply_patch_to_channel() never rewrites
                              held SID voices either */
  uint8_t  block;
  uint16_t fnum;
  uint32_t age;
} fmopl_voice_t;
static fmopl_voice_t fmopl_voices[MIDI_FMOPL_CHANNELS];
static uint32_t fmopl_age_counter = 0;

static uint8_t fmopl_find(uint8_t channel, uint8_t note)
{
  for (uint8_t i = 0; i < MIDI_FMOPL_CHANNELS; i++) {
    if (fmopl_voices[i].midi_channel == channel && fmopl_voices[i].note == note) return i;
  }
  return FMOPL_CH_NONE;
}

/* Free slot if one exists, else steal the oldest - the same default
 * behaviour as MIDI_STEAL_OLDEST in midi_voice.c, kept unconditional here
 * rather than configurable: one physical chip, 9 channels total, not worth
 * a whole steal-mode setting yet. */
static uint8_t fmopl_alloc(uint8_t channel, uint8_t note)
{
  uint8_t existing = fmopl_find(channel, note);
  if (existing != FMOPL_CH_NONE) return existing;  /* retrigger, not a second voice */

  for (uint8_t i = 0; i < MIDI_FMOPL_CHANNELS; i++) {
    if (fmopl_voices[i].midi_channel == FMOPL_CH_NONE) return i;
  }
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < MIDI_FMOPL_CHANNELS; i++) {
    if (fmopl_voices[i].age < fmopl_voices[oldest].age) oldest = i;
  }
  return oldest;
}

/* Rewrites just the Total Level registers (not the whole instrument) on
 * every voice `channel` currently has held - the live part of CC_VOL/
 * CC_PWM, called after either changes so an already-sounding note responds
 * immediately instead of waiting for its next note-on. */
static void fmopl_apply_live_attenuation(uint8_t channel)
{
  if (!cfg.fmopl_enabled) return;
  uint8_t base = fmopl_base_address();
  uint8_t volume = fmopl_channels[channel].volume;
  const uint8_t mod_boost[2] = { fmopl_channels[channel].mod_wheel, 0 };
  for (uint8_t slot = 0; slot < MIDI_FMOPL_CHANNELS; slot++) {
    if (fmopl_voices[slot].midi_channel != channel) continue;
    /* Each voice's own struck instrument, not the channel's current one -
     * a Program Change while this note is still held must not retroactively
     * change its timbre, see fmopl_voice_t.instrument. */
    const opl_instrument_t *ins = &fmopl_patches[fmopl_voices[slot].instrument];
    for (uint8_t op = 0; op < 2; op++) {
      uint8_t off = OPL_OP_OFFSET[slot][op];
      opl_write(base, (uint8_t)(OPL_REG_KSL_TL + off),
                fmopl_scale_tl(ins->op_ksl_tl[op], volume, mod_boost[op]));
    }
  }
  return;
}

/* --- Clock-scaled MIDI-note -> (block, Fnum) table -------------------------
 * Mirrors build_note_table()'s own reasoning in midi_handler.c: build once
 * once at init, not per note-on. Uses floating point deliberately, same
 * tradeoff build_note_table() makes (there: fixed-point good enough; here:
 * OPL2's own Fnum formula needs an exponential, and this only ever runs
 * once, not in any audio-rate path) - vu.c already links <math.h> in this
 * exact build.
 *
 * CORRECTED 2026-08-20, confirmed on real hardware: this used to scale the
 * board's own configurable SID clock rate (cfg.clock_rate, ~1MHz) down for
 * Fnum, on the theory that the FMOpl chip shares the same PIO-generated PHI
 * clock every SID socket gets. It doesn't - the user reported every FMOpl
 * note sounding roughly two octaves too high, exactly what dividing by a
 * ~1MHz clock instead of OPL2's real ~3.58MHz produces (Fnum comes out
 * ~3.58x too large for a given pitch). A SID's ~1MHz PHI2 bus clock and an
 * OPL2's internal FM timing are different clock domains entirely; a clone
 * chip sharing a SID socket almost certainly carries its own crystal for
 * the OPL core and only shares the data/address/chip-select lines
 * clear_fmopl_registers_at_addr() already uses - not the clock line. Fixed
 * to the real, standard OPL2 crystal frequency below, same constant every
 * OPL2 chip, clone, and piece of OPL2 software (trackers, DOS games, ...)
 * assumes; no longer tied to the board's SID clock at all. */
#define FMOPL_NOTE_COUNT 128
#define FMOPL_CLOCK_HZ 3579545u  /* standard OPL2 crystal (NTSC colorburst x4) - compiled-in default */
static uint8_t  fmopl_block_table[FMOPL_NOTE_COUNT];
static uint16_t fmopl_fnum_table[FMOPL_NOTE_COUNT];

/* Runtime override (SYSEX_FMOPL_SET_CLOCK, 0x26 - see sysex.c), 0 = use
 * FMOPL_CLOCK_HZ. Exists because the compiled-in default, though verified
 * against both the general OPL2 spec and SIDKick-pico's own emulator
 * source (frntc/SIDKick-pico, fmopl.c/SKpico.c - it hardcodes the exact
 * same 3579545), still measured wrong on real hardware even after that fix
 * landed - something about a specific clone's actual realised clock isn't
 * fully explained by its own source alone. Rather than guess more constants
 * remotely, this lets it be tuned by ear per board/clone without a
 * firmware rebuild, and whatever value is found to actually work should
 * get folded back into FMOPL_CLOCK_HZ (or a per-clone-type table) once
 * known - this override is a debugging tool, not the intended long-term
 * mechanism for supporting multiple OPL clone types. */
static uint32_t fmopl_clock_override = 0;

static inline uint32_t fmopl_effective_clock(void)
{
  return fmopl_clock_override ? fmopl_clock_override : FMOPL_CLOCK_HZ;
}

/* Shared by the table build below (once, at init or when the clock is
 * overridden) and by pitch bend (arbitrary fractional semitone offset,
 * live - see midi_fmopl_pitch_bend()). Both are infrequent, note-triggered
 * paths, never audio-rate, so the float cost is the same acceptable
 * tradeoff build_note_table() already makes for the SID side. */
static void fmopl_freq_to_block_fnum(double freq, uint8_t *out_block, uint16_t *out_fnum)
{
  double opl_rate = (double)fmopl_effective_clock() / 72.0;  /* OPL2's own Fnum formula divides the input clock by 72 */
  int block = 0;
  double fnum = 0.0;
  for (block = 0; block <= 7; block++) {
    fnum = freq * (double)(1u << (20 - block)) / opl_rate;
    if (fnum < 1024.0) break;  /* lowest block that still fits Fnum's 10 bits: best resolution */
  }
  if (fnum > 1023.0) fnum = 1023.0;
  if (fnum < 0.0) fnum = 0.0;
  *out_block = (uint8_t)block;
  *out_fnum  = (uint16_t)(fnum + 0.5);
  return;
}

static void build_fmopl_note_table(void)
{
  for (int n = 0; n < FMOPL_NOTE_COUNT; n++) {
    double freq = 440.0 * pow(2.0, ((double)n - 69.0) / 12.0);  /* A4 = MIDI note 69 = 440 Hz */
    fmopl_freq_to_block_fnum(freq, &fmopl_block_table[n], &fmopl_fnum_table[n]);
  }
  usNFO("[FMOPL] Note table built (%u Hz OPL2 clock)\n", fmopl_effective_clock());
  return;
}

static void fmopl_note_to_fnum(uint8_t midi_note, uint8_t *out_block, uint16_t *out_fnum)
{
  uint8_t n = (midi_note > 127) ? 127 : midi_note;
  *out_block = fmopl_block_table[n];
  *out_fnum  = fmopl_fnum_table[n];
  return;
}

/* Pitch-bent lookup: the table above only covers integer semitones at
 * bend=0, so a nonzero bend recomputes directly instead of indexing it -
 * still just one call per note-on or per pitch-bend message, not per
 * sample. bend_semitones == 0.0 takes the fast table path. */
static void fmopl_note_to_fnum_bent(uint8_t midi_note, double bend_semitones,
                                     uint8_t *out_block, uint16_t *out_fnum)
{
  if (bend_semitones == 0.0) {
    fmopl_note_to_fnum(midi_note, out_block, out_fnum);
    return;
  }
  double freq = 440.0 * pow(2.0, ((double)midi_note + bend_semitones - 69.0) / 12.0);
  fmopl_freq_to_block_fnum(freq, out_block, out_fnum);
  return;
}

/* --- Public API -------------------------------------------------------- */

void midi_fmopl_init(void)
{
  for (uint8_t i = 0; i < MIDI_FMOPL_CHANNELS; i++) {
    fmopl_voices[i].midi_channel = FMOPL_CH_NONE;
    fmopl_voices[i].age = 0;
  }
  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    fmopl_channels[c].instrument = 0;
    fmopl_channels[c].volume = 127;
    fmopl_channels[c].mod_wheel = 0;
    fmopl_channels[c].bend_semitones = 0.0;
  }
  fmopl_age_counter = 0;
  fmopl_clock_override = 0;  /* back to the compiled-in default every init, same as everything else here */
  build_fmopl_note_table();
  midi_fmopl_patch_init();

  if (!cfg.fmopl_enabled) return;
  uint8_t base = fmopl_base_address();
  clear_fmopl_registers_at_addr(base);
  opl_write(base, OPL_REG_TEST, BIT_5);  /* enable per-operator waveform-select registers */
  usNFO("[FMOPL] Init done, chip at SID%d (base 0x%02x)\n", cfg.fmopl_sid, base);
  return;
}

void midi_fmopl_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
  (void)velocity;  /* not yet used, see midi_fmopl.h */
  if (!cfg.fmopl_enabled) return;

  uint8_t base = fmopl_base_address();
  uint8_t slot = fmopl_alloc(channel, note);
  fmopl_voices[slot].midi_channel = channel;
  fmopl_voices[slot].note = note;
  fmopl_voices[slot].instrument = fmopl_channels[channel].instrument;
  fmopl_voices[slot].age = ++fmopl_age_counter;

  const opl_instrument_t *ins = &fmopl_patches[fmopl_channels[channel].instrument];
  fmopl_write_instrument(base, slot, ins, fmopl_channels[channel].volume, fmopl_channels[channel].mod_wheel);

  /* A note struck while the wheel is already bent starts bent too, same as
   * any other synth - the bend offset doesn't reset between notes, only
   * another pitch-bend message (centre = 0x2000) changes it. */
  uint8_t block; uint16_t fnum;
  fmopl_note_to_fnum_bent(note, fmopl_channels[channel].bend_semitones, &block, &fnum);
  fmopl_voices[slot].block = block;
  fmopl_voices[slot].fnum = fnum;

  opl_write(base, (uint8_t)(OPL_REG_FNUM_LO + slot), (uint8_t)(fnum & 0xFF));
  uint8_t hib = (uint8_t)(((block & 0x7) << 2) | ((fnum >> 8) & 0x3) | BIT_5);  /* BIT_5 = key-on */
  opl_write(base, (uint8_t)(OPL_REG_KEYON_BLOCK_FNUMHI + slot), hib);
  return;
}

void midi_fmopl_note_off(uint8_t channel, uint8_t note, uint8_t velocity)
{
  (void)velocity;
  if (!cfg.fmopl_enabled) return;

  uint8_t slot = fmopl_find(channel, note);
  if (slot == FMOPL_CH_NONE) return;

  uint8_t base = fmopl_base_address();
  uint8_t hib = (uint8_t)(((fmopl_voices[slot].block & 0x7) << 2) | ((fmopl_voices[slot].fnum >> 8) & 0x3));
  opl_write(base, (uint8_t)(OPL_REG_KEYON_BLOCK_FNUMHI + slot), hib);  /* key-on bit cleared */
  fmopl_voices[slot].midi_channel = FMOPL_CH_NONE;
  return;
}

void midi_fmopl_program_change(uint8_t channel, uint8_t program)
{
  /* Range-checked, not wrapped - matches how apply_patch_to_channel()'s own
   * caller checks `< MIDI_PATCH_COUNT` before calling at all
   * (midi_handler.c's 0xC0 case), rather than silently wrapping an
   * out-of-range program number onto some other patch. */
  if (program >= MIDI_FMOPL_PATCH_COUNT) return;
  fmopl_channels[channel].instrument = program;
  usNFO("[FMOPL] ch%d -> patch %d\n", channel, program);
  return;
}

void midi_fmopl_set_target(uint8_t channel, uint8_t value)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  bool was = (ch->flags & MIDI_CH_TARGET_FMOPL) != 0;
  bool now = (value == 127 ? true : value == 0 ? false : !was);
  if (now == was) return;
  if (now) ch->flags = (uint8_t)(ch->flags | MIDI_CH_TARGET_FMOPL);
  else ch->flags = (uint8_t)(ch->flags & (uint8_t)~MIDI_CH_TARGET_FMOPL);
  usNFO("[CC_FMEN] ch%d target FMOpl -> %d\n", channel, now);
  return;
}

void midi_fmopl_set_volume(uint8_t channel, uint8_t value)
{
  /* OPL2 has no separate master-volume register the way SID's MODVOL
   * nibble is - Total Level is baked into each operator's own register -
   * so "live" here means finding every fmopl_voices[] slot this channel
   * currently holds and rewriting just their TL bytes, not the SID-style
   * single chip-wide write. */
  fmopl_channels[channel].volume = value;
  fmopl_apply_live_attenuation(channel);
  return;
}

void midi_fmopl_set_mod_wheel(uint8_t channel, uint8_t value)
{
  fmopl_channels[channel].mod_wheel = value;
  fmopl_apply_live_attenuation(channel);
  return;
}

void midi_fmopl_pitch_bend(uint8_t channel, uint8_t lsb, uint8_t msb)
{
  if (!cfg.fmopl_enabled) return;

  /* 0x2000 is MIDI pitch bend's protocol-fixed centre point - same constant
   * midi_handler.c's own MIDI_BEND_CENTRE names, not shared across the
   * translation unit boundary since it's the only thing that would be. */
  int32_t bend14 = (int32_t)(((uint16_t)msb << 7) | lsb);  /* 0..16383, centre 0x2000 */
  int32_t bend_range = midi_channels[channel].bend_range;  /* semitones, channel-scoped, shared with the SID side */
  fmopl_channels[channel].bend_semitones =
    ((double)(bend14 - 0x2000) / (double)0x2000) * (double)bend_range;

  uint8_t base = fmopl_base_address();
  for (uint8_t slot = 0; slot < MIDI_FMOPL_CHANNELS; slot++) {
    if (fmopl_voices[slot].midi_channel != channel) continue;
    uint8_t block; uint16_t fnum;
    fmopl_note_to_fnum_bent(fmopl_voices[slot].note, fmopl_channels[channel].bend_semitones, &block, &fnum);
    fmopl_voices[slot].block = block;
    fmopl_voices[slot].fnum = fnum;
    opl_write(base, (uint8_t)(OPL_REG_FNUM_LO + slot), (uint8_t)(fnum & 0xFF));
    /* Still held: key-on (BIT_5) stays set, same as any other in-place
     * Fnum/block rewrite on a gated voice. */
    uint8_t hib = (uint8_t)(((block & 0x7) << 2) | ((fnum >> 8) & 0x3) | BIT_5);
    opl_write(base, (uint8_t)(OPL_REG_KEYON_BLOCK_FNUMHI + slot), hib);
  }
  return;
}

void midi_fmopl_set_clock(uint32_t hz)
{
  /* See fmopl_clock_override's own comment: a debugging/tuning knob, not
   * the intended long-term way to support multiple OPL clone types. 0
   * resets to the compiled-in FMOPL_CLOCK_HZ default. */
  fmopl_clock_override = hz;
  build_fmopl_note_table();
  usNFO("[FMOPL] Clock override -> %u Hz%s\n", fmopl_effective_clock(), hz ? "" : " (default)");

  if (!cfg.fmopl_enabled) return;
  uint8_t base = fmopl_base_address();
  for (uint8_t slot = 0; slot < MIDI_FMOPL_CHANNELS; slot++) {
    if (fmopl_voices[slot].midi_channel == FMOPL_CH_NONE) continue;
    uint8_t channel = fmopl_voices[slot].midi_channel;
    uint8_t block; uint16_t fnum;
    fmopl_note_to_fnum_bent(fmopl_voices[slot].note, fmopl_channels[channel].bend_semitones, &block, &fnum);
    fmopl_voices[slot].block = block;
    fmopl_voices[slot].fnum = fnum;
    opl_write(base, (uint8_t)(OPL_REG_FNUM_LO + slot), (uint8_t)(fnum & 0xFF));
    uint8_t hib = (uint8_t)(((block & 0x7) << 2) | ((fnum >> 8) & 0x3) | BIT_5);
    opl_write(base, (uint8_t)(OPL_REG_KEYON_BLOCK_FNUMHI + slot), hib);
  }
  return;
}

void midi_fmopl_all_notes_off(void)
{
  if (!cfg.fmopl_enabled) return;
  uint8_t base = fmopl_base_address();
  for (uint8_t i = 0; i < MIDI_FMOPL_CHANNELS; i++) {
    if (fmopl_voices[i].midi_channel == FMOPL_CH_NONE) continue;
    uint8_t hib = (uint8_t)(((fmopl_voices[i].block & 0x7) << 2) | ((fmopl_voices[i].fnum >> 8) & 0x3));
    opl_write(base, (uint8_t)(OPL_REG_KEYON_BLOCK_FNUMHI + i), hib);
    fmopl_voices[i].midi_channel = FMOPL_CH_NONE;
  }
  return;
}
