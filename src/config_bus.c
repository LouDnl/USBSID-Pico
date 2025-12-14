/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config_bus.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024-2025 LouD
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


#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "globals.h"
#include "config.h"
#include "gpio.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"
#include "macros.h"


/* Config */
extern Config usbsid_config;
extern RuntimeCFG cfg;

/* Declare variables */
const uint8_t sidaddr_default[4] = { 0x00, 0x20, 0x40, 0x60 };
const uint8_t sidmask_default[5] = { 0b100, 0b100, 0b010, 0b010, 0b110 };
const uint8_t addrmask_default[5] = { 0x1F, 0x3F, 0x1F, 0x3F, 0x00 };


void apply_bus_config_OLD(void)
{ /* NOTE: DEPRECATED */
  /* Apply defaults */
  cfg.sidtype[0] = usbsid_config.socketOne.sid1.type;  /* SID1 */
  cfg.sidtype[1] = usbsid_config.socketOne.sid2.type;  /* SID2 */
  cfg.sidtype[2] = usbsid_config.socketTwo.sid1.type;  /* SID3 */
  cfg.sidtype[3] = usbsid_config.socketTwo.sid2.type;  /* SID4 */
  cfg.sidaddr[0] = usbsid_config.socketOne.sid1.addr;
  cfg.sidaddr[1] = usbsid_config.socketOne.sid2.addr;
  cfg.sidaddr[2] = usbsid_config.socketTwo.sid1.addr;
  cfg.sidaddr[3] = usbsid_config.socketTwo.sid2.addr;

  /* one == 0x00, two == 0x20, three == 0x40, four == 0x60 */
  if (cfg.mirrored) {                    /* act-as-one enabled overrules all settings */
    cfg.one = cfg.two = 0;                     /* CS1 low, CS2 low */
    cfg.three = cfg.four = 0;                  /* CS1 low, CS2 low */
    cfg.one_mask = cfg.two_mask = cfg.three_mask = cfg.four_mask = 0x1F;
    /* No changes to sidtypes */
  } else {
    if (cfg.sock_one && !cfg.sock_two) {       /* SocketOne enabled, SocketTwo disabled */
      cfg.one = 0b100;                     /* CS1 low, CS2 high */
      cfg.two = (cfg.sids_one == 2) ? 0b100    /* CS1 low, CS2 high */
        : 0b110;                       /* CS1 high, CS2 high */
      cfg.three = cfg.four = 0b110;            /* CS1 high, CS2 high */
      cfg.one_mask = 0x1F;
      cfg.two_mask = (cfg.sids_one == 2) ? 0x3F : 0x0;
      cfg.three_mask = 0x0;
      cfg.four_mask = 0x0;
      /* Apply differences */
      cfg.sidtype[1] = (cfg.sids_one == 2)  /* SID2 */
        ? usbsid_config.socketOne.sid2.type : 1;
      cfg.sidtype[2] = 1;  /* SID3 */
      cfg.sidtype[3] = 1;  /* SID4 */
    }
    if (!cfg.sock_one && cfg.sock_two) {       /* SocketOne disabled, SocketTwo enabled */
      cfg.one = 0b010;                     /* CS1 high, CS2 low */
      cfg.two = (cfg.sids_two == 2) ? 0b010    /* CS1 high, CS2 low */
        : 0b110;                       /* CS1 high, CS2 high */
      cfg.three = cfg.four = 0b110;            /* CS1 high, CS2 high */
      cfg.one_mask = 0x1F;
      cfg.two_mask = (cfg.sids_two == 2) ? 0x3F : 0x0;
      cfg.three_mask = 0x0;
      cfg.four_mask = 0x0;
      /* Apply differences */
      cfg.sidtype[0] = usbsid_config.socketTwo.sid1.type;  /* SID1 */
      cfg.sidtype[1] = (cfg.sids_two == 2)  /* SID2 */
        ? usbsid_config.socketTwo.sid2.type : 1;
      cfg.sidtype[2] = 1;  /* SID3 */
      cfg.sidtype[3] = 1;  /* SID4 */
    }
    if (cfg.sock_one && cfg.sock_two) {        /* SocketOne enabled, SocketTwo enabled */
      /* TODO: Compact if else spiderweb */
      if (cfg.sids_one == 1 && cfg.sids_two == 1) {
        cfg.one   = 0b100;
        cfg.two   = 0b010;
        cfg.three = 0b110;
        cfg.four  = 0b110;
        cfg.one_mask = 0x1F;
        cfg.two_mask = 0x1F;
        cfg.three_mask = 0x0;
        cfg.four_mask = 0x0;
        /* Apply differences */
        cfg.sidtype[0] = usbsid_config.socketOne.sid1.type;  /* SID1 */
        cfg.sidtype[1] = usbsid_config.socketTwo.sid1.type;  /* SID2 */
        cfg.sidtype[2] = 1;  /* SID3 */
        cfg.sidtype[3] = 1;  /* SID4 */
      }
      if (cfg.sids_one == 2 && cfg.sids_two == 1) {
        cfg.one   = 0b100;
        cfg.two   = 0b100;
        cfg.three = 0b010;
        cfg.four  = 0b110;
        cfg.one_mask = 0x1F;
        cfg.two_mask = 0x3F;
        cfg.three_mask = 0x1F;
        cfg.four_mask = 0x0;
        /* Apply differences */
        cfg.sidtype[0] = usbsid_config.socketOne.sid1.type;  /* SID1 */
        cfg.sidtype[1] = usbsid_config.socketOne.sid2.type;  /* SID2 */
        cfg.sidtype[2] = usbsid_config.socketTwo.sid1.type;  /* SID3 */
        cfg.sidtype[3] = 1;  /* SID4 */
      }
      if (cfg.sids_one == 1 && cfg.sids_two == 2) {
        cfg.one   = 0b100;
        cfg.two   = 0b010;
        cfg.three = 0b010;
        cfg.four  = 0b110;
        cfg.one_mask = 0x1F;
        cfg.two_mask = 0x1F;
        cfg.three_mask = 0x3F;
        cfg.four_mask = 0x0;
        /* Apply differences */
        cfg.sidtype[0] = usbsid_config.socketOne.sid1.type;  /* SID1 */
        cfg.sidtype[1] = usbsid_config.socketTwo.sid2.type;  /* SID2 */
        cfg.sidtype[2] = usbsid_config.socketTwo.sid2.type;  /* SID3 */
        cfg.sidtype[3] = 1;  /* SID4 */
      }
      if (cfg.sids_one == 2 && cfg.sids_two == 2) {
        cfg.one   = 0b100;
        cfg.two   = 0b100;
        cfg.three = 0b010;
        cfg.four  = 0b010;
        cfg.one_mask = 0x1F;
        cfg.two_mask = 0x3F;
        cfg.three_mask = 0x1F;
        cfg.four_mask = 0x3F;
        /* No changes to sidtypes */
      }
    }
  }
  CFG("[TEST OLD] ONE:%02X ONE:%02X TWO:%02X TWO:%02X THREE:%02X THREE:%02X FOUR:%02X FOUR:%02X\n",
      cfg.one, cfg.one_mask, cfg.two, cfg.two_mask, cfg.three, cfg.three_mask, cfg.four, cfg.four_mask);
  return;
}

void apply_bus_config(bool quiet) // ISSUE: FINISH
{ /* bus config doesn't care if the sid is real or fmopl */
  if (!quiet) CFG("[CONFIG] Applying bus settings\n");

  /* Default everything first */
  memset(cfg.ids, 4, 4);  /* Default to 4 N/A */
  memset(cfg.sidid, 4, 4);  /* Default to 4 N/A */
  memset(cfg.sidtype, 1, 4);  /* Default to 1 N/A */
  memset(cfg.sidaddr, 0xFF, 4); /* Default to 0xFF N/A */
  memset(cfg.sidmask, 0x00, 4); /* Default to 0x00 N/A */
  memset(cfg.addrmask, 0b110, 4); /* Default to 0b110 N/A */
  cfg.one = cfg.two = cfg.three = cfg.four = sidmask_default[4]; /* Default to 0x00 N/A */
  cfg.one_mask = cfg.two_mask = cfg.three_mask = cfg.four_mask = addrmask_default[4]; /* Default to 0b110 N/A */

  /* Store SID id's from config in physical order for address arrangements */
  cfg.sidid[0] = usbsid_config.socketOne.sid1.id;
  cfg.sidid[1] = usbsid_config.socketOne.sid2.id;
  cfg.sidid[2] = usbsid_config.socketTwo.sid1.id;
  cfg.sidid[3] = usbsid_config.socketTwo.sid2.id;
  // TODO: Fix is so this is not nescessary by defaulting to 4
  for (int id = 0; id < 4; id++) { /* Change 0xFF to 4 for runtime use */
    /* Set the configured SID order to physical SID order relation */
    cfg.sidid[id] = ((cfg.sidid[id] == 0xFF) ? 4 : cfg.sidid[id]);
  }

  /* Store the config socket->sid id of each addressable SID id in ids */
  for (int id = 0; id < 4; id++) {
    for (int i = 0; i < 4; i++) {
      if (cfg.sidid[id] == i) {
        cfg.ids[i] = id;
      }
    }
  }

  /* Pre set all settings from config */
  /* Ordered by physical SID */
  cfg.sidtype[0] = usbsid_config.socketOne.sid1.type;  /* Physical */
  cfg.sidtype[1] = usbsid_config.socketOne.sid2.type;  /* Virtual */
  cfg.sidtype[2] = usbsid_config.socketTwo.sid1.type;  /* Physical */
  cfg.sidtype[3] = usbsid_config.socketTwo.sid2.type;  /* Virtual */
  cfg.sidaddr[0] = usbsid_config.socketOne.sid1.addr;  /* Physical */
  cfg.sidaddr[1] = usbsid_config.socketOne.sid2.addr;  /* Virtual */
  cfg.sidaddr[2] = usbsid_config.socketTwo.sid1.addr;  /* Physical */
  cfg.sidaddr[3] = usbsid_config.socketTwo.sid2.addr;  /* Virtual */
  /* Ordered by configured SID */
  cfg.sidmask[0] = sidmask_default[cfg.ids[0]];
  cfg.sidmask[1] = sidmask_default[cfg.ids[1]];
  cfg.sidmask[2] = sidmask_default[cfg.ids[2]];
  cfg.sidmask[3] = sidmask_default[cfg.ids[3]];
  cfg.addrmask[0] = addrmask_default[cfg.ids[0]];
  cfg.addrmask[1] = addrmask_default[cfg.ids[1]];
  cfg.addrmask[2] = addrmask_default[cfg.ids[2]];
  cfg.addrmask[3] = addrmask_default[cfg.ids[3]];

  if (!quiet) {
  for (int id = 0; id < 4; id++) {
      CFG("[TEST0] ID:%d IDS:%d SIDID:%d TYPE:%02X ADDR:%02X MASK:%02X ADDRMASK:%02X\n",
        id, cfg.ids[id], cfg.sidid[id], cfg.sidtype[id], cfg.sidaddr[id], cfg.sidmask[cfg.ids[id]], cfg.addrmask[cfg.ids[id]]);
    }
  }

  /* Mirrored is easy */
  if (cfg.mirrored) {  /* Mirrored (act-as-one) overrules everything at runtime :) */
    cfg.one = cfg.two = cfg.three = cfg.four = 0;  /* CS1 & CS2 low */
    if ((cfg.sock_one_dual == true) && (cfg.numsids == 2)) {
      cfg.one_mask = cfg.three_mask = 0x1F;
      cfg.two_mask = cfg.four_mask = 0x3F;
      cfg.sidtype[2] = cfg.sidtype[0];  /* Map sidtype to SID1 */
      cfg.sidtype[3] = cfg.sidtype[1];  /* Map sidtype to SID2 */
    } else {
      cfg.one_mask = cfg.two_mask = cfg.three_mask = cfg.four_mask = 0x1F;  /* Map everything to SID1 */
      cfg.sidtype[1] = cfg.sidtype[2] = cfg.sidtype[3] = cfg.sidtype[0];    /* Map each sidtype to SID1 */
    }

  } else { /* Now for the tricky part */

    /* Set everything from all presets */
    cfg.one   = cfg.ids[0] != 4 ? cfg.sidmask[cfg.sidid[cfg.ids[0]]] : sidmask_default[cfg.ids[0]];
    cfg.two   = cfg.ids[1] != 4 ? cfg.sidmask[cfg.sidid[cfg.ids[1]]] : sidmask_default[cfg.ids[1]];
    cfg.three = cfg.ids[2] != 4 ? cfg.sidmask[cfg.sidid[cfg.ids[2]]] : sidmask_default[cfg.ids[2]];
    cfg.four  = cfg.ids[3] != 4 ? cfg.sidmask[cfg.sidid[cfg.ids[3]]] : sidmask_default[cfg.ids[3]];
    cfg.one_mask   = cfg.ids[0] != 4 ? cfg.addrmask[cfg.sidid[cfg.ids[0]]] : addrmask_default[cfg.ids[0]];
    cfg.two_mask   = cfg.ids[1] != 4 ? cfg.addrmask[cfg.sidid[cfg.ids[1]]] : addrmask_default[cfg.ids[1]];
    cfg.three_mask = cfg.ids[2] != 4 ? cfg.addrmask[cfg.sidid[cfg.ids[2]]] : addrmask_default[cfg.ids[2]];
    cfg.four_mask  = cfg.ids[3] != 4 ? cfg.addrmask[cfg.sidid[cfg.ids[3]]] : addrmask_default[cfg.ids[3]];

    if (!quiet) CFG("[TEST 1] ONE:%02X ONE:%02X TWO:%02X TWO:%02X THREE:%02X THREE:%02X FOUR:%02X FOUR:%02X\n",
      cfg.one, cfg.one_mask, cfg.two, cfg.two_mask, cfg.three, cfg.three_mask, cfg.four, cfg.four_mask);
    /* EVERYTHING WORKS UP TO HERE BUT IGNORES SOCKET SETTINGS */

    /* Disable things based on preset enabled/disabled sockets */
    if (cfg.sock_one && !cfg.sock_two) {
      if (cfg.sids_one == 2) { /* No changes */ };
      if (cfg.sids_one == 1) { cfg.sidtype[1] = 1; };  /* Physical (virtual) SID 2 to N/A */
      cfg.sidtype[2] = 1;  /* Physical SID 3 to N/A */
      cfg.sidtype[3] = 1;  /* Physical (virtual) SID 4 to N/A */
    }
    if (!cfg.sock_one && cfg.sock_two) {
      if (cfg.sids_two == 2) { /* No changes */ };
      if (cfg.sids_two == 1) { cfg.sidtype[2] = 1; };   /* Physical SID 3 to N/A */
      cfg.sidtype[0] = 1;  /* Physical SID 1 to N/A */
      cfg.sidtype[1] = 1;  /* Physical SID 2 to N/A */
    }
    if (cfg.sock_one && cfg.sock_two) { /* NOPE */

      if (cfg.sids_one == 2 && cfg.sids_two == 2) { /* No changes */ };
      if (cfg.sids_one == 2 && cfg.sids_two == 1) { /* NOPE */ };
      if (cfg.sids_one == 1 && cfg.sids_two == 2) { /* NOPE */ };
      if (cfg.sids_one == 1 && cfg.sids_two == 1) { /* NOPE */ };

    }

    if (!quiet) CFG("[TEST 2] ONE:%02X ONE:%02X TWO:%02X TWO:%02X THREE:%02X THREE:%02X FOUR:%02X FOUR:%02X\n",
      cfg.one, cfg.one_mask, cfg.two, cfg.two_mask, cfg.three, cfg.three_mask, cfg.four, cfg.four_mask);
  }

}
