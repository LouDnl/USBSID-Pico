/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config_constants.c
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

#include <config_constants.h>


/**
 * @brief Error message strings
 *
 */
static const char __in_flash("us_statics") *error_messages[] = {
  [CFG_OK]                        = "OK",
  [CFG_ERR_BOTH_SOCKETS_DISABLED] = "Both sockets disabled",
  [CFG_ERR_NO_VALID_SID]          = "No valid SID found",
  [CFG_ERR_DUALSID_ON_REAL]       = "DualSID requires clone chip",
  [CFG_ERR_ADDRESS_CONFLICT]      = "Address conflict detected",
  [CFG_ERR_INVALID_CHIPTYPE]      = "Unknown or invalid chiptype value",
  [CFG_ERR_PRIMARY_SID_DISABLED]  = "Primary SID disabled on enabled socket",
  [CFG_ERR_INVALID_PRESET]        = "Invalid preset value",
  [CFG_ERR_DETECTION_FAILED]      = "Chip/SID detection failed",
  [CFG_ERR_NULL_POINTER]          = "NULL pointer passed to function",
  [CFG_ERR_EQUAL_PRESET]          = "Preset supplied matches preset active"
};

/**
 * @brief Preset name strings
 *
 */
static const char __in_flash("us_statics") *preset_names[] = {
  [PRESET_SINGLE_S1]     = "Single S1",
  [PRESET_SINGLE_S2]     = "Single S2",
  [PRESET_DUAL_BOTH]     = "Dual Both",
  [PRESET_DUAL_S1]       = "Dual S1",
  [PRESET_DUAL_S2]       = "Dual S2",
  [PRESET_TRIPLE_S1]     = "Triple S1",
  [PRESET_TRIPLE_S2]     = "Triple S2",
  [PRESET_QUAD]          = "Quad",
  [PRESET_MIRRORED]      = "Mirrored",
  [PRESET_MIRRORED_DUAL] = "Mirrored Dual",
};

/**
 * @brief Supported Chip type strings
 *
 */
static const char __in_flash("us_statics") *chiptypes[] = {
  [CHIP_REAL]     = "MOS",
  [CHIP_UNKNOWN]  = "Unknown",
  [CHIP_SKPICO]   = "SKPico",
  [CHIP_ARMSID]   = "ARMSID",
  [CHIP_ARM2SID]  = "ARM2SID",
  [CHIP_FPGASID]  = "FPGASID",
  [CHIP_REDIPSID] = "RedipSID",
  [CHIP_PDSID]    = "PDsid",
  [CHIP_BACKSID]  = "BackSID",
};

/**
 * @brief Possible SID type strings
 *
 */
static const char __in_flash("us_statics") *sidtypes[] = {
  [SID_UNKNOWN] = "Unknown",
  [SID_NA]      = "N/A",
  [SID_8580]    = "8580",
  [SID_6581]    = "6581",
  [SID_FMOPL]   = "FMOpl",
};

/**
 * @brief Clock strings
 *
 */
static const char __in_flash("us_statics") *int_ext[] = {
  [CFG_OFF] = "Internal",
  [CFG_ON]  = "External",
};

/**
 * @brief Switch strings
 *
 */
static const char __in_flash("us_statics") *enabled[] = {
  [CFG_OFF] = "Disabled",
  [CFG_ON]  = "Enabled",
};

/**
 * @brief Boolean strings
 *
 */
static const char __in_flash("us_statics") *false_true[] = {
  [CFG_OFF] = "False",
  [CFG_ON]  = "True",
};

/**
 * @brief Locking strings
 *
 */
static const char __in_flash("us_statics") *locked[] = {
  [CFG_OFF] = "Unlocked",
  [CFG_ON]  = "Locked",
};

/**
 * @brief Socket strings
 *
 */
static const char __in_flash("us_statics") *single_dual[] = {
  [CFG_OFF] = "Single SID",
  [CFG_ON]  = "Dual SID",
};

/**
 * @brief Feature strings
 *
 */
static const char __in_flash("us_statics") *mono_stereo[] = {
  [CFG_OFF] = "Mono",
  [CFG_ON]  = "Stereo",
};

/**
 * @brief Configured and allowed socket presets
 *
 */
const PresetDef __in_flash("us_presets") socket_presets[] = {
  [PRESET_SINGLE_S1]     = { true,  false, false, false, false },
  [PRESET_SINGLE_S2]     = { false, false, true,  false, false },
  [PRESET_DUAL_BOTH]     = { true,  false, true,  false, false },
  [PRESET_DUAL_S1]       = { true,  true,  false, false, false },
  [PRESET_DUAL_S2]       = { false, false, true,  true,  false },
  [PRESET_TRIPLE_S1]     = { true,  true,  true,  false, false },
  [PRESET_TRIPLE_S2]     = { true,  false, true,  true,  false },
  [PRESET_QUAD]          = { true,  true,  true,  true,  false },
  [PRESET_MIRRORED]      = { true,  false, true,  false, true  },
  [PRESET_MIRRORED_DUAL] = { true,  true,  true,  true,  true  },
};

/**
 * @brief Address lookup table
 *
 * Index = (s1_enabled << 3) | (s1_dual << 2) | (s2_enabled << 1) | s2_dual
 * Values = { sid1_s1, sid2_s1, sid1_s2, sid2_s2 }
 *
 * Standard C64 addresses: 0x00=D400, 0x20=D420, 0x40=D440, 0x60=D460
 *
 */
const uint8_t __in_flash("us_uints") address_table[16][4] = {
  /* 0b1111: from left to right Sock1 EN, Sock1 Dual, Sock2 EN, Sock2 Dual */
  /* 0b0000: S1:off S1D:off S2:off S2D:off */ { 0xFF, 0xFF, 0xFF, 0xFF },  /*  0: Invalid - both disabled */
  /* 0b0001: S1:off S1D:off S2:off S2D:on  */ { 0xFF, 0xFF, 0xFF, 0xFF },  /*  1: Invalid - S2 disabled but dual */
  /* 0b0010: S1:off S1D:off S2:on  S2D:off */ { 0xFF, 0xFF, 0x00, 0xFF },  /*  2: S2 single only */
  /* 0b0011: S1:off S1D:off S2:on  S2D:on  */ { 0xFF, 0xFF, 0x00, 0x20 },  /*  3: S2 dual only */
  /* 0b0100: S1:off S1D:on  S2:off S2D:off */ { 0xFF, 0xFF, 0xFF, 0xFF },  /*  4: Invalid - S1 disabled but dual */
  /* 0b0101: S1:off S1D:on  S2:off S2D:on  */ { 0xFF, 0xFF, 0xFF, 0xFF },  /*  5: Invalid */
  /* 0b0110: S1:off S1D:on  S2:on  S2D:off */ { 0xFF, 0xFF, 0x00, 0xFF },  /*  6: Invalid S1D, treat as S2 single */
  /* 0b0111: S1:off S1D:on  S2:on  S2D:on  */ { 0xFF, 0xFF, 0x00, 0x20 },  /*  7: Invalid S1D, treat as S2 dual */
  /* 0b1000: S1:on  S1D:off S2:off S2D:off */ { 0x00, 0xFF, 0xFF, 0xFF },  /*  8: S1 single only */
  /* 0b1001: S1:on  S1D:off S2:off S2D:on  */ { 0x00, 0xFF, 0xFF, 0xFF },  /*  9: Invalid S2D, treat as S1 single */
  /* 0b1010: S1:on  S1D:off S2:on  S2D:off */ { 0x00, 0xFF, 0x20, 0xFF },  /* 10: Both single (stereo) */
  /* 0b1011: S1:on  S1D:off S2:on  S2D:on  */ { 0x00, 0xFF, 0x20, 0x40 },  /* 11: S1 single, S2 dual (3 SIDs) */
  /* 0b1100: S1:on  S1D:on  S2:off S2D:off */ { 0x00, 0x20, 0xFF, 0xFF },  /* 12: S1 dual only */
  /* 0b1101: S1:on  S1D:on  S2:off S2D:on  */ { 0x00, 0x20, 0xFF, 0xFF },  /* 13: Invalid S2D, treat as S1 dual */
  /* 0b1110: S1:on  S1D:on  S2:on  S2D:off */ { 0x00, 0x20, 0x40, 0xFF },  /* 14: S1 dual, S2 single (3 SIDs) */
  /* 0b1111: S1:on  S1D:on  S2:on  S2D:on  */ { 0x00, 0x20, 0x40, 0x60 },  /* 15: Quad (4 SIDs) */
};

/**
 * @brief Possible SID addresses (indexed by ID 0-3)
 *
 */
const uint8_t __in_flash("us_uints") sid_addresses[4] = {
  0x00,
  0x20,
  0x40,
  0x60
};


/**
 * @brief Helper function for config error message retrieval
 *
 * @param ConfigError err
 * @return const char* error message
 */
const char * config_error_str(ConfigError err)
{
  if (err >= 0 && err < CFG_ERROR_COUNT) {
    return error_messages[err];
  }
  return "Unknown error";
}

/**
 * @brief Helper function for preset name retrieval
 *
 * @param SocketPreset preset
 * @return const char* preset name
 */
const char * preset_name(SocketPreset preset)
{
  if (preset >= 0 && preset < PRESET_COUNT) {
      return preset_names[preset];
  }
  return "UNKNOWN";
}

/**
 * @brief Helper function for chiptype name retrieval
 *
 * @param ChipType chiptype
 * @return const char* chiptype name
 */
const char * chip_type_name(ChipType chiptype)
{
  if (chiptype >= 0 && chiptype < CHIP_COUNT) {
    return chiptypes[chiptype];
  }
  return chiptypes[1]; /* Unknown */
}

/**
 * @brief Helper function for sidtype name retrieval
 *
 * @param SIDType sidtype
 * @return const char* sidtype name
 */
const char * sid_type_name(SIDType sidtype)
{
  if (sidtype >= 0 && sidtype < SID_COUNT) {
    return sidtypes[sidtype];
  }
  return sidtypes[1]; /* Unknown */
}

/**
 * @brief Helper function
 *
 * @param ZeroOne val
 * @return const char* string
 */
const char * in_ext_str(ZeroOne val)
{
  if (val >= 0 && val < OF_MAX) {
    return int_ext[val];
  }
  return int_ext[0]; /* Internal */
}

/**
 * @brief Helper function
 *
 * @param ZeroOne val
 * @return const char* string
 */
const char * switch_str(ZeroOne val)
{
  if (val >= 0 && val < OF_MAX) {
    return enabled[val];
  }
  return enabled[0]; /* Disabled */
}

/**
 * @brief Helper function
 *
 * @param ZeroOne val
 * @return const char* string
 */
const char * boolean_str(ZeroOne val)
{
  if (val >= 0 && val < OF_MAX) {
    return false_true[val];
  }
  return false_true[0]; /* False */
}

/**
 * @brief Helper function
 *
 * @param ZeroOne val
 * @return const char* string
 */
const char * locked_str(ZeroOne val)
{
  if (val >= 0 && val < OF_MAX) {
    return locked[val];
  }
  return locked[0]; /* Unlocked */
}

/**
 * @brief Helper function
 *
 * @param ZeroOne val
 * @return const char* string
 */
const char * dualsingle_str(ZeroOne val)
{
  if (val >= 0 && val < OF_MAX) {
    return single_dual[val];
  }
  return single_dual[0]; /* Single */
}

/**
 * @brief Helper function
 *
 * @param ZeroOne val
 * @return const char* string
 */
const char * monostereo_str(ZeroOne val)
{
  if (val >= 0 && val < OF_MAX) {
    return mono_stereo[val];
  }
  return mono_stereo[0]; /* Mono */
}
