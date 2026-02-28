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
#include "config_constants.h"
#include "config.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* config.c */
extern ConfigError apply_config(bool at_boot);
extern Config usbsid_config;
extern RuntimeCFG cfg;

/* config_bus.c */
extern void apply_runtime_config(const Config *config, RuntimeCFG *rt);

/* sid.c */
extern void reset_sid_registers(void);

/* Pre declarations */
ConfigError validate_config(void);
Socket default_socket(int id);


/**
 * @brief Returns the FMOpl socket SID id
 *
 * @return int id
 */
int verify_fmopl_sidno(void)
{
  const SIDChip *sids[4] = {
    &usbsid_config.socketOne.sid1,
    &usbsid_config.socketOne.sid2,
    &usbsid_config.socketTwo.sid1,
    &usbsid_config.socketTwo.sid2,
  };

  for (int i = 0; i < 4; i++) {
    if (sids[i]->type == SID_FMOPL && sids[i]->addr != 0xFF) {
      return i + 1;  /* Return 1-based SID number */
    }
  }

  return -1;
}

/**
 * @brief Apply FMOpl configuration
 * @note changes both Config and RuntimeCFG
 *
 */
void apply_fmopl_config(void)
{
  int fmopl_sid = verify_fmopl_sidno();
  cfg.fmopl_sid = fmopl_sid;
  cfg.fmopl_enabled = (fmopl_sid != -1);
  usbsid_config.FMOpl.sidno = fmopl_sid;
  usbsid_config.FMOpl.enabled = cfg.fmopl_enabled;
  return;
}

/**
 * @brief Convert SID address to default socket SID id
 *
 * @param uint8_t addr
 * @return uint8_t id
 */
uint8_t address_to_sid_id(uint8_t addr)
{
  switch (addr) {
    case 0x00: return 0;
    case 0x20: return 1;
    case 0x40: return 2;
    case 0x60: return 3;
    default:   return 0xFF;
  }
}

/**
 * @brief Convert socket SID id to SID address
 *
 * @param uint8_t id
 * @return uint8_t address
 */
uint8_t sid_id_to_address(uint8_t id)
{
  if (id < 4) {
    return sid_addresses[id];
  }
  return 0xFF;
}

/**
 * @brief Apply addresses to socket SID's
 *
 */
void apply_sid_addresses(void)
{
  /* Build index from socket flags */
  int idx = (
    (usbsid_config.socketOne.enabled ? 1 : 0) << 3)
    | ((usbsid_config.socketOne.dualsid ? 1 : 0) << 2)
    | ((usbsid_config.socketTwo.enabled ? 1 : 0) << 1)
    | ((usbsid_config.socketTwo.dualsid ? 1 : 0)
  );

  /* Lookup addresses from table */
  const uint8_t *addrs = address_table[idx];

  /* Apply to config */
  usbsid_config.socketOne.sid1.addr = addrs[0];
  usbsid_config.socketOne.sid2.addr = addrs[1];
  usbsid_config.socketTwo.sid1.addr = addrs[2];
  usbsid_config.socketTwo.sid2.addr = addrs[3];

  /* Compute IDs from addresses */
  usbsid_config.socketOne.sid1.id = address_to_sid_id(addrs[0]);
  usbsid_config.socketOne.sid2.id = address_to_sid_id(addrs[1]);
  usbsid_config.socketTwo.sid1.id = address_to_sid_id(addrs[2]);
  usbsid_config.socketTwo.sid2.id = address_to_sid_id(addrs[3]);

  /* Set disabled SID types to N/A */
  if (addrs[0] == 0xFF) usbsid_config.socketOne.sid1.type = SID_NA;
  if (addrs[1] == 0xFF) usbsid_config.socketOne.sid2.type = SID_NA;
  if (addrs[2] == 0xFF) usbsid_config.socketTwo.sid1.type = SID_NA;
  if (addrs[3] == 0xFF) usbsid_config.socketTwo.sid2.type = SID_NA;

  usCFG("SID addresses applied: S1.1=0x%02X S1.2=0x%02X S2.1=0x%02X S2.2=0x%02X (idx=%d)\n",
  addrs[0], addrs[1], addrs[2], addrs[3], idx);

    return;
}

/**
 * @brief Apply Chip and SID detection results
 *
 * @param DetectionResult *det
 * @return ConfigError config validation
 */
ConfigError apply_detection_results(const DetectionResult *det)
{
  if (!det->success) {
    return det->error != CFG_OK ? det->error : CFG_ERR_DETECTION_FAILED;
  }

  /* Apply Socket 1 results */
  usbsid_config.socketOne.chiptype = det->socket[0].chiptype;
  usbsid_config.socketOne.dualsid = det->socket[0].supports_dual;
  usbsid_config.socketOne.enabled = det->socket[0].present;
  usbsid_config.socketOne.sid1.type = det->socket[0].sid1_type;
  usbsid_config.socketOne.sid2.type = det->socket[0].sid2_type;

  /* Apply Socket 2 results */
  usbsid_config.socketTwo.chiptype = det->socket[1].chiptype;
  usbsid_config.socketTwo.dualsid = det->socket[1].supports_dual;
  usbsid_config.socketTwo.enabled = det->socket[1].present;
  usbsid_config.socketTwo.sid1.type = det->socket[1].sid1_type;
  usbsid_config.socketTwo.sid2.type = det->socket[1].sid2_type;

  /* Ensure at least one socket is enabled */
  if (!usbsid_config.socketOne.enabled && !usbsid_config.socketTwo.enabled) {
    /* Fallback: enable both sockets with unknown chips */
    usbsid_config.socketOne.enabled = true;
    usbsid_config.socketOne.chiptype = CHIP_UNKNOWN;
    usbsid_config.socketTwo.enabled = true;
    usbsid_config.socketTwo.chiptype = CHIP_UNKNOWN;
  }

  /* Recompute addresses based on new config */
  apply_sid_addresses();

  return validate_config();
}

/**
 * @brief Validate socket configuration for provided socket
 *
 * @param Socket *socket
 * @param int socket_num
 * @return ConfigError
 */
ConfigError validate_socket(const Socket *socket, int socket_num)
{
  if (socket == NULL) {
    return CFG_ERR_NULL_POINTER;
  }

  /* DualSID requires clone chiptype (real SIDs can't do dual) */
  if (socket->dualsid && socket->chiptype == CHIP_REAL) {
    usERR("Valdation error, Socket %d: DualSID on real chip\n", socket_num);
    return CFG_ERR_DUALSID_ON_REAL;
  }

  /* If socket enabled, primary SID must have valid address */
  if (socket->enabled && socket->sid1.addr == 0xFF) {
    usERR("Valdation error, Socket %d: Enabled but primary SID disabled\n", socket_num);
    return CFG_ERR_PRIMARY_SID_DISABLED;
  }

  return CFG_OK;
}

/**
 * @brief Validate the global configuration
 *
 * @return ConfigError
 */
ConfigError validate_config(void)
{
  /* At least one socket must be enabled */
  if (!usbsid_config.socketOne.enabled && !usbsid_config.socketTwo.enabled) {
    usERR("Valdation error, both sockets disabled!\n");
    return CFG_ERR_BOTH_SOCKETS_DISABLED;
  }

  /* Validate each socket independently
   * falls back to default configuration
   * if there is a configuration error */
  ConfigError err;
SOCKONE:;
  err = validate_socket(&usbsid_config.socketOne, 1);
  if (err != CFG_OK) {
    usbsid_config.socketOne = default_socket(1);
    goto SOCKONE;
  }
SOCKTWO:;
  err = validate_socket(&usbsid_config.socketTwo, 2);
  if (err != CFG_OK) {
    usbsid_config.socketTwo = default_socket(2);
    goto SOCKTWO;
  }

  /* Check for address conflicts (unless mirrored) */
  if (!usbsid_config.mirrored) {
    uint8_t addrs[4] = {
      usbsid_config.socketOne.sid1.addr,
      usbsid_config.socketOne.sid2.addr,
      usbsid_config.socketTwo.sid1.addr,
      usbsid_config.socketTwo.sid2.addr,
    };

    for (int i = 0; i < 4; i++) {
      if (addrs[i] == 0xFF) continue;  /* Skip disabled slots */
      for (int j = i + 1; j < 4; j++) {
        if (addrs[j] == 0xFF) continue;
        if (addrs[i] == addrs[j]) {
          usERR("Address conflict: slot %d and %d both at 0x%02X\n",
            i, j, addrs[i]);
          return CFG_ERR_ADDRESS_CONFLICT;
        }
      }
    }
  }

  /* At least one SID must be usable (not N/A type) */
  bool has_usable_sid = false;

  if (usbsid_config.socketOne.enabled) {
    if (usbsid_config.socketOne.sid1.type != SID_NA && usbsid_config.socketOne.sid1.addr != 0xFF) {
      has_usable_sid = true;
    }
    if (usbsid_config.socketOne.dualsid &&
      usbsid_config.socketOne.sid2.type != SID_NA && usbsid_config.socketOne.sid2.addr != 0xFF) {
      has_usable_sid = true;
    }
  }
  if (usbsid_config.socketTwo.enabled) {
    if (usbsid_config.socketTwo.sid1.type != SID_NA && usbsid_config.socketTwo.sid1.addr != 0xFF) {
      has_usable_sid = true;
    }
    if (usbsid_config.socketTwo.dualsid &&
      usbsid_config.socketTwo.sid2.type != SID_NA && usbsid_config.socketTwo.sid2.addr != 0xFF) {
      has_usable_sid = true;
    }
  }

  /* Allow unknown SID types (detection might not have run yet) */
  if (!has_usable_sid) {
    /* Check if any SID has unknown type (acceptable pre-detection) */
    bool has_unknown = (
      usbsid_config.socketOne.enabled && usbsid_config.socketOne.sid1.type == SID_UNKNOWN)
      || (usbsid_config.socketTwo.enabled && usbsid_config.socketTwo.sid1.type == SID_UNKNOWN
    );
    if (!has_unknown) {
      usERR("No usable SID found!\n");
      return CFG_ERR_NO_VALID_SID;
    }
  }

  return CFG_OK;
}

/**
 * @brief Flip socket addresses and id's at runtime
 *
 */
void flip_sockets(void)
{
  /* Swap IDs between sockets */
  uint8_t tmp_id;
  uint8_t tmp_addr;

  /* Swap SID1 */
  tmp_id = usbsid_config.socketOne.sid1.id;
  tmp_addr = usbsid_config.socketOne.sid1.addr;
  usbsid_config.socketOne.sid1.id = usbsid_config.socketTwo.sid1.id;
  usbsid_config.socketOne.sid1.addr = usbsid_config.socketTwo.sid1.addr;
  usbsid_config.socketTwo.sid1.id = tmp_id;
  usbsid_config.socketTwo.sid1.addr = tmp_addr;

  /* Swap SID2 */
  tmp_id = usbsid_config.socketOne.sid2.id;
  tmp_addr = usbsid_config.socketOne.sid2.addr;
  usbsid_config.socketOne.sid2.id = usbsid_config.socketTwo.sid2.id;
  usbsid_config.socketOne.sid2.addr = usbsid_config.socketTwo.sid2.addr;
  usbsid_config.socketTwo.sid2.id = tmp_id;
  usbsid_config.socketTwo.sid2.addr = tmp_addr;

  apply_runtime_config(&usbsid_config,&cfg);

  /* Reset SID registers after fiddling with the socket configuration */
  reset_sid_registers();

  return;
}

/**
 * @brief Helper funtion to apply requested socket preset
 *
 * @param SocketPreset preset
 * @return ConfigError
 */
static ConfigError apply_socket_preset(SocketPreset preset)
{
  if (preset < 0 || preset >= PRESET_COUNT) {
    return CFG_ERR_INVALID_PRESET;
  }

  const PresetDef *p = &socket_presets[preset];

  /* Apply preset flags */
  usbsid_config.socketOne.enabled = p->s1_enabled;
  usbsid_config.socketOne.dualsid = p->s1_dual;
  usbsid_config.socketTwo.enabled = p->s2_enabled;
  usbsid_config.socketTwo.dualsid = p->s2_dual;
  usbsid_config.mirrored = p->mirrored;

  /* Validate dualsid/chiptype compatibility */
  if (p->s1_dual && usbsid_config.socketOne.chiptype == CHIP_REAL) {
    /* Upgrade to unknown clone if needed for dualsid */
    usbsid_config.socketOne.chiptype = CHIP_UNKNOWN;
  }
  if (p->s2_dual && usbsid_config.socketTwo.chiptype == CHIP_REAL) {
    /* Upgrade to unknown clone if needed for dualsid */
    usbsid_config.socketTwo.chiptype = CHIP_UNKNOWN;
  }

  /* Compute addresses for this preset */
  apply_sid_addresses();

  /* Validate the result */
  ConfigError err = validate_config();
  if (err != CFG_OK) {
    usERR("Preset '%s' validation failed: %s\n", preset_name(preset), config_error_str(err));
  } else {
    usCFG("Preset applied: %s\n", preset_name(preset));
  }

  return err;
}

/**
 * @brief Helper function to detect an active preset
 *
 * @return SocketPreset
 */
static SocketPreset detect_current_preset(void)
{
  for (int i = 0; i < PRESET_COUNT; i++) {
    const PresetDef *p = &socket_presets[i];
    if (usbsid_config.socketOne.enabled == p->s1_enabled &&
      usbsid_config.socketOne.dualsid == p->s1_dual &&
      usbsid_config.socketTwo.enabled == p->s2_enabled &&
      usbsid_config.socketTwo.dualsid == p->s2_dual &&
      usbsid_config.mirrored == p->mirrored) {
      return (SocketPreset)i;
    }
  }

  return PRESET_COUNT;  /* No match */
}

/**
 * @brief Apply requested preset only if not already active
 *
 * @param SocketPreset preset
 * @param bool at_boot
 * @return ConfigError
 */
ConfigError apply_preset(SocketPreset preset, bool at_boot)
{
  SocketPreset pre = detect_current_preset();
  if (pre == preset) {
    return CFG_ERR_EQUAL_PRESET;
  }

  ConfigError err = apply_socket_preset(preset);
  if (err != CFG_OK) {
    return err;
  }

  return apply_config(at_boot);
}

/**
 * @brief Wraps around apply_preset for non boot presets
 *
 * @param SocketPreset preset
 */
void apply_preset_wrapper(SocketPreset preset) // ISSUE: This routine seems to break if you switch from quad to dual back to quad
{
  err = apply_preset(preset, false);
  if (err != CFG_OK) {
    usERR("Applying preset error: %s\n", config_error_str(err));
  }
  return;
}

/**
 * @brief Returns default Socket configuration
 *
 * @param int id, the socket id 1 or 2
 */
Socket default_socket(int id)
{
  const SIDChip s1s1 = {
    .id = 0,
    .addr = 0x00,
    .type = SID_UNKNOWN,
  };
  const SIDChip s2s1 = {
    .id = 1,
    .addr = 0x20,
    .type = SID_UNKNOWN,
  };
  const SIDChip sNs2 = {
    .id = 0xff,
    .addr = 0xff,
    .type = SID_NA,
  };
  Socket socket_conf = {
    .enabled = true,
    .dualsid = false,
    .chiptype = CHIP_REAL,
    .sid1 = (id == 1 ? s1s1 : s2s1),
    .sid2 = sNs2,
  };
  return socket_conf;
}

/**
 * @brief Applies the default socket
 *        and runtime configuration if required
 */
void socket_config_fallback(void)
{
  usCFG("Applying socket fallback configuration\n");
  /* Socket One */
  usbsid_config.socketOne = default_socket(1);
  /* Socket Two */
  usbsid_config.socketTwo = default_socket(2);
  /* General */
  usbsid_config.mirrored = false;

  RuntimeCFG new_cfg;
  // apply_runtime_config(&usbsid_config,&cfg);
  apply_runtime_config(&usbsid_config, &new_cfg); /* Quietly apply the bus config */
  uint32_t irq = save_and_disable_interrupts();
  memcpy(&cfg, &new_cfg, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  return;
}
