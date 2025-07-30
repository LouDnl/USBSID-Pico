// cRSID lightweight (integer-only) RealSID library (with API-calls) by Hermit (Mihaly Horvath), Year 2024
// License: WTF - do what the fuck you want with the code, but please mention me as the original author

#include <stdlib.h>
#include <stdint.h>
#include "libcRSID.h"
#include "C64/C64.c"

extern unsigned long long main_cpu_clk, prev_cpu_clk;

cRSID_C64instance *cRSID_init(void)
{
    main_cpu_clk = prev_cpu_clk = 0;

    static cRSID_C64instance *C64 = &cRSID_C64;

    C64->HighQualitySID   = 1;
    C64->Stereo           = 0;
    C64->SelectedSIDmodel = 0;
    C64->PlaybackSpeed    = 1; // default model and mode selections

    C64->MainVolume=204; //255;
    C64->CLImode=1; C64->AutoExit=C64->BuiltInMusic=0;
    C64->AutoAdvance=C64->FadeOut=1; C64->FadeLevel=0xF;

    C64 = cRSID_createC64(C64);

    return C64;
}


void cRSID_initSIDtune (cRSID_C64instance* C64, cRSID_SIDheader* SIDheader, char subtune) { //subtune: 1..255
 static const unsigned char PowersOf2[] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
 unsigned int InitTimeout=10000000; //allowed instructions, value should be selected to allow for long-running memory-copiers in init-routines (e.g. Synth Sample)

 C64->PlaytimeExpired=0;

 if (subtune==0) subtune = 1;
 else if (subtune > SIDheader->SubtuneAmount) subtune = SIDheader->SubtuneAmount;
 C64->SubTune = subtune; C64->SecondCnt = C64->PlayTime = C64->Paused = 0;

 cRSID_setC64(C64); cRSID_initC64(C64); //cRSID_writeMemC64(C64,0xD418,0xF); //set C64 hardware and init (reset) it

 //determine init-address:
 C64->InitAddress = ((SIDheader->InitAddressH)<<8) + (SIDheader->InitAddressL); //get info from BASIC-startupcode for some tunes
 if (C64->RAMbank[1] == 0x37) { //are there SIDs with routine under IO area? some PSIDs don't set bank-registers themselves
  if ( (0xA000 <= C64->InitAddress && C64->InitAddress < 0xC000)
       || (C64->LoadAddress < 0xC000 && C64->EndAddress >= 0xA000) ) C64->RAMbank[1] = 0x36;
  else if (C64->InitAddress >= 0xE000 || C64->EndAddress >=0xE000) C64->RAMbank[1] = 0x35;
 }
 cRSID_initCPU( &C64->CPU, C64->InitAddress ); //prepare init-routine call
 C64->CPU.A = subtune - 1;

 if (!C64->RealSIDmode) {
  //call init-routine:
  for (InitTimeout=10000000; InitTimeout>0; InitTimeout--) { if ( cRSID_emulateCPU()>=0xFE ) break; } //give error when timed out?
 }

 //determine timing-source, if CIA, replace FrameCycles previouisly set to VIC-timing
 if (subtune>32) C64->TimerSource = C64->SIDheader->SubtuneTimeSources[0] & 0x80; //subtunes above 32 should use subtune32's timing
 else C64->TimerSource = C64->SIDheader->SubtuneTimeSources[(32-subtune)>>3] & PowersOf2[(subtune-1)&7];
 if (C64->TimerSource || C64->IObankWR[0xDC05]!=0x40 || C64->IObankWR[0xDC04]!=0x24) { //CIA1-timing (probably multispeed tune)
  C64->FrameCycles = ( ( C64->IObankWR[0xDC04] + (C64->IObankWR[0xDC05]<<8) ) ); //<< 4) / C64->ClockRatio;
  C64->TimerSource = 1; //if init-routine changed DC04 or DC05, assume CIA-timing
 }

 //determine playaddress:
 C64->PlayAddress = (SIDheader->PlayAddressH<<8) + SIDheader->PlayAddressL;
 if (C64->PlayAddress) { //normal play-address called with JSR
  if (C64->RAMbank[1] == 0x37) { //are there SIDs with routine under IO area?
   if (0xA000 <= C64->PlayAddress && C64->PlayAddress < 0xC000) C64->RAMbank[1] = 0x36;
  }
  else if (C64->PlayAddress >= 0xE000) C64->RAMbank[1] = 0x35; //player under KERNAL (e.g. Crystal Kingdom Dizzy)
 }
 else { //IRQ-playaddress for multispeed-tunes set by init-routine (some tunes turn off KERNAL ROM but doesn't set IRQ-vector!)
  C64->PlayAddress = (C64->RAMbank[1] & 3) < 2 ? cRSID_readMemC64(C64,0xFFFE) + (cRSID_readMemC64(C64,0xFFFF)<<8) //for PSID
                                                 : cRSID_readMemC64(C64,0x314) + (cRSID_readMemC64(C64,0x315)<<8);
  if (C64->PlayAddress==0) { //if 0, still try with RSID-mode fallback
   cRSID_initCPU( &C64->CPU, C64->PlayAddress ); //point CPU to play-routine
   C64->Finished=1; C64->Returned=1; return;
  }
 }

 if (!C64->RealSIDmode) {  //prepare (PSID) play-routine playback:
  cRSID_initCPU( &C64->CPU, C64->PlayAddress ); //point CPU to play-routine
  C64->FrameCycleCnt=0; C64->Finished=1; C64->SampleCycleCnt=0; //C64->CIAisSet=0;
 }
 else { C64->Finished=0; C64->Returned=0; }

}

cRSID_SIDheader* cRSID_processSIDfile(cRSID_C64instance* C64, unsigned char* filedata, int filesize) {
 static int i;
 static unsigned short SIDdataOffset;
 static cRSID_SIDheader *SIDheader;
 static const char MagicStringPSID[]="PSID";
 //static const char MagicStringRSID[]="RSID";

 C64->SIDheader = SIDheader = (cRSID_SIDheader*) filedata;

 for (i=0x0000; i < 0xA000; ++i) C64->RAMbank[i]=0; //fresh start (maybe some bugged SIDs want 0 at certain RAM-locations)
 for (i=0xC000; i < 0xD000; ++i) C64->RAMbank[i]=0;

 if ( SIDheader->MagicString[0] != 'P' && SIDheader->MagicString[0] != 'R' ) return NULL;
 for (i=1; i < (int)(sizeof(MagicStringPSID)-1); ++i) { if (SIDheader->MagicString[i] != MagicStringPSID[i]) return NULL; }
 C64->RealSIDmode = ( SIDheader->MagicString[0] == 'R' );

 /* TODO: Fix 'n finish or remove in unnescessary */
 if(C64->RealSIDmode) { writeFuncPtr = &cycled_write_operation; }
 else if(!C64->RealSIDmode) { writeFuncPtr = &cycled_write_operation; }

 if (SIDheader->LoadAddressH==0 && SIDheader->LoadAddressL==0) { //load-address taken from first 2 bytes of the C64 PRG
  C64->LoadAddress = (filedata[SIDheader->HeaderSize+1]<<8) + (filedata[SIDheader->HeaderSize+0]);
  SIDdataOffset = SIDheader->HeaderSize+2;
 }
 else { //load-adress taken from SID-header
  C64->LoadAddress = (SIDheader->LoadAddressH<<8) + (SIDheader->LoadAddressL);
  SIDdataOffset = SIDheader->HeaderSize;
 }

 for (i=SIDdataOffset; i<filesize; ++i) C64->RAMbank [ C64->LoadAddress + (i-SIDdataOffset) ] = filedata[i];

 i = C64->LoadAddress + (filesize-SIDdataOffset);
 C64->EndAddress = (i<0x10000) ? i : 0xFFFF;

 C64->PSIDdigiMode = ( !C64->RealSIDmode && (SIDheader->ModelFormatStandard & 2) );

 return C64->SIDheader;
}
