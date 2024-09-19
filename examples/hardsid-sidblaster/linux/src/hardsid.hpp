#ifndef _Included_HardSID
#define _Included_HardSID

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	HSID_USB_WSTATE_OK = 1,
	HSID_USB_WSTATE_BUSY,
	HSID_USB_WSTATE_ERROR,
	HSID_USB_WSTATE_END
};

enum SID_TYPE {
	SID_TYPE_NONE = 0,
	SID_TYPE_6581,
	SID_TYPE_8580
};

#define MAXSID         4

/* HardSID API */

uint16_t HardSID_Version(void);
uint8_t HardSID_Devices(void);
uint8_t HardSID_SIDCount(void);

void HardSID_Write(uint8_t DeviceID, int Cycles, uint8_t SID_reg, uint8_t Data);
uint8_t HardSID_Read(uint8_t DeviceID, int Cycles, uint8_t SID_reg);
void HardSID_Delay(uint8_t DeviceID, uint16_t Cycles);
void HardSID_Flush(uint8_t DeviceID);
void HardSID_SoftFlush(uint8_t DeviceID);
bool HardSID_Lock(uint8_t DeviceID);
void HardSID_Unlock(uint8_t DeviceID);
void HardSID_Filter(uint8_t DeviceID, bool Filter);
void HardSID_Reset(uint8_t DeviceID);
void HardSID_Reset2(uint8_t DeviceID, uint8_t Volume);
void HardSID_Sync(uint8_t DeviceID);
void HardSID_Mute(uint8_t DeviceID, uint8_t Channel, bool Mute);
void HardSID_MuteAll(uint8_t DeviceID, bool Mute);
/* used in DLL version 0x0301 and above (SIDBlasterUSB) */
uint8_t HardSID_Try_Write(uint8_t DeviceID, int Cycles, uint8_t SID_reg, uint8_t Data);
/* used in DLL version 0x0301 and above */
bool HardSID_ExternalTiming(uint8_t DeviceID);
/* used in DLL version 0x202 and above */
void HardSID_GetSerial(char* output, int bufferSize, uint8_t DeviceID);
/* used in DLL version 0x202 and above */
void HardSID_SetWriteBufferSize(uint8_t bufferSize);
/* used in DLL version 0x202 and above */
int HardSID_SetSIDType(uint8_t DeviceID, int sidtype_);
/* used in DLL version 0x202 and above */
int HardSID_GetSIDType(uint8_t DeviceID);


/* Unknown API */

void InitHardSID_Mapper(void);
uint8_t GetHardSIDCount(void);
void WriteToHardSID(uint8_t DeviceID, uint8_t SID_reg, uint8_t Data);
uint8_t ReadFromHardSID(uint8_t DeviceID, uint8_t SID_reg);
void MuteHardSID_Line(int Mute);

#ifdef __cplusplus
}
#endif
#endif
