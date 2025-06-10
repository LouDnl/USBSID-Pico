/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_detection.c
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
extern void clear_bus(int sidno);

/* Config externals */
extern Config usbsid_config;
extern RuntimeCFG cfg;
extern char *sidtypes[5];

/* Config socket */
extern void verify_chipdetection_results(bool quiet);
extern void verify_sid_addr(bool quiet);
extern void apply_socket_change(bool quiet);
extern void apply_fmopl_config(bool quiet);

/* Pre declarations */
uint8_t detect_sid_version(uint8_t start_addr);
uint8_t detect_sid_model(uint8_t start_addr);
uint8_t detect_sid_version_skpico(uint8_t start_addr);
uint8_t detect_sid_unsafe(uint8_t start_addr);
uint8_t (*sid_detection[4])(uint8_t) = { detect_sid_model, detect_sid_version, detect_sid_version_skpico, detect_sid_unsafe };


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
  if (readtest != 3) sidtype = 0;  /* if the readtest does not read 3, return unknown */
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
  CFG("[SID] CHECKING FOR SIDKICK-pico @ 0x%02X\n", base_address);
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
    CFG("[SID] SIDKICK-pico @ 0x%02X version is: %.36s\n", base_address, skpico_version);
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
      CFG("[SID] ERROR INCORRECT ADDRESS (0x%02x) AND CLONETYPE (%d %d)\n",
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
  CFG("[SID] READING CONFIGURATION FROM FPGASID @ 0x%02X\n", base_address);
  /* Enable diag mode */
  cycled_write_operation((0x19 + base_address), 0xEE, 6);  /* Write magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0xAB, 6);  /* Write magic cookie Lo */

  /* Double check that we are actually data from an FPGASID */
  idLo = cycled_read_operation((0x0 + base_address), 4);      /* Read identify Hi */
  idHi = cycled_read_operation((0x1 + base_address), 4);      /* Read identify Lo */
  uint16_t fpgasid_id = (idHi << 8 | idLo);
  if (fpgasid_id != 0xF51D) {
    CFG("[SID] ERROR: 0x%04X != 0xF51D FPGASID NOT FOUND @ 0x%02X\n", fpgasid_id, base_address);
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
  CFG("[SID] CHECKING FOR FPGASID @ 0x%02X\n", base_address);
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
  CFG("[SID] READ IDENTIFY 0x%04X (0x%02X,0x%02X) @ 0x%02X\n", fpgasid_id, idHi, idLo, base_address);
  if (fpgasid_id == 0xF51D) {
    CFG("[SID] Found FPGASID @ 0x%02X\n", base_address);
    return true;
  }
  return false;
}

bool detect_fmopl(uint8_t base_address)
{
  CFG("[SID] DETECT FMOPL @: %02X\n", base_address);
  int restart = 0;
restart:
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x60, 10);
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x80, 10);
  uint8_t r = cycled_read_operation(base_address, 10);
  CFG("[SID] READ FMOPL = 0xC0? %02X\n", r);
  if (restart == 3) goto end;
  if (r != 0xC0) {
    restart++;
    goto restart;
  }
end:
  return (r == 0xC0 ? true : false);
}

uint8_t detect_clone_type(Socket * cfg_ptr)
{
  if (!cfg_ptr->enabled) return false;

  // struct Socket * cfg_ptr = (socket == 1 ? &usbsid_config.socketOne : &usbsid_config.socketTwo);
  uint8_t base_address = cfg_ptr->sid1.addr;
  static int chip, clone;

  if (detect_skpico(base_address))  { chip = 1; clone = 2; goto done_clone; }  /* Clone, SKpico */
  if (detect_fpgasid(base_address)) { chip = 1; clone = 4; goto done_clone; }  /* Clone, FPGASID */
  chip = 0;   /* Real */
  clone = 0;  /* Disabled */
  /* Disable SID detection for SID2 in this socket since no supporting clone is present */
  cfg_ptr->dualsid = false;  /* NOTICE: Workaround, might break detection for other clone types */
  cfg_ptr->sid2.addr = 0xFF;  /* NOTICE: Workaround, might break detection for other clone types */
  cfg_ptr->sid2.id = 0xFF;  /* NOTICE: Workaround, might break detection for other clone types */
  cfg_ptr->sid2.type = 1;  /* NOTICE: Workaround, might break detection for other clone types */
done_clone:
  cfg_ptr->chiptype  = chip;
  cfg_ptr->clonetype = clone;
  return clone;
}

uint8_t detect_sid_type(Socket * socket, SIDChip * sidchip)
{
  if (!socket->enabled) {   /* Socket must be enabled! */
    socket->chiptype = 0;   /* real */
    socket->clonetype = 0;  /* disabled */
    sidchip->type = 1;      /* N/A */
    return sidchip->type;
  }
  /* routine 2 for SKPico, all others use routine 0 */
  int detection_routine = (socket->clonetype != 2 ? 0 : 2);
  if (sidchip->addr != 0xFF) {
    uint8_t sid = sid_detection[detection_routine](sidchip->addr);
    CFG("[CONFIG] [READ SID%d] [%02x %s]\n", (sidchip->id + 1), sid, sidtypes[sid]);
    sidchip->type = sid;
    goto done_sid;
  }
  sidchip->type = 0;  /* unknown */
done_sid:
  /* If SKPico and unknown SID then try detecting if there is FMOpl activated */
  if (socket->clonetype == 2 && sidchip->type == 0) {
    if (detect_fmopl(sidchip->addr)) {
      sidchip->type = 4; /* FMOpl */
      usbsid_config.FMOpl.enabled = true;
      apply_fmopl_config(false);
    };
  }
  CFG("[CONFIG] SOCKET ONE SID%d TYPE: %s\n", sidchip->id, sidtypes[sidchip->type]);
  return sidchip->type;
}

/* Auto detect Chip and SID type routine
 * `auto_config` will ignore current socket and sid settings
 * `with_delay` is required for SIDKICK-pico at boot time
 */
void auto_detect_routine(bool auto_config, bool with_delay)
{
  CFG("\n");
  CFG("[START AUTO DETECT ROUTINE]\n");
  if (auto_config) {
    CFG("[SID] SET AUTO CONFIG DEFAULT VALUES\n");
    usbsid_config.mirrored = false;           /* Yeah let's just disable that for now okay? */

    /* Socket One */
    usbsid_config.socketOne.enabled = true;   /* start enabled */
    usbsid_config.socketOne.dualsid = true;   /* start as dualsid */
    usbsid_config.socketOne.chiptype = 0;     /* real */
    usbsid_config.socketOne.clonetype = 0;    /* disabled */
    usbsid_config.socketOne.sid1.id = 0;      /* default id */
    usbsid_config.socketOne.sid1.addr = 0x00; /* default address */
    usbsid_config.socketOne.sid1.type = 0;    /* unknown */
    usbsid_config.socketOne.sid2.id = 1;      /* default id */
    usbsid_config.socketOne.sid2.addr = 0x20; /* default address */
    usbsid_config.socketOne.sid2.type = 0;    /* unknown */

    /* Socket Two */
    usbsid_config.socketTwo.enabled = true;   /* start enabled */
    usbsid_config.socketTwo.dualsid = true;   /* start as dualsid */
    usbsid_config.socketTwo.chiptype = 0;     /* real */
    usbsid_config.socketTwo.clonetype = 0;    /* disabled */
    usbsid_config.socketTwo.sid1.id = 2;      /* default id */
    usbsid_config.socketTwo.sid1.addr = 0x40; /* default address */
    usbsid_config.socketTwo.sid1.type = 0;    /* unknown */
    usbsid_config.socketTwo.sid2.id = 3;      /* default id */
    usbsid_config.socketTwo.sid2.addr = 0x60; /* default address */
    usbsid_config.socketTwo.sid2.type = 0;    /* unknown */

    /* Apply socket and bus config before continuing or detection routines will not work properly */
    apply_socket_change(true);
    if (with_delay) sleep_ms(250); /* Stupid workaround for SKPico requiring a zillion ms to boot up */
  }
  CFG("[SID] START CHIP TYPE DETECTION FOR SOCKET ONE\n");
  if (with_delay) sleep_ms(500); /* Stupid workaround for SKPico requiring a zillion ms to boot up */
  /* SocketOne (twice if failed) */
  if (detect_clone_type(&usbsid_config.socketOne) == 0) detect_clone_type(&usbsid_config.socketOne);
  CFG("[SID] START CHIP TYPE DETECTION FOR SOCKET TWO\n");
  if (with_delay) sleep_ms(500);
   /* SocketTwo (twice if failed) */
  if (detect_clone_type(&usbsid_config.socketTwo) == 0) detect_clone_type(&usbsid_config.socketTwo);
  if (auto_config) verify_sid_addr(true);

  apply_socket_change(true);

  if (with_delay) sleep_ms(250); /* Stupid workaround for SKPico requiring a zillion ms to boot up */
  /* Detect SID types at default config once, uses the chiptype to define the detection routine */
  CFG("[SID] START SID TYPE DETECTION FOR SOCKET ONE\n");
  detect_sid_type(&usbsid_config.socketOne, &usbsid_config.socketOne.sid1);
  if (usbsid_config.socketOne.dualsid)
    detect_sid_type(&usbsid_config.socketOne, &usbsid_config.socketOne.sid2);
  CFG("[SID] START SID TYPE DETECTION FOR SOCKET TWO\n");
  detect_sid_type(&usbsid_config.socketTwo, &usbsid_config.socketTwo.sid1);
  if (usbsid_config.socketTwo.dualsid)
    detect_sid_type(&usbsid_config.socketTwo, &usbsid_config.socketTwo.sid2);
  if (auto_config) {
    verify_chipdetection_results(false); /* Only on auto config verify results */
    verify_sid_addr(false);
  }
  /* Apply new socket and bus config */
  apply_socket_change(true);

  CFG("[END AUTO DETECT ROUTINE]\n");
}
