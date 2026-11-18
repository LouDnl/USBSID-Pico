/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * usbsid_protocol.h
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

/**
 * @brief USBSID-Pico wire protocol
 *
 * Master source for command bytes, config sub-commands, upload and
 * player ids, feature bits, reply layout, config item ids and
 * SID stream constants.
 * !! Do not edit copies in other repos: update this file and
 * re-sync the copies !!
 *
 * Pure C without includes or build option guards, every constant is
 * defined on a full include. Include at global scope in C or inside a
 * namespace in C++.
 *
 * Header is made up in two parts:
 * A: USBSID_* macros, guarded once per translation unit.
 * B: enums and remaining defines, included once per scope.
 * no #pragma once (a pragma breaks namespace compilation in the driver)
 *
 * Use #define USBSID_PROTOCOL_MACROS_ONLY before including to get
 * the USBSID_* macros without global enum names, for C APIs.
 * Any full include later one still adds the enums.
 *
 * Byte numbering for config packets:
 *   byte 0 = (COMMAND << 6) | CONFIG
 *   byte 1 = config sub-command
 *   byte 2 and up = arguments
 * Firmware config handlers receive the packet without byte 0: buffer[n]
 * holds byte n + 1. Reply offsets count from the first reply byte.
 */

#ifndef _USBSID_PROTOCOL_DEFS_
#define _USBSID_PROTOCOL_DEFS_

/* US_FEATURES reply bits, bit 1 (0x02) is unused */
#define USBSID_FEATURE_RP2350            0x01  /* Pico2 (RP2350), unset on Pico1 (RP2040) */
#define USBSID_FEATURE_RGBVU             0x04  /* RGB LED onboard */
#define USBSID_FEATURE_PIOUART           0x08  /* PIO UART */
#define USBSID_FEATURE_NET               0x10  /* WiFi/Bluetooth or USB net, WIFI_READ_IP reports live state */
#define USBSID_FEATURE_NSD               0x20  /* Network SID Device, enables DISCOVERY_ENABLE */
#define USBSID_FEATURE_SIDSTREAM         0x40  /* SID stream interface (USB interface 7) built in */
#define USBSID_FEATURE_EMULATOR          0x80  /* Embedded SID player & emulator */

/* READ_CONFIG reply byte offsets */
#define USBSID_CFG_IDX_UNACK                2  /* (v1.5+ PCB) 1 = config needs CONFIG_ACK, socket power off */
#define USBSID_CFG_IDX_SOCKDETECT           3  /* (v1.5+ PCB) Socket change detection on boot */
#define USBSID_CFG_IDX_PRESET               4  /* Bits 0-6 last applied preset, bit 7 preset auto detect */
#define USBSID_CFG_IDX_LOCKCLOCK            5  /* Clock rate locked */
#define USBSID_CFG_IDX_EXTCLOCK             6  /* Bit 0 external clock, bit 1 board clock link */
#define USBSID_CFG_IDX_CLOCKHZ              7  /* Clock rate in Hz, bytes 7-9 big endian */
#define USBSID_CFG_IDX_FMOPL_EN            55  /* FMOpl enabled */
#define USBSID_CFG_IDX_FMOPL_SID           56  /* FMOpl SID number */
#define USBSID_CFG_IDX_STEREO              57  /* Audio switch, 1 = stereo */
#define USBSID_CFG_IDX_AUDIOLOCK           58  /* Audio switch locked */
#define USBSID_CFG_IDX_SOCKMODE            60  /* Bit 0 mirrored, bit 1 flipped, bit 2 mixed */

/* Raw reply sizes */
#define USBSID_CONFIG_LEN                  64  /* READ_CONFIG reply */
#define USBSID_SOCKETCONFIG_LEN            12  /* READ_SOCKETCFG reply */

/* Internal SID player sub-commands */
#define USBSID_PLAYER_START              0xE1  /* Start play of a loaded tune */
#define USBSID_PLAYER_STOP               0xE2  /* Stop play */
#define USBSID_PLAYER_PAUSE              0xE3  /* Toggle pause */
#define USBSID_PLAYER_NEXT               0xE4  /* Play the following subtune */
#define USBSID_PLAYER_PREV               0xE5  /* Play the previous subtune */
#define USBSID_PLAYER_TWO                0xE6  /* Force play on socket two or SID two */
#define USBSID_PLAYER_FFWD               0xE7  /* Fast forward, byte 2 = 1 on, 0 off */
#define USBSID_PLAYER_MUTE               0xE9  /* Mute chip/voice, bytes 2-4 = chip, voice, mute */
#define USBSID_PLAYER_MUTED              0xEA  /* Read mute state, 5 byte reply */
#define USBSID_PLAYER_TIME               0xEB  /* Read play time in ms, 4 byte big endian reply */

/* File types for UPLOAD_SID_START byte 2 */
#define USBSID_UPLOAD_FILE_STDIN         0x00  /* Driver only, firmware handles it like UPLOAD_FILE_SID */
#define USBSID_UPLOAD_FILE_SID           0x01  /* Driver only, firmware defaults to PSID/RSID file */
#define USBSID_UPLOAD_FILE_PRG           0x02  /* C64 PRG/P00 file */

/* SID stream vendor interface, present if US_FEATURES reports USBSID_FEATURE_SIDSTREAM */
#define USBSID_STREAM_ITF_NUM               7  /* Vendor interface number */
#define USBSID_STREAM_EP_OUT             0x07  /* Bulk OUT endpoint, data is discarded */
#define USBSID_STREAM_EP_IN              0x87  /* Bulk IN endpoint, SID frames */
#define USBSID_STREAM_REQUEST_START      0x10  /* Vendor control OUT, wLength 0 (defaults) or USBSID_STREAM_START_LEN */
#define USBSID_STREAM_REQUEST_STOP       0x11  /* Vendor control OUT, no data stage */
#define USBSID_STREAM_REQUEST_INFO       0x12  /* Vendor control IN, USBSID_STREAM_INFO_LEN byte reply */
#define USBSID_STREAM_INFO_LEN             16  /* STREAM_REQUEST_INFO reply */
#define USBSID_STREAM_START_LEN             4  /* STREAM_REQUEST_START data: rate Hz LE, SID mask, reserved */
#define USBSID_STREAM_FRAME_SIZE           63  /* SID frame, short packet, one SID per frame */
#define USBSID_STREAM_FRAME_SID          0x53  /* frame[0] of a SID frame */
#define USBSID_STREAM_FORMAT_VERSION        1  /* STREAM_REQUEST_INFO reply byte 0 */
#define USBSID_STREAM_FLAG_OTHER_OWNER   0x40  /* frame[1] bit 6, bus owned by a non USB owner */

/* Firmware version integer from which batch READ exists, no feature bit */
#define USBSID_FW_VERSION_BATCH_READ       81

#endif /* _USBSID_PROTOCOL_DEFS_ */


#if !defined(USBSID_PROTOCOL_MACROS_ONLY) && !defined(_USBSID_PROTOCOL_H_)
#define _USBSID_PROTOCOL_H_


/* USBSID command byte */
enum CommandBytes
{
  PACKET_TYPE     = 0xC0,  /* 0b11000000 ~ 192  */
  BYTE_MASK       = 0x3F,  /*   0b111111 ~  63  */
  COMMAND_MASK    = 0x1F,  /*    0b11111 ~  31  */

  /* BYTE 0 - top 2 bits */
  WRITE           =    0,  /*        0b0 ~ 0x00 */
  READ            =    1,  /*        0b1 ~ 0x40 */
  CYCLED_WRITE    =    2,  /*       0b10 ~ 0x80 */
  COMMAND         =    3,  /*       0b11 ~ 0xC0 */
  /* BYTE 0 - lower 6 bits for byte count */
  /* BYTE 0 - lower 5 bits for Commands */
  CYCLED_READ     =    4,  /*      0b100 ~ 0x04 */
  DELAY_CYCLES    =    5,  /*      0b101 ~ 0x05 */
  PAUSE           =   10,  /*     0b1010 ~ 0x0A */
  UNPAUSE         =   11,  /*     0b1011 ~ 0x0B */
  MUTE            =   12,  /*     0b1100 ~ 0x0C */
  UNMUTE          =   13,  /*     0b1101 ~ 0x0D */
  RESET_SID       =   14,  /*     0b1110 ~ 0x0E */
  DISABLE_SID     =   15,  /*     0b1111 ~ 0x0F */
  ENABLE_SID      =   16,  /*    0b10000 ~ 0x10 */
  CLEAR_BUS       =   17,  /*    0b10001 ~ 0x11 */
  CONFIG          =   18,  /*    0b10010 ~ 0x12 */
  RESET_MCU       =   19,  /*    0b10011 ~ 0x13 */
  BOOTLOADER      =   20,  /*    0b10100 ~ 0x14 */

  /* WebUSB additional commands */
  WEBUSB_COMMAND  = 0xFF,
  WEBUSB_RESET    = 0x15,
  WEBUSB_CONTINUE = 0x16,
  WEBUSB_NUMSIDS  = 0x39,
  WEBUSB_FMOPLSID = 0x3A,
  WEBUSB_TOGGLEAU = 0x3B,
};


/* Config sub-commands, byte 1 after (COMMAND << 6) | CONFIG */
enum ConfigSubCommands
{
  RESET_USBSID        = 0x20,  /* Resets the MCU including the USB connection */

  READ_CONFIG         = 0x30,  /* Read full config as bytes, CONFIG_BUFFER_SIZE reply */
  APPLY_CONFIG        = 0x31,  /* Apply config from memory */
  SET_CONFIG          = 0x32,  /* Set single config item, byte 2    = item id */
  SAVE_CONFIG         = 0x33,  /* Save and load config and then reboot */
  SAVE_NORESET        = 0x34,  /* Save, load and apply config */
  RESET_CONFIG        = 0x35,  /* Reset to default settings */
  WRITE_CONFIG        = 0x36,  /* Write full config as bytes */
  READ_SOCKETCFG      = 0x37,  /* Read socket config as bytes, SOCKET_BUFFER_SIZE reply */
  RELOAD_CONFIG       = 0x38,  /* Reload and apply stored config from flash */
  READ_NUMSIDS        = 0x39,  /* Returns the number of SIDs in byte 0 */
  READ_FMOPLSID       = 0x3A,  /* Returns the sidno for FMOpl 1~4, 0 is disable */
  READ_CONFIGACK      = 0x3F,  /* Returns 1 if socket power is disabled and read/writes are dropped */

  SINGLE_SID          = 0x40,  /* Single SID Socket One */
  DUAL_SID            = 0x41,  /* Dual SID Socket One */
  QUAD_SID            = 0x42,  /* Four SID's in 2 sockets */
  TRIPLE_SID          = 0x43,  /* Two SID's in socket One, One SID in socket two */
  TRIPLE_SID_TWO      = 0x44,  /* One SID in Socket One, Two SID's in socket two */
  MIRRORED_SID        = 0x45,  /* Socket Two is linked to Socket One */
  DUAL_SOCKET1        = 0x46,  /* Two SID's in socket One, Socket Two disabled */
  DUAL_SOCKET2        = 0x47,  /* Two SID's in socket Two, Socket One disabled */
  DUAL_FLIPPED        = 0x48,  /* Two SID's in 2 sockets, sockets flipped */
  QUAD_FLIPPED        = 0x49,  /* Four SID's in 2 sockets, sockets flipped */
  QUAD_MIXED          = 0x4A,  /* Four SID's in 2 sockets, mixed addresses */
  QUAD_FLIPMIX        = 0x4B,  /* Four SID's in 2 sockets, sockets flipped and addresses flipped */
  HOTFLIP_SOCKETS     = 0x4F,  /* Socket Two is Socket One and vice versa */

  SET_CLOCK           = 0x50,  /* Change SID clock frequency by array id */
  DETECT_SIDS         = 0x51,  /* Try to detect the SID types per socket */
  TEST_ALLSIDS        = 0x52,  /* Runs a very long test on all SID's */
  TEST_SID1           = 0x53,  /* Runs a very long test on SID 1 */
  TEST_SID2           = 0x54,  /* Runs a very long test on SID 2 */
  TEST_SID3           = 0x55,  /* Runs a very long test on SID 3 */
  TEST_SID4           = 0x56,  /* Runs a very long test on SID 4 */
  GET_CLOCK           = 0x57,  /* Returns the clockrate as array id in byte 0 */
  LOCK_CLOCK          = 0x58,  /* Locks the clockrate from being changed, saved in config */
  STOP_TESTS          = 0x59,  /* Interrupt any running SID tests */
  DETECT_CLONES       = 0x5A,  /* Detect clone SID types */
  AUTO_DETECT         = 0x5B,  /* Run auto detection routine (fallback/workaround for rp2350 bug) */

  /* MIDI */
  LOAD_MIDI_STATE     = 0x60,  /* Load the MIDI config (channels, patches, arp tables, CC map) from flash into memory */
  SAVE_MIDI_STATE     = 0x61,  /* Save the MIDI config from memory to flash */
  RESET_MIDI_STATE    = 0x63,  /* Reset the MIDI config to defaults and erase its flash slots */
  LOAD_MIDI_CC        = 0x64,  /* Load the MIDI CC table from flash into memory */
  SAVE_MIDI_CC        = 0x65,  /* Save the MIDI CC table from memory to flash */
  RESET_MIDI_CC       = 0x66,  /* Reset the MIDI CC table to defaults and erase its flash slots */
  SET_C64_MIDIAPP     = 0x67,  /* Byte 2 = app (0 Cynthcart, 1 SID SYNTH), byte 3 = 1 saves config, emulator only */
  START_C64_MIDIAPP   = 0x6D,  /* Start the configured C64 MIDI app, emulator only */
  STOP_C64_MIDIAPP    = 0x6E,  /* Stop the configured C64 MIDI app, emulator only */
  RESTART_C64_MIDIAPP = 0x6F,  /* Restart the configured C64 MIDI app, emulator only */

  /* Network SID Device provisioning (WiFi/Bluetooth, USB net subset).
   * Handled on builds with network support only. WIFI_STATUS and
   * WIFI_READ_IP reply on every build, 0xFF state bytes without network. */
  WIFI_STATUS         = 0x70,  /* Returns {link_up, session_active} */
  WIFI_SET_SSID       = 0x71,  /* Byte 2    = length, bytes 3 and up    = SSID bytes */
  WIFI_SET_PSK        = 0x72,  /* Byte 2    = length, bytes 3 and up    = PSK bytes, write-only */
  WIFI_SET_HOSTNAME   = 0x73,  /* Byte 2    = length, bytes 3 and up    = hostname bytes */
  WIFI_ENABLE         = 0x74,  /* Byte 2    = 0/1 */
  NSD_ENABLE          = 0x75,  /* Byte 2    = 0/1 */
  NSD_SET_PORT        = 0x76,  /* Bytes 2-3    = port, big-endian */
  WIFI_APPLY          = 0x77,  /* Persist net config to flash and apply live */
  WIFI_FORGET         = 0x78,  /* Erase stored credentials */
  BT_NSD_ENABLE       = 0x79,  /* Byte 2    = 0/1 */
  READ_NETCFG         = 0x7A,  /* Read net config as bytes */
  WIFI_READ_IP        = 0x7B,  /* Returns {state, ip0, ip1, ip2, ip3}, every build */
  DISCOVERY_ENABLE    = 0x7C,  /* Byte 2    = 0/1, applied live, persisted by WIFI_APPLY */
  USBNET_IP_MODE      = 0x7D,  /* Byte 2    = USBNET_IP_MODE_*, USB net only, persisted by WIFI_APPLY */
  USBNET_SET_STATIC   = 0x7E,  /* Bytes 2-5 ip, 6-9 netmask, 10-13 gateway, USB net only, persisted by WIFI_APPLY */
  USBNET_READ_IPCFG   = 0x7F,  /* Returns {mode, ip[4], netmask[4], gateway[4]}, USB net only */

  USBSID_VERSION      = 0x80,  /* Byte 2    = 0: 64 byte reply {USBSID_VERSION, length, version string}, else 1 byte version integer (81    = 0.8.1) */
  US_PCB_VERSION      = 0x81,  /* Byte 2    = 0: 64 byte reply {US_PCB_VERSION, length, version string}, else 1 byte version integer */
  US_FEATURES         = 0x82,  /* Read compiled firmware features, 1 byte reply of US_FEATURE_* bits */
  SET_RX_RING         = 0x83,  /* Byte 2    = 0/1, USB RX ringbuffer (auto save and reboot on change) */

  LINK_CLOCK          = 0x84,  /* Byte 2    = 0/1, link with external clock (auto save and reboot on change) */
  RESTART_BUS         = 0x85,  /* Restart DMA & PIO */
  RESTART_BUS_CLK     = 0x86,  /* Restart PIO clocks */
  SYNC_PIOS           = 0x87,  /* Sync PIO clocks */
  TOGGLE_AUDIO        = 0x88,  /* Toggle mono <-> stereo (v1.3+ boards only) */
  SET_AUDIO           = 0x89,  /* Set mono <-> stereo (v1.3+ boards only) */
  LOCK_AUDIO          = 0x90,  /* Locks the audio switch into it's current state (v1.3+ boards only) */
  GET_AUDIO           = 0x91,  /* Get current audio switch setting */

  /* Hardware SID clone configuration */
  READ_CLONECHIP      = 0xA0,  /* Chip config read initiator byte, OR with a CHIP_* id */
  WRITE_CLONECHIP     = 0xB0,  /* Chip config write initiator byte, OR with a CHIP_* id */
  PDSID               = 0xA3,  /* Legacy: holds the reset line for 5 seconds to change SID type on a PDSID */

  CONFIG_ACK          = 0xFA,  /* (v1.5+ PCB) Acknowledge and save the current configuration and switch on regulators */
  SOCKET_DETECT       = 0xFD,  /* (v1.5+ PCB) Enable/disable and save automatic socket change detection on boot */
  PRESET_DETECT       = 0xFE,  /* Enable/disable and save automatic detection on preset change */
};


/* Onboard emulator sub-commands, firmware built with ONBOARD_EMULATOR only */
enum EmulatorCommands
{
  /* SID file upload */
  UPLOAD_SID_START    = 0xD0,  /* Enter receiving mode, byte 2 = file type */
  UPLOAD_SID_DATA     = 0xD1,  /* Data packet, UPLOAD_PAYLOAD_SIZE payload bytes */
  UPLOAD_SID_END      = 0xD2,  /* Leave receiving mode */
  UPLOAD_SID_SIZE     = 0xD3,  /* File size, 16 bit big endian */
  UPLOAD_SID_PLAYTIME = 0xD4,  /* Max play time in ms, 32 bit big endian, 5 minutes if unset */

  /* Internal SID player, values from USBSID_PLAYER_* */
  SID_PLAYER_TUNE     = 0xE0,  /* Load uploaded tune, byte 3 = subtune (0 based), starts play */
  SID_PLAYER_START    = USBSID_PLAYER_START,
  SID_PLAYER_STOP     = USBSID_PLAYER_STOP,
  SID_PLAYER_PAUSE    = USBSID_PLAYER_PAUSE,
  SID_PLAYER_NEXT     = USBSID_PLAYER_NEXT,
  SID_PLAYER_PREV     = USBSID_PLAYER_PREV,
  SID_PLAYER_TWO      = USBSID_PLAYER_TWO,
  SID_PLAYER_FFWD     = USBSID_PLAYER_FFWD,
  SID_PLAYER_RWND     = 0xE8,  /* Rewind, not implemented in firmware */
  SID_PLAYER_MUTE     = USBSID_PLAYER_MUTE,
  SID_PLAYER_MUTED    = USBSID_PLAYER_MUTED,
  SID_PLAYER_TIME     = USBSID_PLAYER_TIME,
};

/* File types for UPLOAD_SID_START byte 2, values from USBSID_UPLOAD_FILE_* */
enum EmulatorUploadTypes
{
  /* Receiver */
  UPLOAD_FILE_STDIN = USBSID_UPLOAD_FILE_STDIN,
  UPLOAD_FILE_SID   = USBSID_UPLOAD_FILE_SID,
  UPLOAD_FILE_PRG   = USBSID_UPLOAD_FILE_PRG,
};


/* Config read/write bytes */
enum ConfigIOBytes
{
  FULL_CONFIG       = READ_CONFIG,    /* 0x30 */
  SOCKET_CONFIG     = READ_SOCKETCFG, /* 0x37 */
  MIDI_CONFIG       = 0x60,           /* Reserved, not implemented */
  MIDI_CCVALUES     = 0x6A,           /* Reserved, not implemented */
  NETWORK_CONFIG    = READ_NETCFG,    /* 0x7A */
  VERIFICATION_BYTE = 0x7F,           /* After init byte */
  END_BYTE          = 0x8F,           /* Before termination byte, only if buffer can exceed 64 bytes */
  TERMINATION_BYTE  = 0xFF,           /* Last byte in buffer */
};


/**
 * @brief Known Chip types
 * These are the currently chiptypes USBSID-Pico can identify
 *
 */
typedef enum ChipType
{
  CHIP_REAL = 0,  /* Real SID chip (assumed) */
  CHIP_UNKNOWN,   /* Unknown SID chip */
  CHIP_SKPICO,    /* SIDKick-pico */
  CHIP_ARMSID,    /* ARMSID */
  CHIP_ARM2SID,   /* ARM2SID */
  CHIP_FPGASID,   /* FPGASID */
  CHIP_REDIPSID,  /* RedipSID */
  CHIP_PDSID,     /* Public Domain SID */
  CHIP_BACKSID,   /* BackSID */
  CHIP_SIDEMU,    /* SIDEmu */
  CHIP_COUNT,     /* Number of chiptypes */
} ChipType;

/* Clone chip config read/write initiator bytes */
enum CloneChipIOBytes
{
  READ_SKPICO    = (READ_CLONECHIP | (int)CHIP_SKPICO),    /* 0xA2 */
  READ_ARMSID    = (READ_CLONECHIP | (int)CHIP_ARMSID),    /* 0xA3 */
  READ_ARM2SID   = (READ_CLONECHIP | (int)CHIP_ARM2SID),   /* 0xA4 */
  READ_FPGASID   = (READ_CLONECHIP | (int)CHIP_FPGASID),   /* 0xA5 */
  READ_REDIPSID  = (READ_CLONECHIP | (int)CHIP_REDIPSID),  /* 0xA6 */
  READ_PDSID     = (READ_CLONECHIP | (int)CHIP_PDSID),     /* 0xA7 */
  READ_BACKSID   = (READ_CLONECHIP | (int)CHIP_BACKSID),   /* 0xA8 */
  READ_SIDEMU    = (READ_CLONECHIP | (int)CHIP_SIDEMU),    /* 0xA9 */

  WRITE_SKPICO   = (WRITE_CLONECHIP | (int)CHIP_SKPICO),   /* 0xB2 */
  WRITE_ARMSID   = (WRITE_CLONECHIP | (int)CHIP_ARMSID),   /* 0xB3 */
  WRITE_ARM2SID  = (WRITE_CLONECHIP | (int)CHIP_ARM2SID),  /* 0xB4 */
  WRITE_FPGASID  = (WRITE_CLONECHIP | (int)CHIP_FPGASID),  /* 0xB5 */
  WRITE_REDIPSID = (WRITE_CLONECHIP | (int)CHIP_REDIPSID), /* 0xB6 */
  WRITE_PDSID    = (WRITE_CLONECHIP | (int)CHIP_PDSID),    /* 0xB7 */
  WRITE_BACKSID  = (WRITE_CLONECHIP | (int)CHIP_BACKSID),  /* 0xB8 */
  WRITE_SIDEMU   = (WRITE_CLONECHIP | (int)CHIP_SIDEMU),   /* 0xB9 */
};


/* SET_CONFIG item ids, byte 2 = item, bytes 3 and up = values */
enum SetConfigTypes
{
  CONFIG_ENABLED  =  0,
  BOARD_CLOCKRATE =  0,
  SOCKET_ONE      =  1,
  SOCKET_TWO      =  2,
  SOCKET_DUALSID  =  1,
  SOCKET_CHIPTYPE =  2,
  SOCKET_UNUSED   =  3,
  SOCKET_SID1TYPE =  4,
  SOCKET_SID2TYPE =  5,
  OLD_S2_MIRRORED =  6, /* Deprecated post fw v0.7.0 */
  BOARD_LED       =  3,
  BOARD_RGBLED    =  4,
  LED_IDLEBREATH  =  1,
  LED_BRIGHTNESS  =  2,
  LED_SIDTOUSE    =  3,
  BOARD_CDC       =  5,
  BOARD_WEBUSB    =  6,
  BOARD_ASID      =  7,
  BOARD_MIDI      =  8,
  BOARD_FMOPL     =  9,
  BOARD_AUSWITCH  = 10,
  BOARD_AULOCK    = 11,
  BOARD_MIRRORED  = 12,
  BOARD_FLIPPED   = 13,
  BOARD_MIXED     = 14,
  BOARD_SDETECT   = 15, /* (PCB v1.5+) Automatic socket change detection */
  BOARD_PADETECT  = 16, /* (PCB v1.5+) Preset automatic detection */
  BOARD_RXRING    = 17, /* USB RX ringbuffer 0/1, applied after save and reboot */
  /* 18 reserved for BOARD_MIDIUART */
  BOARD_EMUAPP    = 19, /* ONBOARD_EMULATOR: C64 MIDI app the emulator starts, 0 Cynthcart, 1 SID SYNTH */
};


/* USBNET_IP_MODE byte 2 and USBNET_READ_IPCFG reply mode byte */
enum NetModes
{
  USBNET_IP_MODE_DHCP_CLIENT = 0,  /* Lease from a LAN DHCP server via host bridge */
  USBNET_IP_MODE_STATIC      = 1,  /* Use the stored ip, netmask and gateway */
  USBNET_IP_MODE_DHCP_SERVER = 2,  /* Factory default, serves the USB link only */
};


/**
 * @brief US_FEATURES reply bits, values from USBSID_FEATURE_*
 * Bit 1 (0x02) is unused
 *
 */
enum USFeatures
{
  US_FEATURE_RP2350    = USBSID_FEATURE_RP2350,
  US_FEATURE_RGBVU     = USBSID_FEATURE_RGBVU,
  US_FEATURE_PIOUART   = USBSID_FEATURE_PIOUART,
  US_FEATURE_NET       = USBSID_FEATURE_NET,
  US_FEATURE_NSD       = USBSID_FEATURE_NSD,
  US_FEATURE_SIDSTREAM = USBSID_FEATURE_SIDSTREAM,
  US_FEATURE_EMULATOR  = USBSID_FEATURE_EMULATOR,
};


/* SID stream interface constants, values from USBSID_STREAM_* */
enum StreamConstants
{
  STREAM_ITF_NUM          = USBSID_STREAM_ITF_NUM,
  STREAM_EP_OUT           = USBSID_STREAM_EP_OUT,
  STREAM_EP_IN            = USBSID_STREAM_EP_IN,
  STREAM_REQUEST_START    = USBSID_STREAM_REQUEST_START,
  STREAM_REQUEST_STOP     = USBSID_STREAM_REQUEST_STOP,
  STREAM_REQUEST_INFO     = USBSID_STREAM_REQUEST_INFO,
  STREAM_INFO_LEN         = USBSID_STREAM_INFO_LEN,
  STREAM_START_LEN        = USBSID_STREAM_START_LEN,
  STREAM_FRAME_SIZE       = USBSID_STREAM_FRAME_SIZE,
  STREAM_FRAME_SID        = USBSID_STREAM_FRAME_SID,
  STREAM_FORMAT_VERSION   = USBSID_STREAM_FORMAT_VERSION,
  STREAM_FLAG_OTHER_OWNER = USBSID_STREAM_FLAG_OTHER_OWNER,
};


/**
 * @brief READ_CONFIG reply layout
 * [0] = READ_CONFIG, [1] = VERIFICATION_BYTE,
 * [62] = END_BYTE, [63] = TERMINATION_BYTE
 *
 */
#define CONFIG_BUFFER_SIZE USBSID_CONFIG_LEN

/* READ_CONFIG reply byte offsets, the 11 USBSID_CFG_IDX_* offsets derive
 * from those macros, the others name the firmware reply layout */
enum ReadConfigOffsets
{
  CFG_IDX_UNACK           = USBSID_CFG_IDX_UNACK,
  CFG_IDX_SOCKDETECT      = USBSID_CFG_IDX_SOCKDETECT,
  CFG_IDX_PRESET          = USBSID_CFG_IDX_PRESET,
  CFG_IDX_LOCKCLOCK       = USBSID_CFG_IDX_LOCKCLOCK,
  CFG_IDX_EXTCLOCK        = USBSID_CFG_IDX_EXTCLOCK,
  CFG_IDX_CLOCKHZ         = USBSID_CFG_IDX_CLOCKHZ,
  CFG_IDX_S1_ENABLED      = 10,  /* Socket one enabled */
  CFG_IDX_S1_DUALSID      = 11,  /* Socket one dual SID */
  CFG_IDX_S1_CHIPTYPE     = 12,  /* Socket one chip type */
  CFG_IDX_S1_SIDIDS       = 13,  /* Socket one, low nibble SID1 id, high nibble SID2 id */
  CFG_IDX_S1_SID1TYPE     = 14,  /* Socket one SID1 type */
  CFG_IDX_S1_SID2TYPE     = 15,  /* Socket one SID2 type */
  CFG_IDX_S2_ENABLED      = 20,  /* Socket two enabled */
  CFG_IDX_S2_DUALSID      = 21,  /* Socket two dual SID */
  CFG_IDX_MIRRORED_COMPAT = 22,  /* Mirrored flag, kept for pre v0.7.0 firmware compatibility */
  CFG_IDX_S2_CHIPTYPE     = 23,  /* Socket two chip type */
  CFG_IDX_S2_SIDIDS       = 24,  /* Socket two, low nibble SID1 id, high nibble SID2 id */
  CFG_IDX_S2_SID1TYPE     = 25,  /* Socket two SID1 type */
  CFG_IDX_S2_SID2TYPE     = 26,  /* Socket two SID2 type */
  CFG_IDX_LED_ENABLED     = 30,  /* LED enabled */
  CFG_IDX_LED_BREATHE     = 31,  /* LED idle breathe */
  CFG_IDX_RGB_ENABLED     = 40,  /* RGB LED enabled */
  CFG_IDX_RGB_BREATHE     = 41,  /* RGB LED idle breathe */
  CFG_IDX_RGB_BRIGHTNESS  = 42,  /* RGB LED brightness */
  CFG_IDX_RGB_SIDTOUSE    = 43,  /* RGB LED SID to use */
  CFG_IDX_EMULATOR_APP    = 49,  /* ONBOARD_EMULATOR: C64 MIDI app, 0 Cynthcart, 1 SID SYNTH */
  CFG_IDX_CDC_ENABLED     = 51,  /* CDC enabled */
  CFG_IDX_WEBUSB_ENABLED  = 52,  /* WebUSB enabled */
  CFG_IDX_ASID_ENABLED    = 53,  /* ASID enabled */
  CFG_IDX_MIDI_ENABLED    = 54,  /* MIDI enabled */
  CFG_IDX_FMOPL_EN        = USBSID_CFG_IDX_FMOPL_EN,
  CFG_IDX_FMOPL_SID       = USBSID_CFG_IDX_FMOPL_SID,
  CFG_IDX_STEREO          = USBSID_CFG_IDX_STEREO,
  CFG_IDX_AUDIOLOCK       = USBSID_CFG_IDX_AUDIOLOCK,
  CFG_IDX_RX_RING         = 59,  /* USB RX ringbuffer */
  CFG_IDX_SOCKMODE        = USBSID_CFG_IDX_SOCKMODE,
};

/**
 * @brief READ_SOCKETCFG reply layout
 *  0 Initiator     = READ_SOCKETCFG
 *  1 Verification  = VERIFICATION_BYTE
 *  2 HiByte        = socketOne enabled
 *  2 LoByte        = socketOne dualsid
 *  3               = socketOne chipType
 *  4 HiByte        = socketOne sid1Type
 *  4 LoByte        = socketOne sid2Type
 *  5 HiByte        = socketTwo enabled
 *  5 LoByte        = socketTwo dualsid
 *  6               = socketTwo chipType
 *  7 HiByte        = socketTwo sid1Type
 *  7 LoByte        = socketTwo sid2Type
 *  8 HiByte        = socketOne SID2 id
 *  8 LoByte        = socketOne SID1 id
 *  9 HiByte        = socketTwo SID2 id
 *  9 LoByte        = socketTwo SID1 id
 * 10 Bit 0         = socketTwo mirrors socketOne
 * 10 Bit 1         = sockets are flipped, One is Two and vice versa
 * 10 Bit 2         = SID addresses are mixed (quad SID only)
 * 11 Terminator    = TERMINATION_BYTE
 *
 */
#define SOCKET_BUFFER_SIZE USBSID_SOCKETCONFIG_LEN


/* Upload packet sizes */
#define UPLOAD_PACKET_SIZE  64  /* Every upload packet is a full 64 byte packet */
#define UPLOAD_PAYLOAD_SIZE 62  /* Data bytes per UPLOAD_SID_DATA packet */


#endif /* !USBSID_PROTOCOL_MACROS_ONLY && !_USBSID_PROTOCOL_H_ */
