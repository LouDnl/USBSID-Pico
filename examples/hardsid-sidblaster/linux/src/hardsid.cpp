#include "hardsid.hpp"

#define HARDSID_VERSION 0x0200

/* #define HSDBEBUG */
#ifdef HSDBEBUG
#define HSDBG(...)  printf(__VA_ARGS__)
#else
#define HSDBG(...)  ((void)0)
#endif

extern void usbsid_reset(void);
extern int usbsid_open(void);
extern int usbsid_read(uint16_t addr, int chipno);
extern void usbsid_store(uint16_t addr, uint8_t val, int chipno);
extern int WaitForCycle(int cycle);


uint16_t HardSID_Version( void ) {
  return( HARDSID_VERSION );
}
int us = -1;
uint8_t HardSID_Devices(void) {
  HSDBG("%s\r\n", __func__);
  if (us < 0) us = usbsid_open();
  HSDBG("%s us: %d\r\n", __func__, us);
  if (us < 0) exit(1);
  return (MAXSID);
}

uint8_t HardSID_SIDCount(void) {
  HSDBG("%s\r\n", __func__);
  return (MAXSID);
}

uint8_t HardSID_Read(uint8_t DeviceID, int Cycles, uint8_t SID_reg ) {
  HSDBG("HardSID_Read: 0x%04x %d 0x%04x\r\n", DeviceID, Cycles, SID_reg);
  HardSID_Delay(DeviceID, Cycles);
  int r = usbsid_read((uint16_t)SID_reg, (int)DeviceID);
  return (uint8_t)r;
}

void HardSID_Delay(uint8_t DeviceID, uint16_t Cycles) {
  HSDBG("%s\r\n", __func__);
  if (Cycles > 0) {
    WaitForCycle(Cycles);
  }
}

void HardSID_Write(uint8_t DeviceID, int Cycles, uint8_t SID_reg, uint8_t Data) {
  HSDBG("HardSID_Write: 0x%02x %d $%02x $%02x\r\n", DeviceID, Cycles, SID_reg, Data);
  usbsid_store((uint16_t)SID_reg, Data, (int)DeviceID);
  HardSID_Delay(DeviceID, Cycles);
}

void HardSID_Flush(uint8_t DeviceID) {
  HSDBG("HardSID_Flush: 0x%04x\r\n", DeviceID);
}

void HardSID_SoftFlush(uint8_t DeviceID) {
  HSDBG("HardSID_SoftFlush: 0x%04x\r\n", DeviceID);
}

bool HardSID_Lock(uint8_t DeviceID) {
  HSDBG("HardSID_Lock: 0x%04x\r\n", DeviceID);
  return true;
}

void HardSID_Unlock(uint8_t DeviceID) {
  HSDBG("HardSID_Unlock: 0x%04x\r\n", DeviceID);
}

void HardSID_Filter( uint8_t DeviceID, bool Filter ) {
  HSDBG("HardSID_Filter: 0x%04x 0x%04x\r\n", DeviceID, Filter);
}

void HardSID_Reset( uint8_t DeviceID ) {
  HSDBG("HardSID_Reset: 0x%04x\r\n", DeviceID);
  usbsid_reset();
}

void HardSID_Reset2(uint8_t DeviceID, uint8_t Volume) {
  (void)Volume; /* TODO: Implement */
  HSDBG("HardSID_Reset2: 0x%04x 0x%04x\r\n", DeviceID, Volume);
  usbsid_reset();
}

void HardSID_Sync( uint8_t DeviceID ) {
  HSDBG("HardSID_Sync: 0x%04x\r\n", DeviceID);
}

void HardSID_Mute( uint8_t DeviceID, uint8_t Channel, bool Mute ) {
  HSDBG("HardSID_Mute: 0x%04x 0x%04x 0x%04x\r\n", DeviceID, Channel, Mute);
}

void HardSID_MuteAll( uint8_t DeviceID, bool Mute ) {
  HSDBG("HardSID_Mute: 0x%04x 0x%04x\r\n", DeviceID, Mute);
}

uint8_t HardSID_Try_Write(uint8_t DeviceID, int Cycles, uint8_t SID_reg, uint8_t Data) {
  HSDBG("HardSID_Try_Write: 0x%02x %d $%02x $%02x\r\n", DeviceID, Cycles, SID_reg, Data);
  HardSID_Write(DeviceID, Cycles, SID_reg, Data);
  return 0;
}

bool HardSID_ExternalTiming(uint8_t DeviceID) {
  HSDBG("HardSID_ExternalTiming: 0x%04x\r\n", DeviceID);
  return true;
}

void HardSID_GetSerial(char* output, int bufferSize, uint8_t DeviceID) {
  (void)output;
  HSDBG("HardSID_GetSerial: 0x%04x %d\r\n", DeviceID, bufferSize);
}

void HardSID_SetWriteBufferSize(uint8_t bufferSize) {
  HSDBG("HardSID_SetWriteBufferSize: 0x%04x\r\n", bufferSize);
}

int HardSID_SetSIDType(uint8_t DeviceID, int sidtype_) {
  HSDBG("HardSID_SetSIDType: 0x%04x %d\r\n", DeviceID, sidtype_);
  return 0;
}

int HardSID_GetSIDType(uint8_t DeviceID) {
  HSDBG("HardSID_GetSIDType: 0x%04x\r\n", DeviceID);
  return SID_TYPE_6581;
}

void InitHardSID_Mapper(void) {
  HSDBG("InitHardSID_Mapper\r\n");
}

uint8_t GetHardSIDCount(void) {
  HSDBG("GetHardSIDCount\r\n");
  return( HardSID_Devices() );
}

void WriteToHardSID(uint8_t DeviceID, uint8_t SID_reg, uint8_t Data) {
  HSDBG("WriteToHardSID: 0x%04x 0x%04x 0x%04x\r\n", DeviceID, SID_reg, Data);
  HardSID_Write( DeviceID, 1, SID_reg, Data );
}

uint8_t ReadFromHardSID(uint8_t DeviceID, uint8_t SID_reg) {
  HSDBG("ReadFromHardSID: 0x%04x 0x%04x\r\n", DeviceID, SID_reg);
  return( HardSID_Read( DeviceID, 1, SID_reg ) );
}

void MuteHardSID_Line(int Mute) {
  HSDBG("MuteHardSID_Line\r\n");
}
