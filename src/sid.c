/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid.c
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

#include "globals.h"
#include "config.h"
#include "usbsid.h"
#include "sid.h"
#include "logging.h"


/* GPIO externals */
extern void __no_inline_not_in_flash_func(cycled_write_operation)(uint8_t address, uint8_t data, uint16_t cycles);
extern uint8_t __no_inline_not_in_flash_func(cycled_read_operation)(uint8_t address, uint16_t cycles);
extern void reset_sid(void);
extern void clear_sid_registers(int sidno);
extern void reset_sid_registers(void);
extern void clear_bus(int sidno);

/* config externals */
extern Config usbsid_config;

/* Declare variables */
volatile bool running_tests = false;
uint8_t waveforms[4] = { 16, 32, 64, 128 };


/* This routine works on real MOS SID chips and does not work on SKPico */
uint8_t detect_sid_model(uint8_t start_addr)
{ /* https://github.com/GideonZ/1541ultimate/blob/master/software/6502/sidcrt/player/advanced/detection.asm */
  CFG("[SID] DETECT SID MODEL\n");
  clear_bus(start_addr / 0x20);
  int restart = 0;
restart:
  /* lda #$48  ~ 2 cycles */
  /* sta $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x48, 6);
  /* sta $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0F), 0x48, 4);
  /* lsr ~ 2 cycles */
  /* sta $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x24, 4);  /* <-- 4 cycles instead of 6 */
  /* lda $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1B), 3);  /* <-- 3 cycles instead of 4 */
  /* tax      ~ 2 cycles */
  /* and #$fe ~ 2 cycles */
  /* bne      ~ 2 cycles */
  uint8_t readtest = cycled_read_operation((start_addr + 0x1B), 6);  /* SHOULD READ 3 */
  if (restart == 3) goto end;
  if (sidtype != 0 && sidtype != 1) {
    restart++;
    CFG("[SID] RESTART %d (ST:%u)\n", restart, sidtype);
    goto restart;
  }
end:
  /* output 0 = 8580, 1 = 6581, 2 = unknown
   * that is: Carry flag is set for 6581, and clear for 8580. */
  CFG("[SID] 0x%02X detect_sid_model sidtype raw %02X\n", start_addr, sidtype);
  CFG("[SID] 0x%02X detect_sid_model readtest %02X\n", start_addr, readtest);  /* SHOULD READ 3 */
  sidtype = (sidtype == 0 ? 2 : sidtype == 1 ? 3 : 0);  /* return 0 = unknown, 2 = 8580, 3 = 6581 */
  CFG("[SID] 0x%02X detect_sid_model return %u\n", start_addr, sidtype);
  return sidtype;
}

/* This routine works on real MOS SID chips and does not work on SKPico */
uint8_t detect_sid_version(uint8_t start_addr)
{ /* https://codebase64.org/doku.php?id=base:detecting_sid_type_-_safe_method */
  CFG("[SID] DETECT SID VERSION\n");
  int restart = 0;
restart:
  /* LDA #$ff  ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0xFF, 6);  /* Set testbit in voice 3 control register to disable oscillator */
  /* STA $d40e ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0E), 0xFF, 4);  /* Set frequency in voice 3 to $ffff */
  /* STA $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0F), 0xFF, 4);  /* Set frequency in voice 3 to $ffff */
  /* LDA #$20  ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x20, 6);  /* Set Sawtooth wave and gatebit off to start oscillator again */
  /* LDA $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1B), 3);  /* Accu now has different value depending on sid model (6581=3/8580=2) */

  if (restart == 3) goto end;
  if (sidtype != 2 && sidtype != 3) {
    restart++;
    CFG("[SID] RESTART %d (ST:%u)\n", restart, sidtype);
    goto restart;
  }
end:
  restart = 0;
  CFG("[SID] 0x%02X detect_sid_version raw %02X\n", start_addr, sidtype);
  // sidtype = (sidtype < 4 ? sidtype : 0);
  sidtype = ((sidtype >= 1) && (sidtype % 2) == 0 ? 2 : 3);
  CFG("[SID] 0x%02X detect_sid_version return %02X\n", start_addr, sidtype);
  return sidtype;
}

uint8_t detect_sid_unsafe(uint8_t start_addr)
{ /* https://codebase64.org/doku.php?id=base:detecting_sid_type */
  CFG("[SID] DETECT SID UNSAFE\n");
  /* clear sid registers */
  for (int reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    /* STA $D400,x ~ 5 cycles */
    /* DEX         ~ 2 cycles*/
    /* BPL         ~ 2 cycles */
    cycled_write_operation((start_addr | sid_registers[reg]), 0x00, 9);
  }

  /* LDA #$02  ~ 2 cycles */
  /* STA $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0F), 0x02, 6);
  /* LDA #$30  ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x30, 6);
  /* LDY #$00  ~ 2 cycles */
	/* LDX #$00  ~ 2 cycles */
  /* LDA $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1B), 8);

  CFG("[SID] 0x%02X detect_sid_version raw %02X\n", start_addr, sidtype);
  // sidtype = (sidtype < 4 ? sidtype : 0);
  sidtype = ((sidtype >= 1) && (sidtype % 2) == 0 ? 2 : 3);
  CFG("[SID] 0x%02X detect_sid_version return %02X\n", start_addr, sidtype);
  return sidtype;
}

/* This routine works on SKPico and does not work on real MOS SID chips */
uint8_t detect_sid_version_skpico(uint8_t start_addr)  /* Not working on real SIDS!? */
{ /* https://codebase64.org/doku.php?id=base:detecting_sid_type_-_safe_method */
  CFG("[SID] DETECT SID VERSION SKPICO\n");
  int restart = 0;
restart:
  /* LDA #$ff ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0xFF, 1000);  /* Set testbit in voice 3 control register to disable oscillator */
  /* STA $d40e ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0E), 0xFF, 1000);  /* Set frequency in voice 3 to $ffff */
  sleep_ms(1);
  /* STA $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0F), 0xFF, 1000);  /* Set frequency in voice 3 to $ffff */
  sleep_ms(1);
  /* LDA #$20 ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x20, 1000);  /* Set Sawtooth wave and gatebit OFF to start oscillator again */
  sleep_ms(1);
  /* LDA $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1B), 1000);  /* Accu now has different value depending on sid model (6581=3/8580=2) */
  if (restart == 3) goto end;
  if (sidtype != 2 && sidtype != 3) {
    restart++;
    CFG("[SID] RESTART %d (ST:%u)\n", restart, sidtype);
    goto restart;
  }
end:
  restart = 0;
  CFG("[SID] 0x%02X detect_sid_version_skpico raw %02X\n", start_addr, sidtype);
  sidtype = (sidtype < 4 ? sidtype : 0);
  CFG("[SID] 0x%02X detect_sid_version_skpico %02X\n", start_addr, sidtype);
  return sidtype;  /* that is: Carry flag is set for 6581, and clear for 8580. */
}

bool detect_skpico(uint8_t base_address)
{
  CFG("[CFG] CHECKING FOR SIDKICK-pico @ 0x%02X\n", base_address);
  /* SKPico routine */
  char skpico_version[36] = {0};
  cycled_write_operation((0x1F + base_address), 0xFF, 10); /* Init config mode */
  cycled_write_operation((0x1D + base_address), 0xFA, 10); /* Extend config mode */
  for (int i = 0; i < 36; i++) {
    cycled_write_operation((0x1E + base_address), 0xE0 + i, 10);
    skpico_version[i] = cycled_read_operation((0x1D + base_address), 10);
    if (i >= 2 && i <= 5) skpico_version[i] |= 0x60;
  }
  /* Reset after writes
   * is needed for Real SID's to recover for SID detection
   * but breaks SKPico SID detection */
  cycled_write_operation((0x1D + base_address), 0xFB, 10); /* Exit config mode */
  if (skpico_version[2] == 0x70
      && skpico_version[3] == 0x69
      && skpico_version[4] == 0x63
      && skpico_version[5] == 0x6F) {
    CFG("[CFG] SIDKICK-pico @ 0x%02X version is: %.36s\n", base_address, skpico_version);
    return true;
  }
  return false;
}

void print_fpgasid_sidconfig(int slot, int sidno, uint8_t * configarray)
{
  const char * slots[] = { "A", "B" };
  const char * extinsource[] = { "analog input", "disabled", "other SID", "digifix (8580)" };
  const char * readback[] = { "bitrotation 6581", "always read value", "always read $00", "bitrotation 8580" };
  const char * regdelay[] = { "6581", "8580" };
  const char * mixedwave[] = { "6581", "8580" };
  const char * crunchydac[] = { "on (6581)", "off (8580)" };
  const char * filtermode[] = { "6581", "8580" };
  const char * outputmode[] = { "dual output over SID1 & SID2 channels -> stereo", "dual output over SID1 channel -> mono mix" };
  const char * sid2addr[] = { "$D400 ", "$DE00 ", "$D500 ", "$D420 ", "" };
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

  CFG("\n");
  CFG("[FPGASID CURRENT CONFIG SLOT%s SID %d]\n", slots[slot], sidno);
  if (sidno == 1) {
    CFG("OUTPUT MODE: %s\n", outputmode[outp]);
    CFG("SID2 ADDRESSES: %s%s%s%s\n", sid2addr[addr], sid2addr[addr1], sid2addr[addr2], sid2addr[addr3]);
  }
  CFG("SOURCE EXTIN: %s\n", extinsource[extin]);
  CFG("WRITEREGS READBACK: %s\n", readback[readb]);
  CFG("REG DELAY: %s\n", regdelay[regd]);
  CFG("MIXED WAVE: %s\n", mixedwave[mixw]);
  CFG("CRUNCHY DAC: %s\n", crunchydac[crunch]);
  CFG("FILTER MODE: %s\n", filtermode[fltr]);
  CFG("DIGIFIX VALUE: %02X\n", configarray[0]);
}

void read_fpgasid_configuration(uint8_t base_address)
{

  if (((base_address >= 0x0 && base_address < 0x40) && (usbsid_config.socketOne.clonetype != 4))
    || ((base_address >= 0x40 && base_address < 0x80) && (usbsid_config.socketTwo.clonetype != 4))) {
      CFG("[CFG] ERROR INCORRECT ADDRESS (0x%02x) AND CLONETYPE (%d %d)\n",
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
  CFG("[CFG] READING CONFIGURATION FROM FPGASID @ 0x%02X\n", base_address);
  /* Enable diag mode */
  cycled_write_operation((0x19 + base_address), 0xEE, 6);  /* Write magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0xAB, 6);  /* Write magic cookie Lo */

  /* Double check that we are actually data from an FPGASID */
  idLo = cycled_read_operation((0x0 + base_address), 4);      /* Read identify Hi */
  idHi = cycled_read_operation((0x1 + base_address), 4);      /* Read identify Lo */
  uint16_t fpgasid_id = (idHi << 8 | idLo);
  if (fpgasid_id != 0xF51D) {
    CFG("[CFG] ERROR: 0x%04X != 0xF51D FPGASID NOT FOUND @ 0x%02X\n", fpgasid_id, base_address);
    return true;
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

  CFG("\n");
  CFG("[FPGASID DIAGNOSTIC RESULT]\n");
  CFG("ID: %04X (FPGASID)\n", fpgasid_id);
  CFG("CPLD REVISION: %02X\n", cpld);
  CFG("FPGA REVISION: %02X\n", fpga);
  CFG("PCA REVISION: %02X\n", pca);
  CFG("UNIQUE IDENTIFIER: %02X%02X%02X%02X%02X%02X%02X%02X\n",
     unique_id[0], unique_id[1], unique_id[2], unique_id[3], unique_id[4], unique_id[5], unique_id[6], unique_id[7]);
  CFG("CLOCK FREQUENCY READS: %.3f uS\n", frequency);
  CFG("SELECT PINS: %02X\n", select_pins);
  CFG("INDEX CFG A: %02X\n", idxa);
  CFG("SID 1 A: %02X%02X%02X\n", sid_one_a[0], sid_one_a[1], sid_one_a[2]);
  CFG("SID 2 A: %02X%02X%02X\n", sid_two_a[0], sid_two_a[1], sid_two_a[2]);
  CFG("FILTERBIAS A SID1/SID2: %02X\n", flta);
  CFG("INDEX CFG B: %02X\n", idxb);
  CFG("SID 1 B: %02X%02X%02X\n", sid_one_b[0], sid_one_b[1], sid_one_b[2]);
  CFG("SID 2 B: %02X%02X%02X\n", sid_two_b[0], sid_two_b[1], sid_two_b[2]);
  CFG("FILTERBIAS B SID1/SID2: %02X\n", fltb);

  print_fpgasid_sidconfig(0, 1, sid_one_a);
  print_fpgasid_sidconfig(0, 2, sid_two_a);
  print_fpgasid_sidconfig(1, 1, sid_one_b);
  print_fpgasid_sidconfig(1, 2, sid_two_b);


  CFG("\n");

  return;
}

bool detect_fpgasid(uint8_t base_address)
{
  CFG("[CFG] CHECKING FOR FPGASID @ 0x%02X\n", base_address);
  uint8_t idHi, idLo;
  /* Enable configuration mode (if available) */
  cycled_write_operation((0x19 + base_address), 0x80, 6);      /* Write magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0x65, 6);      /* Write magic cookie Lo */
  /* Start identification routine */
  cycled_write_operation((0x1E + base_address), (1 << 7), 6);  /* Set identify bit to 1 */
  idLo = cycled_read_operation((0x19 + base_address), 4);      /* Read identify Hi */
  idHi = cycled_read_operation((0x1A + base_address), 4);      /* Read identify Lo */
  /* Exit configuration mode */
  cycled_write_operation((0x19 + base_address), 0x0, 6);       /* Clear magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0x0, 6);       /* Clear magic cookie Lo */
  uint16_t fpgasid_id = (idHi << 8 | idLo);
  CFG("[CFG] READ IDENTIFY 0x%04X (0x%02X,0x%02X) @ 0x%02X\n", fpgasid_id, idHi, idLo, base_address);
  if (fpgasid_id == 0xF51D) {
    CFG("[CFG] Found FPGASID @ 0x%02X\n", base_address);
    return true;
  }

  return false;
}

void detect_clone_types(bool sockone, bool onedual, bool socktwo, bool twodual)
{ /* TODO: Add other clone type detection routines */
  uint8_t base_address = 0x00;
  int chip = 0, result = 0;
  if (sockone) {
    if (detect_skpico(base_address))  { chip = 1; result = 2; goto done_one; }  /* Clone, SKpico */
    if (detect_fpgasid(base_address)) { chip = 1; result = 4; goto done_one; }  /* Clone, FPGASID */
    chip = 2;    /* Unknown */
    result = 1;  /* Other */
done_one:
    usbsid_config.socketOne.chiptype  = chip;
    usbsid_config.socketOne.clonetype = result;
  }
  if (!onedual)
    base_address = 0x20;
  if (onedual)
    base_address = 0x40;
  chip = 0, result = 0;
  if(socktwo && (base_address > 0x00)) {
    if (detect_skpico(base_address))  { chip = 1; result = 2; goto done_two; }  /* Clone, SKpico */
    if (detect_fpgasid(base_address)) { chip = 1; result = 4; goto done_two; }  /* Clone, FPGASID */
    chip = 2;    /* Unknown */
    result = 1;  /* Other */
done_two:
    usbsid_config.socketTwo.chiptype  = 1;  /* Clone */
    usbsid_config.socketTwo.clonetype = 2;  /* SKpico */
  }
  return;
}

bool detect_fmopl(uint8_t base_address)
{
  CFG("[CFG] DETECT FMOPL @: %02X\n", base_address);
  int restart = 0;
restart:
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x60, 10);
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x80, 10);
  uint8_t r = cycled_read_operation(base_address, 10);
  CFG("[CFG] READ FMOPL = 0xC0? %02X\n", r);
  if (restart == 3) goto end;
  if (r != 0xC0) {
    restart++;
    goto restart;
  }
end:
  return (r == 0xC0 ? true : false);
}

void test_operation(uint8_t reg, uint8_t val)
{
  cycled_write_operation(reg, val, 5);  /* 5 cycle wait before writing */
}

void wave_form_test(uint8_t voices[3], int v, int on, int off)
{
    if (!running_tests) { reset_sid_registers(); return; };
    test_operation((voices[v] + sid_registers[CONTR]), on);    /* CONTR */
    for (int f = 1; f <= 255; f++) {
      if (!running_tests) { reset_sid_registers(); return; };
      test_operation((voices[v] + sid_registers[NOTEHI]), f);  /* NOTEHI */
      sleep_ms(8);
    }
    test_operation((voices[v] + sid_registers[CONTR]), off);   /* CONTR */
}

void pulse_sweep_test(uint8_t voices[3], int v, int on, int off)
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((voices[v] + sid_registers[CONTR]), on);     /* CONTR */
  for (int x = 0; x <= 5; x++) {
    if (!running_tests) { reset_sid_registers(); return; };
    for (int y = 0; y <= 15; y++) {
      if (!running_tests) { reset_sid_registers(); return; };
      test_operation((voices[v] + sid_registers[PWMHI]), y);  /* PWMHI */
      sleep_ms(16);
    }
    for (int y = 15; y >= 0; y--) {
      if (!running_tests) { reset_sid_registers(); return; };
      test_operation((voices[v] + sid_registers[PWMHI]), y);  /* PWMHI */
      sleep_ms(16);
    }
  }
  test_operation((voices[v] + sid_registers[CONTR]), off);    /* CONTR */
}

void test_all_waveforms(uint8_t addr, uint8_t voices[3])
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  for (int v = 0; v < 3; v++) {
    if (!running_tests) { reset_sid_registers(); return; };
    DBG("TEST VOICE %d\n", (v + 1));
    DBG("WAVEFORM TESTS\n");
    test_operation((voices[v] + sid_registers[ATTDEC]), 33);   /* ATTDEC 2*16+1 */
    test_operation((voices[v] + sid_registers[SUSREL]), 242);  /* SUSREL 15*16+2 */
    test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */

    DBG("TRIANGLE TESTING ");
    wave_form_test(voices, v, 17, 16);
    DBG("COMPLETE\n");
    DBG("SAWTOOTH TESTING ");
    wave_form_test(voices, v, 33, 32);
    DBG("COMPLETE\n");
    DBG("PULSE TESTING ");
    wave_form_test(voices, v, 65, 64);
    DBG("COMPLETE\n");
    DBG("NOISE TESTING ");
    wave_form_test(voices, v, 129, 128);
    DBG("COMPLETE\n");

    DBG("PULSE WIDTH SWEEP TESTING ");
    test_operation((voices[v] + sid_registers[NOTEHI]), 40);  /* NOTEHI */
    pulse_sweep_test(voices, v, 65, 64);
    DBG("COMPLETE\n");
  }
}

void filter_tests(uint8_t addr, uint8_t voices[3], int wf)
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  DBG("FILTER TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 2 ? "SAWTOOTH" : wf == 3 ? "PULSE" : "NOISE"));
  for (int fq = 15; fq <= 45; fq += 15) {
    if (!running_tests) { reset_sid_registers(); return; };
    DBG("HIGH FILTER FREQUENCY = %d\n", fq);
    test_operation(sid_registers[RESFLT], 87);  /* RESFLT 5*16+1+2+4 */
    for (int flt = 1; flt <= 3; flt++) {
      if (!running_tests) { reset_sid_registers(); return; };
      DBG("%s PASS\n", (flt == 1 ? "LOW" : flt == 2 ? "BAND" : "HIGH"));
      test_operation(sid_registers[MODVOL], (flt == 1 ? 31 : flt == 2 ? 47 : 79));  /* MODVOL */
      for (int v = 0; v < 3; v++) {
        if (!running_tests) { reset_sid_registers(); return; };
        DBG("VOICE %d TESTING ", (v + 1));
        test_operation((voices[v] + sid_registers[NOTEHI]), fq);   /* NOTEHI */
        test_operation((voices[v] + sid_registers[ATTDEC]), 0);    /* ATTDEC */
        test_operation((voices[v] + sid_registers[SUSREL]), 240);  /* SUSREL */
        test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */
        test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf] + 1);  /* CONTR */
        for (int f = 0; f <= 255; f++) {
          if (!running_tests) { reset_sid_registers(); return; };
          test_operation((voices[v] + sid_registers[FC_HI]), f);  /* FC_HI */
          sleep_ms(8);
        }
        test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
        DBG("COMPLETE\n");
      }
    }
  }
}

void envelope_tests(uint8_t addr, uint8_t voices[3], int wf)
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  DBG("A D S R TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 1 ? "SAWTOOTH" : wf == 2 ? "PULSE" : "NOISE"));
  for (int v = 0; v < 3; v++) {
    if (!running_tests) { reset_sid_registers(); return; };
    DBG("VOICE %d A D S R TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 170);  /* ATTDEC 10*16+10 */
    test_operation((voices[v] + sid_registers[SUSREL]), 58);   /* SUSREL 3*16+10 */
    test_operation((voices[v] + sid_registers[NOTEHI]), 40);   /* NOTEHI */
    test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(3000);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    DBG("RELEASE ");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(1000);
    DBG("COMPLETE\n");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);

    DBG("VOICE %d S R TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 0);    /* ATTDEC */
    test_operation((voices[v] + sid_registers[SUSREL]), 250);  /* SUSREL 15*16+10 */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    DBG("RELEASE ");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
    DBG("COMPLETE\n");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);

    DBG("VOICE %d A D TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 170);  /* ATTDEC 10*16+10 */
    test_operation((voices[v] + sid_registers[SUSREL]), 0);    /* SUSREL */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(2000);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    DBG("RELEASE ");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
    DBG("COMPLETE\n");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
  }
}

void modulation_tests(uint8_t addr, uint8_t voices[3], int wf)
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  DBG("RING MODULATION TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 1 ? "SAWTOOTH" : wf == 2 ? "PULSE" : "NOISE"));
  for (int v = 0; v < 3; v++) {
    if (!running_tests) { reset_sid_registers(); return; };
    int v2 = (v == 1 ? 0 : v == 2 ? 1 : 2);
    DBG("VOICE %d WITH VOICE %d ", (v + 1), (v2 + 1));
    test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */
    test_operation((voices[v] + sid_registers[ATTDEC]), 0);    /* ATTDEC */
    test_operation((voices[v] + sid_registers[SUSREL]), 250);  /* SUSREL 15*16+10 */
    test_operation((voices[v2] + sid_registers[NOTEHI]), 10);   /* NOTEHI */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 3));  /* CONTR */
    for (int f = 0; f <= 255; f++) {
      if (!running_tests) { reset_sid_registers(); return; };
      test_operation((voices[v] + sid_registers[NOTEHI]), f);   /* NOTEHI */
      sleep_ms(24);
    }
    test_operation((voices[v] + sid_registers[CONTR]), 0);   /* CONTR */
    DBG("COMPLETE\n");
  }
}

void sid_test(int sidno, char test, char wf)
{
  if (!running_tests) { reset_sid_registers(); return; };
  clear_sid_registers(sidno);
  uint8_t addr = (sidno * 0x20);
  uint8_t voices[3] = {
    (sid_registers[0] + addr),
    (sid_registers[7] + addr),
    (sid_registers[14] + addr)
  };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */

  switch (test) {
    case '1':  /* RUN ALL TESTS */
      if (!running_tests) { reset_sid_registers(); return; };
      test_all_waveforms(addr, voices);
      clear_sid_registers(sidno);
      filter_tests(addr, voices, 0);
      filter_tests(addr, voices, 1);
      filter_tests(addr, voices, 2);
      filter_tests(addr, voices, 3);
      clear_sid_registers(sidno);
      envelope_tests(addr, voices, 0);
      envelope_tests(addr, voices, 1);
      envelope_tests(addr, voices, 2);
      envelope_tests(addr, voices, 3);
      clear_sid_registers(sidno);
      modulation_tests(addr, voices, 0);
      modulation_tests(addr, voices, 1);
      modulation_tests(addr, voices, 2);
      modulation_tests(addr, voices, 3);
      clear_sid_registers(sidno);
      break;
    case '2':  /* ALL WAVEFORMS */
      if (!running_tests) { reset_sid_registers(); return; };
      test_all_waveforms(addr, voices);
      break;
    case '3':  /* FILTER */
      if (wf == 'A') {
        for (int w = 0; w < 4; w++) {
          if (!running_tests) { reset_sid_registers(); return; };
          filter_tests(addr, voices, w);
        }
      } else {
        if (!running_tests) { reset_sid_registers(); return; };
        filter_tests(addr, voices, (wf == 'T' ? 0 : wf == 'S' ? 1 : wf == 'P' ? 2 : 3));
      }
      break;
    case '4':  /* ENVELOPE */
      if (wf == 'A') {
        for (int w = 0; w < 4; w++) {
          if (!running_tests) { reset_sid_registers(); return; };
          envelope_tests(addr, voices, w);
        }
      } else {
        if (!running_tests) { reset_sid_registers(); return; };
        envelope_tests(addr, voices, (wf == 'T' ? 0 : wf == 'S' ? 1 : wf == 'P' ? 2 : 3));
      }
      break;
    case '5':  /* MODULATION */
      if (!running_tests) { reset_sid_registers(); return; };
      test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
      if (wf == 'A') {
        for (int w = 0; w < 4; w++) {
          if (!running_tests) { reset_sid_registers(); return; };
          modulation_tests(addr, voices, w);
        }
      } else {
        if (!running_tests) { reset_sid_registers(); return; };
        modulation_tests(addr, voices, (wf == 'T' ? 0 : wf == 'S' ? 1 : wf == 'P' ? 2 : 3));
      }
      break;
    default:
      break;
  }
  return;
}
