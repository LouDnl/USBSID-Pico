/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config_constants.h
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

#ifndef _USBSID_CONFIG_CONSTANTS_H_
#define _USBSID_CONFIG_CONSTANTS_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/types.h"
#include "pico/platform.h"


/**
 * @brief Logging helper
 *
 */
typedef enum ZeroOne {
  CFG_OFF = 0,
  CFG_ON,
  OF_MAX,
} ZeroOne;

/**
 * @brief Configuration error codes
 * All _config_ validation and application functions (should) return these codes.
 * CFG_OK (0) indicates success, all other values indicate specific errors.
 *
 */
typedef enum ConfigError {
  CFG_OK = 0,                     /* Configuration is valid */
  CFG_ERR_BOTH_SOCKETS_DISABLED,  /* At least one socket must be enabled */
  CFG_ERR_NO_VALID_SID,           /* No usable SID found in config */
  CFG_ERR_DUALSID_ON_REAL,        /* DualSID mode requires clone chiptype */
  CFG_ERR_ADDRESS_CONFLICT,       /* Multiple SIDs have same address */
  CFG_ERR_INVALID_CHIPTYPE,       /* Unknown or invalid chiptype value */
  CFG_ERR_PRIMARY_SID_DISABLED,   /* Enabled socket has disabled primary SID */
  CFG_ERR_INVALID_PRESET,         /* Invalid preset value */
  CFG_ERR_DETECTION_FAILED,       /* Chip/SID detection failed */
  CFG_ERR_NULL_POINTER,           /* NULL pointer passed to function */
  CFG_ERR_EQUAL_PRESET,           /* Preset supplied matches preset active */
  CFG_ERROR_COUNT,                /* Number of config errors */
} ConfigError;

/**
 * @brief Socket configuration presets
 * These presets define available socket configurations.
 *
 */
typedef enum SocketPreset {
  PRESET_SINGLE_S1 = 0,  /* Single SID in Socket 1 only */
  PRESET_SINGLE_S2,      /* Single SID in Socket 2 only */
  PRESET_DUAL_BOTH,      /* One SID per socket (stereo) */
  PRESET_DUAL_S1,        /* Dual SID in Socket 1, Socket 2 disabled */
  PRESET_DUAL_S2,        /* Dual SID in Socket 2, Socket 1 disabled */
  PRESET_TRIPLE_S1,      /* Dual S1 + Single S2 (3 SIDs) */
  PRESET_TRIPLE_S2,      /* Single S1 + Dual S2 (3 SIDs) */
  PRESET_QUAD,           /* Dual SID in both sockets (4 SIDs) */
  PRESET_MIRRORED,       /* S2 mirrors S1 (mono output on both) */
  PRESET_MIRRORED_DUAL,  /* S2 mirrors dual S1 */
  PRESET_COUNT,          /* Number of presets */
} SocketPreset;

/**
 * @brief Socket preset definitions
 * Each preset defines: s1_enabled, s1_dual, s2_enabled, s2_dual, mirrored
 *
 */
typedef struct {
  bool s1_enabled;
  bool s1_dual;
  bool s2_enabled;
  bool s2_dual;
  bool mirrored;
} PresetDef;


/**
 * @brief Known Chip types
 * These are the currently chiptypes USBSID-Pico can identify
 *
 */
typedef enum ChipType
{
  CHIP_REAL = 0,  /* Real SID chip (assumed) */
  CHIP_UNKNOWN,   /* Unknown SID chip */
  CHIP_SKPICO,    /* SIDKick-pico */
  CHIP_ARMSID,    /* ARMSID */
  CHIP_ARM2SID,   /* ARM2SID */
  CHIP_FPGASID,   /* FPGASID */
  CHIP_REDIPSID,  /* RedipSID */
  CHIP_PDSID,     /* Public Domain SID */
  CHIP_BACKSID,   /* BackSID */
  CHIP_COUNT,     /* Number of chiptypes */
} ChipType;

/**
 * @brief SID types
 * Possible SID types USBSID-Pico can detect
 *
 */
typedef enum SIDType
{
  SID_UNKNOWN = 0,  /* Unknown SID */
  SID_NA,           /* Not available */
  SID_8580,         /* MOS8580 */
  SID_6581,         /* MOS6581 */
  SID_FMOPL,        /* FM Sound generator OPL and OPL2 */
  SID_COUNT,        /* Number of SID types */
} SIDType;

/**
 * @brief Per-socket detection results
 *
 */
typedef struct SocketDetection {
    uint8_t chiptype;     /* Detected clone type (CHIP_MOS, CHIP_SKPICO, etc.) */
    bool    supports_dual;/* Hardware supports dual SID mode */
    uint8_t sid1_type;    /* Primary SID type (SID_UNKNOWN, SID_8580, SID_6581, SID_FMOPL) */
    uint8_t sid2_type;    /* Secondary SID type (if dual supported) */
    bool    present;      /* Physical chip detected in socket */
} SocketDetection;

/**
 * @brief Complete detection result from auto-detect routine
 *
 */
typedef struct DetectionResult {
    SocketDetection socket[2];  /* Results for Socket 1 and Socket 2 */
    bool success;               /* Overall detection completed successfully */
    ConfigError error;          /* Error code if success == false */
} DetectionResult;


/* Externalise me whatever! */

extern const PresetDef __in_flash("us_presets") socket_presets[];
extern const uint8_t __in_flash("us_uints") address_table[16][4];
extern const uint8_t __in_flash("us_uints") sid_addresses[];


/* These strings are made for logging */

extern const char * const config_error_str(ConfigError err);
extern const char * const preset_name(SocketPreset preset);
extern const char * const chip_type_name(ChipType chiptype);
extern const char * const sid_type_name(SIDType sidtype);
extern const char * const in_ext_str(ZeroOne val);
extern const char * const switch_str(ZeroOne val);
extern const char * const boolean_str(ZeroOne val);
extern const char * const locked_str(ZeroOne val);
extern const char * const dualsingle_str(ZeroOne val);
extern const char * const monostereo_str(ZeroOne val);


#ifdef __cplusplus
}
#endif

#endif /* _USBSID_CONFIG_CONSTANTS_H_ */
