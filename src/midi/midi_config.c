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
#include <midi.h>          /* midi_ccvalues_defaults */
#include <midi_handler.h>  /* midi_handler_get_ccmap/set_ccmap/reset_ccmap */


midi_channel_cfg_t midi_channels[MAX_CHANNELS];

/**
 * @brief Bitmask of the pool slots that belong to SID index `sid`
 *
 * Slot layout matches midi_voice.c: slot n is SID n/MAX_VOICES, voice
 * n%MAX_VOICES.
 *
 * @param uint8_t sid, SID index (0..MAX_SIDS-1)
 * @return uint16_t bitmask of the MAX_VOICES pool slots belonging to that SID
 */
static inline uint16_t sid_slot_bits(uint8_t sid)
{
  return (uint16_t)(((1u << MAX_VOICES) - 1) << (sid * MAX_VOICES));
}

/**
 * @brief Initialise midi_channels[] to their compiled-in default configuration
 *
 * Channel 1 gets every voice slot and full polyphony, channels 2-5 get one
 * SID each exclusive, channels 6-16 are left disabled (empty voice_mask).
 */
void midi_config_init(void)
{
  for (uint8_t c = 0; c < MAX_CHANNELS; c++) {
    midi_channel_cfg_t *ch = &midi_channels[c];
    ch->voice_mask     = 0;  /* disabled unless set below */
    ch->poly_limit     = 1;
    ch->steal_mode     = MIDI_STEAL_OLDEST;
    ch->transpose      = MIDI_TRANSPOSE;  /* was a compile-time-only default, per-channel since */
    ch->bend_range     = 2;
    ch->at_target      = MIDI_AT_FILTER;
    ch->flags          = MIDI_CH_AUTO_GATE;  /* velocity mode off by default */
    ch->sid_override   = MIDI_OVERRIDE_NONE;
    ch->voice_override = MIDI_OVERRIDE_NONE;
    /* Audible defaults, not silent ones: triangle needs no PWM setting to
     * produce output (a default pulse wave would be 0% duty, i.e. silent),
     * and nonzero sustain means a held note doesn't decay to silence right
     * after attack. Without both, gate/frequency/envelope timing was
     * correct but totally silent. */
    ch->tmpl_contr     = BIT_4;   /* triangle */
    ch->tmpl_attdec    = 0x00;    /* instant attack, instant decay to sustain */
    ch->tmpl_susrel    = 0xF0;    /* full sustain, fast release */
    ch->tmpl_pwmlo     = 0;
    ch->tmpl_pwmhi     = 0;
    ch->unison_detune  = 0;

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

    ch->lfo2_wave     = MIDI_LFO_TRI;
    ch->lfo2_rate     = 32;
    ch->lfo2_depth    = 0;   /* off */
    ch->lfo2_dest     = MIDI_LFO_DEST_PITCH;
    ch->lfo2_phase    = 0;
    ch->lfo2_sh_seed  = (uint32_t)(c + 1) * 40503u; /* distinct multiplier from lfo_sh_seed's,
                                                        keeping LFO1 and LFO2 S&H from locking step */
    ch->lfo2_sh_value = 0;

    ch->arp_mode           = MIDI_ARP_UP;
    ch->arp_rate           = 64;
    ch->arp_octaves        = 0;
    ch->arp_table_sel      = 0;
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
   * allocation on that channel a no-op: no separate "enabled" flag is
   * needed. Nothing else in this file special-cases it. */

  return;
}

/**
 * @brief Compute the runtime-usable voice mask for a MIDI channel
 *
 * Starts from the channel's configured voice_mask, restricts it to the SID
 * slots actually present (cfg.numsids), then further restricts it to a
 * single SID and/or a single voice if the channel's sid_override or
 * voice_override is set.
 *
 * @param uint8_t channel
 * @return uint16_t effective voice mask
 */
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
static_assert(MIDI_CC_FN_COUNT <= MIDI_CC_MAP_SLOTS,
  "[MIDI CONFIG] MIDI_CC_FN_COUNT exceeds MIDI_CC_MAP_SLOTS");

/* Version 3 blob, frozen: load-side migration source only, never written.
 * Same header as v4, 64-byte midi_ccvalues map in place of ccmap_count/ccmap[]. */
#define MIDI_CONFIG_VERSION_V3 3

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t crc32;
  uint8_t  saveid;
  uint32_t sequence;
  midi_ccvalues       ccmap;  /* ignored on load, compiled defaults kept */
  midi_channel_cfg_t  channel[MAX_CHANNELS];
  midi_patch_t        patch[MIDI_PATCH_COUNT];
  midi_arp_table_t    arp_table[MIDI_ARP_TABLE_COUNT];
} midi_config_blob_v3_t;

/* Shipped v3 layout (arm-none-eabi-gcc, pre-v4 midi_config.h). A change to
 * midi_channel_cfg_t/midi_patch_t/midi_arp_table_t breaks v3 migration */
static_assert(sizeof(midi_config_blob_v3_t) == 2484,
  "[MIDI CONFIG] midi_config_blob_v3_t size differs from the shipped v3 layout");
static_assert(offsetof(midi_config_blob_v3_t, channel) == 84,
  "[MIDI CONFIG] midi_config_blob_v3_t channel offset differs from the shipped v3 layout");
static_assert(offsetof(midi_config_blob_v3_t, patch) == 1364,
  "[MIDI CONFIG] midi_config_blob_v3_t patch offset differs from the shipped v3 layout");
static_assert(offsetof(midi_config_blob_v3_t, arp_table) == 2196,
  "[MIDI CONFIG] midi_config_blob_v3_t arp_table offset differs from the shipped v3 layout");
/* Slot scan reads the header of either version through midi_config_blob_t */
static_assert(offsetof(midi_config_blob_v3_t, version)  == offsetof(midi_config_blob_t, version)
           && offsetof(midi_config_blob_v3_t, size)     == offsetof(midi_config_blob_t, size)
           && offsetof(midi_config_blob_v3_t, crc32)    == offsetof(midi_config_blob_t, crc32)
           && offsetof(midi_config_blob_v3_t, saveid)   == offsetof(midi_config_blob_t, saveid)
           && offsetof(midi_config_blob_v3_t, sequence) == offsetof(midi_config_blob_t, sequence),
  "[MIDI CONFIG] v3 and v4 blob headers differ");

/* Written into slot 0 the first time midi_config_save() ever runs, then
 * incremented on every save after that (including ones from a later boot,
 * since it is re-seeded from the highest sequence a load actually found -
 * see midi_config_load()). Mirrors config.c's own config_saveid static. */
static uint8_t  midi_saveid = 0;
static uint32_t midi_sequence = 0;

/**
 * @brief IEEE 802.3 CRC32 over a byte buffer, bit-by-bit
 *
 * Bit-by-bit rather than table-based: a save is rare enough that a
 * 256-entry lookup table isn't worth the flash.
 *
 * @param uint8_t *data
 * @param size_t len
 * @return uint32_t CRC32 checksum
 */
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

/**
 * @brief Compute the CRC32 of a MIDI config blob's persisted content
 *
 * Covers everything from `saveid` onward, i.e. everything except the
 * magic/version/size/crc32 header fields themselves, which describe the
 * blob rather than being covered by its own checksum.
 *
 * @param midi_config_blob_t *blob
 * @return uint32_t CRC32 of the blob's covered range
 */
static uint32_t midi_blob_crc(const midi_config_blob_t *blob)
{
  const uint8_t *start = (const uint8_t *)&blob->saveid;
  size_t len = sizeof(midi_config_blob_t) - offsetof(midi_config_blob_t, saveid);
  return midi_crc32(start, len);
}

/** @brief: CRC32 of a frozen v3 blob, same covered range as midi_blob_crc() */
static uint32_t midi_blob_v3_crc(const midi_config_blob_v3_t *blob)
{
  const uint8_t *start = (const uint8_t *)&blob->saveid;
  size_t len = sizeof(midi_config_blob_v3_t) - offsetof(midi_config_blob_v3_t, saveid);
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
 * Unlike config.c's write_config_lowlevel(), every MIDI slot in this file is its
 * own whole sector: each save must erase it every time.
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

/**
 * @brief Save the current MIDI configuration to the following flash slot
 *
 * Builds a midi_config_blob_t (channels, patches, arp tables, CC map),
 * stamps it with header/sequence/CRC32, and writes it round-robin.
 * CC map: MIDI_CC_FN_COUNT live bytes, remaining slots MIDI_CC_UNASSIGNED.
 */
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
  midi_ccvalues live_map;
  midi_handler_get_ccmap(&live_map);
  blob->ccmap_count = MIDI_CC_FN_COUNT;
  memcpy(blob->ccmap, &live_map, MIDI_CC_FN_COUNT);
  memset(&blob->ccmap[MIDI_CC_FN_COUNT], MIDI_CC_UNASSIGNED, MIDI_CC_MAP_SLOTS - MIDI_CC_FN_COUNT);
  memcpy(blob->channel, midi_channels, sizeof(blob->channel));
  memcpy(blob->patch, midi_patches, sizeof(blob->patch));
  memcpy(blob->arp_table, arp_tables, sizeof(blob->arp_table));
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
 * @brief Clear transient runtime state after a bulk overwrite of
 *        midi_channels[] from flash
 *
 * The persisted blob keeps the whole live midi_channel_cfg_t rather than a
 * separate persistable-subset struct: a reboot never resumes mid-glide
 * or with a note the arpeggiator thinks is still held - every field that
 * would be wrong to resume with gets reset in this function.
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
    ch->lfo2_phase          = 0;
    ch->lfo2_sh_value       = 0;
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

/** @brief: Read-only XIP flash address of save slot `slot` */
static inline const uint8_t *midi_slot_xip(uint8_t slot)
{
  return (const uint8_t *)(XIP_BASE + FLASH_MIDICONFIG_OFFSET + (MIDICONFIG_SLOT_SIZE * slot));
}

/**
 * @brief: Validate one slot in place, magic + version + size + CRC32
 *
 * @param const uint8_t *slot, XIP flash address of the slot
 * @return uint16_t, MIDI_CONFIG_VERSION, MIDI_CONFIG_VERSION_V3 or 0 when invalid
 */
static uint16_t midi_slot_valid_version(const uint8_t *slot)
{
  const midi_config_blob_t *b = (const midi_config_blob_t *)slot;
  if (b->magic != MIDI_CONFIG_MAGIC) return 0;
  if (b->version == MIDI_CONFIG_VERSION && b->size == sizeof(midi_config_blob_t)) {
    return (midi_blob_crc(b) == b->crc32) ? MIDI_CONFIG_VERSION : 0;
  }
  if (b->version == MIDI_CONFIG_VERSION_V3 && b->size == sizeof(midi_config_blob_v3_t)) {
    const midi_config_blob_v3_t *v3 = (const midi_config_blob_v3_t *)slot;
    return (midi_blob_v3_crc(v3) == v3->crc32) ? MIDI_CONFIG_VERSION_V3 : 0;
  }
  return 0;  /* other layout, do not trust it */
}

/**
 * @brief: Build a midi_ccvalues from a stored v4 map
 *
 * Ids below ccmap_count take the stored byte. Ids at or above it take the
 * compiled default unless an earlier id holds that CC, else MIDI_CC_UNASSIGNED.
 * Stored ids past MIDI_CC_FN_COUNT are ignored.
 *
 * @param const midi_config_blob_t *b, validated v4 blob
 * @param midi_ccvalues *out, unvalidated map for midi_handler_set_ccmap()
 */
static void midi_ccmap_from_blob(const midi_config_blob_t *b, midi_ccvalues *out)
{
  uint8_t *m = (uint8_t *)out;
  const uint8_t *def = (const uint8_t *)&midi_ccvalues_defaults;
  uint8_t count = (b->ccmap_count > MIDI_CC_FN_COUNT) ? MIDI_CC_FN_COUNT : b->ccmap_count;
  uint32_t held[4] = { 0 };  /* one bit per CC number 0-127 */

  for (uint8_t id = 0; id < count; id++) {
    uint8_t cc = b->ccmap[id];
    m[id] = cc;
    if (cc <= MIDI_CC_MAX) held[cc >> 5] |= 1u << (cc & 31);
  }
  for (uint8_t id = count; id < MIDI_CC_FN_COUNT; id++) {
    uint8_t cc = def[id];
    if (cc > MIDI_CC_MAX || (held[cc >> 5] & (1u << (cc & 31)))) {
      m[id] = MIDI_CC_UNASSIGNED;
      continue;
    }
    m[id] = cc;
    held[cc >> 5] |= 1u << (cc & 31);
  }
  return;
}

/**
 * @brief Load the most recently saved MIDI configuration from flash
 *
 * Scans all slots in place through XIP, validates magic/version/size/CRC32
 * for v4 and frozen v3, keeps the highest sequence across both and applies
 * it. v3 keeps the compiled default CC map, an invalid v4 map too (usERR).
 * Leaves compiled-in defaults if no slot is valid.
 */
void midi_config_load(void)
{
  if (!midiconfig_offset_ok) {
    usERR("[MIDI CONFIG] Refusing to load: MIDI flash offset failed its boot-time cross-check\n");
    return;
  }

  int32_t best_slot = -1;
  uint16_t best_version = 0;
  uint32_t best_sequence = 0;

  for (uint8_t slot = 0; slot < MIDICONFIG_SAVE_SLOTS; slot++) {
    /* Memory-mapped flash read, same technique load_config() (config.c)
     * uses - XIP flash is plain memory, read in place without a copy. */
    const uint8_t *p = midi_slot_xip(slot);
    uint16_t version = midi_slot_valid_version(p);
    if (version == 0) continue;

    uint32_t sequence = ((const midi_config_blob_t *)p)->sequence;
    if (best_slot < 0 || sequence > best_sequence) {
      best_slot = slot;
      best_version = version;
      best_sequence = sequence;
    }
  }

  if (best_slot < 0) {
    usCFG("[MIDI CONFIG] No valid saved state found, keeping compiled-in defaults\n");
    return;
  }

  const uint8_t *p = midi_slot_xip((uint8_t)best_slot);
  if (best_version == MIDI_CONFIG_VERSION_V3) {
    const midi_config_blob_v3_t *v3 = (const midi_config_blob_v3_t *)p;
    midi_handler_reset_ccmap(MIDI_CCMAP_RESET_DEFAULTS);  /* v3 map never user-chosen */
    memcpy(midi_channels, v3->channel, sizeof(midi_channels));
    memcpy(midi_patches, v3->patch, sizeof(midi_patches));
    memcpy(arp_tables, v3->arp_table, sizeof(arp_tables));
  } else {
    const midi_config_blob_t *b = (const midi_config_blob_t *)p;
    midi_ccvalues map;
    midi_ccmap_from_blob(b, &map);
    if (!midi_handler_set_ccmap(&map)) {
      midi_handler_reset_ccmap(MIDI_CCMAP_RESET_DEFAULTS);
      usERR("[MIDI CONFIG] Stored CC map in slot %u invalid, keeping compiled defaults\n", (unsigned)best_slot);
    }
    memcpy(midi_channels, b->channel, sizeof(midi_channels));
    memcpy(midi_patches, b->patch, sizeof(midi_patches));
    memcpy(arp_tables, b->arp_table, sizeof(arp_tables));
  }
  sanitise_loaded_channels();

  midi_saveid = (uint8_t)((best_slot + 1) % MIDICONFIG_SAVE_SLOTS);
  midi_sequence = best_sequence;

  usCFG("[MIDI CONFIG] Loaded slot %u (v%u, sequence %u). Next save will use slot %u\n",
    (unsigned)best_slot, (unsigned)best_version, best_sequence, midi_saveid);
  return;
}

/**
 * @brief Erase one MIDI config flash slot, run inside flash_safe_execute()
 *
 * The slot offset is passed through `param` as its numeric value, not a
 * pointer - it's fully known before the call, nothing to point to.
 *
 * @param void *param, the slot's flash offset, cast to a pointer-sized value
 */
static void __no_inline_not_in_flash_func(erase_one_midi_slot_lowlevel)(void *param)
{ /* No logging in this function to avoid errors, matching write_midi_config_lowlevel() */
  uint32_t slot_offset = (uint32_t)(uintptr_t)param;
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(slot_offset, MIDICONFIG_SLOT_SIZE);
  restore_interrupts(ints);
  return;
}

/**
 * @brief Reset MIDI configuration to compiled-in defaults and erase flash
 *
 * Reinitialises midi_channels/midi_patches/arp_tables and the CC map in RAM,
 * then erases every flash slot and resets the save/sequence counters.
 */
void midi_config_reset(void)
{
  midi_config_init();
  midi_patch_init();
  midi_arp_table_init();
  midi_handler_reset_ccmap(MIDI_CCMAP_RESET_DEFAULTS);

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
