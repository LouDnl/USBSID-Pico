/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_tests.c
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

#include <globals.h>
#include <config.h>
#include <bus.h>
#include <sid.h>
#include <logging.h>


/* Declare variables */
volatile bool running_tests = false;
static const uint8_t waveforms[4] = { 16, 32, 64, 128 };


/**
 * @brief Write a single SID register with a fixed cycle delay
 *
 * @param uint8_t reg
 * @param uint8_t val
 */
void test_operation(uint8_t reg, uint8_t val)
{
  cycled_write_operation(reg, val, 5);  /* 5 cycle wait before writing */
}

/**
 * @brief Sweep a voice's note frequency while a waveform is held on
 *
 * Sets the voice's CONTR register to `on`, sweeps NOTEHI from 1 to 255 with
 * an 8ms delay between steps, then sets CONTR to `off`.
 *
 * @note aborts early and resets the SID registers if running_tests goes false
 *
 * @param uint8_t voices[3]
 * @param int v, voice index 0..2
 * @param int on, CONTR value to enable the waveform
 * @param int off, CONTR value to disable the waveform
 */
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

/**
 * @brief Sweep a voice's pulse width up and down while a waveform is held on
 *
 * Sets the voice's CONTR register to `on`, then for 6 iterations sweeps
 * PWMHI from 0 to 15 and back down to 0 with a 16ms delay between steps,
 * finally sets CONTR to `off`.
 *
 * @note aborts early and resets the SID registers if running_tests goes false
 *
 * @param uint8_t voices[3]
 * @param int v, voice index 0..2
 * @param int on, CONTR value to enable the waveform
 * @param int off, CONTR value to disable the waveform
 */
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

/**
 * @brief Run the triangle/sawtooth/pulse/noise waveform and pulse width sweep tests on every voice
 *
 * Sets volume to full, then for each of the 3 voices runs wave_form_test()
 * for each of the 4 waveforms followed by pulse_sweep_test().
 *
 * @note aborts early and resets the SID registers if running_tests goes false
 *
 * @param uint8_t addr, SID base address offset
 * @param uint8_t voices[3]
 */
void test_all_waveforms(uint8_t addr, uint8_t voices[3])
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  for (int v = 0; v < 3; v++) {
    if (!running_tests) { reset_sid_registers(); return; };
    usDBG("TEST VOICE %d\n", (v + 1));
    usDBG("WAVEFORM TESTS\n");
    test_operation((voices[v] + sid_registers[ATTDEC]), 33);   /* ATTDEC 2*16+1 */
    test_operation((voices[v] + sid_registers[SUSREL]), 242);  /* SUSREL 15*16+2 */
    test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */

    usDBG("TRIANGLE TESTING ");
    wave_form_test(voices, v, 17, 16);
    usDBG("COMPLETE\n");
    usDBG("SAWTOOTH TESTING ");
    wave_form_test(voices, v, 33, 32);
    usDBG("COMPLETE\n");
    usDBG("PULSE TESTING ");
    wave_form_test(voices, v, 65, 64);
    usDBG("COMPLETE\n");
    usDBG("NOISE TESTING ");
    wave_form_test(voices, v, 129, 128);
    usDBG("COMPLETE\n");

    usDBG("PULSE WIDTH SWEEP TESTING ");
    test_operation((voices[v] + sid_registers[NOTEHI]), 40);  /* NOTEHI */
    pulse_sweep_test(voices, v, 65, 64);
    usDBG("COMPLETE\n");
  }
}

/**
 * @brief Sweep the filter cutoff through low/band/high pass for a given waveform on every voice
 *
 * Sets volume to full, then for filter frequencies 15/30/45 and each of the
 * low/band/high pass modes, sweeps FC_HI from 0 to 255 with an 8ms delay
 * per step on each of the 3 voices.
 *
 * @note aborts early and resets the SID registers if running_tests goes false
 *
 * @param uint8_t addr, SID base address offset
 * @param uint8_t voices[3]
 * @param int wf, waveform index: 0 triangle, 1 sawtooth, 2 pulse, 3 noise
 */
void filter_tests(uint8_t addr, uint8_t voices[3], int wf)
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  usDBG("FILTER TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 1 ? "SAWTOOTH" : wf == 2 ? "PULSE" : "NOISE"));
  for (int fq = 15; fq <= 45; fq += 15) {
    if (!running_tests) { reset_sid_registers(); return; };
    usDBG("HIGH FILTER FREQUENCY = %d\n", fq);
    test_operation(sid_registers[RESFLT], 87);  /* RESFLT 5*16+1+2+4 */
    for (int flt = 1; flt <= 3; flt++) {
      if (!running_tests) { reset_sid_registers(); return; };
      usDBG("%s PASS\n", (flt == 1 ? "LOW" : flt == 2 ? "BAND" : "HIGH"));
      test_operation(sid_registers[MODVOL], (flt == 1 ? 31 : flt == 2 ? 47 : 79));  /* MODVOL */
      for (int v = 0; v < 3; v++) {
        if (!running_tests) { reset_sid_registers(); return; };
        usDBG("VOICE %d TESTING ", (v + 1));
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
        usDBG("COMPLETE\n");
      }
    }
  }
}

/**
 * @brief Run attack/decay/sustain/release scenarios for a given waveform on every voice
 *
 * Sets volume to full, then for each voice runs three scenarios in
 * sequence: a full ADSR (attack/decay then a held sustain then release), a
 * sustain/release only test, and an attack/decay only test, with sleep_ms()
 * delays between phases to let the envelope be heard.
 *
 * @note aborts early and resets the SID registers if running_tests goes false
 *
 * @param uint8_t addr, SID base address offset
 * @param uint8_t voices[3]
 * @param int wf, waveform index: 0 triangle, 1 sawtooth, 2 pulse, 3 noise
 */
void envelope_tests(uint8_t addr, uint8_t voices[3], int wf)
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  usDBG("A D S R TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 1 ? "SAWTOOTH" : wf == 2 ? "PULSE" : "NOISE"));
  for (int v = 0; v < 3; v++) {
    if (!running_tests) { reset_sid_registers(); return; };
    usDBG("VOICE %d A D S R TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 170);  /* ATTDEC 10*16+10 */
    test_operation((voices[v] + sid_registers[SUSREL]), 58);   /* SUSREL 3*16+10 */
    test_operation((voices[v] + sid_registers[NOTEHI]), 40);   /* NOTEHI */
    test_operation((voices[v] + sid_registers[PWMHI]), 8);     /* PWMHI */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(3000);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    usDBG("RELEASE ");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(1000);
    usDBG("COMPLETE\n");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);

    usDBG("VOICE %d S R TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 0);    /* ATTDEC */
    test_operation((voices[v] + sid_registers[SUSREL]), 250);  /* SUSREL 15*16+10 */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    usDBG("RELEASE ");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
    usDBG("COMPLETE\n");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);

    usDBG("VOICE %d A D TESTING ", (v + 1));
    test_operation((voices[v] + sid_registers[ATTDEC]), 170);  /* ATTDEC 10*16+10 */
    test_operation((voices[v] + sid_registers[SUSREL]), 0);    /* SUSREL */
    test_operation((voices[v] + sid_registers[CONTR]), (waveforms[wf] + 1));  /* CONTR */
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(2000);
    test_operation((voices[v] + sid_registers[CONTR]), waveforms[wf]);  /* CONTR */
    usDBG("RELEASE ");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
    usDBG("COMPLETE\n");
    if (!running_tests) { reset_sid_registers(); return; };
    sleep_ms(600);
  }
}

/**
 * @brief Run ring modulation tests pairing each voice with its modulator voice
 *
 * Sets volume to full, then for each voice pairs it with the SID's fixed
 * ring-mod partner voice, enables the given waveform with ring modulation
 * (CONTR + 3), and sweeps NOTEHI from 0 to 255 with a 24ms delay per step.
 *
 * @note aborts early and resets the SID registers if running_tests goes false
 *
 * @param uint8_t addr, SID base address offset
 * @param uint8_t voices[3]
 * @param int wf, waveform index: 0 triangle, 1 sawtooth, 2 pulse, 3 noise
 */
void modulation_tests(uint8_t addr, uint8_t voices[3], int wf)
{
  if (!running_tests) { reset_sid_registers(); return; };
  test_operation((sid_registers[MODVOL] + addr), 0x0F);  /* Volume to full */
  usDBG("RING MODULATION TESTS %s\n", (wf == 0 ? "TRIANGLE" : wf == 1 ? "SAWTOOTH" : wf == 2 ? "PULSE" : "NOISE"));
  for (int v = 0; v < 3; v++) {
    if (!running_tests) { reset_sid_registers(); return; };
    int v2 = (v == 1 ? 0 : v == 2 ? 1 : 2);
    usDBG("VOICE %d WITH VOICE %d ", (v + 1), (v2 + 1));
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
    usDBG("COMPLETE\n");
  }
}

/**
 * @brief Entry point for the interactive SID test suite
 *
 * Clears the target SID's registers, computes its 3 voice register
 * addresses from `sidno`, sets volume to full, and dispatches to the
 * requested test routine(s): '1' runs the full suite (waveforms, filter,
 * envelope, modulation), '2' runs test_all_waveforms(), '3' runs
 * filter_tests(), '4' runs envelope_tests(), '5' runs modulation_tests().
 * For '3'/'4'/'5', `wf` selects a single waveform ('T'/'S'/'P', anything
 * else meaning noise) or 'A' to run all four in turn.
 *
 * @note aborts early and resets the SID registers if running_tests goes false
 *
 * @param int sidno, SID index used to compute the register base address
 * @param char test, which test suite to run: '1'..'5'
 * @param char wf, waveform selector for tests '3'/'4'/'5'
 */
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
