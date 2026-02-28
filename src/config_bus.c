/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config_bus.c
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


#include "globals.h"
#include "config_constants.h"
#include "config.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* config.c */
extern Config usbsid_config;
extern RuntimeCFG cfg;


/**
 * @brief ChipSelect mask by physical slot
 *
 * Slot 0 (SID 0,1) = Socket 1 (CS1)
 * Slot 1 (SID 2,3) = Socket 2 (CS2)
 * Bit pattern:
 * b2=CS2, b1=CS1 (active low)
 * b0=RW (default write, active low)
 */
static const uint8_t cs_mask_by_slot[5] = {
  0b100,  /* Slot 0: CS1 active */
  0b100,  /* Slot 1: CS1 active */
  0b010,  /* Slot 2: CS2 active */
  0b010,  /* Slot 3: CS2 active */
  0b110,  /* N/A: both inactive */
};

/**
 * @brief Address decode mask by physical slot
 *
 * Primary SIDs use 0x1F (regs 0-31)
 * Secondary SIDs use 0x3F (regs 32-64)
 */
static const uint8_t addr_mask_by_slot[5] = {
  0x1F,   /* Slot 0: Primary S1 (0x00-0x1F) */
  0x3F,   /* Slot 0: Secondary S1 (0x20-0x3F) */
  0x1F,   /* Slot 1: Primary S2 (0x40-0x5F) */
  0x3F,   /* Slot 1: Secondary S2 (0x60-0x7F) */
  0x00,   /* N/A: no address decode */
};


/**
 * @brief Apply new busmask from supplied runtime configuration
 *        The busmask is used by bus.c functions
 *
 * @param RuntimeCFG *rt
 */
void apply_bus_masks(RuntimeCFG *rt)
{
  if (rt == NULL) return;

  /* Pointers to mask outputs for easier iteration */
  uint8_t *cs_out[4] = { &rt->one, &rt->two, &rt->three, &rt->four };
  uint8_t *mask_out[4] = { &rt->one_mask, &rt->two_mask, &rt->three_mask, &rt->four_mask };

  if (rt->mirrored) {
    /* Mirrored mode: both CS lines active, same address decode */
    rt->one = rt->two = rt->three = rt->four = 0;  /* Both CS low = active */

    if (rt->sock_one_dual) {
      /* Dual mirrored: SID1 at 0x00, SID2 at 0x20 on both sockets */
      rt->one_mask = rt->three_mask = 0x1F;  /* SID1 mask */
      rt->two_mask = rt->four_mask = 0x3F;   /* SID2 mask */
      /* Mirror types from S1 to S2 */
      rt->sidtype[2] = rt->sidtype[0];
      rt->sidtype[3] = rt->sidtype[1];
    } else {
      /* Single mirrored: only SID1 on both sockets */
      rt->one_mask = rt->two_mask = rt->three_mask = rt->four_mask = 0x1F;
      /* All types match SID1 */
      rt->sidtype[1] = rt->sidtype[2] = rt->sidtype[3] = rt->sidtype[0];
    }
    return;
  }

  /* Non-mirrored: compute per-SID masks based on ID→slot mapping */
  for (int id = 0; id < 4; id++) {
    int slot = rt->ids[id];

    if (slot == 4 || slot > 3) {
      /* N/A slot: disable this SID */
      *cs_out[id] = cs_mask_by_slot[4];    /* Both CS high */
      *mask_out[id] = addr_mask_by_slot[4]; /* No address decode */
    } else {
      *cs_out[id] = cs_mask_by_slot[slot];
      *mask_out[id] = addr_mask_by_slot[slot];
    }
  }

  /* Apply socket disable rules to sidtype array */
  if (rt->sock_one && !rt->sock_two) {
    /* Only Socket 1 enabled */
    if (!rt->sock_one_dual) {
      rt->sidtype[1] = SID_NA;  /* S1 SID2 not present */
    }
    rt->sidtype[2] = SID_NA;  /* S2 SID1 not present */
    rt->sidtype[3] = SID_NA;  /* S2 SID2 not present */
  }
  else if (!rt->sock_one && rt->sock_two) {
    /* Only Socket 2 enabled */
    rt->sidtype[0] = SID_NA;  /* S1 SID1 not present */
    rt->sidtype[1] = SID_NA;  /* S1 SID2 not present */
    if (!rt->sock_two_dual) {
      rt->sidtype[3] = SID_NA;  /* S2 SID2 not present */
    }
  }
  else if (rt->sock_one && rt->sock_two) {
    /* Both sockets enabled */
    if (!rt->sock_one_dual) {
      rt->sidtype[1] = SID_NA;  /* S1 has no SID2 */
    }
    if (!rt->sock_two_dual) {
      rt->sidtype[3] = SID_NA;  /* S2 has no SID2 */
    }
  }

  return;
}

/**
 * @brief Applies a config to a runtime config and calls `apply_bus_masks`
 *
 * @param Config *config
 * @param RuntimeCFG *rt
 */
void apply_runtime_config(const Config *config, RuntimeCFG *rt)
{
  if (config == NULL || rt == NULL) return;

  /* Clear supplied runtime config first */
  uint32_t irq = save_and_disable_interrupts();
  memset(rt, 0, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  /* Socket state flags */
  rt->sock_one = config->socketOne.enabled;
  rt->sock_two = config->socketTwo.enabled;
  rt->sock_one_dual = config->socketOne.dualsid;
  rt->sock_two_dual = config->socketTwo.dualsid;
  rt->mirrored = config->mirrored;

  /* Chip types */
  rt->chip_one = config->socketOne.chiptype;
  rt->chip_two = config->socketTwo.chiptype;

  /* SID counts */
  if (rt->mirrored) {
    /* Mirrored mode: numsids reflects what's addressable, not physical */
    rt->sids_one = rt->sock_one_dual ? 2 : 1;
    rt->sids_two = rt->sids_one;  /* Mirror S1 count */
    rt->numsids = rt->sids_one;   /* Only S1's count matters */
  } else {
    rt->sids_one = rt->sock_one ? (rt->sock_one_dual ? 2 : 1) : 0;
    rt->sids_two = rt->sock_two ? (rt->sock_two_dual ? 2 : 1) : 0;
    rt->numsids = rt->sids_one + rt->sids_two;
  }

  /* Physical SID slot data (indexed 0-3) */
  const SIDChip *slots[4] = {
    &config->socketOne.sid1,  /* Slot 0: S1 primary */
    &config->socketOne.sid2,  /* Slot 1: S1 secondary */
    &config->socketTwo.sid1,  /* Slot 2: S2 primary */
    &config->socketTwo.sid2,  /* Slot 3: S2 secondary */
  };

  /* Initialise with defaults */
  memset(rt->ids, 4, 4);          /* Default: all slots N/A */
  memset(rt->sidid, 4, 4);        /* Default: all IDs N/A */
  memset(rt->sidtype, SID_NA, 4); /* Default: all types N/A */
  memset(rt->sidaddr, 0xFF, 4);   /* Default: all addresses disabled */
  memset(rt->sidmask, 0, 4);      /* Default: no masks */
  memset(rt->addrmask, 0, 4);     /* Default: no address masks */

  /* Copy from config, converting 0xFF IDs to 4 (N/A marker) */
  for (int slot = 0; slot < 4; slot++) {
    uint8_t id = slots[slot]->id;
    rt->sidid[slot] = (id == 0xFF) ? 4 : id;
    rt->sidaddr[slot] = slots[slot]->addr;
    rt->sidtype[slot] = slots[slot]->type;
  }

  /* Build reverse mapping: ID → slot */
  for (int slot = 0; slot < 4; slot++) {
    uint8_t id = rt->sidid[slot];
    if (id < 4) {
      rt->ids[id] = slot;
    }
  }

  /* Assign masks based on slot positions */
  for (int slot = 0; slot < 4; slot++) {
    rt->sidmask[slot] = cs_mask_by_slot[slot];
    rt->addrmask[slot] = addr_mask_by_slot[slot];
  }

  /* Apply bus control masks */
  apply_bus_masks(rt);

  usCFG("Applied RuntimeCFG sids=%d (s1=%d s2=%d) mirrored=%d\n",
    rt->numsids, rt->sids_one, rt->sids_two, rt->mirrored);

  return;
}
