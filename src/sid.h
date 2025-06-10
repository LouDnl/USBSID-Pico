/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid.h
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

#ifndef _USBSID_SID_H_
#define _USBSID_SID_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif


/* Default includes */
#include <stdint.h>
#include "pico/util/queue.h"

/* Queues */
typedef struct {
  // void sid_test(int sidno, char test, char wf)
  void (*func)(int,char,char);
  int s;
  char t;
  char wf;
} sidtest_queue_entry_t;


/* Rasterrates (cycles) in microseconds
 * Source: https://www.c64-wiki.com/wiki/raster_time
 *
 * PAL: 1 horizontal raster line takes 63 cycles
 * or 504 pixels including side borders
 * whole screen consists of 312 horizontal lines
 * for a frame including upper and lower borders
 * 63 * 312 CPU cycles is 19656 for a complete
 * frame update @ 985248 Hertz
 * 985248 / 19656 = approx 50.12 Hz frame rate
 *
 * NTSC: 1 horizontal raster line takes 65 cycles
 * whole screen consists of 263 rasters per frame
 * 65 * 263 CPU cycles is 17096 for a complete
 * frame update @ 985248 Hertz
 * 1022727 / 17096 = approx 59.83 Hz frame rate
 *
 */

/* Clock cycles per second
 * Clock speed: 0.985 MHz (PAL) or 1.023 MHz (NTSC)
 *
 * For some reason both 1022727 and 1022730 are
 * mentioned as NTSC clock cycles per second
 * Going for the rates specified by Vice it should
 * be 1022730, except in the link about raster time
 * on c64 wiki it's 1022727.
 * I chose to implement both, let's see how this
 * works out
 *
 * https://sourceforge.net/p/vice-emu/code/HEAD/tree/trunk/vice/src/c64/c64.h
 */

/* Clock cycles per second */
typedef enum {
  CLOCK_DEFAULT  = 1000000,  /* @125MHz clock ~ 125000000 / 62,5f = 2000000 / 2 = 1000000 == 1.00MHz @ 125MHz and 1.06MHz @ 133MHz */
  CLOCK_PAL      = 985248,   /* @125MHz clock ~ 125000000 / 63,435804995f = 1970496,000009025 / 2 = 985248,000004513 */
  CLOCK_NTSC     = 1022727,  /* @125MHz clock ~ 125000000 / 61,111127407f = 2045454,000013782 / 2 = 1022727,000006891 */
  CLOCK_DREAN    = 1023440,  /* @125MHz clock ~ 125000000 / 61,068553115f = 2046879,999999489 / 2 = 1023439,999999745 */
  CLOCK_NTSC2    = 1022730,  /* @125MHz clock ~ 125000000 / 61,111127407f = 2045454,000013782 / 2 = 1022727,000006891 */
} clock_rates;

/* Clock speed translation */
enum config_clockrates
{
  DEFAULT = CLOCK_DEFAULT,  /* 0 */
  PAL     = CLOCK_PAL,      /* 1 */
  NTSC    = CLOCK_NTSC,     /* 2 */
  DREAN   = CLOCK_DREAN,    /* 3 */
  NTSC2   = CLOCK_NTSC2,    /* 4 */
};

/* Clock speed array */
static const enum config_clockrates clockrates[] = { DEFAULT, PAL, NTSC, DREAN, NTSC2 };

/* Hertz ~ refresh rates */
typedef enum {
  HZ_DEFAULT = 20000,  /* 50Hz ~ 20000 == 20us */
  HZ_50      = 19950,  /* 50Hz ~ 20000 == 20us / 50.125Hz ~ 19.950124688279us exact */
  HZ_60      = 16715,  /* 60Hz ~ 16667 == 16.67us / 59.826Hz ~ 16.715140574332 exact */
} hertz_values;

enum refresh_rates
{
  HZDFLT = HZ_DEFAULT,
  HZ50   = HZ_50,
  HZ60   = HZ_60,
};

static const enum refresh_rates refreshrates[]  = { HZDFLT, HZ50, HZ60, HZ60, HZ60 };

/* Rasterrates (cycles) in microseconds */
enum raster_rates
{
  R_DEFAULT = 20000,  /* 20us  ~ fallback */
  R_PAL     = 19656,  /* PAL:  63 cycles * 312 lines = 19656 cycles per frame update @  985248 Hz = 50.12 Hz frame rate */
  R_NTSC    = 17096,  /* NTSC: 65 cycles * 263 lines = 17096 cycles per frame update @ 1022727 Hz = 59.83 Hz Hz frame rate */
};

static const enum raster_rates rasterrates[]  = { R_DEFAULT, R_PAL, R_NTSC, R_NTSC, R_NTSC };

/* Masks 🤿 we use */
typedef enum {
  BYTE = 0xFF,
  L_NIBBLE = 0xF0,
  R_NIBBLE = 0xF,
  TRIPLE_NIBBLE = 0xFFF,
  NIBBLE_3 = 0xF00,
  H_BYTE = 0xFF00,
  F_MASK_HI = 0x7F8,
  F_MASK_LO = 0x7,
  MIDI_CC_MAX = 0x7F,
  VOICE_FREQLO = BYTE,
  VOICE_FREQHI = BYTE,
  SHIFT_3 = 3,
  SHIFT_8 = 8,
  ZERO  = 0,
  ONE   = 1,
  BIT_0 = 0x1,
  BIT_1 = 0x2,
  BIT_2 = 0x4,
  BIT_3 = 0x8,
  BIT_4 = 0x10,
  BIT_5 = 0x20,
  BIT_6 = 0x40,
  BIT_7 = 0x80
} register_masks;

/*
 * Voice registers
 *
 * 0: Freq low
 * 1: Freq high
 * 2: Pulsewidth low
 * 3: Pulsewidth high
 * 4: Control registers
 * 5: Attack/Decay
 * 6: Sustain/Release
 */
static const uint8_t sid_registers[29] =
{
  /* Voice 1 */
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  /* Voice 2 */
  0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
  /* Voice 3 */
  0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
  /* Filter low */
  0x15,
  /* Filter high */
  0x16,
  /* Resonance & filter channels */
  0x17,
  /* Volume */
  0x18,
  /* PotX/PotY */
  0x19, 0x1A,
  /* Oscillator Voice 3 */
  0x1B,
  /* Envelope Voice 3 */
  0x1C
};

enum {
  NO_SID_REGISTERS = 25, /* 29 - 4 non writeable */
  NO_VOICES_PER_SID = 3,
};

enum {
  /* sid_states ~  SID registers */
  /* TODO: THESE NEED TO GO BUT ARE USED IN sid_routines.c  */
  NOTELO =  0,
  NOTEHI =  1,
  PWMLO  =  2,
  PWMHI  =  3,
  CONTR  =  4,
  ATTDEC =  5,
  SUSREL =  6,
  /* VOICE 1 */
  V1_NOTELO =  0,
  V1_NOTEHI =  1,
  V1_PWMLO  =  2,
  V1_PWMHI  =  3,
  V1_CONTR  =  4,
  V1_ATTDEC =  5,
  V1_SUSREL =  6,
  /* VOICE 2 = + 7 */
  V2_NOTELO =  7,
  V2_NOTEHI =  8,
  V2_PWMLO  =  9,
  V2_PWMHI  = 10,
  V2_CONTR  = 11,
  V2_ATTDEC = 12,
  V2_SUSREL = 13,
  /* VOICE 3 = + 14 */
  V3_NOTELO = 14,
  V3_NOTEHI = 15,
  V3_PWMLO  = 16,
  V3_PWMHI  = 17,
  V3_CONTR  = 18,
  V3_ATTDEC = 19,
  V3_SUSREL = 20,
  /* Control */
  FC_LO     = 21,
  FC_HI     = 22,
  RESFLT    = 23,
  MODVOL    = 24,
  POTX      = 25, /* readonly */
  POTY      = 26, /* readonly */
  V3_OSC    = 27, /* readonly */
  V3_ENV    = 28  /* readonly */
};

/* HEX DEC Attack(Time/Cycle) Decay(Time/Cycle) */
static const uint8_t attack_decay[] =
{
  0x0,  // 0     2 mS    6 mS
  0x1,  // 1     8 mS   24 mS
  0x2,  // 2    16 mS   48 mS
  0x3,  // 3    24 mS   72 mS
  0x4,  // 4    38 mS  114 mS
  0x5,  // 5    56 mS  168 mS
  0x6,  // 6    68 mS  204 mS
  0x7,  // 7    80 mS  240 mS
  0x8,  // 8   100 mS  300 mS
  0x9,  // 9   250 mS  750 mS
  0xA,  // 10  500 mS    1.5 S
  0xB,  // 11  800 mS    2.4 S
  0xC,  // 12    1 S     3 S
  0xD,  // 13    3 S     9 S
  0xE,  // 14    5 S    15 S
  0xF,  // 15    8 S    24 S
};

/*
 * Osc Fn (Hex), No, Musical Note, Freq (Hz), Osc Fn (Decimal)
 */
static const uint32_t musical_scale_values[] =
{
  0x0112,   // 0    C0    16.35   274
  0x0123,   // 1    C0$   17.32   291
  0x0134,   // 2    D0    18.35   308
  0x0146,   // 3    D0$   19.44   326
  0x015A,   // 4    E0    20.60   346
  0x016E,   // 5    F0    21.83   366
  0x0184,   // 6    F0$   23.12   388
  0x018B,   // 7    G0    24.50   411
  0x01B3,   // 8    G0$   25.96   435
  0x01CD,   // 9    A0    27.50   461
  0x01E9,   // 10   A0$   29.14   489
  0x0206,   // 11   B0    30.87   518
  0x0225,   // 12   C1    32.70   549
  0x0245,   // 13   C1$   34.65   581
  0x0268,   // 14   D1    36.71   616
  0x028C,   // 15   D1$   38.89   652
  0x02B3,   // 16   E1    41.20   691
  0x02DC,   // 17   F1    43.65   732
  0x0308,   // 18   F1$   46.25   776
  0x0336,   // 19   G1    49.00   822
  0x0367,   // 20   G1$   51.91   871
  0x039B,   // 21   A1    55.00   923
  0x03D2,   // 22   A1$   58.27   978
  0x040C,   // 23   B1    61.74   1036
  0x0449,   // 24   C2    65.41   1097
  0x048B,   // 25   C2$   69.30   1163
  0x04D0,   // 26   D2    73.42   1232
  0x0519,   // 27   D2$   77.78   1305
  0x0567,   // 28   E2    82.41   1383
  0x05B9,   // 29   F2    87.31   1465
  0x0610,   // 30   F2$   92.50   1552
  0x066C,   // 31   G2    98.00   1644
  0x06CE,   // 32   G2$   103.83   1742
  0x0735,   // 33   A2    110.00   1845
  0x07A3,   // 34   A2$   116.54   1955
  0x0817,   // 35   B2    123.47   2071
  0x0893,   // 36   C3    130.81   2195
  0x0915,   // 37   C3$   138.59   2325
  0x099F,   // 38   D3    146.83   2463
  0x0A32,   // 39   D3$   155.56   2610
  0x0ACD,   // 40   E3    164.81   2765
  0x0B72,   // 41   F3    174.61   2930
  0x0C20,   // 42   F3$   185.00   3104
  0x0CD8,   // 43   G3    196.00   3288
  0x0D9C,   // 44   G3$   207.65   3484
  0x0E6B,   // 45   A3    220.00   3691
  0x0F46,   // 46   A3$   233.08   3910
  0x102F,   // 47   B3    246.94   4143
  0x1125,   // 48   C4    261.63   4389
  0x122A,   // 49   C4$   277.18   4650
  0x133F,   // 50   D4    293.66   4927
  0x1464,   // 51   D4$   311.13   5220
  0x159A,   // 52   E4    329.63   5530
  0x16E3,   // 53   F4    349.23   5859
  0x183F,   // 54   F4$   370.00   6207
  0x1981,   // 55   G4    392.00   6577
  0x1B38,   // 56   G4$   415.30   6968
  0x1CD6,   // 57   A4    440.00   7382
  0x1E80,   // 58   A4$   466.16   7821
  0x205E,   // 59   B4    493.88   8286
  0x224B,   // 60   C5    523.25   8779
  0x2455,   // 61   C5$   554.37   9301
  0x267E,   // 62   D5    587.33   9854
  0x28C8,   // 63   D5$   622.25   10440
  0x2B34,   // 64   E5    659.25   11060
  0x2DC6,   // 65   F5    698.46   11718
  0x307F,   // 66   F5$   740.00   12415
  0x3361,   // 67   G5    783.99   13153
  0x366F,   // 68   G5$   830.61   13935
  0x39AC,   // 69   A5    880.00   14764
  0x3D1A,   // 70   A5$   932.33   15642
  0x40BC,   // 71   B5    987.77   16572
  0x4495,   // 72   C6    1046.50   17557
  0x48A9,   // 73   C6$   1108.73   18601
  0x4CFC,   // 74   D6    1174.66   19709
  0x518F,   // 75   D6$   1244.51   20897
  0x5669,   // 76   E6    1381.51   22121
  0x5B8C,   // 77   F6    1396.91   23436
  0x60FE,   // 78   F6$   1479.98   24830
  0x6602,   // 79   G6    1567.98   26306
  0x6CDF,   // 80   G6$   1661.22   27871
  0x7358,   // 81   A6    1760.00   29528
  0x7A34,   // 82   A6$   1864.65   31234
  0x8178,   // 83   B6    1975.53   33144
  0x892B,   // 84   C7    2093.00   35115
  0x9153,   // 85   C7$   2217.46   37203
  0x99F7,   // 86   D7    2349.32   39415
  0xA31F,   // 87   D7$   2489.01   41759
  0xACD2,   // 88   E7    2637.02   44242
  0xB719,   // 89   F7    2793.83   46873
  0xC1FC,   // 90   F7$   2959.95   49660
  0xCD85,   // 91   G7    3135.96   52613
  0xD9BD,   // 92   G7$   3322.44   55741
  0xE6B0,   // 93   A7    3520.00   59056
  0xF467,   // 94   A7$   3729.31   62567
  0x102F0,  // 95   B7    3951.06   66288
};

/* 12 musical note notations */
static const char notes[12][2] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H"};

/*  */

typedef struct SID {
 uint8_t addr;
 uint8_t registers[31];
} SID;

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_SID_H_ */
