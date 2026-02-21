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
#include "config.h"
#include "usbsid.h"
#include "sid.h"
#include "logging.h"
#include "sid_fpgasid.h"
#include "sid_skpico.h"


/* bus.c */
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern uint8_t cycled_read_operation(uint8_t address, uint16_t cycles);
extern void cycled_delay_operation(uint16_t cycles);

/* sid.c */
extern void clear_sid_memory(void);
extern void clear_volume_state(void);
extern void set_reset_state(bool state);
extern void set_paused_state(bool state);

/* config.c */
extern Config usbsid_config;
extern RuntimeCFG cfg;

/* config_logging.c */
extern void print_cfg(const uint8_t *buf, size_t len);
extern char *sidtypes[5];

/* Init local variables */
static uint8_t skpico_config[64] = {0xFF};
static const char __in_flash("usbsid_vars") * error_type[1] = { "ERROR" };


void print_fpgasid_sidconfig(int slot, int sidno, uint8_t * configarray)
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
  usCFG("[FPGASID CURRENT CONFIG SLOT%s SID %d]\n", slots[slot], sidno);
  if (sidno == 1) {
    usCFG("OUTPUT MODE: %s\n", outputmode[outp]);
    usCFG("SID2 ADDRESSES: %s%s%s%s\n", sid2addr[addr], sid2addr[addr1], sid2addr[addr2], sid2addr[addr3]);
  }
  usCFG("SOURCE EXTIN: %s\n", extinsource[extin]);
  usCFG("REGISTER READ: %s\n", readback[readb]);
  usCFG("REGISTER DELAY: %s\n", regdelay[regd]);
  usCFG("MIXED WAVEFORM: %s\n", mixedwave[mixw]);
  usCFG("CRUNCHY DAC: %s\n", crunchydac[crunch]);
  usCFG("ANALOG FILTER: %s\n", filtermode[fltr]);
  usCFG("DIGIFIX VALUE: %02X\n", configarray[0]);
  usCFG("\n");
}

void read_fpgasid_configuration(uint8_t base_address)
{

  if (((/* base_address >= 0x0 &&  */base_address < 0x40) && (usbsid_config.socketOne.clonetype != 4))
    || ((base_address >= 0x40 && base_address < 0x80) && (usbsid_config.socketTwo.clonetype != 4))) {
      usCFG("[SID] ERROR INCORRECT ADDRESS (0x%02x) AND CLONETYPE (%d %d)\n",
         base_address, usbsid_config.socketOne.clonetype, usbsid_config.socketTwo.clonetype);
    return; /* Do nothing if no FPGASID configured */
  }
  uint8_t idHi, idLo, cpld, fpga, pca, select_pins, idxa, idxb, flta, fltb;
  float frequency = 0.0;
  uint8_t unique_id[8];
  uint8_t sid_one_a[3];
  uint8_t sid_two_a[3];
  uint8_t sid_one_b[3];
  uint8_t sid_two_b[3];
  usCFG("[SID] READING CONFIGURATION FROM FPGASID @ 0x%02X\n", base_address);
  /* Enable diag mode */
  cycled_write_operation((0x19 + base_address), 0xEE, 6);  /* Write magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0xAB, 6);  /* Write magic cookie Lo */

  /* Double check that we are actually data from an FPGASID */
  idLo = cycled_read_operation((0x0 + base_address), 4);      /* Read identify Hi */
  idHi = cycled_read_operation((0x1 + base_address), 4);      /* Read identify Lo */
  uint16_t fpgasid_id = (idHi << 8 | idLo);
  if (fpgasid_id != FPGASID_IDENTIFIER) {
    usCFG("[SID] ERROR: 0x%04X != 0x%04X FPGASID NOT FOUND @ 0x%02X\n", fpgasid_id, FPGASID_IDENTIFIER, base_address);
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

  usCFG("\n");
  usCFG("[FPGASID DIAGNOSTIC RESULT]\n");
  usCFG("ID: %04X (FPGASID)\n", fpgasid_id);
  usCFG("CPLD REVISION: %02X\n", cpld);
  usCFG("FPGA REVISION: %02X\n", fpga);
  usCFG("PCA REVISION: %02X\n", pca);
  usCFG("UNIQUE IDENTIFIER: %02X%02X%02X%02X%02X%02X%02X%02X\n",
     unique_id[0], unique_id[1], unique_id[2], unique_id[3], unique_id[4], unique_id[5], unique_id[6], unique_id[7]);
  usCFG("CLOCK FREQUENCY READS: %.3f uS\n", frequency);
  usCFG("SELECT PINS: %02X\n", select_pins);
  usCFG("INDEX CFG A: %02X\n", idxa);
  usCFG("SID 1 A: %02X%02X%02X\n", sid_one_a[0], sid_one_a[1], sid_one_a[2]);
  usCFG("SID 2 A: %02X%02X%02X\n", sid_two_a[0], sid_two_a[1], sid_two_a[2]);
  usCFG("FILTERBIAS A SID1/SID2: %02X\n", flta);
  usCFG("INDEX CFG B: %02X\n", idxb);
  usCFG("SID 1 B: %02X%02X%02X\n", sid_one_b[0], sid_one_b[1], sid_one_b[2]);
  usCFG("SID 2 B: %02X%02X%02X\n", sid_two_b[0], sid_two_b[1], sid_two_b[2]);
  usCFG("FILTERBIAS B SID1/SID2: %02X\n", fltb);

  print_fpgasid_sidconfig(0, 1, sid_one_a);
  print_fpgasid_sidconfig(0, 2, sid_two_a);
  print_fpgasid_sidconfig(1, 1, sid_one_b);
  print_fpgasid_sidconfig(1, 2, sid_two_b);

  usCFG("\n");

  return;
}

void print_skpico_configuration(uint8_t base_address, uint8_t * configarray)
{

  usCFG("\n");
  usCFG("[SIDKICK-pico @ 0x%02X CONFIG]\n", base_address);
  for (size_t i = 0; i < 64; i++) {
    if (i >= 4 && i <= 7) continue;
    if (i >= 13 && i <= 56) continue;
    if (i == 62 || i == 63) continue;
    if (i == 0 || i == 8) {
      usCFG("[%02ld] %s: %02X ~ %s\n", i, config_names[i], configarray[i], (configarray[i] < s_sid_types) ? (char*)sid_types[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    if (i == 10) {
      usCFG("[%02ld] %s: %02X ~ %s\n", i, config_names[i], configarray[i], (configarray[i] < s_sid2_address) ? (char*)sid2_address[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    if (i == 59) {
      usCFG("[%02ld] %s: %02X ~ %s\n", i, config_names[i], configarray[i], (configarray[i] < s_clock_speed) ? (char*)clock_speed[configarray[i]] : (char*)error_type[0]);
      continue;
    }
    usCFG("[%02ld] %s: %02X\n", i, config_names[i], configarray[i]);
  }
  usCFG("[PRINT CFG SETTINGS END]\n");
  usCFG("\n");

}

void read_skpico_configuration(uint8_t base_address)
{
  /* Enter config mode */
  cycled_write_operation((init_configmode[0] + base_address), init_configmode[1], 6);
  /* usCFG("[W]$%02X:%02X\n", (init_configmode[0] + base_address), init_configmode[1]); */

  /* Read config from SKPico */
  for (int i = 0; i <= 63; ++i) {
    sleep_us(1);
    skpico_config[i] = cycled_read_operation((0x1D + base_address), 0);
    /* usCFG("[R%d]$%02X:%02X\n", i, (0x1D + base_address), skpico_config[i]); */
  }

  /* Exit config mode */
  cycled_write_operation((config_exit[0] + base_address), config_exit[1], 6);
  /* usCFG("[W]$%02X:%02X\n", (config_exit[0] + base_address), config_exit[1]); */

  usCFG("\n");
  usCFG("[SKPICO RECEIVED BUFFER]\n");
  print_cfg(skpico_config, 64);

  print_skpico_configuration(base_address, skpico_config);
  usCFG("\n");

}

/**
 * @brief Change Public Domain SID type
 * This requires a 5 second reset hold
 * to switch to and or from 6581/8580
 */
void switch_pdsid_type(void)
{
  set_reset_state(true); // reset_state = 1;
  set_paused_state(false); // paused_state = 0;
  clear_volume_state(); // memset(volume_state, 0, 4);
  clear_sid_memory(); // memset(sid_memory, 0, count_of(sid_memory));
  usCFG("[SID] RST PIN LOW\n");
  cPIN(RES); // gpio_put(RES, 0);
  usCFG("[SID] SLEEP\n");
  sleep_ms(6000);
  usCFG("[SID] RST PIN HIGH\n");
  sPIN(RES); // gpio_put(RES, 1);
  set_reset_state(false); // reset_state = 0;
  return;
}
