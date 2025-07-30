// cRSID lightweight (integer-only) RealSID library (with API-calls) by Hermit (Mihaly Horvath), Year 2024
// License: WTF - do what the fuck you want with the code, but please mention me as the original author
// VIC-II emulation
#include "../libcRSID.h"

extern void USBSID_FrameHandler(cRSID_C64instance *C64);
extern unsigned long long main_cpu_clk, prev_cpu_clk, flush_cpu_clk;
extern unsigned int us_writecycles;
unsigned long long vic_cpu_clk = 0;

void cRSID_createVICchip (cRSID_C64instance* C64, cRSID_VICinstance* VIC, unsigned short baseaddress) {
 VIC->C64 = C64;
 VIC->ChipModel = 0;
 VIC->BaseAddress = baseaddress;
 VIC->BasePtrWR = &C64->IObankWR[baseaddress]; VIC->BasePtrRD = &C64->IObankRD[baseaddress];
 cRSID_initVICchip (VIC);
}


void cRSID_initVICchip (cRSID_VICinstance* VIC) {
 unsigned char i;
 for (i=0; i<0x3F; ++i) VIC->BasePtrWR[i] = VIC->BasePtrRD[i] = 0x00;
 VIC->RowCycleCnt=0;
}


static inline char cRSID_emulateVIC (cRSID_VICinstance* VIC, char cycles) {

 unsigned short RasterRow;

 enum VICregisters { CONTROL = 0x11, RASTERROWL = 0x12, SPRITE_ENABLE=0x15, INTERRUPT = 0x19, INTERRUPT_ENABLE = 0x1A };

 enum ControlBitVal { RASTERROWMSB = 0x80, DISPLAY_ENABLE = 0x10, ROWS = 0x08, YSCROLL_MASK = 0x07 };

 enum InterruptBitVal { VIC_IRQ = 0x80, RASTERROW_MATCH_IRQ = 0x01 };


 VIC->RowCycleCnt += cycles;
 if (VIC->RowCycleCnt >= VIC->RasterRowCycles) {
  VIC->RowCycleCnt -= VIC->RasterRowCycles;

  RasterRow = ( (VIC->BasePtrRD[CONTROL]&RASTERROWMSB) << 1 ) + VIC->BasePtrRD[RASTERROWL];
  ++RasterRow; if (RasterRow >= VIC->RasterLines) RasterRow = 0;
  VIC->BasePtrRD[CONTROL] = ( VIC->BasePtrRD[CONTROL] & ~RASTERROWMSB ) | ((RasterRow&0x100)>>1);
  VIC->BasePtrRD[RASTERROWL] = RasterRow & 0xFF;

  if (VIC->BasePtrWR[INTERRUPT_ENABLE] & RASTERROW_MATCH_IRQ) {
   if ( RasterRow == ( (VIC->BasePtrWR[CONTROL]&RASTERROWMSB) << 1 ) + VIC->BasePtrWR[RASTERROWL] ) {
    VIC->BasePtrRD[INTERRUPT] |= VIC_IRQ | RASTERROW_MATCH_IRQ;
   }
  }

 }

 vic_cpu_clk = main_cpu_clk;
 return VIC->BasePtrRD[INTERRUPT] & VIC_IRQ;
}

static inline void cRSID_acknowledgeVICrasterIRQ (cRSID_VICinstance* VIC) {
 enum VICregisters { INTERRUPT = 0x19 };
 enum InterruptBitVal { VIC_IRQ = 0x80, RASTERROW_MATCH_IRQ = 0x01 };

 /* ISSUE: ? cycles and write cycles are always the exact amount  */
 // printf("VIC IRQ @ %llu cycles, wr_cycles %u\n", (main_cpu_clk - vic_cpu_clk), us_writecycles);

 us_writecycles = 0; /* Works for Antarctic Burial */

 //An 1 is to be written into the IRQ-flag (bit0) of $d019 to clear it and deassert IRQ signal
 //if (VIC->BasePtrWR[INTERRUPT] & RASTERROW_MATCH_IRQ) { //acknowledge raster-interrupt by writing to $d019 bit0?
 //But oftentimes INC/LSR/etc. RMW commands are used to acknowledge VIC IRQ, they work on real
 //CPU because it writes the unmodified original value itself to memory before writing the modified there
  VIC->BasePtrWR[INTERRUPT] &= ~RASTERROW_MATCH_IRQ; //prepare for next acknowledge-detection
  VIC->BasePtrRD[INTERRUPT] &= ~(VIC_IRQ | RASTERROW_MATCH_IRQ); //remove IRQ flag and state
  vic_cpu_clk = main_cpu_clk;
}
