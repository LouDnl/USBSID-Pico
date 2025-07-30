// cRSID lightweight (integer-only) RealSID library (with API-calls) by Hermit (Mihaly Horvath), Year 2024
// License: WTF - do what the fuck you want with the code, but please mention me as the original author
// cRSID SID emulation engine

void cRSID_createSIDchip (cRSID_C64instance* C64, cRSID_SIDinstance* SID, unsigned short model, char channel, unsigned short baseaddress) {
 SID->C64 = C64;
 SID->ChipModel = model; SID->Channel=channel;
 if( baseaddress>=0xD400 && (baseaddress<0xD800 || (0xDE00<=baseaddress && baseaddress<=0xDFE0)) ) { //check valid address, avoid Color-RAM
  SID->BaseAddress = baseaddress; SID->BasePtr = &C64->IObankWR[baseaddress];
 }
 else { SID->BaseAddress=0x0000; SID->BasePtr = NULL; }
 cRSID_initSIDchip(SID);
}
extern unsigned long long main_cpu_clk, vic_cpu_clk;
unsigned long long prev_cpu_clk = 0, usid_main_clk = 0, flush_cpu_clk = 0;
unsigned int us_writecycles = 0;
unsigned char sidwr[2][0x20] = {0};
void (*writeFuncPtr)(uint8_t,uint8_t,uint16_t);

void cRSID_initSIDchip (cRSID_SIDinstance* SID)
{
  prev_cpu_clk = 0, usid_main_clk = 0, flush_cpu_clk = 0;
  us_writecycles = 0;
}

int USBSID_CalcSIDno(cRSID_C64instance *C64, unsigned short address)
{
  if ((address >= C64->SID[0].BaseAddress) && (address < (C64->SID[0].BaseAddress + 0x20))) { /* SID1 */
    return 0;
  }
  if ((address >= C64->SID[1].BaseAddress) && (address < (C64->SID[1].BaseAddress + 0x20))) { /* SID2 */
    if (C64->SID[1].BaseAddress) {
      return 1;
    } else return 0;
  }
  if ((address >= C64->SID[2].BaseAddress) && (address < (C64->SID[2].BaseAddress + 0x20))) { /* SID3 */
    if (C64->SID[2].BaseAddress) {
      return 2;
    } else return 0;
  }
  if ((address >= C64->SID[3].BaseAddress) && (address < (C64->SID[3].BaseAddress + 0x20))) { /* SID4 */
    if (C64->SID[3].BaseAddress) {
      return 3;
    } else return 0;
  }
  return -1;
}

uint16_t USBSID_Delay(cRSID_C64instance *C64)
{
  if (main_cpu_clk < usid_main_clk) { /* Sync reset */
    usid_main_clk = main_cpu_clk;
    return 0;
  }

  int cycles = (main_cpu_clk - usid_main_clk);
  while (cycles > 0xffff) {
    cycles -= 0xffff;
  }
  /* ISSUE: Slows down Antarctic Burial A LOT and doesn't really improve other RSID */
  /* A bit better for tunes like BMX Kids and Stormlord, but delaying for Antarctic Burial */
  // if(C64->RealSIDmode) {
  //   cycled_delay_operation((uint16_t)cycles);
  // }
  usid_main_clk = main_cpu_clk;
  return (uint16_t)cycles;
}

void USBSID_FrameHandler(cRSID_C64instance *C64)
{ /* For PSID Only */
  int cycles = C64->FrameCycles - (int)us_writecycles;
  if (cycles > 0 && cycles <= C64->FrameCycles) {
    cycled_delay_operation((uint16_t)cycles);
  }
  usid_main_clk = main_cpu_clk;
  us_writecycles = 0;
  return;
}

void USBSID_WriteOperation(cRSID_C64instance *C64, uint16_t address, uint8_t data)
{
  if (!(address >= 0xD400 && address <= 0xD5FF)) return;
  // unsigned short cycles;
  int sidno = USBSID_CalcSIDno(C64,address);
  if (sidno < 0) return;
  /* Works for tunes like Antarctic Burial, doesn't for BMX Kidz */
  uint16_t cycles = USBSID_Delay(C64);
  uint8_t addr = (address & 0x1F);

  if(C64->RealSIDmode) {
    if(sidwr[0][addr] != data) {
      /* 0 cycles delay between writes is best for RSID tunes like Antarctic burial but shit for all others */
      writeFuncPtr((uint8_t)(addr | (sidno * 0x20)), data, 0);
      sidwr[0][addr] = data;
    }
  } else {
    writeFuncPtr((uint8_t)(addr | (sidno * 0x20)), data, (uint16_t)cycles);
  }
  us_writecycles+=cycles;
}
