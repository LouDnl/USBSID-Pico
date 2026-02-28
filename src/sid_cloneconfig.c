/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_detection.c
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
#include "sid.h"
#include "logging.h"
#include "sid_fpgasid.h"
#include "sid_skpico.h"
#include "sid_pdsid.h"
#include "sid_backsid.h"


/* bus.c */
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern uint8_t cycled_read_operation(uint8_t address, uint16_t cycles);

/* sid.c */
extern void clear_sid_memory(void);
extern void clear_volume_state(void);
extern void set_reset_state(bool state);
extern void set_paused_state(bool state);

/* sid_detection.c */
extern bool detect_pdsid(uint8_t base_address, bool silent);

/* config.c */
extern Config usbsid_config;
extern RuntimeCFG cfg;

/* config_logging.c */
extern void print_cfg(const uint8_t *buf, size_t len, bool newline);

/* Init local variables */
static uint8_t skpico_config[64] = {0xff};
static uint8_t skpico_version_result[32] = {0xff};
static char skpico_version[32] = {0};
static int skpico_v = 0;
static const char __in_flash("us_vars_static") * error_type[1] = { "ERROR" };


/**
 * @brief Parses and prints supplied FPGASID configuration array
 *
 * @param int slot
 * @param int sidno
 * @param uint8_t *configarray
 */
static void print_fpgasid_sidconfig(int slot, int sidno, uint8_t * configarray)
{
  int extin = ((configarray[2] & 0b11000000) >> 6);
  int readb = ((configarray[2] & 0b110000) >> 4);
  int regd = ((configarray[2] & 0b1000) >> 3);
  int mixw = ((configarray[2] & 0b100) >> 2);
  int crunch = ((configarray[2] & 0b10) >> 1);
  int fltr = (configarray[2] & 0b1);
  int outp = ((configarray[1] & 0b1000) >> 3);
  int addr1 = (((configarray[1] & 0b100) >> 2) == 1 ? 1 : 4);
  int addr2 = (((configarray[1] & 0b10) >> 1) == 1 ? 2 : 4);
  int addr3 = ((configarray[1] & 0b1) == 1 ? 3 : 4);
  int addr = ((addr1 + addr2 + addr3) == 0 ? 12 : 4);

  usCFG("\n");
  usCFG("FPGASID configuration of slot %s and SID %d\n", slots[slot], sidno);
  if (sidno == 1) {
    usCFG("  Output mode    = %s\n", outputmode[outp]);
    usCFG("  SID2 addresses = %s%s%s%s\n", sid2addr[addr], sid2addr[addr1], sid2addr[addr2], sid2addr[addr3]);
  }
  usCFG("  ExtIn source   = %s\n", extinsource[extin]);
  usCFG("  Register read  = %s\n", readback[readb]);
  usCFG("  Register delay = %s\n", regdelay[regd]);
  usCFG("  Mixed wavform  = %s\n", mixedwave[mixw]);
  usCFG("  Crunchy DAC    = %s\n", crunchydac[crunch]);
  usCFG("  Analog filter  = %s\n", filtermode[fltr]);
  usCFG("  DigiFIX values = %02x\n", configarray[0]);

  return;
}

/**
 * @brief Read configuration from an FPGASID at supplied address
 *
 * @param uint8_t base_address
 */
void read_fpgasid_configuration(uint8_t base_address)
{
  if (((/* base_address >= 0x0 &&  */base_address < 0x40) && (usbsid_config.socketOne.chiptype != CHIP_FPGASID))
    || ((base_address >= 0x40 && base_address < 0x80) && (usbsid_config.socketTwo.chiptype != CHIP_FPGASID))) {
      usERR("[SID] Incorrect address $%02x and Chip type %s (%d)\n",
        base_address, chip_type_name((int)usbsid_config.socketOne.chiptype), usbsid_config.socketTwo.chiptype);
    return; /* Do nothing if no FPGASID configured */
  }
  static uint8_t idHi, idLo, cpld, fpga, pca, select_pins, idxa, idxb, flta, fltb;
  static float frequency = 0.0;
  static uint8_t unique_id[8];
  static uint8_t sid_one_a[3];
  static uint8_t sid_two_a[3];
  static uint8_t sid_one_b[3];
  static uint8_t sid_two_b[3];

  usNFO("\n");
  usCFG("Reading FPGASID configuration from address $%02x\n", base_address);
  /* Enable diag mode */
  cycled_write_operation((0x19 + base_address), 0xEE, 6);  /* Write magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0xAB, 6);  /* Write magic cookie Lo */

  /* Double check that we are actually data from an FPGASID */
  idLo = cycled_read_operation((0x0 + base_address), 4);      /* Read identify Hi */
  idHi = cycled_read_operation((0x1 + base_address), 4);      /* Read identify Lo */
  uint16_t fpgasid_id = (idHi << 8 | idLo);
  if (fpgasid_id != FPGASID_ID) {
    usERR("Incorrect identifier 0x%04X != 0x%04X, no FPGASID found @ $%02x\n", fpgasid_id, FPGASID_ID, base_address);
    return;
  }

  /* Start diag config read */
  cpld = cycled_read_operation((0x02 + base_address), 4);  /* Read clpd revision */
  fpga = cycled_read_operation((0x03 + base_address), 4);  /* Read fpga revision */
  pca  = cycled_read_operation((0x0D + base_address), 4);  /* Read pca revision */
  select_pins  = cycled_read_operation((0x0D + base_address), 4);  /* Read select pins */
  for (int i = 0; i < 8; i++) {
    unique_id[i] = cycled_read_operation(((0x04 + base_address) + i), 4);  /* Read unique identifier */
  }
  idxa = cycled_read_operation((0x1B + base_address), 4);  /* Read config index A revision */
  idxb = cycled_read_operation((0x1C + base_address), 4);  /* Read config index B revision */
  flta = cycled_read_operation((0x1D + base_address), 4);  /* Read filterbias A revision */
  fltb = cycled_read_operation((0x1E + base_address), 4);  /* Read filterbias B revision */
  /* 0 digifix, 1 stereo, 2 mode */
  for (int i = 0; i < 3; i++) {
    sid_one_a[i] = cycled_read_operation(((0x0F + base_address) + i), 4);  /* Read SID1 A config */
  }
  for (int i = 0; i < 3; i++) {
    sid_two_a[i] = cycled_read_operation(((0x12 + base_address) + i), 4);  /* Read SID2 A config */
  }
  for (int i = 0; i < 3; i++) {
    sid_one_b[i] = cycled_read_operation(((0x15 + base_address) + i), 4);  /* Read SID1 B config */
  }
  for (int i = 0; i < 3; i++) {
    sid_two_b[i] = cycled_read_operation(((0x18 + base_address) + i), 4);  /* Read SID2 B config */
  }
  for (int i = 0; i < 10; i++) {
    frequency += cycled_read_operation((0x0C + base_address), 4);  /* Read Ø2 frequency */
  }
  frequency /= 10;   // 10 reads
  frequency *= 12.5; // kHz
  frequency /= 1000; // to uS

  /* Exit diag mode */
  cycled_write_operation((0x19 + base_address), 0x0, 6);   /* Clear magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0x0, 6);   /* Clear magic cookie Lo */

  usCFG("FPGASID Diagnostic result:\n");
  usCFG("  Identifier:        %04X (FPGASID)\n", fpgasid_id);
  usCFG("  CPLD Revision:     %02X\n", cpld);
  usCFG("  FPGA Revision:     %02X\n", fpga);
  usCFG("  PCA Revision:      %02X\n", pca);
  usCFG("  Unique identifier: %02X%02X%02X%02X%02X%02X%02X%02X\n",
    unique_id[0], unique_id[1], unique_id[2], unique_id[3],
    unique_id[4], unique_id[5], unique_id[6], unique_id[7]);
  usCFG("  Clock frequency:   %.3fμs\n", frequency);
  usCFG("  Select pins:       %02X\n", select_pins);
  usCFG("  Index config A:    %02X\n", idxa);
  usCFG("    SID 1 A:         %02X%02X%02X\n",
    sid_one_a[0], sid_one_a[1], sid_one_a[2]);
  usCFG("    SID 2 A:         %02X%02X%02X\n",
    sid_two_a[0], sid_two_a[1], sid_two_a[2]);
  usCFG("    Filterbias A\n");
  usCFG("      SID1/SID2:     %02x\n", flta);
  usCFG("  Index config B:    %02X\n", idxb);
  usCFG("    SID 1 B:         %02X%02X%02X\n",
    sid_one_b[0], sid_one_b[1], sid_one_b[2]);
  usCFG("    SID 2 B:         %02X%02X%02X\n",
    sid_two_b[0], sid_two_b[1], sid_two_b[2]);
  usCFG("    Filterbias B\n");
  usCFG("      SID1/SID2:     %02x\n", fltb);

  print_fpgasid_sidconfig(0, 1, sid_one_a);
  print_fpgasid_sidconfig(0, 2, sid_two_a);
  print_fpgasid_sidconfig(1, 1, sid_one_b);
  print_fpgasid_sidconfig(1, 2, sid_two_b);

  usNFO("\n");

  return;
}

/**
 * @brief Parses and prints supplied SIDKick-pico configuration array
 *
 * @param bool is_u64fw
 * @param uint8_t *configarray
 */
static void print_skpico_configuration(bool is_u64fw, uint8_t * configarray)
{
  usNFO("\n");
  usCFG("SIDKICK-pico configuration:\n");
  for (size_t i = 0; i < 64; i++) {
    if (i >= 4 && i <= 7) continue;
    if (i >= 13 && i <= 56) continue;
    if (i == 62 || i == 63) continue;
    if (i == 0 || i == 8) {
      usCFG("  [%02ld] %s: %02X ~ %s\n", i, config_names[i], configarray[i], (configarray[i] < s_sid_types) ? (char*)sid_types[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    if (i == 10 && !is_u64fw) {
      usCFG("  [%02ld] %s: %02X ~ %s\n", i, config_names[i], configarray[i], (configarray[i] < s_sid2_address) ? (char*)sid2_address[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    if ((i == 10 || i == 11) && is_u64fw) {
      usCFG("  [%02ld] %s: %02X ~ Ultimate64\n", i, u64_names[i-10], configarray[i]);
      continue;
    }
    if (i == 59) {
      usCFG("  [%02ld] %s: %02X ~ %s\n", i, config_names[i], configarray[i], (configarray[i] < s_clock_speed) ? (char*)clock_speed[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    usCFG("  [%02ld] %s: %02X\n", i, config_names[i], configarray[i]);
  }

  return;
}

/**
 * @brief Read SIDKick-pico version at supplied address
 *
 * @param uint8_t base_address
 * @return true  is Ultimate64 firmware
 * @return false is regular firmware
 */
bool read_skpico_version(uint8_t base_address)
{
  usNFO("\n");
  usCFG("Reading SIDKick-pico version @ $%02x\n", base_address);

  const char skpico_correction = 0x60;  /* { 0x70, 0x69, 0x63, 0x6F } */

  /* Extend config mode */
  sleep_us(10);
  cycled_write_operation((config_extend[0] + base_address), config_extend[1], 6);

  for (int i = 0; i < 32; i++) {
    cycled_write_operation((0x1e + base_address), (224 + i), 6);
    sleep_us(10);
    skpico_version_result[i] = cycled_read_operation((0x1d + base_address), 4);
    if (i >= 2 && i <= 5) skpico_version_result[i] |= skpico_correction;
  }
  skpico_v = (((skpico_version_result[8] - '0') * 10) + (skpico_version_result[9] - '0'));
  bool is_u64fw = ((skpico_version_result[11] == 0x55) ? true : false);

  /* Extend config mode */
  sleep_us(10);
  cycled_write_operation((config_extend[0] + base_address), config_extend[1], 6);

  /* Finalize version string */
  memcpy(&skpico_version[0], &skpico_version_result, 32);
  usCFG("  SIDKick-pico version: %.36s (skpico_v: %d is_u64fw: %s)\n", skpico_version, skpico_v, (is_u64fw ? "true" : "false"));

  /* Clear used arrays after use */
  memset(skpico_version_result, 0xff, 32);
  memset(skpico_version, 0, 32);

  return is_u64fw;
}

/**
 * @brief Read configuration from an SIDKick-pico at supplied address
 *
 * @param uint8_t base_address
 */
void read_skpico_configuration(uint8_t base_address, uint8_t profile)
{
  /* Enter config mode */
  cycled_write_operation((init_configmode[0] + base_address), init_configmode[1], 6);

  bool is_u64fw = read_skpico_version(base_address);

  /* Extend config mode */
  sleep_us(10);
  cycled_write_operation((config_extend[0] + base_address), config_extend[1], 6);

  sleep_us(10);
  if (skpico_v >= 22) {
    cycled_write_operation((select_profile[0] + base_address), profile, 6);
  } else {
    cycled_write_operation((select_profile[0] + base_address), 0, 6);
  }

  usCFG("\n");
  usCFG("Reading SIDKick-pico configuration @ $%02x\n", base_address);

  /* Extend config mode */
  sleep_us(10);
  cycled_write_operation((config_extend[0] + base_address), config_extend[1], 6);

  /* Read config from SKPico */
  for (int i = 0; i <= 63; ++i) {
    sleep_us(10);
    skpico_config[i] = cycled_read_operation((0x1d + base_address), 4);
  }

  /* Exit config mode */
  sleep_us(10);
  cycled_write_operation((config_exit[0] + base_address), config_exit[1], 6);

  print_cfg(skpico_config, 64, false);

  print_skpico_configuration(is_u64fw, skpico_config);

  memset(skpico_config, 0xff, 64);

  return;
}

/**
 * @brief Change Public Domain SID type
 * This requires a 5 second reset hold
 * to switch to and or from 6581/8580
 */
void reset_switch_pdsid_type(void)
{
  usCFG("Switching PDSID SID type with reset pin\n");
  set_reset_state(true); // reset_state = 1;
  set_paused_state(false); // paused_state = 0;
  clear_volume_state(); // memset(volume_state, 0, 4);
  clear_sid_memory(); // memset(sid_memory, 0, count_of(sid_memory));
  usCFG("  RST pin low\n");
  cPIN(RES); // gpio_put(RES, 0);
  usCFG("  Sleep 6 seconds\n");
  sleep_ms(6000);
  usCFG("  RST pin high\n");
  sPIN(RES); // gpio_put(RES, 1);
  set_reset_state(false); // reset_state = 0;

  return;
}

/** BUG: Inconsistent results when reading
 * @brief Read PDSID configured type
 *
 * @param uint8_t base_address
 * @return uint8_t pdsid type 0 = 6581, 1 = 8580
 */
uint8_t read_pdsid_sid_type(uint8_t base_address)
{
  detect_pdsid(base_address, true);
  cycled_write_operation((base_address + PDREG_P),PDSID_P,6);  /* 'P' */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  cycled_write_operation((base_address + PDREG_D),PDSID_D,6);  /* 'D' */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t result = cycled_read_operation((base_address + PDREG_S),6); /* 0 = 6581, 1 = 8580 */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  // cycled_write_operation((base_address + PDREG_P),0x00,6);  /* end config mode */
  return result;
}

/**
 * @brief Set the pdsid sid type object
 *
 * @param uint8_t base_address
 * @param uint8_t type
 * @return boolean true succes
 * @return boolean false false
 */
bool set_pdsid_sid_type(uint8_t base_address, uint8_t type)
{
  if (type > 1) { return false; } /* Cannot do that here! */
  detect_pdsid(base_address, true);
  cycled_write_operation((base_address + PDREG_P),PDSID_P,6); /* 'P' */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  cycled_write_operation((base_address + PDREG_D),PDSID_D,6); /* 'D' */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  cycled_write_operation((base_address + PDREG_S),type,6); /* 0 = 6581, 1 = 8580 */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  // cycled_write_operation((base_address + PDREG_P),0x00,6);  /* end config mode */
  return true;
}

/**
 * @brief Read and print BackSID version
 * @note Reverse engineered from `backsid223.prg`
 *
 * @param uint8_t base_address
 */
void print_backsid_version(uint8_t base_address)
{
  cycled_write_operation((base_address + BACKSID_REG), BACKSID_MAJ, 6);   /* Set major register */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t major = cycled_read_operation((base_address + BACKSID_RD), 6); /* Read major */

  cycled_write_operation((base_address + BACKSID_REG),BACKSID_MIN, 6);   /* Set minor register */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t minor = cycled_read_operation((base_address + BACKSID_RD), 6); /* Read minor */

  cycled_write_operation((base_address + BACKSID_REG), BACKSID_PAT, 6);   /* Set minor register */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t patch = cycled_read_operation((base_address + BACKSID_RD), 6); /* Read minor */

  cycled_write_operation((base_address + BACKSID_REG), BACKSID_REV, 6);   /* Set hardware revision register */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t hwrev = cycled_read_operation((base_address + BACKSID_RD), 6); /* Read hardware revision */

  usNFO("\n");
  usCFG("Reading BackSID version @ $%02x]\n", base_address);
  usCFG("  BackSID version:   %d%d%d\n", major, minor, patch);
  usCFG("  Hardware revision: %d\n", hwrev);
  return;
}

/**
 * @brief Read and print BackSID filter type
 * @note Reverse engineered from `backsid223.prg`
 *
 * @param uint8_t base_address
 */
void print_backsid_filter_type(uint8_t base_address)
{
  bool again = false;
  usNFO("\n");
  usCFG("Reading BackSID filters @ $%02x]\n", base_address);
again:;
  cycled_write_operation((base_address + BACKSID_REG), BACKSID_FLT, 6);         /* Set filter register */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t filter_type = cycled_read_operation((base_address + BACKSID_RD), 6); /* Read filter type */
  usCFG("  Filters read RAW: %u\n", filter_type);
  if (!again) {
    again = true;
    if (filter_type >= 3) again = false; /* Incorrect result, read again! */
    goto again; /* Read twice */
  }
  usCFG("  Filters type: %u = %s\n", filter_type, backsid_filters[filter_type]);
  return;
}

/**
 * @brief Set the backsid filter type and print afterwards
 * @note Reverse engineered from `backsid223.prg`
 *
 * @param uint8_t base_address
 * @param uint8_t type
 */
void set_backsid_filter_type(uint8_t base_address, uint8_t type)
{
  uint8_t type_set = ((type >= 3) ? 0 : type); /* Failsafe defaults to 6581 */
  usNFO("\n");
  usCFG("Settings BackSID Filters:\n");
  usCFG("  requested: %u\n", type);
  usCFG("  actual:    %u = %s\n",
    type_set, backsid_filters[type_set]);
  /* LDX #$03  ~ 2 cycles */
  /* STX $d41b ~ 4 cycles */
  cycled_write_operation((base_address + BACKSID_REG), BACKSID_FLT, 6); /* Set filter register */
  /* LDA #$nn  ~ 2 cycles but ignored due to loading before previous write */
  /* STA $d41c ~ 4 cycles */
  cycled_write_operation((base_address + BACKSID_WR), type, 4);         /* Write filter type */
  /* LDA #$b5  ~ 2 cycles */
  /* STA $d41d ~ 4 cycles */
  cycled_write_operation((base_address + BACKSID_HS1), BACKSID_HV1,6); /* Write handshake 1 */
  /* LDA #$1d  ~ 2 cycles */
  /* STA $d41e ~ 4 cycles */
  cycled_write_operation((base_address + BACKSID_HS2), BACKSID_HV2,6); /* Write handshake 2 */
  sleep_ms(1327); /* 79 Jiffies of 16,8ms each */
  print_backsid_filter_type(base_address);
  return;
}
