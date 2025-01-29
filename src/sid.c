/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
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
extern uint8_t __not_in_flash_func(bus_operation)(uint8_t command, uint8_t address, uint8_t data);
extern void clear_sid_registers(int sidno);

/* Declare variables */
uint8_t waveforms[4] = { 16, 32, 64, 128 };

uint8_t detect_sid_version(uint8_t start_addr)  /* Not working on real SIDS!? */
{
  static int restart = 0;
restart:
  bus_operation(0x10, (start_addr + 0x12), 0xFF);  /* Set testbit in voice 3 control register to disable oscillator */
  sleep_ms(1);
  bus_operation(0x10, (start_addr + 0x0E), 0xFF);  /* Set frequency in voice 3 to $ffff */
  sleep_ms(1);
  bus_operation(0x10, (start_addr + 0x0F), 0xFF);  /* Set frequency in voice 3 to $ffff */
  sleep_ms(1);
  bus_operation(0x10, (start_addr + 0x12), 0x20);  /* Set Sawtooth wave and gatebit OFF to start oscillator again */
  sleep_ms(1);
  uint8_t sidtype = bus_operation(0x11, (start_addr + 0x1B), 0x00);  /* Accu now has different value depending on sid model (6581=3/8580=2) */
  if (restart == 3) goto end;
  if (sidtype != 2 || sidtype != 3) {
    restart++;
    goto restart;
  }
end:
  restart = 0;
  return sidtype;  /* that is: Carry flag is set for 6581, and clear for 8580. */
}

void test_operation(uint8_t reg, uint8_t val)
{
  bus_operation(0x10, reg, val);
  sleep_us(5);
}

void wave_form_test(uint8_t voices[3], int v, int on, int off)
{
    test_operation((voices[v] + sid_registers[CONTR]), on);    /* CONTR */
    for (int f = 1; f <= 255; f++) {
      test_operation((voices[v] + sid_registers[NOTEHI]), f);  /* NOTEHI */
      sleep_ms(8);
    }
    test_operation((voices[v] + sid_registers[CONTR]), off);   /* CONTR */
}

void pulse_sweep_test(uint8_t voices[3], int v, int on, int off)
{
  test_operation((voices[v] + sid_registers[CONTR]), on);     /* CONTR */
  for (int x = 0; x <= 5; x++) {
    for (int y = 0; y <= 15; y++) {
      test_operation((voices[v] + sid_registers[PWMHI]), y);  /* PWMHI */
      sleep_ms(16);
    }
    for (int y = 15; y >= 0; y--) {
      test_operation((voices[v] + sid_registers[PWMHI]), y);  /* PWMHI */
      sleep_ms(16);
    }
  }
  test_operation((voices[v] + sid_registers[CONTR]), off);    /* CONTR */
}

void test_all_waveforms(uint8_t addr, uint8_t voices[3])
{
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  for (int v = 0; v < 3; v++) {
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
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  DBG("FILTER TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 2 ? "SAWTOOTH" : wf == 3 ? "PULSE" : "NOISE"));
  for (int fq = 15; fq <= 45; fq += 15) {
    DBG("HIGH FILTER FREQUENCY = %d\n", fq);
    test_operation(sid_registers[RESFLT], 87);  /* RESFLT 5*16+1+2+4 */
    for (int flt = 1; flt <= 3; flt++) {
      DBG("%s PASS\n", (flt == 1 ? "LOW" : flt == 2 ? "BAND" : "HIGH"));
      test_operation(sid_registers[MODVOL], (flt == 1 ? 31 : flt == 2 ? 47 : 79));  /* MODVOL */
      for (int v = 0; v < 3; v++) {
        DBG("VOICE %d TESTING ", (v + 1));
        test_operation((voices[v] + sid_registers[NOTEHI]), fq);   /* NOTEHI */
        test_operation((voices[v] + sid_registers[ATTDEC]), 0);    /* ATTDEC */
        test_operation((voices[v] + sid_registers[SUSREL]), 240);  /* SUSREL */
        test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */
        test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf] + 1);  /* CONTR */
        for (int f = 0; f <= 255; f++) {
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
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  DBG("A D S R TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 1 ? "SAWTOOTH" : wf == 2 ? "PULSE" : "NOISE"));
  for (int v = 0; v < 3; v++) {
    DBG("VOICE %d A D S R TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 170);  /* ATTDEC 10*16+10 */
    test_operation((voices[v] + sid_registers[SUSREL]), 58);   /* SUSREL 3*16+10 */
    test_operation((voices[v] + sid_registers[NOTEHI]), 40);   /* NOTEHI */
    test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    sleep_ms(3000);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    DBG("RELEASE ");
    sleep_ms(1000);
    DBG("COMPLETE\n");
    sleep_ms(600);

    DBG("VOICE %d S R TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 0);    /* ATTDEC */
    test_operation((voices[v] + sid_registers[SUSREL]), 250);  /* SUSREL 15*16+10 */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    sleep_ms(600);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    DBG("RELEASE ");
    sleep_ms(600);
    DBG("COMPLETE\n");
    sleep_ms(600);

    DBG("VOICE %d A D TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 170);  /* ATTDEC 10*16+10 */
    test_operation((voices[v] + sid_registers[SUSREL]), 0);    /* SUSREL */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    sleep_ms(2000);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    DBG("RELEASE ");
    sleep_ms(600);
    DBG("COMPLETE\n");
    sleep_ms(600);
  }
}

void modulation_tests(uint8_t addr, uint8_t voices[3], int wf)
{
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  DBG("RING MODULATION TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 1 ? "SAWTOOTH" : wf == 2 ? "PULSE" : "NOISE"));
  for (int v = 0; v < 3; v++) {
    int v2 = (v == 1 ? 0 : v == 2 ? 1 : 2);
    DBG("VOICE %d WITH VOICE %d ", (v + 1), (v2 + 1));
    test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */
    test_operation((voices[v] + sid_registers[ATTDEC]), 0);    /* ATTDEC */
    test_operation((voices[v] + sid_registers[SUSREL]), 250);  /* SUSREL 15*16+10 */
    test_operation((voices[v2] + sid_registers[NOTEHI]), 10);   /* NOTEHI */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 3));  /* CONTR */
    for (int f = 0; f <= 255; f++) {
      test_operation((voices[v] + sid_registers[NOTEHI]), f);   /* NOTEHI */
      sleep_ms(24);
    }
    test_operation((voices[v] + sid_registers[CONTR]), 0);   /* CONTR */
    DBG("COMPLETE\n");
  }
}

void sid_test(int sidno, char test, char wf)
{
  clear_sid_registers(sidno);
  uint8_t addr = (sidno * 0x20);
  uint8_t voices[3] = { (sid_registers[0] + addr), (sid_registers[7] + addr), (sid_registers[14] + addr) };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */

  switch (test) {
    case '1':  /* RUN ALL TESTS */
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
      test_all_waveforms(addr, voices);
      break;
    case '3':  /* FILTER */
      if (wf == 'A') {
        for (int w = 0; w < 4; w++) {
          filter_tests(addr, voices, w);
        }
      } else {
        filter_tests(addr, voices, (wf == 'T' ? 0 : wf == 'S' ? 1 : wf == 'P' ? 2 : 3));
      }
      break;
    case '4':  /* ENVELOPE */
      if (wf == 'A') {
        for (int w = 0; w < 4; w++) {
          envelope_tests(addr, voices, w);
        }
      } else {
        envelope_tests(addr, voices, (wf == 'T' ? 0 : wf == 'S' ? 1 : wf == 'P' ? 2 : 3));
      }
      break;
    case '5':  /* MODULATION */
      test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
      if (wf == 'A') {
        for (int w = 0; w < 4; w++) {
          modulation_tests(addr, voices, w);
        }
      } else {
        modulation_tests(addr, voices, (wf == 'T' ? 0 : wf == 'S' ? 1 : wf == 'P' ? 2 : 3));
      }
      break;
    default:
      break;
  }
}
