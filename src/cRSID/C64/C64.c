
// cRSID lightweight (integer-only) RealSID library (with API-calls) by Hermit (Mihaly Horvath), Year 2024
// License: WTF - do what the fuck you want with the code, but please mention me as the original author
// C64 emulation (SID-playback related)

#include <stdio.h>
#include "pico/time.h"
#include "../libcRSID.h"

#include "MEM.c"
#include "CPU.c"
#include "CIA.c"
#include "VIC.c"
#include "SID.c"

extern void (*writeFuncPtr)(uint8_t,uint8_t,uint16_t);
extern void USBSID_FrameHandler(cRSID_C64instance *C64);
extern unsigned long long main_cpu_clk, prev_cpu_clk, flush_cpu_clk, vic_cpu_clk, usid_main_clk;
unsigned long long emulation_clk;
extern unsigned int us_writecycles;

cRSID_C64instance* cRSID_createC64 (cRSID_C64instance* C64) { //init a basic PAL C64 instance
  if (C64->ram_init_done == 0) {
    /* ISSUE: THIS TAKES UP TOO MUCH SPACE FOR PICO1 */
    C64->RAMbank = malloc(0x10100);
    C64->IObankWR = malloc(0x10100);
    C64->IObankRD = malloc(0x10100);
    C64->ROMbanks = malloc(0x10100);
    C64->ram_init_done = 1;
    /* TODO: FIX free() */
  }
  enum C64clocks { C64_PAL_CPUCLK=985248, DEFAULT_SAMPLERATE=44100 };
  unsigned short samplerate;
  C64->SampleRate = samplerate = DEFAULT_SAMPLERATE;
  C64->SampleClockRatio = (C64_PAL_CPUCLK<<4)/samplerate; //shifting (multiplication) enhances SampleClockRatio precision
  C64->Attenuation = 26; C64->SIDchipCount=1;
  C64->CPU.C64 = C64;
  cRSID_createSIDchip ( C64, &C64->SID[0], 8580, CRSID_CHANNEL_BOTH, 0xD400 ); //default C64 setup with only 1 SID and 2 CIAs and 1 VIC
  cRSID_createCIAchip ( C64, &C64->CIA[0], 0xDC00 );
  cRSID_createCIAchip ( C64, &C64->CIA[1], 0xDD00 );
  cRSID_createVICchip ( C64, &C64->VIC, 0xD000 );
  //if(C64->RealSIDmode) {
  cRSID_setROMcontent ( C64 );
  //}
  cRSID_initC64(C64);
  return C64;
}

void cRSID_setC64 (cRSID_C64instance* C64) {   //set hardware-parameters (Models, SIDs) for playback of loaded SID-tune
  enum C64clocks { C64_PAL_CPUCLK=985248, C64_NTSC_CPUCLK=1022727 };
  enum C64scanlines { C64_PAL_SCANLINES = 312, C64_NTSC_SCANLINES = 263 };
  enum C64scanlineCycles { C64_PAL_SCANLINE_CYCLES = 63, C64_NTSC_SCANLINE_CYCLES = 65 };
  //enum C64framerates { PAL_FRAMERATE = 50, NTSC_FRAMERATE = 60 }; //Hz

  static const unsigned int CPUspeeds[] = { C64_NTSC_CPUCLK, C64_PAL_CPUCLK };
  static const unsigned short ScanLines[] = { C64_NTSC_SCANLINES, C64_PAL_SCANLINES };
  static const unsigned char ScanLineCycles[] = { C64_NTSC_SCANLINE_CYCLES, C64_PAL_SCANLINE_CYCLES };
  //unsigned char FrameRates[] = { NTSC_FRAMERATE, PAL_FRAMERATE };

  static const short Attenuations[]={0,26,43,137,200}; //increase for 2SID (to 43) and 3SID (to 137)
  short SIDmodel; char SIDchannel;


  C64->VideoStandard = ( (C64->SIDheader->ModelFormatStandard & 0x0C) >> 2 ) != 2;
  if (C64->SampleRate==0) C64->SampleRate = 44100;
  C64->CPUfrequency = CPUspeeds[C64->VideoStandard];
  C64->SampleClockRatio = ( C64->CPUfrequency << 4 ) / C64->SampleRate; //shifting (multiplication) enhances SampleClockRatio precision

  C64->VIC.RasterLines = ScanLines[C64->VideoStandard];
  C64->VIC.RasterRowCycles = ScanLineCycles[C64->VideoStandard];
  C64->FrameCycles = C64->VIC.RasterLines * C64->VIC.RasterRowCycles; ///C64->SampleRate / PAL_FRAMERATE; //1x speed tune with VIC Vertical-blank timing

  C64->PrevRasterLine=-1; //so if $d012 is set once only don't disturb FrameCycleCnt

  SIDmodel = (C64->SIDheader->ModelFormatStandard&0x30) >= 0x20 ? 8580:6581;
  C64->SID[0].ChipModel = C64->SelectedSIDmodel? C64->SelectedSIDmodel : SIDmodel;


  if (C64->SIDheader->Version != CRSID_FILEVERSION_WEBSID) {

    C64->SID[0].Channel = CRSID_CHANNEL_LEFT;

    SIDmodel = C64->SIDheader->ModelFormatStandard & 0xC0;
    if (SIDmodel) SIDmodel = (SIDmodel >= 0x80) ? 8580:6581; else SIDmodel = C64->SID[0].ChipModel;
    if (C64->SelectedSIDmodel) SIDmodel = C64->SelectedSIDmodel;
    cRSID_createSIDchip ( C64, &C64->SID[1], SIDmodel, CRSID_CHANNEL_RIGHT, 0xD000 + C64->SIDheader->SID2baseAddress*16 );

    SIDmodel = C64->SIDheader->ModelFormatStandardH & 0x03;
    if (SIDmodel) SIDmodel = (SIDmodel >= 0x02) ? 8580:6581; else SIDmodel = C64->SID[0].ChipModel;
    if (C64->SelectedSIDmodel) SIDmodel = C64->SelectedSIDmodel;
    cRSID_createSIDchip ( C64, &C64->SID[2], SIDmodel, CRSID_CHANNEL_BOTH, 0xD000 + C64->SIDheader->SID3baseAddress*16 );

    C64->SID[3].BaseAddress=0x0000; C64->SID[3].BasePtr = NULL; //ensure disabling SID4 in non-WebSID format

  }
  else {

    C64->SID[0].Channel = (C64->SIDheader->ModelFormatStandardH & 0x40)? CRSID_CHANNEL_RIGHT:CRSID_CHANNEL_LEFT;
    if (C64->SIDheader->ModelFormatStandardH & 0x80) C64->SID[0].Channel = CRSID_CHANNEL_BOTH; //my own proposal for 'middle' channel

    SIDmodel = C64->SIDheader->SID2flagsL & 0x30;
    SIDchannel = (C64->SIDheader->SID2flagsL & 0x40) ? CRSID_CHANNEL_RIGHT:CRSID_CHANNEL_LEFT;
    if (C64->SIDheader->SID2flagsL & 0x80) SIDchannel = CRSID_CHANNEL_BOTH;
    if (SIDmodel) SIDmodel = (SIDmodel >= 0x20) ? 8580:6581; else SIDmodel = C64->SID[0].ChipModel;
    if (C64->SelectedSIDmodel) SIDmodel = C64->SelectedSIDmodel;
    cRSID_createSIDchip ( C64, &C64->SID[1], SIDmodel, SIDchannel, 0xD000 + C64->SIDheader->SID2baseAddress*16 );

    SIDmodel = C64->SIDheader->SID3flagsL & 0x30;
    SIDchannel = (C64->SIDheader->SID3flagsL & 0x40) ? CRSID_CHANNEL_RIGHT:CRSID_CHANNEL_LEFT;
    if (C64->SIDheader->SID3flagsL & 0x80) SIDchannel = CRSID_CHANNEL_BOTH;
    if (SIDmodel) SIDmodel = (SIDmodel >= 0x20) ? 8580:6581; else SIDmodel = C64->SID[0].ChipModel;
    if (C64->SelectedSIDmodel) SIDmodel = C64->SelectedSIDmodel;
    cRSID_createSIDchip ( C64, &C64->SID[2], SIDmodel, SIDchannel, 0xD000 + C64->SIDheader->SID3flagsH*16 );

    SIDmodel = C64->SIDheader->SID4flagsL & 0x30;
    SIDchannel = (C64->SIDheader->SID4flagsL & 0x40) ? CRSID_CHANNEL_RIGHT:CRSID_CHANNEL_LEFT;
    if (C64->SIDheader->SID4flagsL & 0x80) SIDchannel = CRSID_CHANNEL_BOTH;
    if (SIDmodel) SIDmodel = (SIDmodel >= 0x20) ? 8580:6581; else SIDmodel = C64->SID[0].ChipModel;
    if (C64->SelectedSIDmodel) SIDmodel = C64->SelectedSIDmodel;
    cRSID_createSIDchip ( C64, &C64->SID[3], SIDmodel, SIDchannel, 0xD000 + C64->SIDheader->SID4baseAddress*16 );

  }


  C64->SIDchipCount = 1 + (C64->SID[1].BaseAddress > 0) + (C64->SID[2].BaseAddress > 0) + (C64->SID[3].BaseAddress > 0);
  if (C64->SIDchipCount == 1) C64->SID[0].Channel = CRSID_CHANNEL_BOTH;
  C64->Attenuation = Attenuations[C64->SIDchipCount];

}

void cRSID_initC64 (cRSID_C64instance* C64) { //C64 Reset
  cRSID_initSIDchip( &C64->SID[0] );
  cRSID_initCIAchip( &C64->CIA[0] );
  cRSID_initCIAchip( &C64->CIA[1] );
  cRSID_initMem(C64);
  cRSID_initCPU( &C64->CPU, (cRSID_readMemC64(C64,0xFFFD)<<8) + cRSID_readMemC64(C64,0xFFFC) );
  C64->IRQ = C64->NMI = 0;
  C64->SampleCycleCnt = C64->OverSampleCycleCnt = 0;

  main_cpu_clk = prev_cpu_clk = flush_cpu_clk = vic_cpu_clk = usid_main_clk = 0;
  us_writecycles = 0;
  emulation_clk = 0;
}

unsigned long long cRSID_emulateC64 (cRSID_C64instance *C64) {
  static unsigned char InstructionCycles;
  emulation_clk = main_cpu_clk;
  //Cycle-based part of emulations:
  while (main_cpu_clk - emulation_clk == 0) {
    if (!C64->RealSIDmode) {
      if (C64->FrameCycleCnt >= C64->FrameCycles) {
        USBSID_FrameHandler(C64);
        C64->FrameCycleCnt -= C64->FrameCycles;
        if (C64->Finished) { //some tunes (e.g. Barbarian, A-Maze-Ing) doesn't always finish in 1 frame
          cRSID_initCPU(&C64->CPU, C64->PlayAddress); //(PSID docs say bank-register should always be set for each call's region)
          C64->Finished = 0; //C64->SampleCycleCnt=0; //PSID workaround for some tunes (e.g. Galdrumway):
          if (C64->TimerSource == 0) {
            C64->IObankRD[0xD019] = 0x81; //always simulate to player-calls that VIC-IRQ happened
          } else {
            C64->IObankRD[0xDC0D] = 0x83; //always simulate to player-calls that CIA TIMERA/TIMERB-IRQ happened
          }
        }
      }
      if (C64->Finished==0) {
        if ((InstructionCycles = cRSID_emulateCPU()) >= 0xFE) {
          InstructionCycles=6;
          C64->Finished=1;
        }
      } else {
        InstructionCycles=7; //idle between player-calls
      }
      C64->FrameCycleCnt += InstructionCycles;
      C64->IObankRD[0xDC04] += InstructionCycles; //very simple CIA1 TimerA simulation for PSID (e.g. Delta-Mix_E-Load_loader)

    } else { //RealSID emulations:

      if (cRSID_handleCPUinterrupts(&C64->CPU)) {
        C64->Finished = 0;
        InstructionCycles = 7;
      } else if (C64->Finished == 0) {
        if ((InstructionCycles = cRSID_emulateCPU()) >= 0xFE) {
          InstructionCycles = 6;
          C64->Finished = 1;
        }
      } else {
        InstructionCycles = 7; //idle between IRQ-calls
      }
      C64->IRQ = C64->NMI = 0; //prepare for collecting IRQ sources
      C64->IRQ |= cRSID_emulateCIA (&C64->CIA[0], InstructionCycles);
      C64->NMI |= cRSID_emulateCIA (&C64->CIA[1], InstructionCycles);
      C64->IRQ |= cRSID_emulateVIC (&C64->VIC, InstructionCycles);
    }
  }

  if (!C64->RealSIDmode) { //some PSID tunes use CIA TOD-clock (e.g. Kawasaki Synthesizer Demo)
    --C64->TenthSecondCnt;
    if (C64->TenthSecondCnt <= 0) {
      C64->TenthSecondCnt = C64->SampleRate / 10;
      ++(C64->IObankRD[0xDC08]);
      if(C64->IObankRD[0xDC08]>=10) {
        C64->IObankRD[0xDC08]=0; ++(C64->IObankRD[0xDC09]);
      }
    }
  }

  if (C64->SecondCnt < C64->SampleRate) {
    ++C64->SecondCnt;
  } else {
    C64->SecondCnt = 0;
    if(C64->PlayTime < 3600) {
      ++C64->PlayTime;
    }
  }
  prev_cpu_clk = main_cpu_clk;
  return (main_cpu_clk - emulation_clk);
}
