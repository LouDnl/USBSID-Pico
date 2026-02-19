/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_handler.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
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


#include "pico/multicore.h"
#include "pico/util/queue.h"

#include <math.h>

#include "globals.h"
#include "config.h"
#include "logging.h"
#include "sid.h"
#include "midi.h"
#include "midi_ccdefaults.h"

// #define NOTEHI(n) ((musical_scale_values[(n - ((n >= 0x81) ? 0x80 : 0))] & 0xFF00) >> 8)
// #define NOTELO(n) (musical_scale_values[(n - ((n >= 0x81) ? 0x80 : 0))] & 0xFF)

/* usbsid.c */
#ifdef ONBOARD_EMULATOR
extern uint8_t *sid_memory;
#else
extern uint8_t sid_memory[];
#endif

/* config.c */
extern RuntimeCFG cfg;

/* bus.c */
extern uint8_t cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);

/* Initialize variables */
// struct MidiSID_s {
//   bool v1_active : 1;
//   bool v2_active : 1;
//   bool v3_active : 1;
//   bool active : 1;
//   bool auto_gate : 1;
// } MidiSID_t = {0};
// typedef struct MidiSID_s MidiSID;
// volatile static MidiSID msid[4];
volatile uint8_t active_sid = 0;
volatile uint8_t active_voice = 0;
volatile static bool auto_gate[4] = {0};
volatile static int keys_pressed;
volatile bool pitch_wheel = false;
extern const midi_ccvalues midi_ccvalues_defaults; /* midi.c */
midi_ccvalues CC;
void (*cc_func_ptr_array[128])(uint8_t a, uint8_t b);

/* Helper functions */
void midi_bus_operation(uint8_t a, uint8_t b)
{
  cycled_write_operation(a, b, 6);  /* 6 cycles constant for LDA 2 and STA 4 */
  return;
}

/**
 * @brief Retrieve the base address of the active sid
 *
 * @return uint8_t the base address
 */
static uint8_t sidbase(void)
{
  return cfg.sidaddr[active_sid];
}

/**
 * @brief Retrieve the voice address addition
 *
 * @return uint8_t the base voice
 */
static uint8_t voicebase(void)
{
  return (active_voice*7);
}

/**
 * @brief Toggle a single bit in sid memory at register
 *
 * @param uint8_t reg, the register to set
 * @param int bit, the bit to toggle
 */
static inline void toggle_bit(uint8_t reg, int bit)
{
  sid_memory[(sidbase()+reg)] = (
    (~(sid_memory[(sidbase()+reg)] & bit) & bit) | /* Not bit then and */
    (sid_memory[(sidbase()+reg)] & ~bit) /* Readd existing state */
  );
  return;
}

/**
 * @brief Set a single bit in sid memory at register
 *
 * @param uint8_t reg, the register to set
 * @param int bit, the bit to set
 */
static inline void set_bit(uint8_t reg, int bit)
{
  sid_memory[(sidbase()+reg)] = (
    sid_memory[(sidbase()+reg)] | bit /* Unset the bit by notting it out */
  );
  return;
}

/**
 * @brief Unset a single bit in sid memory at register
 *
 * @param uint8_t reg, the register to set
 * @param int bit, the bit to unset
 */
static inline void unset_bit(uint8_t reg, int bit)
{
  sid_memory[(sidbase()+reg)] = (
    sid_memory[(sidbase()+reg)] & ~bit /* Unset the bit by notting it out */
  );
  return;
}

/**
 * @brief Set the nibble of a sid register
 * Will shift val based on supplied nibble
 *
 * @param uint8_t reg, the register to set
 * @param uint8_t val, the value to set
 * @param int nibble, the register nibble to preserve
 */
static inline void set_nibble(uint8_t reg, uint8_t val, int nibble)
{
  uint8_t wrval = ((nibble == 0x0f) ? (val << SHIFT_4) : val);
  sid_memory[(sidbase()+reg)] = ((sid_memory[(sidbase()+reg)] & nibble) | wrval);
  /* usNFO("[NIB]%02x %02x:%02x|%02x\n",nibble,reg,val,wrval); */
  return;
}

static void select_sid(uint8_t cc, uint8_t onoff)
{
  (void)onoff;
  if (cc == CC.CC_SID1) active_sid = 0;
  if (cc == CC.CC_SID2) active_sid = 1;
  if (cc == CC.CC_SID3) active_sid = 2;
  if (cc == CC.CC_SID4) active_sid = 3;
  usMVCE("SID SELECT: %d [%d]\n", active_sid, cc);
  return;
}

static void select_voice(uint8_t cc, uint8_t onoff)
{
  (void)onoff;
  if (cc == CC.CC_VCE1) active_voice = 0;
  if (cc == CC.CC_VCE2) active_voice = 1;
  if (cc == CC.CC_VCE3) active_voice = 2;
  usMVCE("VOICE SELECT: %d [%d]\n", active_voice, cc);
  return;
}

static void set_filtercutoff(uint8_t cc, uint8_t value)
{
  uint16_t cutoff = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, CUTOFF_MAX);
  uint8_t Clo = (uint8_t)(cutoff & F_MASK_LO);
  uint8_t Chi = (uint8_t)((cutoff & F_MASK_HI) >> SHIFT_3);

  sid_memory[(sidbase()+(voicebase()+FC_HI))] = Chi;
  midi_bus_operation((sidbase()+(voicebase()+FC_HI)),sid_memory[(sidbase()+(voicebase()+FC_HI))]);
  sid_memory[(sidbase()+(voicebase()+FC_LO))] = Clo;
  midi_bus_operation((sidbase()+(voicebase()+FC_LO)),sid_memory[(sidbase()+(voicebase()+FC_LO))]);

  return;
}

static void set_resfilter(uint8_t cc, uint8_t value)
{
  if (cc == CC.CC_RES) { /* Write resonance */
    uint8_t mapped_val = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
    set_nibble(RESFLT,mapped_val,R_NIBBLE);
  }
  else
  if (cc == CC.CC_FLTE) {
    if (value) toggle_bit(RESFLT,BIT_3);
    else unset_bit(RESFLT,BIT_3);
  }
  else
  if (cc == CC.CC_FLT1) {
    if (value) toggle_bit(RESFLT,BIT_2);
    else unset_bit(RESFLT,BIT_2);
  }
  else
  if (cc == CC.CC_FLT2) {
    if (value) toggle_bit(RESFLT,BIT_1);
    else unset_bit(RESFLT,BIT_1);
  }
  else
  if (cc == CC.CC_FLT3) {
    if (value) toggle_bit(RESFLT,BIT_0);
    else unset_bit(RESFLT,BIT_0);
  }
  midi_bus_operation((sidbase()+RESFLT),sid_memory[(sidbase()+RESFLT)]);

  return;
}

static void set_modevolume(uint8_t cc, uint8_t value)
{
  /* usMVCE("MODVOL: %02x BEFORE\n",sid_memory[(sidbase()+MODVOL)]); */
  if (cc == CC.CC_VOL) { /* Write volume */
    uint8_t mapped_val = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
    set_nibble(MODVOL,mapped_val,L_NIBBLE);
  }
  else
  if (cc == CC.CC_3OFF) {
    if (value) toggle_bit(MODVOL,BIT_7);
    else unset_bit(MODVOL,BIT_7);
  }
  else
  if (cc == CC.CC_HPF) {
    if (value) toggle_bit(MODVOL,BIT_6);
    else unset_bit(MODVOL,BIT_6);
  }
  else
  if (cc == CC.CC_BPF) {
    if (value) toggle_bit(MODVOL,BIT_5);
    else unset_bit(MODVOL,BIT_5);
  }
  else
  if (cc == CC.CC_LPF) {
    if (value) toggle_bit(MODVOL,BIT_4);
    else unset_bit(MODVOL,BIT_4);
  }
  midi_bus_operation((sidbase()+MODVOL),sid_memory[(sidbase()+MODVOL)]);
  /* usMVCE("MODVOL: %02x AFTER\n",sid_memory[(sidbase()+MODVOL)]); */

  return;
}

static void set_controlreg(uint8_t cc, uint8_t value)
{
  usMVCE("(voicebase()+CONTR): %02x BEFORE\n",sid_memory[(sidbase()+(voicebase()+CONTR))]);
  if (cc == CC.CC_NOIS) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_7);
    else unset_bit((voicebase()+CONTR),BIT_7);
  }
  else
  if (cc == CC.CC_PULS) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_6);
    else unset_bit((voicebase()+CONTR),BIT_6);
  }
  else
  if (cc == CC.CC_SAWT) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_5);
    else unset_bit((voicebase()+CONTR),BIT_5);
  }
  else
  if (cc == CC.CC_TRIA) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_4);
    else unset_bit((voicebase()+CONTR),BIT_4);
  }
  else
  if (cc == CC.CC_TEST) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_3);
    else unset_bit((voicebase()+CONTR),BIT_3);
  }
  else
  if (cc == CC.CC_RMOD) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_2);
    else unset_bit((voicebase()+CONTR),BIT_2);
  }
  else
  if (cc == CC.CC_SYNC) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_1);
    else unset_bit((voicebase()+CONTR),BIT_1);
  }
  else
  if (cc == CC.CC_GATE) {
    if (value) toggle_bit((voicebase()+CONTR),BIT_0);
    else unset_bit((voicebase()+CONTR),BIT_0);
  }

  midi_bus_operation((sidbase()+(voicebase()+CONTR)),sid_memory[(sidbase()+(voicebase()+CONTR))]);
  usMVCE("(voicebase()+CONTR): %02x AFTER\n",sid_memory[(sidbase()+(voicebase()+CONTR))]);
}

static void set_adsr(uint8_t cc, uint8_t value)
{
  int nib = (((cc == CC.CC_ATT) || (cc == CC.CC_SUS)) ? R_NIBBLE : L_NIBBLE); /* Nibble to preserve */
  uint8_t mapped_val = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
  uint8_t reg = (((cc == CC.CC_ATT) || (cc == CC.CC_DEC)) ? ATTDEC : SUSREL);
  set_nibble((voicebase()+reg),mapped_val,nib);
  midi_bus_operation((sidbase()+(voicebase()+reg)),sid_memory[(sidbase()+(voicebase()+reg))]);
  return;
}

static void set_handler(uint8_t cc, uint8_t value)
{
  if (cc == CC.CC_GTEN) {
    if (value) auto_gate[active_sid] = !auto_gate[active_sid];
    else auto_gate[active_sid] = false;
  }

  return;
}

static void set_voicepwm(uint8_t cc, uint8_t value)
{
  uint16_t pwm = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, TRIPLE_NIBBLE);
  uint8_t Plo  = (uint8_t)(pwm & BYTE);
  uint8_t Phi  = (uint8_t)((pwm & NIBBLE_3) >> SHIFT_8);
  usNFO("[PWM] %04x %02x %02x\n", pwm, Phi, Plo);

  sid_memory[(sidbase()+(voicebase()+PWMLO))] = Plo;
  midi_bus_operation((sidbase()+(voicebase()+PWMLO)),sid_memory[(sidbase()+(voicebase()+PWMLO))]);
  sid_memory[(sidbase()+(voicebase()+PWMHI))] = Phi;
  midi_bus_operation((sidbase()+(voicebase()+PWMHI)),sid_memory[(sidbase()+(voicebase()+PWMHI))]);

  return;
}

static void set_notefrequency(uint8_t cc, uint8_t value)
{
  uint16_t frequency;
  if (pitch_wheel) { /* BUG: BROKEN */
    uint16_t currfreq = ((sid_memory[(sidbase()+(voicebase()+NOTEHI))] << SHIFT_8) | sid_memory[(sidbase()+(voicebase()+NOTELO))]);
    /* uint16_t mapval = ((cc << SHIFT_8) | value); */
    for (int i = 0; i < 96; i++) {
      if (musical_scale_values[i] == currfreq) {
        value = i;
        break;
      }
    }
    /* uint8_t step = MAP(mapval, MIN_VAL, MIDI_MAX, MIN_VAL, R_NIBBLE); */
    /* frequency = ((mapval < (MIDI_MAX / 2)) ? musical_scale_values[value-step] : musical_scale_values[value+step]); */
    uint8_t step = MAP(cc, MIN_VAL, MIDI_CC_MAX, MIN_VAL, R_NIBBLE);
    frequency = ((cc < 64) ? musical_scale_values[value-step] : musical_scale_values[value+step]);
    /* currfreq = (mapval < (MIDI_MAX / 2)) ? (currfreq - mapval) : (currfreq + mapval); */
    /* frequency = MAP(currfreq, MIN_VAL, MIDI_MAX, musical_scale_values[0], musical_scale_values[95]); */
  } else {
    frequency = MAP(value, MIN_VAL, MIDI_CC_MAX, musical_scale_values[0], musical_scale_values[95]);
  }
  uint8_t Flo = (uint8_t)(frequency & VOICE_FREQLO);
  uint8_t Fhi = (uint8_t)((frequency >> SHIFT_8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> SHIFT_8));
  usNFO("[NFREQ] %04x %02x %02x\n", frequency, Fhi, Flo);

  sid_memory[(sidbase()+(voicebase()+NOTEHI))] = Fhi;
  midi_bus_operation((sidbase()+(voicebase()+NOTEHI)),sid_memory[(sidbase()+(voicebase()+NOTEHI))]);
  sid_memory[(sidbase()+(voicebase()+NOTELO))] = Flo;
  midi_bus_operation((sidbase()+(voicebase()+NOTELO)),sid_memory[(sidbase()+(voicebase()+NOTELO))]);
  pitch_wheel = false;
  return;
}

static void note_on(uint8_t note_index, uint8_t velocity)
{
  (void)velocity; /* TODO: Implement into ADSR? */
  uint16_t frequency = musical_scale_values[note_index]; // ISSUE: Possible issue here with indexes that are out of range!?
  uint8_t Flo = (frequency & VOICE_FREQLO);
  uint8_t Fhi = ((frequency >> SHIFT_8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> SHIFT_8));

  sid_memory[(sidbase()+(voicebase()+NOTEHI))] = Fhi;
  midi_bus_operation((sidbase()+(voicebase()+NOTEHI)),sid_memory[(sidbase()+(voicebase()+NOTEHI))]);
  sid_memory[(sidbase()+(voicebase()+NOTELO))] = Flo;
  midi_bus_operation((sidbase()+(voicebase()+NOTELO)),sid_memory[(sidbase()+(voicebase()+NOTELO))]);

  if (auto_gate[active_sid]) {
    set_bit((voicebase()+CONTR),BIT_0); /* Set get bit on */
    midi_bus_operation((sidbase()+(voicebase()+CONTR)),sid_memory[(sidbase()+(voicebase()+CONTR))]);
  }

  return;
}

static void note_off(uint8_t note_index, uint8_t velocity)
{
  (void)velocity; /* TODO: Implement into ADSR? */
  uint16_t frequency = musical_scale_values[note_index];
  uint8_t Flo = (frequency & VOICE_FREQLO);
  uint8_t Fhi = ((frequency >> SHIFT_8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> SHIFT_8));

  sid_memory[(sidbase()+(voicebase()+NOTEHI))] = Fhi;
  midi_bus_operation((sidbase()+(voicebase()+NOTEHI)),sid_memory[(sidbase()+(voicebase()+NOTEHI))]);
  sid_memory[(sidbase()+(voicebase()+NOTELO))] = Flo;
  midi_bus_operation((sidbase()+(voicebase()+NOTELO)),sid_memory[(sidbase()+(voicebase()+NOTELO))]);

  if (auto_gate[active_sid] && (keys_pressed == 0)) { /* NOTE: TESTING KEY_PRESSED */
    unset_bit((voicebase()+CONTR),BIT_0); /* Set get bit on */
    midi_bus_operation((sidbase()+(voicebase()+CONTR)),sid_memory[(sidbase()+(voicebase()+CONTR))]);
  }

  return;
}

void midi_cc_init(void)
{
  for (int i = 0; i < 128; i++) {
    cc_func_ptr_array[i] = NULL;
  }
  /* Handler settings */
  cc_func_ptr_array[CC.CC_GTEN] = set_handler;
  /* SID selection */
  cc_func_ptr_array[CC.CC_SID1] = select_sid;
  cc_func_ptr_array[CC.CC_SID2] = select_sid;
  cc_func_ptr_array[CC.CC_SID3] = select_sid;
  cc_func_ptr_array[CC.CC_SID4] = select_sid;
  /* Voice selection */
  cc_func_ptr_array[CC.CC_VCE1] = select_voice;
  cc_func_ptr_array[CC.CC_VCE2] = select_voice;
  cc_func_ptr_array[CC.CC_VCE3] = select_voice;

  /* SID->Voice settings */
  /* cc_func_ptr_array[CC.CC_NOTE] = set_notefrequency; */
  cc_func_ptr_array[CC.CC_PWM]  = set_voicepwm;
  cc_func_ptr_array[CC.CC_NOIS] = set_controlreg;
  cc_func_ptr_array[CC.CC_PULS] = set_controlreg;
  cc_func_ptr_array[CC.CC_SAWT] = set_controlreg;
  cc_func_ptr_array[CC.CC_TRIA] = set_controlreg;
  cc_func_ptr_array[CC.CC_TEST] = set_controlreg;
  cc_func_ptr_array[CC.CC_RMOD] = set_controlreg;
  cc_func_ptr_array[CC.CC_SYNC] = set_controlreg;
  cc_func_ptr_array[CC.CC_GATE] = set_controlreg;
  cc_func_ptr_array[CC.CC_ATT]  = set_adsr;
  cc_func_ptr_array[CC.CC_DEC]  = set_adsr;
  cc_func_ptr_array[CC.CC_SUS]  = set_adsr;
  cc_func_ptr_array[CC.CC_REL]  = set_adsr;
  /* SID settings */
  cc_func_ptr_array[CC.CC_FFC]  = set_filtercutoff;
  cc_func_ptr_array[CC.CC_RES]  = set_resfilter;
  cc_func_ptr_array[CC.CC_FLT1] = set_resfilter;
  cc_func_ptr_array[CC.CC_FLT2] = set_resfilter;
  cc_func_ptr_array[CC.CC_FLT3] = set_resfilter;
  cc_func_ptr_array[CC.CC_FLTE] = set_resfilter;
  cc_func_ptr_array[CC.CC_3OFF] = set_modevolume;
  cc_func_ptr_array[CC.CC_HPF]  = set_modevolume;
  cc_func_ptr_array[CC.CC_BPF]  = set_modevolume;
  cc_func_ptr_array[CC.CC_LPF]  = set_modevolume;
  cc_func_ptr_array[CC.CC_VOL]  = set_modevolume;

  return;
}

void midi_processor_init(void)
{
  usCFG("Start Midi handler init\n");

  /* NOTE: Temporary - always init defaults */
  memcpy(&CC, &midi_ccvalues_defaults, sizeof(midi_ccvalues)); /* TODO: midi.c and midi_handler.c must always use the same mapping */

  /* Explicitly initialize auto_gate - static initializer is unreliable with `-O3` */
  for (int i = 0; i < 4; i++) {
    auto_gate[i] = true;
  }

  midi_cc_init();

  usCFG("End Midi handler init\n");

  return;
}

void handle_control_change(uint8_t *buffer, int size)
{
  if (cc_func_ptr_array[buffer[0]] != NULL) {
    cc_func_ptr_array[buffer[0]](buffer[0],buffer[1]);
  }
  return;
}

void process_midi(uint8_t *buffer, int size)
{
  usMVCE("");
  for (int i = 0; i < size; i++) {
    usMDAT("[%d]%02x",i,buffer[i]);
  }
  usMDAT("\n");

  switch(buffer[0]) {
    case 0xB0 ... 0xBF:  /* Channel 0~16 Control/Mode Change */
      handle_control_change((buffer+1), (size-1));
      break;
    case 0x80 ... 0x8F:  /* Channel 0~16 Note Off */
      if (keys_pressed > 0) keys_pressed--;
      note_off(buffer[1],buffer[2]);
      break;
    case 0x90 ... 0x9F:  /* Channel 0~16 Note On */
      keys_pressed++;
      note_on(buffer[1],buffer[2]);
      break;
    case 0xE0 ... 0xEF:  /* Channel 0~16 Pitch Bend Change */
      pitch_wheel = true;
      set_notefrequency(buffer[1],buffer[2]);
      break;
    case 0xC0 ... 0xCF:  /* Channel 0~16 Program change */
    case 0xD0 ... 0xDF:  /* Channel 0~16 Pressure (After-touch) */
    default:
      break;
  }

  return;
}
