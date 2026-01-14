/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config_socket.c
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
#include "config.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* Config */
extern void save_load_apply_config(bool at_boot, bool print_cfg);
extern Config usbsid_config;
extern RuntimeCFG cfg;

/* Config bus */
extern void apply_bus_config(bool quiet);
extern uint8_t sidaddr_default[4];

/* Pre declarations */
void apply_socket_change(bool quiet);


/* Called from config.c:apply_config, config.c:apply_socket_change and set_socket_config */
void verify_socket_settings(void)
{
  /* Pre applying default SocketOne settings if needed */
  if (usbsid_config.socketOne.enabled == true) {
    if (usbsid_config.socketOne.dualsid == true) {
      if (usbsid_config.socketOne.chiptype != 1) {
        usbsid_config.socketOne.chiptype = 1;  /* chiptype cannot be real with dualsid */
      }
      if (usbsid_config.socketOne.clonetype == 0) {
        usbsid_config.socketOne.clonetype = 1;  /* clonetype cannot be disabled with dualsid */
      }
    } else {
      if (usbsid_config.socketOne.chiptype == 1) {
        if (usbsid_config.socketOne.clonetype == 0) {
          usbsid_config.socketOne.clonetype = 1;  /* clonetype cannot be disabled when chiptype is clone */
        }
      } else {
        usbsid_config.socketOne.clonetype = 0;  /* clonetype cannot be anything else when chiptype is real */
      }
    }
  }
  /* Pre applying default SocketTwo settings if needed */
  if (usbsid_config.socketTwo.enabled == true) {
    if (usbsid_config.socketTwo.dualsid == true) {
      if (usbsid_config.socketTwo.chiptype != 1) {
        usbsid_config.socketTwo.chiptype = 1;  /* chiptype cannot be real with dualsid */
      }
      if (usbsid_config.socketTwo.clonetype == 0) {
        usbsid_config.socketTwo.clonetype = 1;  /* clonetype cannot be disabled with dualsid */
      }
    } else {
      if (usbsid_config.socketTwo.chiptype == 1) {
        if (usbsid_config.socketTwo.clonetype == 0) {
          usbsid_config.socketTwo.clonetype = 1;  /* clonetype cannot be disabled when chiptype is clone */
        }
      } else {
        usbsid_config.socketTwo.clonetype = 0;  /* clonetype cannot be anything else when chiptype is real */
      }
    }
  }

  return;
}

void verify_chipdetection_results(bool quiet)
{ /* Is run only from sid_detection.c:auto_detect_routine after detection in auto_config mode */
  if (!quiet) CFG("[SID] VERIFY SOCKET DETECTION RESULTS\n");
  /* Socket One */
  if (usbsid_config.socketOne.sid1.type != 0 && usbsid_config.socketOne.sid2.type == 1) {
    /* Unidentified SID as sid2? */
    usbsid_config.socketOne.dualsid = false; /* Disable dualsid */
  }
  if (usbsid_config.socketOne.sid1.type == 0 && usbsid_config.socketOne.sid2.type == 1) {
    /* 2 unidentified SID's and chiptype is Real and clonetype is Disabled? */
    if (usbsid_config.socketOne.chiptype == 0 && usbsid_config.socketOne.clonetype == 0) {
      usbsid_config.socketOne.enabled = false; /* Disable the socket */
    }
  }
  if (!quiet) CFG("[SID] SOCKET ONE CHIPTYPE: %d CLONETYPE: %d ENABLED: %d DUALSID: %d\n",
    usbsid_config.socketOne.chiptype, usbsid_config.socketOne.clonetype,
    usbsid_config.socketOne.enabled, usbsid_config.socketOne.dualsid);

  /* Socket Two */
  if (usbsid_config.socketTwo.sid1.type != 0 && usbsid_config.socketTwo.sid2.type == 1) {
    /* Unidentified SID as sid2? */
    usbsid_config.socketTwo.dualsid = false; /* Disable dualsid */
  }
  if (usbsid_config.socketTwo.sid1.type == 0 && usbsid_config.socketTwo.sid2.type == 1) {
    /* 2 unidentified SID's and chiptype is Real and clonetype is Disabled? */
    if (usbsid_config.socketTwo.chiptype == 0 && usbsid_config.socketTwo.clonetype == 0) {
      usbsid_config.socketTwo.enabled = false; /* Disable the socket */
    }
  }
  if (!quiet) CFG("[SID] SOCKET TWO CHIPTYPE: %d CLONETYPE: %d ENABLED: %d DUALSID: %d\n",
    usbsid_config.socketTwo.chiptype, usbsid_config.socketTwo.clonetype,
    usbsid_config.socketTwo.enabled, usbsid_config.socketTwo.dualsid);
}

void verify_sid_addr(bool quiet)
{ /* Is run only from sid_detection.c:auto_detect_routine after detection in auto_config mode */
  if (!quiet) CFG("[SID] REASSIGN SID ADDRESS AND ID BY SOCKET CONFIG\n");
  /* Socket One */
  if (usbsid_config.socketOne.enabled) {
    if (usbsid_config.socketOne.dualsid) {
      usbsid_config.socketOne.sid1.addr = 0x00;
      usbsid_config.socketOne.sid2.addr = 0x20;
      usbsid_config.socketOne.sid1.id = 0;
      usbsid_config.socketOne.sid2.id = 1;
    } else {
      usbsid_config.socketOne.sid1.addr = 0x00;
      usbsid_config.socketOne.sid2.addr = 0xFF;
      usbsid_config.socketOne.sid1.id = 0;
      usbsid_config.socketOne.sid2.id = 0xFF;
    }
  } else {
    usbsid_config.socketOne.sid1.addr = 0xFF;
    usbsid_config.socketOne.sid2.addr = 0xFF;
    usbsid_config.socketOne.sid1.id = 0xFF;
    usbsid_config.socketOne.sid2.id = 0xFF;
  }
  /* Socket Two */
  if (usbsid_config.socketTwo.enabled) {
    if (usbsid_config.socketTwo.dualsid) {
      if (usbsid_config.socketOne.enabled) {
        if (usbsid_config.socketOne.dualsid) {
          usbsid_config.socketTwo.sid1.addr = 0x40;
          usbsid_config.socketTwo.sid2.addr = 0x60;
          usbsid_config.socketTwo.sid1.id = 2;
          usbsid_config.socketTwo.sid2.id = 3;
        } else {
          usbsid_config.socketTwo.sid1.addr = 0x20;
          usbsid_config.socketTwo.sid2.addr = 0x40;
          usbsid_config.socketTwo.sid1.id = 1;
          usbsid_config.socketTwo.sid2.id = 2;
        }
      } else {
        usbsid_config.socketTwo.sid1.addr = 0x00;
        usbsid_config.socketTwo.sid2.addr = 0x20;
        usbsid_config.socketTwo.sid1.id = 0;
        usbsid_config.socketTwo.sid2.id = 1;
      }
    } else {
      if (usbsid_config.socketOne.enabled) {
        if (usbsid_config.socketOne.dualsid) {
          usbsid_config.socketTwo.sid1.addr = 0x40;
          usbsid_config.socketTwo.sid1.id = 2;
        } else {
          usbsid_config.socketTwo.sid1.addr = 0x20;
          usbsid_config.socketTwo.sid1.id = 1;
        }
      } else {
        usbsid_config.socketTwo.sid1.addr = 0x00;
        usbsid_config.socketTwo.sid1.id = 0;
      }
      usbsid_config.socketTwo.sid2.addr = 0xFF;
      usbsid_config.socketTwo.sid2.id = 0xFF;
    }
  } else {
    usbsid_config.socketTwo.sid1.addr = 0xFF;
    usbsid_config.socketTwo.sid2.addr = 0xFF;
    usbsid_config.socketTwo.sid1.id = 0xFF;
    usbsid_config.socketTwo.sid2.id = 0xFF;
  }
}

static void autoset_sid_default_address(void)
{ /* NOTE: DO NOT USE, UNTESTED AND UNUSED */
  int id = usbsid_config.socketOne.sid1.id;
  usbsid_config.socketOne.sid1.addr = (id != 255 ? sidaddr_default[id] : 0xFF);
  id = usbsid_config.socketOne.sid2.id;
  usbsid_config.socketOne.sid2.addr = (id != 255 ? sidaddr_default[id] : 0xFF);
  id = usbsid_config.socketTwo.sid1.id;
  usbsid_config.socketTwo.sid1.addr = (id != 255 ? sidaddr_default[id] : 0xFF);
  id = usbsid_config.socketTwo.sid2.id;
  usbsid_config.socketTwo.sid2.addr = (id != 255 ? sidaddr_default[id] : 0xFF);

  return;
}

static void autoset_sid_id(void)
{ /* NOTE: DO NOT USE, UNTESTED AND UNUSED */
  /* Set SID id's based on above config */
  // cfg.sidid[0] = (cfg.sock_one ? 0 : -1);
  // cfg.sidid[1] = ((cfg.sock_one && cfg.sock_one_dual) ? 1 : -1);
  // cfg.sidid[2] = (cfg.mirrored ? 0
  //   : (cfg.sock_two && cfg.sock_one && cfg.sock_one_dual) ? 2
  //   : (cfg.sock_two && !cfg.sock_one) ? 0
  //   : (cfg.sock_two && cfg.sock_one && !cfg.sock_one_dual) ? 1
  //   : -1);
  // cfg.sidid[3] = (cfg.mirrored ? 1
  //   : (cfg.sock_two && cfg.sock_two_dual && cfg.sock_one && cfg.sock_one_dual) ? 3
  //   : (cfg.sock_two && cfg.sock_two_dual && !cfg.sock_one) ? 1
  //   : (cfg.sock_two && cfg.sock_two_dual && cfg.sock_one && !cfg.sock_one_dual) ? 2
  //   : -1);
  // usbsid_config.socketOne.sid1.id = cfg.sidid[0];
  // usbsid_config.socketOne.sid2.id = cfg.sidid[1];
  // usbsid_config.socketTwo.sid1.id = cfg.sidid[2];
  // usbsid_config.socketTwo.sid2.id = cfg.sidid[3];

  return;
}

static uint8_t address_to_id(uint8_t addr)
{ /* NOTE: DO NOT USE, UNTESTED AND UNUSED */
  switch (addr) {
    case 0x00:
      return 0;
    case 0x20:
      return 1;
    case 0x40:
      return 2;
    case 0x60:
      return 3;
    default:
      return 0;
  }
  return 0;
}

static void set_sid_addr_id(int socket, int sid, uint8_t addr)
{ /* NOTE: DO NOT USE, UNTESTED AND UNUSED */

  switch (socket) {
    case 1:
      switch (sid) {
        case 1:
          usbsid_config.socketOne.sid1.id = address_to_id(addr);
          usbsid_config.socketOne.sid1.addr = addr;
          break;
        case 2:
          usbsid_config.socketOne.sid2.addr = addr;
          usbsid_config.socketOne.sid2.id = address_to_id(addr);
          break;
      }
      break;
    case 2:
      switch (sid) {
        case 1:
          usbsid_config.socketTwo.sid1.addr = addr;
          usbsid_config.socketTwo.sid1.id = address_to_id(addr);
          break;
        case 2:
          usbsid_config.socketTwo.sid2.addr = addr;
          usbsid_config.socketTwo.sid2.id = address_to_id(addr);
          break;
      }
      break;
  }
  return;
}

void set_sid_id_addr(int socket, int sid, int id)
{ /* Used for socket configuration */
  switch (socket) {
    case 1:
      switch (sid) {
        case 1:
          usbsid_config.socketOne.sid1.id = id;
          usbsid_config.socketOne.sid1.addr = sidaddr_default[id];
          break;
        case 2:
          usbsid_config.socketOne.sid2.id = id;
          usbsid_config.socketOne.sid2.addr = sidaddr_default[id];
          break;
      }
      break;
    case 2:
      switch (sid) {
        case 1:
          usbsid_config.socketTwo.sid1.id = id;
          usbsid_config.socketTwo.sid1.addr = sidaddr_default[id];
          break;
        case 2:
          usbsid_config.socketTwo.sid2.id = id;
          usbsid_config.socketTwo.sid2.addr = sidaddr_default[id];
          break;
      }
      break;
  }
  return;
}

/* Called from apply_config apply_socket_change */
int verify_fmopl_sidno(void)
{ /* TODO: THIS USES CONFIG AND RUNTIMECFG CROSSED, THIS MUST BE CONFIG ONLY */
  int fmoplsidno = -1;
  // if (usbsid_config.FMOpl.enabled) {
  if (usbsid_config.socketOne.enabled) {
    if ((usbsid_config.socketOne.chiptype == 1) && (cfg.sids_one >= 1)) {
      if (usbsid_config.socketOne.sid1.type == 4) {
        fmoplsidno = 1;
        return fmoplsidno;
      } else if ((usbsid_config.socketOne.sid2.type == 4) && (cfg.sids_one == 2)) {
        fmoplsidno = 2;
        return fmoplsidno;
      }
    }
  }
  if (usbsid_config.socketTwo.enabled) {// && (fmoplsidno == -1)) {
    if ((usbsid_config.socketTwo.chiptype == 1) && (cfg.sids_two >= 1)) {
      if (usbsid_config.socketTwo.sid1.type == 4) {
        fmoplsidno = (cfg.sids_one == 0)
        ? 1 : (cfg.sids_one == 1)
        ? 2 : (cfg.sids_one == 2)
        ? 3 : 0;
        return fmoplsidno;
      } else if (usbsid_config.socketTwo.sid2.type == 4 && (cfg.sids_two == 2)) {
        fmoplsidno = (cfg.sids_one == 0)
        ? 2 : (cfg.sids_one == 1)
        ? 3 : (cfg.sids_one == 2)
        ? 4 : 0;
        return fmoplsidno;
      }
    }
  }
  // }
  return fmoplsidno;
}

void apply_fmopl_config(bool quiet)
{ /* TODO: REWORK */
  if (!quiet) CFG("[CONFIG] Checking for optional FMOpl\n");
  /* FMOpl */
  int fmoplsid = verify_fmopl_sidno();
  if (fmoplsid != -1) {
    if (!quiet) CFG("[CONFIG] FMOpl @ SIDNO %d\n", fmoplsid);
    cfg.fmopl_enabled = usbsid_config.FMOpl.enabled = true;
    usbsid_config.FMOpl.sidno = cfg.fmopl_sid = fmoplsid;
  } else {
    cfg.fmopl_enabled = usbsid_config.FMOpl.enabled = false;
    usbsid_config.FMOpl.sidno = cfg.fmopl_sid = -1;
  }
}

void set_socket_config(uint8_t cmd, bool s1en, bool s1dual, uint8_t s1chip, bool s2en, bool s2dual, uint8_t s2chip, bool mirror)
{
  usbsid_config.socketOne.enabled = s1en;
  usbsid_config.socketOne.dualsid = s1dual;
  usbsid_config.socketOne.chiptype = s1chip;  /* Chiptype must be clone for dualsid to work! */
  if (s1en) {
    /* Set sid types to unknown if set to N/A */
    usbsid_config.socketOne.sid1.type = (usbsid_config.socketOne.sid1.type == 1 ? 0 : usbsid_config.socketOne.sid1.type);
    if (s1dual)
      usbsid_config.socketOne.sid2.type = (usbsid_config.socketOne.sid2.type == 1 ? 0 : usbsid_config.socketOne.sid2.type);
  }
  usbsid_config.socketTwo.enabled = s2en;
  usbsid_config.socketTwo.dualsid = s2dual;
  usbsid_config.socketTwo.chiptype = s2chip;  /* Chiptype must be clone for dualsid to work! */
  if (s2en) {
    /* Set sid types to unknown if set to N/A */
    usbsid_config.socketOne.sid1.type = (usbsid_config.socketOne.sid1.type == 1 ? 0 : usbsid_config.socketOne.sid1.type);
    if (s2dual)
      usbsid_config.socketOne.sid2.type = (usbsid_config.socketOne.sid2.type == 1 ? 0 : usbsid_config.socketOne.sid2.type);
  }
  usbsid_config.mirrored = mirror;
  /* PCB v1.3 will lock Audio to Stereo in mirrored mode :) */
  #if defined(HAS_AUDIOSWITCH)
  if (mirror) {
    usbsid_config.stereo_en = true;
    usbsid_config.lock_audio_sw = true;
  }
  #endif

  verify_socket_settings();
  verify_chipdetection_results(true);
  verify_sid_addr(true);

  if (cmd == 0) {
    save_load_apply_config(false, true);
  } else if (cmd == 1) {
    apply_socket_change(false);
  }
  return;
}

void apply_socket_config(bool quiet)
{
  if (!quiet) CFG("[CONFIG] Applying socket settings\n");
  cfg.mirrored = usbsid_config.mirrored;

  cfg.sock_one = usbsid_config.socketOne.enabled;
  cfg.sock_one_dual = usbsid_config.socketOne.dualsid;
  cfg.chip_one = usbsid_config.socketOne.chiptype;  /* Chiptype must be clone for dualsid to work! */

  if (!cfg.mirrored) {
    cfg.sock_two = usbsid_config.socketTwo.enabled;
    cfg.sock_two_dual = usbsid_config.socketTwo.dualsid;
    cfg.chip_two = usbsid_config.socketTwo.chiptype;  /* Chiptype must be clone for dualsid to work! */
  } else {
    if (cfg.sock_one_dual == true) {
      cfg.sock_two = usbsid_config.socketOne.enabled;
      cfg.sock_two_dual = usbsid_config.socketOne.dualsid;
      cfg.chip_two = usbsid_config.socketOne.chiptype;  /* Chiptype must be clone for dualsid to work! */
    } else {
      cfg.sock_two = usbsid_config.socketTwo.enabled;
      cfg.sock_two_dual = usbsid_config.socketTwo.dualsid;
      cfg.chip_two = usbsid_config.socketTwo.chiptype;  /* Chiptype must be clone for dualsid to work! */
    }
  }

  if (!cfg.mirrored) {
    cfg.sids_one = (cfg.sock_one == true) ? (cfg.sock_one_dual == true) ? 2 : 1 : 0;
    cfg.sids_two = (cfg.sock_two == true) ? (cfg.sock_two_dual == true) ? 2 : 1 : 0;
    cfg.numsids = (cfg.sids_one + cfg.sids_two);
  } else {
    /* Mirrored (act-as-one) overrules everything at runtime :) */
    if (cfg.sock_one_dual == true) {
      cfg.sids_one = cfg.sids_two = cfg.numsids = 2;
    } else {
      cfg.sids_one = cfg.sids_two = cfg.numsids = 1;
    }

  }
  return;
}

void apply_socket_change(bool quiet)
{
  verify_socket_settings();
  apply_socket_config(quiet);
  apply_bus_config(quiet);
  return;
}

/**
 * @brief Checks the applied socket config
 * for anomalies and returns true if found
 */
bool check_socket_config_errors(void)
{
  bool has_error = false;
  /* Cannot have both sockets disabled */
  if (!usbsid_config.socketOne.enabled
    && !usbsid_config.socketTwo.enabled) {
    has_error = true;
  }
  /* Cannot have all 0xFF id's */
  if (usbsid_config.socketOne.sid1.id == 0xFF
    && usbsid_config.socketOne.sid2.id == 0xFF
    && usbsid_config.socketTwo.sid1.id == 0xFF
    && usbsid_config.socketTwo.sid2.id == 0xFF) {
    has_error = true;
  }
  /* Cannot have all 0xFF addresses */
  if (usbsid_config.socketOne.sid1.addr == 0xFF
    && usbsid_config.socketOne.sid2.addr == 0xFF
    && usbsid_config.socketTwo.sid1.addr == 0xFF
    && usbsid_config.socketTwo.sid2.addr == 0xFF) {
    has_error = true;
  }
  /* Cannot have all SID types at N/A */
  if (usbsid_config.socketOne.sid1.type == 1
    && usbsid_config.socketOne.sid2.type == 1
    && usbsid_config.socketTwo.sid1.type == 1
    && usbsid_config.socketTwo.sid2.type == 1) {
    has_error = true;
  }
  return has_error;
}

/**
 * @brief Applies the default socket
 * and runtime configuration if required
 */
void socket_config_fallback(void)
{
  CFG("[SET DEFAULT FALLBACK SOCKET CONFIG]\n");
  /* Socket One */
  usbsid_config.socketOne.chiptype = 2;     /* unknown */
  usbsid_config.socketOne.clonetype = 0;    /* disabled */
  usbsid_config.socketOne.sid1.id = 0;      /* enabled */
  usbsid_config.socketOne.sid1.addr = 0x00; /* enabled */
  usbsid_config.socketOne.sid1.type = 0;    /* unknown */
  usbsid_config.socketOne.sid2.id = 0xFF;   /* disabled */
  usbsid_config.socketOne.sid2.addr = 0xFF; /* disabled */
  usbsid_config.socketOne.sid2.type = 0;    /* unknown */
  usbsid_config.socketOne.enabled = true;
  usbsid_config.socketOne.dualsid = false;
  /* Socket Two */
  usbsid_config.socketTwo.chiptype = 2;     /* unknown */
  usbsid_config.socketTwo.clonetype = 0;    /* disabled */
  usbsid_config.socketTwo.sid1.id = 1;      /* enabled */
  usbsid_config.socketTwo.sid1.addr = 0x20; /* enabled */
  usbsid_config.socketTwo.sid1.type = 0;    /* unknown */
  usbsid_config.socketTwo.sid2.id = 0xFF;   /* disabled */
  usbsid_config.socketTwo.sid2.addr = 0xFF; /* disabled */
  usbsid_config.socketTwo.sid2.type = 0;    /* unknown */
  usbsid_config.socketTwo.enabled = true;
  usbsid_config.socketTwo.dualsid = false;
  /* General */
  usbsid_config.mirrored = false;

  apply_bus_config(true); /* Quietly apply the bus config */
}
