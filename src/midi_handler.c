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

#include <math.h>

#include <globals.h>
#include <usbsid.h>
#include <config.h>
#include <bus.h>
#include <logging.h>
#include <sid.h>
#include <sid_defs.h>
#include <midi.h>
#include <midi_defs.h>
#include <midi_handler.h>


// #define NOTEHI(n) ((musical_scale_values[(n - ((n >= 0x81) ? 0x80 : 0))] & 0xFF00) >> 8)
// #define NOTELO(n) (musical_scale_values[(n - ((n >= 0x81) ? 0x80 : 0))] & 0xFF)

/* Initialise variables */
typedef struct Voice_m {
   uint8_t note_index;
   int8_t keyno;
   bool poly_on;
} Voice_m;
typedef struct SID_m {
  Voice_m v[3];
  uint8_t active_voice;
  uint8_t previous_voice;
  bool polyfonic;
  bool auto_gate; /* Set to true on midi processor init */
  /* Polyfonic templates — kept in sync with CC changes */
  uint8_t tmpl_contr;   /* control register (waveform bits, excl. gate) */
  uint8_t tmpl_attdec;
  uint8_t tmpl_susrel;
  uint8_t tmpl_pwmlo;
  uint8_t tmpl_pwmhi;
} SID_m;
typedef struct Instr_m {
  SID_m sid[4];
  int keys_pressed;
  uint8_t active_sid;
  uint8_t previous_sid;
  bool copy_voice;
  bool copy_sid;
  bool link_voice;
  bool link_sid;
} Instr_m;
typedef struct Midi_m {
  Instr_m channel[10]; // let's start testing with 10 channels
} Midi_m;

volatile static int current_channel = 0; /* NOTE: This will probably not work in the long run */
volatile static Midi_m msid = {0};

static midi_ccvalues CC;
static void (*cc_func_ptr_array[128])(uint8_t a, uint8_t b);


/* Internal helper functions */
static void midi_bus_operation_(uint8_t a, uint8_t b)
{
  cycled_write_operation(a, b, 6);  /* 6 cycles constant for LDA 2 and STA 4 */
  return;
}
static void midi_bus_operation(uint8_t a, uint8_t b)
{
  cycled_write_operation(a, b, 0);  /* 0 cycles constant for fast writing */
  return;
}

/**
 * @brief Retrieve the base address of the active sid
 *
 * @return uint8_t the base address
 */
static uint8_t sidbase(void)
{
  return cfg.sidaddr[cfg.ids[msid.channel[current_channel].active_sid]];
}

/**
 * @brief Retrieve the voice address addition
 *
 * @return uint8_t the base voice
 */
static uint8_t voicebase(void)
{
  return (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice*7);
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

static inline void handle_bit(uint8_t reg, int bit, uint8_t cc_value)
{
  switch (cc_value) {
    case 127: /* On */
      set_bit(reg,bit);
      break;
    case 0:   /* Off */
      unset_bit(reg,bit);
      break;
    default:  /* All other values toggle :) */
      toggle_bit(reg,bit);
      break;
  }
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

static void copy_sid_to_sid(void)
{
  uint8_t sid_base = sidbase();
  uint8_t prev_sid_base = cfg.sidaddr[cfg.ids[msid.channel[current_channel].previous_sid]];
  for (uint8_t i = MIN_VAL; i < MAX_REGS; i++) {
    uint8_t reg = (sid_base+i);
    uint8_t val = sid_memory[(prev_sid_base+i)];
    midi_bus_operation(reg,val);
  }
  return;
}

static void copy_voice_to_voice(void)
{
  uint8_t voice_base = (sidbase()+voicebase());
  uint8_t prev_voice_base = (sidbase()+msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].previous_voice*7);
  for (uint8_t i = MIN_VAL; i < VOICE_REGS; i++) {
    uint8_t reg = (voice_base+i);
    uint8_t val = sid_memory[(prev_voice_base+i)];
    midi_bus_operation(reg,val);
  }
  return;
}

static void copy_voice_poly(void)
{
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].previous_voice = 0;
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = 1;
  copy_voice_to_voice();
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].previous_voice = 1;
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = 2;
  copy_voice_to_voice();
  return;
}

static void select_sid(uint8_t cc, uint8_t onoff)
{
  (void)onoff;
  msid.channel[current_channel].previous_sid = msid.channel[current_channel].active_sid;
  if (cc == CC.CC_SID1) {
    msid.channel[current_channel].active_sid = 0;
  }
  if (cc == CC.CC_SID2) {
    msid.channel[current_channel].active_sid = 1;
  }
  if (cc == CC.CC_SID3) {
    msid.channel[current_channel].active_sid = 2;
  }
  if (cc == CC.CC_SID4) {
    msid.channel[current_channel].active_sid = 3;
  }
  if (msid.channel[current_channel].active_sid > cfg.numsids) {
    msid.channel[current_channel].active_sid = cfg.numsids; /* Fallback to max sid */
  }

  if (msid.channel[current_channel].copy_sid) {
    copy_sid_to_sid();
    msid.channel[current_channel].copy_sid = false;
  }

  usMVCE("SID SELECT: %d [%d]\n", msid.channel[current_channel].active_sid, cc);
  return;
}

static void select_voice(uint8_t cc, uint8_t onoff)
{
  (void)onoff;
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].previous_voice = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice;
  if (cc == CC.CC_VCE1) {
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = 0;
  }
  else
  if (cc == CC.CC_VCE2) {
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = 1;
  }
  else
  if (cc == CC.CC_VCE3) {
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = 2;
  }

  if (msid.channel[current_channel].copy_voice) {
    copy_voice_to_voice();
    msid.channel[current_channel].copy_voice = false;
  }

  usMVCE("VOICE SELECT: %d [%d]\n", msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice, cc);
  return;
}

static void set_handler(uint8_t cc, uint8_t value)
{
  if (cc == CC.CC_GTEN) {
    usNFO("[CC_GTEN] SID%d From %d ", msid.channel[current_channel].active_sid, msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate);
    if (value == 127) /* On */
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate = true;
    else if (value == 0) /* Off */
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate = false;
    else /* Toggle */
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate = !msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate;
    usNFO("To %d\n", msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate);
    return;
  }
  else
  if (cc == CC.CC_POLY) {
    usNFO("[CC_POLY] From %d ", msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic);
    if (value == 127) /* On */
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic = true;
    else if (value == 0) /* Off */
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic = false;
    else /* Toggle */
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic = !msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic;
    usNFO("To %d\n", msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic);
    if __us_unlikely (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic) {
      copy_voice_poly();
    }
    return;
  }
  else
  if (cc == CC.CC_CVCE) {
    usNFO("[CC_CVCE] From %d ", msid.channel[current_channel].copy_voice);
    if (value == 127) /* On */
      msid.channel[current_channel].copy_voice = true;
    else if (value == 0) /* Off */
      msid.channel[current_channel].copy_voice = false;
    else /* Toggle */
      msid.channel[current_channel].copy_voice = !msid.channel[current_channel].copy_voice;
    usNFO("To %d\n", msid.channel[current_channel].copy_voice);
    return;
  }
  else
  if (cc == CC.CC_CSID) {
    usNFO("[CC_CSID] From %d ", msid.channel[current_channel].copy_sid);
    if (value == 127) /* On */
      msid.channel[current_channel].copy_sid = true;
    else if (value == 0) /* Off */
      msid.channel[current_channel].copy_sid = false;
    else /* Toggle */
      msid.channel[current_channel].copy_sid = !msid.channel[current_channel].copy_sid;
    usNFO("To %d\n", msid.channel[current_channel].copy_sid);
    return;
  }
  else
  if (cc == CC.CC_LVCE) {
    usNFO("[CC_LVCE] From %d ", msid.channel[current_channel].link_voice);
    if (value == 127) /* On */
      msid.channel[current_channel].link_voice = true;
    else if (value == 0) /* Off */
      msid.channel[current_channel].link_voice = false;
    else /* Toggle */
      msid.channel[current_channel].link_voice = !msid.channel[current_channel].link_voice;
    usNFO("To %d\n", msid.channel[current_channel].link_voice);
    return;
  }
  else
  if (cc == CC.CC_LSID) {
    usNFO("[CC_LSID] From %d ", msid.channel[current_channel].link_sid);
    if (value == 127) /* On */
      msid.channel[current_channel].link_sid = true;
    else if (value == 0) /* Off */
      msid.channel[current_channel].link_sid = false;
    else /* Toggle */
      msid.channel[current_channel].link_sid = !msid.channel[current_channel].link_sid;
    usNFO("To %d\n", msid.channel[current_channel].link_sid);
    return;
  }

  return;
}

static void set_filtercutoff(uint8_t cc, uint8_t value)
{
  uint16_t cutoff = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, CUTOFF_MAX);
  uint8_t Clo = (uint8_t)(cutoff & F_MASK_LO);
  uint8_t Chi = (uint8_t)((cutoff & F_MASK_HI) >> SHIFT_3);

  sid_memory[(sidbase()+FC_HI)] = Chi;
  midi_bus_operation((sidbase()+FC_HI),sid_memory[(sidbase()+FC_HI)]);
  sid_memory[(sidbase()+FC_LO)] = Clo;
  midi_bus_operation((sidbase()+FC_LO),sid_memory[(sidbase()+FC_LO)]);

  return;
}

static void set_resfilter(uint8_t cc, uint8_t value)
{
  if (cc == CC.CC_RES) { /* Write resonance */
    uint8_t mapped_val = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
    set_nibble(RESFLT,mapped_val,R_NIBBLE);
  }
  else if (cc == CC.CC_FLTE) {
    handle_bit(RESFLT,BIT_3,value);
  }
  else if (cc == CC.CC_FLT1) {
    handle_bit(RESFLT,BIT_2,value);
  }
  else if (cc == CC.CC_FLT2) {
    handle_bit(RESFLT,BIT_1,value);
  }
  else if (cc == CC.CC_FLT3) {
    handle_bit(RESFLT,BIT_0,value);
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
  else if (cc == CC.CC_3OFF) {
    handle_bit(MODVOL,BIT_7,value);
  }
  else if (cc == CC.CC_HPF) {
    handle_bit(MODVOL,BIT_6,value);
  }
  else if (cc == CC.CC_BPF) {
    handle_bit(MODVOL,BIT_5,value);
  }
  else if (cc == CC.CC_LPF) {
    handle_bit(MODVOL,BIT_4,value);
  }
  midi_bus_operation((sidbase()+MODVOL),sid_memory[(sidbase()+MODVOL)]);
  /* usMVCE("MODVOL: %02x AFTER\n",sid_memory[(sidbase()+MODVOL)]); */

  return;
}

static void set_controlreg(uint8_t cc, uint8_t value)
{
  usMVCE("(voicebase()+CONTR): %02x BEFORE\n",sid_memory[(sidbase()+(voicebase()+CONTR))]);
  if (cc == CC.CC_NOIS) {
    handle_bit((voicebase()+CONTR),BIT_7,value);
  }
  else if (cc == CC.CC_PULS) {
    handle_bit((voicebase()+CONTR),BIT_6,value);
  }
  else if (cc == CC.CC_SAWT) {
    handle_bit((voicebase()+CONTR),BIT_5,value);
  }
  else if (cc == CC.CC_TRIA) {
    handle_bit((voicebase()+CONTR),BIT_4,value);
  }
  else if (cc == CC.CC_TEST) {
    handle_bit((voicebase()+CONTR),BIT_3,value);
  }
  else if (cc == CC.CC_RMOD) {
    handle_bit((voicebase()+CONTR),BIT_2,value);
  }
  else if (cc == CC.CC_SYNC) {
    handle_bit((voicebase()+CONTR),BIT_1,value);
  }
  else if (cc == CC.CC_GATE) {
    handle_bit((voicebase()+CONTR),BIT_0,value);
  }

  midi_bus_operation((sidbase()+(voicebase()+CONTR)),sid_memory[(sidbase()+(voicebase()+CONTR))]);
  usMVCE("(voicebase()+CONTR): %02x AFTER\n",sid_memory[(sidbase()+(voicebase()+CONTR))]);

  if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic) {
    /* Preserve control register in template without gate bit */
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_contr =
      sid_memory[(sidbase()+(voicebase()+CONTR))] & ~BIT_0;
    /* Copy settings to other voices immediately */
    uint8_t cur = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice;
    for (uint8_t i = 0; i < MAX_VOICES; i++) {
      if (i == cur) continue;
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = i;
      /* Preserve per-voice gate state */
      uint8_t gate = sid_memory[(sidbase()+(voicebase()+CONTR))] & BIT_0;
      sid_memory[(sidbase()+(voicebase()+CONTR))] =
        (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_contr | gate);
      midi_bus_operation((sidbase()+(voicebase()+CONTR)),
                         sid_memory[(sidbase()+(voicebase()+CONTR))]);
    }
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = cur;
  }
}

static void set_adsr(uint8_t cc, uint8_t value)
{
  int nib = (((cc == CC.CC_ATT) || (cc == CC.CC_SUS)) ? R_NIBBLE : L_NIBBLE); /* Nibble to preserve */
  uint8_t mapped_val = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, NIBBLE_MAX);
  uint8_t reg = (((cc == CC.CC_ATT) || (cc == CC.CC_DEC)) ? ATTDEC : SUSREL);
  set_nibble((voicebase()+reg),mapped_val,nib);
  midi_bus_operation((sidbase()+(voicebase()+reg)),sid_memory[(sidbase()+(voicebase()+reg))]);

  if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic) {
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_attdec =
      sid_memory[(sidbase()+(voicebase()+ATTDEC))];
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_susrel =
      sid_memory[(sidbase()+(voicebase()+SUSREL))];
    /* Copy settings to other voices immediately */
    uint8_t cur = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice;
    for (uint8_t i = 0; i < MAX_VOICES; i++) {
      if (i == cur) continue;
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = i;
      sid_memory[(sidbase()+(voicebase()+ATTDEC))] =
        msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_attdec;
      sid_memory[(sidbase()+(voicebase()+SUSREL))] =
        msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_susrel;
      midi_bus_operation((sidbase()+(voicebase()+ATTDEC)),
                         sid_memory[(sidbase()+(voicebase()+ATTDEC))]);
      midi_bus_operation((sidbase()+(voicebase()+SUSREL)),
                         sid_memory[(sidbase()+(voicebase()+SUSREL))]);
    }
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = cur;
  }

  return;
}

static void set_voicepwm(uint8_t cc, uint8_t value)
{
  uint16_t pwm = MAP(value, MIN_VAL, MIDI_CC_MAX, MIN_VAL, TRIPLE_NIBBLE);
  uint8_t Plo  = (uint8_t)(pwm & BYTE);
  uint8_t Phi  = (uint8_t)((pwm & NIBBLE_3) >> SHIFT_8);
  /* usNFO("[PWM] %04x %02x %02x\n", pwm, Phi, Plo); */

  sid_memory[(sidbase()+(voicebase()+PWMLO))] = Plo;
  midi_bus_operation((sidbase()+(voicebase()+PWMLO)),sid_memory[(sidbase()+(voicebase()+PWMLO))]);
  sid_memory[(sidbase()+(voicebase()+PWMHI))] = Phi;
  midi_bus_operation((sidbase()+(voicebase()+PWMHI)),sid_memory[(sidbase()+(voicebase()+PWMHI))]);

  if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic) {
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_pwmlo =
      sid_memory[(sidbase()+(voicebase()+PWMLO))];
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_pwmhi =
      sid_memory[(sidbase()+(voicebase()+PWMHI))];
    /* Copy to other voices immediately */
    uint8_t cur = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice;
    for (uint8_t i = 0; i < MAX_VOICES; i++) {
      if (i == cur) continue;
      msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = i;
      sid_memory[(sidbase()+(voicebase()+PWMLO))] =
        msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_pwmlo;
      sid_memory[(sidbase()+(voicebase()+PWMHI))] =
        msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].tmpl_pwmhi;
      midi_bus_operation((sidbase()+(voicebase()+PWMLO)),
                         sid_memory[(sidbase()+(voicebase()+PWMLO))]);
      midi_bus_operation((sidbase()+(voicebase()+PWMHI)),
                         sid_memory[(sidbase()+(voicebase()+PWMHI))]);
    }
    msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = cur;
  }

  return;
}

static void set_notefrequency(uint8_t cc, uint8_t value)
{
  uint16_t frequency = MAP(value, MIN_VAL, MIDI_CC_MAX, musical_scale_values[MIN_VAL], musical_scale_values[SCALE_MAX]);
  uint8_t Flo = (uint8_t)(frequency & VOICE_FREQLO);
  uint8_t Fhi = (uint8_t)((frequency >> SHIFT_8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> SHIFT_8));

  sid_memory[(sidbase()+(voicebase()+NOTEHI))] = Fhi;
  midi_bus_operation((sidbase()+(voicebase()+NOTEHI)),sid_memory[(sidbase()+(voicebase()+NOTEHI))]);
  sid_memory[(sidbase()+(voicebase()+NOTELO))] = Flo;
  midi_bus_operation((sidbase()+(voicebase()+NOTELO)),sid_memory[(sidbase()+(voicebase()+NOTELO))]);
  return;
}

static void pitch_notefrequency(uint8_t lo, uint8_t hi)
{
  uint16_t pitch_note = (uint16_t)((hi<<SHIFT_8)|lo);
  uint8_t step = (pitch_note <= (uint16_t)MIDI_HALF)
    ? MAP(pitch_note, MIN_VAL, MIDI_HALF, PITCH_MAX, MIN_VAL)
    : MAP(pitch_note, MIDI_HALF, MIDI_MAX, MIN_VAL, PITCH_MAX);
  uint8_t curr_index = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice].note_index;
  int8_t new_index = ((pitch_note < (uint16_t)MIDI_HALF) ? (curr_index-step) : (curr_index+step));
  new_index = (new_index < 0 ? 0 : new_index > SCALE_MAX ? SCALE_MAX : new_index);

  uint16_t frequency =  musical_scale_values[new_index];

  uint8_t Flo = (uint8_t)(frequency & VOICE_FREQLO);
  uint8_t Fhi = (uint8_t)((frequency >> SHIFT_8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> SHIFT_8));

  sid_memory[(sidbase()+(voicebase()+NOTEHI))] = Fhi;
  midi_bus_operation((sidbase()+(voicebase()+NOTEHI)),sid_memory[(sidbase()+(voicebase()+NOTEHI))]);
  sid_memory[(sidbase()+(voicebase()+NOTELO))] = Flo;
  midi_bus_operation((sidbase()+(voicebase()+NOTELO)),sid_memory[(sidbase()+(voicebase()+NOTELO))]);
  return;
}

static uint8_t lowest_key_pressed(void)
{
  int lowest = 255;
  int key = 0;
  for (int v = 0; v < 3; v++) {
    if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[v].poly_on
      && msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[v].keyno != 0
      && msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[v].keyno < lowest) {
      lowest = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[v].keyno;
      key = v;
    }
  }
  if (lowest == 255) key = 0;
  return key;
}

static uint8_t find_free_poly_voice(void) // TODO: If all taken, then lowest keyno must be used!!
{
  for (uint8_t i = MIN_VAL; i < MAX_VOICES; i++) {
    if (!msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[i].poly_on) {
      return i;
    }
  }
  /* Else return lowest key pressed */
  return lowest_key_pressed();
}

static uint8_t find_prev_poly_voice(uint8_t note_index)
{
  /* Match by note_index (primary, normally works) */
  for (uint8_t i = MIN_VAL; i < MAX_VOICES; i++) {
    if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[i].poly_on &&
        msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[i].note_index == note_index) {
        return i;
    }
  }
  /* Fallback: highest keyno among active voices (most recently pressed) */
  int8_t max_keyno = -1;
  uint8_t candidate = 0;
  for (uint8_t i = 0; i < MAX_VOICES; i++) {
    if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[i].poly_on &&
        msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[i].keyno > max_keyno) {
      max_keyno = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[i].keyno;
      candidate = i;
    }
  }
  return candidate;
}

static void note_on_poly(uint8_t note_index, uint8_t velocity)
{
  (void)velocity;

  /* Clamp to valid scale range */
  if (note_index > SCALE_MAX) note_index = SCALE_MAX;

  msid.channel[current_channel].keys_pressed++;

  /* Select target voice BEFORE writing anything */
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = find_free_poly_voice();

  /* Track note on this voice */
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice].note_index = note_index;

  /* Write frequency */
  uint16_t frequency = musical_scale_values[note_index];
  uint8_t Flo = (uint8_t)(frequency & VOICE_FREQLO);
  uint8_t Fhi = (uint8_t)((frequency >> SHIFT_8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> SHIFT_8));
  sid_memory[(sidbase()+(voicebase()+NOTEHI))] = Fhi;
  midi_bus_operation((sidbase()+(voicebase()+NOTEHI)), sid_memory[(sidbase()+(voicebase()+NOTEHI))]);
  sid_memory[(sidbase()+(voicebase()+NOTELO))] = Flo;
  midi_bus_operation((sidbase()+(voicebase()+NOTELO)), sid_memory[(sidbase()+(voicebase()+NOTELO))]);

  /* Stamp current timbre template onto this voice */
  volatile SID_m *s = &msid.channel[current_channel].sid[msid.channel[current_channel].active_sid];
  sid_memory[(sidbase()+(voicebase()+CONTR))]  = s->tmpl_contr & ~BIT_0; /* gate off initially */
  midi_bus_operation((sidbase()+(voicebase()+CONTR)), sid_memory[(sidbase()+(voicebase()+CONTR))]);
  sid_memory[(sidbase()+(voicebase()+ATTDEC))] = s->tmpl_attdec;
  midi_bus_operation((sidbase()+(voicebase()+ATTDEC)), sid_memory[(sidbase()+(voicebase()+ATTDEC))]);
  sid_memory[(sidbase()+(voicebase()+SUSREL))] = s->tmpl_susrel;
  midi_bus_operation((sidbase()+(voicebase()+SUSREL)), sid_memory[(sidbase()+(voicebase()+SUSREL))]);
  sid_memory[(sidbase()+(voicebase()+PWMLO))]  = s->tmpl_pwmlo;
  midi_bus_operation((sidbase()+(voicebase()+PWMLO)), sid_memory[(sidbase()+(voicebase()+PWMLO))]);
  sid_memory[(sidbase()+(voicebase()+PWMHI))]  = s->tmpl_pwmhi;
  midi_bus_operation((sidbase()+(voicebase()+PWMHI)), sid_memory[(sidbase()+(voicebase()+PWMHI))]);

  if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate) {
    set_bit((voicebase()+CONTR), BIT_0);
    midi_bus_operation((sidbase()+(voicebase()+CONTR)), sid_memory[(sidbase()+(voicebase()+CONTR))]);
  }

  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice].poly_on = true;
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice].keyno   = msid.channel[current_channel].keys_pressed;
  /* Do NOT advance active_voice here — next note_on calls find_free_poly_voice() */
}

static void note_off_poly(uint8_t note_index, uint8_t velocity)
{
  (void)velocity;

  if (msid.channel[current_channel].keys_pressed > 0) msid.channel[current_channel].keys_pressed--;

  uint8_t v = find_prev_poly_voice(note_index);

  /* Gate off the specific voice without permanently changing active_voice */
  uint8_t saved = msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice;
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = v;
  if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate) {
    unset_bit((voicebase()+CONTR), BIT_0);
    midi_bus_operation((sidbase()+(voicebase()+CONTR)), sid_memory[(sidbase()+(voicebase()+CONTR))]);
  }
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice = saved;

  /* Free the voice slot */
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[v].poly_on    = false;
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[v].keyno       = -1;
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[v].note_index  = 0;
  /* active_voice is restored — next note_on will find a free slot */
}

static void note_on(uint8_t note_index, uint8_t velocity)
{
  (void)velocity; /* TODO: Implement into ADSR? */

  msid.channel[current_channel].keys_pressed++;
  /* usNFO("[KEYS] ON  %d\n",msid.channel[current_channel].keys_pressed); */

  /* Write frequency */
  msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].v[msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].active_voice].note_index = note_index;
  uint16_t frequency = musical_scale_values[note_index]; // ISSUE: Possible issue here with indexes that are out of range!?
  uint8_t Flo = (frequency & VOICE_FREQLO);
  uint8_t Fhi = ((frequency >> SHIFT_8) >= VOICE_FREQHI ? VOICE_FREQHI : (frequency >> SHIFT_8));
  sid_memory[(sidbase()+(voicebase()+NOTEHI))] = Fhi;
  midi_bus_operation((sidbase()+(voicebase()+NOTEHI)),sid_memory[(sidbase()+(voicebase()+NOTEHI))]);
  sid_memory[(sidbase()+(voicebase()+NOTELO))] = Flo;
  midi_bus_operation((sidbase()+(voicebase()+NOTELO)),sid_memory[(sidbase()+(voicebase()+NOTELO))]);

  /* Write voice settings before gate from memory to this voice */
  midi_bus_operation((sidbase()+(voicebase()+CONTR)),  sid_memory[(sidbase()+(voicebase()+CONTR))]);
  midi_bus_operation((sidbase()+(voicebase()+ATTDEC)), sid_memory[(sidbase()+(voicebase()+ATTDEC))]);
  midi_bus_operation((sidbase()+(voicebase()+SUSREL)), sid_memory[(sidbase()+(voicebase()+SUSREL))]);
  midi_bus_operation((sidbase()+(voicebase()+PWMLO)),  sid_memory[(sidbase()+(voicebase()+PWMLO))]);
  midi_bus_operation((sidbase()+(voicebase()+PWMHI)),  sid_memory[(sidbase()+(voicebase()+PWMHI))]);

  if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate) {
    set_bit((voicebase()+CONTR),BIT_0); /* Set get bit on */
    midi_bus_operation((sidbase()+(voicebase()+CONTR)),sid_memory[(sidbase()+(voicebase()+CONTR))]);
  }

  return;
}

static void note_off(uint8_t note_index, uint8_t velocity)
{
  (void)velocity; /* TODO: Implement into ADSR? */

  if (msid.channel[current_channel].keys_pressed > 0) msid.channel[current_channel].keys_pressed--;
  /* usNFO("[KEYS] OFF %d\n",msid.channel[current_channel].keys_pressed); */

  if (msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].auto_gate) {// && (msid.channel[current_channel].keys_pressed == 0)) { /* NOTE: KEY_PRESSED wait till 0 breaks polyfonic */
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
  cc_func_ptr_array[CC.CC_POLY] = set_handler;
  cc_func_ptr_array[CC.CC_CVCE] = set_handler;
  cc_func_ptr_array[CC.CC_CSID] = set_handler;
  cc_func_ptr_array[CC.CC_LVCE] = set_handler;
  cc_func_ptr_array[CC.CC_LSID] = set_handler;

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
  cc_func_ptr_array[CC.CC_NOTE] = set_notefrequency;
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
  usNFO("[MIDI] Handler init\n");

  /* NOTE: Temporary - always init defaults */
  memcpy(&CC, &midi_ccvalues_defaults, sizeof(midi_ccvalues)); /* TODO: midi.c and midi_handler.c must always use the same mapping */

  /* Initialize defaults on boot */
  msid.channel[current_channel].keys_pressed = 0;
  msid.channel[current_channel].active_sid = 0;
  msid.channel[current_channel].previous_sid = 0;
  msid.channel[current_channel].copy_voice = false;
  msid.channel[current_channel].copy_sid = false;
  msid.channel[current_channel].link_voice = false;
  msid.channel[current_channel].link_sid = false;

  /* Explicitly initialise auto_gate - static initialiser is unreliable with `-O3` */
  for (int i = 0; i < 4; i++) {
    msid.channel[current_channel].sid[i].active_voice = 0;
    msid.channel[current_channel].sid[i].previous_voice = 0;
    msid.channel[current_channel].sid[i].polyfonic = false;
    msid.channel[current_channel].sid[i].auto_gate = true;
    msid.channel[current_channel].sid[i].v[0].poly_on = false;
    msid.channel[current_channel].sid[i].v[1].poly_on = false;
    msid.channel[current_channel].sid[i].v[2].poly_on = false;
    msid.channel[current_channel].sid[i].v[0].keyno = -1;
    msid.channel[current_channel].sid[i].v[1].keyno = -1;
    msid.channel[current_channel].sid[i].v[2].keyno = -1;
    msid.channel[current_channel].sid[i].v[0].note_index = 0;
    msid.channel[current_channel].sid[i].v[1].note_index = 0;
    msid.channel[current_channel].sid[i].v[2].note_index = 0;
    /* Initialise all template fields to default state */
    msid.channel[current_channel].sid[i].tmpl_contr  = 0x00;
    msid.channel[current_channel].sid[i].tmpl_attdec = 0x00;
    msid.channel[current_channel].sid[i].tmpl_susrel = 0x00;
    msid.channel[current_channel].sid[i].tmpl_pwmlo  = 0x00;
    msid.channel[current_channel].sid[i].tmpl_pwmhi  = 0x00;
  }

  midi_cc_init();

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
  /* TODO: Add optional globel poly setting next to the already available per SID setting */
  /* TODO: Channel should not be deciding factor, program 0 should be polyfonic, program 1+ configurable */
  usMVCE("");
  for (int i = 0; i < size; i++) {
    usMDAT("[%d]%02x",i,buffer[i]);
  }
  usMDAT("\n");

  current_channel = (buffer[0] & 0xF);

  switch(buffer[0]) {
    //case 0xB0 ... 0xB3:  /* Channel 0~3 Control/Mode Change */
      //msid.channel[current_channel].active_sid = (buffer[0]&0x3);
    case 0xB0 ... 0xBF:  /* Channel 4~16 Control/Mode Change */
      handle_control_change((buffer+1), (size-1));
      break;
    //case 0x80 ... 0x83:  /* Channel 0~3 Note Off */
      //msid.channel[current_channel].active_sid = (buffer[0]&0x3);
    case 0x80 ... 0x8F:  /* Channel 4~16 Note Off */
      if (!msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic) {
        note_off(buffer[1],buffer[2]);
      } else  {
        note_off_poly(buffer[1],buffer[2]);
      }
      break;
    //case 0x90 ... 0x93:  /* Channel 0~3 Note On */
      //msid.channel[current_channel].active_sid = (buffer[0]&0x3);
    case 0x90 ... 0x9F:  /* Channel 4~16 Note On */
      if (!msid.channel[current_channel].sid[msid.channel[current_channel].active_sid].polyfonic) {
        note_on(buffer[1],buffer[2]);
      } else {
        note_on_poly(buffer[1],buffer[2]);
      }
      break;
    //case 0xE0 ... 0xE3:  /* Channel 0~3 Pitch Bend Change */
      //msid.channel[current_channel].active_sid = (buffer[0]&0x3);
    case 0xE0 ... 0xEF:  /* Channel 4~16 Pitch Bend Change */
      pitch_notefrequency(buffer[1],buffer[2]);
      break;
    case 0xC0 ... 0xCF:  /* Channel 0~16 Program change */
    case 0xD0 ... 0xDF:  /* Channel 0~16 Pressure (After-touch) */
    default:
      break;
  }

  return;
}
