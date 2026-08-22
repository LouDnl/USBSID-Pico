/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_config.c
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
#include <config.h>
#include <sid_defs.h>
#include <logging.h>
#include <midi_config.h>
#include <midi_handler.h>  /* midi_handler_get_ccmap/set_ccmap */


midi_channel_cfg_t midi_channels[MAX_CHANNELS];

/* Bitmask of the 3 pool slots that belong to SID index `sid` (0..MAX_SIDS-1).
 * Slot layout matches midi_voice.c: slot n is SID n/MAX_VOICES, voice n%MAX_VOICES. */
static inline uint16_t sid_slot_bits(uint8_t sid)
{
  return (uint16_t)(((1u << MAX_VOICES) - 1) << (sid * MAX_VOICES));
}

void midi_config_init(void)
{
  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    midi_channel_cfg_t *ch = &midi_channels[c];
    ch->voice_mask     = 0;  /* disabled unless set below */
    ch->poly_limit     = 1;
    ch->steal_mode     = MIDI_STEAL_OLDEST;
    ch->transpose      = MIDI_TRANSPOSE;  /* Phase 0's compile-time default, now per-channel */
    ch->bend_range     = 2;
    ch->at_target      = MIDI_AT_FILTER;
    ch->flags          = MIDI_CH_AUTO_GATE;  /* velocity mode off, matches the Phase 0 fix */
    ch->sid_override   = MIDI_OVERRIDE_NONE;
    ch->voice_override = MIDI_OVERRIDE_NONE;
    /* Audible defaults, not silent ones: BIT_4 selects the triangle
     * waveform (the one waveform that needs no PWM setting to actually
     * produce output - a default pulse wave with pwmlo/hi still 0 would be
     * a 0% duty cycle, i.e. also silent), and a nonzero sustain (high
     * nibble of tmpl_susrel) means a held note actually sustains instead
     * of decaying to silence immediately after the attack. Without both of
     * these, "plug in a keyboard and play" (this file's own stated intent
     * for channel 1, below) produced perfectly correct gate/frequency/
     * envelope timing and total silence - CONTR with no waveform bit set
     * has no oscillator output at all, regardless of the gate or envelope. */
    ch->tmpl_contr     = BIT_4;   /* triangle */
    ch->tmpl_attdec    = 0x00;    /* instant attack, instant decay to sustain */
    ch->tmpl_susrel    = 0xF0;    /* full sustain, fast release */
    ch->tmpl_pwmlo     = 0;
    ch->tmpl_pwmhi     = 0;

    ch->bend_x256        = 0;
    ch->porta_time       = 0;  /* off */
    ch->last_target_x256 = -1; /* nothing to glide from yet */

    ch->lfo_wave      = MIDI_LFO_TRI;
    ch->lfo_rate      = 32;  /* mid-range default, only matters once depth > 0 */
    ch->lfo_depth     = 0;   /* off */
    ch->lfo_dest      = MIDI_LFO_DEST_PITCH;
    ch->lfo_phase     = 0;
    ch->lfo_sh_seed   = (uint32_t)(c + 1) * 2654435761u; /* distinct per channel */
    ch->lfo_sh_value  = 0;

    ch->arp_mode           = MIDI_ARP_UP;
    ch->arp_rate           = 64;
    ch->arp_octaves        = 0;
    ch->arp_held_count     = 0;
    ch->arp_step            = 0;
    ch->arp_step_up         = true;
    ch->arp_voice_slot      = 0xFF; /* MIDI_VOICE_NONE */
    ch->arp_phase           = 0;
    ch->arp_last_step_pulses = 0;
    for (uint8_t i = 0; i < MIDI_ARP_MAX_NOTES; i++) ch->arp_held[i] = 0;

    ch->filter_cutoff = 0;  /* matches the SID's own default (filter closed) */
    ch->patch = 0xFF;  /* MIDI_PATCH_NONE: no Program Change received yet */
  }

  /* Channel 1 (index 0): every slot, full poly. Plug in a keyboard and play
   * without touching a config tool first. */
  midi_channels[0].voice_mask = (uint16_t)((1u << (MAX_SIDS * MAX_VOICES)) - 1);
  midi_channels[0].poly_limit = (MAX_SIDS * MAX_VOICES);

  /* Channels 2-5 (index 1-4): one SID each, exclusive, poly within that SID.
   * A sane multi-timbral split out of the box instead of everyone landing
   * on SID 0. */
  for (uint8_t s = 0; s < MAX_SIDS; s++) {
    midi_channels[1 + s].voice_mask = sid_slot_bits(s);
    midi_channels[1 + s].poly_limit = MAX_VOICES;
  }

  /* Channels 6-16 (index 5-15): disabled. Empty voice_mask makes every
   * allocation on that channel a no-op, so no separate "enabled" flag is
   * needed; nothing else in this file special-cases it. */

  return;
}

uint16_t midi_channel_effective_mask(uint8_t channel)
{
  midi_channel_cfg_t *ch = &midi_channels[channel];
  uint16_t mask = ch->voice_mask;

  uint8_t present_sids = (cfg.numsids > MAX_SIDS ? MAX_SIDS : cfg.numsids);
  uint16_t present = 0;
  for (uint8_t s = 0; s < present_sids; s++) present |= sid_slot_bits(s);
  mask &= present;

  if (ch->sid_override != MIDI_OVERRIDE_NONE) {
    mask &= sid_slot_bits(ch->sid_override);
  }
  if (ch->voice_override != MIDI_OVERRIDE_NONE) {
    uint16_t vbits = 0;
    for (uint8_t s = 0; s < MAX_SIDS; s++) {
      vbits |= (uint16_t)(1u << ((s * MAX_VOICES) + ch->voice_override));
    }
    mask &= vbits;
  }

  return mask;
}

/* --- Flash persistence ---------------------------------------------- */

static_assert(sizeof(midi_config_blob_t) < MIDICONFIG_SLOT_SIZE,
  "[MIDI CONFIG] SAVE ERROR: midi_config_blob_t doesn't fit inside one MIDICONFIG_SLOT_SIZE sector");

/* Written into slot 0 the first time midi_config_save() ever runs, then
 * incremented on every save after that (including ones from a later boot,
 * since it is re-seeded from the highest sequence a load actually found -
 * see midi_config_load()). Mirrors config.c's own config_saveid static. */
static uint8_t  midi_saveid = 0;
static uint32_t midi_sequence = 0;

/* IEEE 802.3 CRC32, bit-by-bit rather than table-based: nothing else in
 * this firmware needed a CRC32 before, and a save happens rarely enough
 * (a user action, not a hot path) that a 256-entry lookup table would cost
 * flash for no measurable benefit. */
static uint32_t midi_crc32(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

/* CRC covers everything from `saveid` onward - i.e. everything except the
 * magic/version/size/crc32 header fields themselves, which describe the
 * blob rather than being covered by its own checksum. */
static uint32_t midi_blob_crc(const midi_config_blob_t *blob)
{
  const uint8_t *start = (const uint8_t *)&blob->saveid;
  size_t len = sizeof(midi_config_blob_t) - offsetof(midi_config_blob_t, saveid);
  return midi_crc32(start, len);
}

/* Statically allocated rather than a stack-local: this is the source
 * buffer flash_range_program() reads from, its lifetime crosses the
 * flash_safe_execute() callback where interrupts are disabled, and at
 * several KB it is not something to put on a stack frame regardless. */
typedef union {
  midi_config_blob_t blob;
  uint8_t             bytes[MIDICONFIG_SLOT_SIZE];
} midi_flash_write_buf_t;

static midi_flash_write_buf_t __not_in_flash("midi") midi_flash_write_buf;

/**
 * @brief The actual flash erase + program, run inside flash_safe_execute()
 *
 * Unlike Config's write_config_lowlevel() (`config.c`), which erases only
 * once every 16 writes because its 16 slots share one 4KB sector, every
 * MIDI slot here IS its own whole sector - each save must erase its own
 * slot every time, there is nothing to skip.
 */
static void __no_inline_not_in_flash_func(write_midi_config_lowlevel)(void *blob_data)
{ /* No logging in this function to avoid errors, matching write_config_lowlevel() */
  uint32_t ints = save_and_disable_interrupts();
  uint32_t slot_offset = FLASH_MIDICONFIG_OFFSET + (MIDICONFIG_SLOT_SIZE * midi_saveid);
  flash_range_erase(slot_offset, MIDICONFIG_SLOT_SIZE);
  flash_range_program(slot_offset, (uint8_t *)blob_data, MIDICONFIG_SLOT_SIZE);
  restore_interrupts(ints);
  return;
}

void midi_config_save(void)
{
  if (!midiconfig_offset_ok) {
    usERR("[MIDI CONFIG] Refusing to save: MIDI flash offset failed its boot-time cross-check\n");
    return;
  }

  memset(&midi_flash_write_buf, 0, sizeof(midi_flash_write_buf));
  midi_config_blob_t *blob = &midi_flash_write_buf.blob;

  blob->magic   = MIDI_CONFIG_MAGIC;
  blob->version = MIDI_CONFIG_VERSION;
  blob->size    = sizeof(midi_config_blob_t);
  blob->saveid  = midi_saveid;
  blob->sequence = ++midi_sequence;
  midi_handler_get_ccmap(&blob->ccmap);
  memcpy(blob->channel, midi_channels, sizeof(blob->channel));
  memcpy(blob->patch, midi_patches, sizeof(blob->patch));
  blob->crc32 = midi_blob_crc(blob);

  usCFG("[MIDI CONFIG] Saving to slot %u (sequence %u) at 0x%x\n",
    midi_saveid, blob->sequence, (FLASH_MIDICONFIG_OFFSET + (MIDICONFIG_SLOT_SIZE * midi_saveid)));

  int err = flash_safe_execute(write_midi_config_lowlevel, midi_flash_write_buf.bytes, 100);
  if (err) {
    usERR("[MIDI CONFIG] Saving failed: %d\n", err);
    return;
  }
  sleep_ms(100);

  midi_saveid = (uint8_t)((midi_saveid + 1) % MIDICONFIG_SAVE_SLOTS);
  usCFG("[MIDI CONFIG] Saved. Next save will use slot %u\n", midi_saveid);
  return;
}

/**
 * @brief Clear whatever describes a moment mid-performance rather than a
 *        setting, after a bulk overwrite of midi_channels[] from flash
 *
 * The persisted blob keeps the whole live midi_channel_cfg_t, transient
 * runtime fields included - simpler and less error-prone than hand
 * maintaining a second, parallel "persistable subset" struct that would
 * silently drift out of sync every time midi_channel_cfg_t gains a field.
 * This is what actually enforces "a reboot never resumes mid-glide, or
 * with a note the arpeggiator thinks is still held": every field that
 * would be wrong to resume with gets reset here, explicitly, right after
 * the load that overwrote them.
 */
static void sanitise_loaded_channels(void)
{
  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    midi_channel_cfg_t *ch = &midi_channels[c];
    ch->sid_override      = MIDI_OVERRIDE_NONE;
    ch->voice_override    = MIDI_OVERRIDE_NONE;
    ch->bend_x256          = 0;
    ch->last_target_x256   = -1;
    ch->lfo_phase           = 0;
    ch->lfo_sh_value        = 0;
    ch->arp_held_count      = 0;
    ch->arp_step             = 0;
    ch->arp_step_up          = true;
    ch->arp_voice_slot       = 0xFF;  /* MIDI_VOICE_NONE */
    ch->arp_phase            = 0;
    ch->arp_last_step_pulses = 0;
    for (uint8_t i = 0; i < MIDI_ARP_MAX_NOTES; i++) ch->arp_held[i] = 0;
  }
  return;
}

void midi_config_load(void)
{
  if (!midiconfig_offset_ok) {
    usERR("[MIDI CONFIG] Refusing to load: MIDI flash offset failed its boot-time cross-check\n");
    return;
  }

  midi_config_blob_t candidate;
  int32_t best_slot = -1;
  uint32_t best_sequence = 0;

  for (uint8_t slot = 0; slot < MIDICONFIG_SAVE_SLOTS; slot++) {
    /* Memory-mapped flash read, same technique load_config() (config.c)
     * uses - no flash_range_* API needed for reading, XIP flash is just
     * memory. Bounded to exactly MIDICONFIG_SAVE_SLOTS iterations, unlike
     * Config's own open-ended "keep scanning while it looks valid" loop -
     * see the note on midi_config_blob_t in midi_config.h for why. */
    memcpy(&candidate,
      (void *)(XIP_BASE + FLASH_MIDICONFIG_OFFSET + (MIDICONFIG_SLOT_SIZE * slot)),
      sizeof(candidate));
    stdio_flush();

    if (candidate.magic != MIDI_CONFIG_MAGIC) continue;
    if (candidate.size != sizeof(midi_config_blob_t)) continue;  /* different struct layout, do not trust it */
    uint32_t stored_crc = candidate.crc32;
    if (midi_blob_crc(&candidate) != stored_crc) continue;

    if (best_slot < 0 || candidate.sequence > best_sequence) {
      best_slot = slot;
      best_sequence = candidate.sequence;
    }
  }

  if (best_slot < 0) {
    usCFG("[MIDI CONFIG] No valid saved state found, keeping compiled-in defaults\n");
    return;
  }

  memcpy(&candidate,
    (void *)(XIP_BASE + FLASH_MIDICONFIG_OFFSET + (MIDICONFIG_SLOT_SIZE * best_slot)),
    sizeof(candidate));
  stdio_flush();

  midi_handler_set_ccmap(&candidate.ccmap);
  memcpy(midi_channels, candidate.channel, sizeof(midi_channels));
  memcpy(midi_patches, candidate.patch, sizeof(midi_patches));
  sanitise_loaded_channels();

  midi_saveid = (uint8_t)((best_slot + 1) % MIDICONFIG_SAVE_SLOTS);
  midi_sequence = best_sequence;

  usCFG("[MIDI CONFIG] Loaded slot %u (sequence %u). Next save will use slot %u\n",
    (unsigned)best_slot, best_sequence, midi_saveid);
  return;
}

/* flash_safe_execute() calls back with `void (*)(void *param)`; the slot
 * offset to erase is passed through `param` as its numeric value rather
 * than a pointer to it, since there is nothing to point to that outlives
 * the call - the offset is fully known before the call is made. */
static void __no_inline_not_in_flash_func(erase_one_midi_slot_lowlevel)(void *param)
{ /* No logging in this function to avoid errors, matching write_midi_config_lowlevel() */
  uint32_t slot_offset = (uint32_t)(uintptr_t)param;
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(slot_offset, MIDICONFIG_SLOT_SIZE);
  restore_interrupts(ints);
  return;
}

void midi_config_reset(void)
{
  midi_config_init();
  midi_patch_init();

  if (!midiconfig_offset_ok) {
    usERR("[MIDI CONFIG] Reset applied to RAM only: MIDI flash offset failed its boot-time cross-check\n");
    return;
  }

  for (uint8_t slot = 0; slot < MIDICONFIG_SAVE_SLOTS; slot++) {
    uint32_t slot_offset = FLASH_MIDICONFIG_OFFSET + (MIDICONFIG_SLOT_SIZE * slot);
    int err = flash_safe_execute(erase_one_midi_slot_lowlevel, (void *)(uintptr_t)slot_offset, 100);
    if (err) {
      usERR("[MIDI CONFIG] Erasing slot %u failed: %d\n", slot, err);
    }
  }
  midi_saveid = 0;
  midi_sequence = 0;

  usCFG("[MIDI CONFIG] Reset to defaults and erased all %d save slots\n", MIDICONFIG_SAVE_SLOTS);
  return;
}
