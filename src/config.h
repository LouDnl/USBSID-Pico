/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config.h
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

#ifndef _USBSID_CONFIG_H_
#define _USBSID_CONFIG_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

/* Project includes required for this header */
#include <usbsid_constants.h>
#include <usbsid_protocol.h>


#ifdef PICO_DEFAULT_LED_PIN
#define LED_PWM true
#else
#define LED_PWM false
#endif
#ifdef USE_RGB
#define RGB_ENABLED true
#else
#define RGB_ENABLED false
#endif

/* Flash information
 *
 * USBSID-Pico flash layout:
 * - 1024KB FW (rp2040)
 * - 3072KB FW (rp2350)
 * - 1024KB Persistent storage
 *
 * On rp2350 the flash is cleared completely every time a new uf2 is loaded,
 * this results in a default config on each fw flash.
 * This can be solved by reserving a larger part of the flash for persistent
 * storage e.g. 1024KB instead of only 4 KB
 *
 * Non working solutions
 * - adding a partitiontable had no effect
 * - adding a 4KB persistent part to the flash had no effect
 *
 * Ram and flash defaults on official pico boards:
 * XIP_BASE ~ Flash base address 0x10000000
 *
 * rp2040 PICO_FLASH_SIZE_BYTES ~ (1 * 1024 * 1024) = 0x200000 == 2MB(2097152)
 * rp2350 PICO_FLASH_SIZE_BYTES ~ (2 * 1024 * 1024) = 0x400000 == 4MB(4194304)
 * rp2040 RAM_SIZE ~ 0x40000 == 256KB(262144)
 * rp2350 RAM_SIZE ~ 0x80000 == 512KB(524288)
 *
 * FLASH_PAGE_SIZE   ~ (1u << 8)  = 0x100   == 256B
 * FLASH_SECTOR_SIZE ~ (1u << 12) = 0x1000  == 4096B
 * FLASH_BLOCK_SIZE  ~ (1u << 16) = 0x10000 == 65536B
 */

/* Init config vars
 * Based on: https://community.element14.com/products/raspberry-pi/b/blog/posts/raspberry-pico-c-sdk-reserve-a-flash-memory-block-for-persistent-storage
 */

 /* .section_persistent from the linker script, 4 bytes aligned */
extern uint32_t ADDR_PERSISTENT[];
/* Base address */
#define ADDR_PERSISTENT_BASE_ADDR (ADDR_PERSISTENT)
/* rp2040: 0x0x1FF000, rp2350: 0x3FF000 */
#define FLASH_PERSISTENT_OFFSET ((uint32_t)ADDR_PERSISTENT_BASE_ADDR - XIP_BASE)
/* Storage size in flash memory */
#define FLASH_PERSISTENT_SIZE (PICO_FLASH_SIZE_BYTES - FLASH_PERSISTENT_OFFSET)
/* Default config offset in flash memory */
#define FLASH_CONFIG_OFFSET (FLASH_PERSISTENT_OFFSET + ((FLASH_PERSISTENT_SIZE / 4) * 3))

/* MIDI expansion has its own linker script defined
 * persistent section after the config one */
extern uint32_t ADDR_CONFIG[];
extern uint32_t ADDR_MIDICONFIG[];
/* Set by verify_midiconfig_offset() at boot. The midi_config_save()/load() must
 * refuse to touch flash while this is false. */
extern bool midiconfig_offset_ok;
#define FLASH_MIDICONFIG_OFFSET ((uint32_t)ADDR_MIDICONFIG - XIP_BASE)
/* One save = one whole sector (the blob does not fit in a 256B page like
 * Config's does), 16 sectors round-robin = 64KB, matches __MIDI_STORAGE_LEN
 * in the linker scripts. */
#define MIDICONFIG_SAVE_SLOTS 16
#define MIDICONFIG_SLOT_SIZE  FLASH_SECTOR_SIZE

/* Max config size = 256 Bytes == FLASH_PAGE_SIZE (FLASH_SECTOR_SIZE / 16 config saves) */
#define CONFIG_SIZE (FLASH_SECTOR_SIZE / 16)

/* WiFi credentials and Bluetooth NSD settings live in their own page, well
 * away from Config: Config's full contents are serialised back to the
 * host by read_config() (any WebUSB page could read it), and a PSK must
 * never travel that path.
 *
 * Persistent region layout (usbsidpico-rp2040/-rp2350/-rp2350b.ld):
 *   quarter 0-2 - free
 *   quarter 3   - ADDR_CONFIG (16 x 256B slots), then ADDR_MIDICONFIG (16 x 4K slots, 64KB)
 * FLASH_NET_OFFSET below sits at quarter index 2, inside the unclaimed
 * first three quarters. */
#define FLASH_NET_OFFSET (FLASH_PERSISTENT_OFFSET + ((FLASH_PERSISTENT_SIZE / 4) * 2))
/* Same 16-slot wear-levelling shape as Config (CONFIG_SIZE/MIDICONFIG_SAVE_SLOTS) */
#define NETCONFIG_SAVE_SLOTS 16
#define NETCONFIG_SIZE       (FLASH_SECTOR_SIZE / 16) /* 256B page, same as Config */

typedef struct NetConfig {
  uint32_t magic;             /* MAGIC_SMOKE, same wipe-on-rebuild semantics as Config */
  uint8_t  version;           /* Internal version control */
  uint8_t  save_id;           /* 16-slot wear levelling, mirrors load_config()/save_config() */
  char     ssid[33];          /* None until user sets one */
  char     psk[64];           /* NEVER serialised back to the host, see read_config() */
  char     hostname[24];      /* discovery + LWIP_NETIF_HOSTNAME, default "USBSID-Pico" */
  uint16_t nsd_port;          /* default 6581 */
  struct {
    bool wifi_enabled      : 1;
    bool nsd_enabled       : 1;
    bool bt_nsd_enabled    : 1;
    bool discovery_enabled : 1;
    uint8_t usbnet_ip_mode : 2;  /* USB net only: USBNET_IP_MODE_* */
  } flags;
#if defined(USE_USB_NET)
  /* USB net static mode addresses, dotted order, 0.0.0.0 = unset */
  uint8_t usbnet_ip[4];
  uint8_t usbnet_netmask[4];
  uint8_t usbnet_gateway[4];  /* 0.0.0.0 = no gateway */
#endif /* USE_USB_NET */
} NetConfig;

/* NetConfig.flags.usbnet_ip_mode values.
 * DHCP server (factory default): board 192.168.65.1/24, serves USB link only
 * DHCP broadcasts never cross host routing or proxy ARP.
 * DHCP client: needs a host bridge from USB link to LAN.
 * Static: needs host routing, proxy ARP or a bridge.
 * Do not bridge the USB network interface into a LAN that has its own DHCP
 * server while in DHCP server mode.
 * USBNET_IP_MODE_* ids: see usbsid_protocol.h. */

/* Factory default IP mode, 0 on builds without USB net (flags byte unchanged) */
#if defined(USE_USB_NET)
#define USBNET_IP_MODE_DEFAULT USBNET_IP_MODE_DHCP_SERVER
#else
#define USBNET_IP_MODE_DEFAULT USBNET_IP_MODE_DHCP_CLIENT
#endif

#define NETCONFIG_DEFAULT_INIT { \
  .magic = MAGIC_SMOKE, \
  .version = 1, \
  .save_id = 0, \
  .ssid = {0}, \
  .psk = {0}, \
  .hostname = "USBSID-Pico", \
  .nsd_port = 6581, \
  .flags = { \
    .wifi_enabled = false, \
    .nsd_enabled = true, \
    .bt_nsd_enabled = false, \
    .discovery_enabled = true, \
    .usbnet_ip_mode = USBNET_IP_MODE_DEFAULT, \
  }, \
}

/* USBSID-Pico config struct */
typedef struct SIDChip {
  uint8_t id;    /* ID for this SID 0 ~ 4, 255 = disabled */
  uint8_t addr;  /* Starting address to use for this SID, 255 = disabled */
  uint8_t type;  /* 0 = unknown, 1 = N/A, 2 = MOS8580, 3 = MOS6581, 4 = FMopl */
} SIDChip;

typedef struct Socket {
  uint8_t chiptype;     /* 0 = real, 1 = unknown, 2...etc see config_logging.c */
  SIDChip sid1;
  SIDChip sid2;
  bool    enabled : 1;  /* enable / disable this socket */
  bool    dualsid : 1;  /* enable / disable dual SID support for this socket (requires clone) */
} Socket;

/* NOTE: WiFi/Bluetooth/NSD are explicitely not set in this config */
typedef struct Config { // TODO: Add overrides for detect_default_config and add 5v/9v/12 overrides to make them unchangeable
  /* First three items must stay in the same order! */
  uint32_t magic;                /* Contains firmware build magic */
  int      default_config;       /* Defines if config is default config or not */
  uint8_t  config_saveid;        /* What the nth position is in flash where this config comes from */
  /* Don't care from here */
  uint32_t clock_rate;           /* clock speed identifier */
  uint16_t refresh_rate;         /* refresh rate identifier based on clockspeed ~ not configurable */
  uint16_t raster_rate;          /* raster rate identifier based on clockspeed ~ not configurable */
  uint8_t  last_preset;          /* last applied preset */
  Socket   socketOne;            /* 1 */
  Socket   socketTwo;            /* 2 */
  struct {
    bool enabled : 1;
    bool idle_breathe : 1;
  } LED;                         /* 3 */
  struct {
    uint8_t brightness;
    int     sid_to_use;          /* 0/-1 = off, 1...4 = sid 1 ... sid 4 */
    bool    enabled : 1;
    bool    idle_breathe : 1;
  } RGBLED;                      /* 4 */
  struct {
    bool enabled : 1;            /* Cannot be disabled */
  } Cdc;                         /* 5 */
  struct {
    bool enabled : 1;            /* Cannot be disabled */
  } WebUSB;                      /* 6 */
  struct {
    bool enabled : 1;
    /* bool buffered : 1; */           /* Enable/Disable ASID buffering by default (protocal can enable this) */
  } Asid;                        /* 7 */
  struct {
    bool enabled : 1;
    uint8_t emulator_app : 3;    /* ONBOARD_EMULATOR: c64app_t, 0 Cynthcart, 1 SID SYNTH. Shares the byte with enabled, old saves read 0 */
  } Midi;                        /* 8 */
  struct {
    int sidno;                   /* 0 = disabled, saves the sidno of the sid set to FMOpl */
    bool enabled : 1;            /* Requires a clone SID! */
  } FMOpl;                       /* 9 */
  bool external_clock : 1;       /* enable / disable external oscillator */
  bool lock_clockrate : 1;       /* lock the set clockspeed from being changed */
  bool stereo_en : 1;            /* (PCB v1.3+) audio switch is off (mono) or on (stereo) */
  bool lock_audio_sw : 1;        /* (PCB v1.3+) lock the audio switch into it's current stateand prevent it from being changed */
  bool mirrored : 1;             /* act as socket 1 */
  bool flipped : 1;              /* socket 1 is socket 2 and vice versa */
  bool mixed : 1;                /* addresses are mixed up (quad SID only!) */
  bool need_confirmation : 1;    /* (PCB v1.5+) current configuration needs confirmation, SID's are disabled until confirmed! */
  bool socket_change_detect : 1; /* (PCB v1.5+) disables socket change detection on boot */
  bool preset_auto_detect : 1;   /* disables silent auto detection before present change, default disabled on v1.0~v1.3, default enabled on v1.5+ */
  bool board_clock_link : 1;     /* uses GPIO17 as external clock detection, PHI1 wil generate based on the inpu */
  bool rx_ring : 1;              /* USB RX ringbuffer, latched at boot (save and reboot to apply) */
} Config;

/* Fixed defaults that differ between Pico1 and Pico2 */
#if PICO_RP2350
#define USBSID_CLOCK_RATE_DEFAULT    PAL
#define USBSID_REFRESH_RATE_DEFAULT  HZ50
#define USBSID_RASTER_RATE_DEFAULT   R_PAL
#else
#define USBSID_CLOCK_RATE_DEFAULT    DEFAULT
#define USBSID_REFRESH_RATE_DEFAULT  HZ_DEFAULT
#define USBSID_RASTER_RATE_DEFAULT   R_DEFAULT
#endif

/* Fixed defaults that differ between PCB versions */
#if PCB_VERSION_INT >= 15
#define PRESET_AUTO_DETECT_DEFAULT true
#else
#define PRESET_AUTO_DETECT_DEFAULT false
#endif

#define USBSID_DEFAULT_CONFIG_INIT { \
  .magic = MAGIC_SMOKE, \
  .default_config = true, \
  .config_saveid = 0, \
  .clock_rate = USBSID_CLOCK_RATE_DEFAULT, \
  .refresh_rate = USBSID_REFRESH_RATE_DEFAULT, \
  .raster_rate = USBSID_RASTER_RATE_DEFAULT, \
  .last_preset = 2, /* PRESET_DUAL_BOTH */ \
  .socketOne = { \
    .chiptype = 1,  /* unknown */ \
    .sid1 = { \
      .id = 0, \
      .addr = 0x00, \
      .type = 0,  /* unknown */ \
    }, \
    .sid2 = { \
      .id = 0xFF, \
      .addr = 0xFF, \
      .type = 1,  /* n/a */ \
    }, \
    .enabled = true, \
    .dualsid = false, \
  }, \
  .socketTwo = { \
    .chiptype = 1,  /* unknown */ \
    .sid1 = { \
      .id = 1, \
      .addr = 0x20, \
      .type = 0,  /* unknown */ \
    }, \
    .sid2 = { \
      .id = 0xFF, \
      .addr = 0xFF, \
      .type = 1,  /* n/a */ \
    }, \
    .enabled = true, \
    .dualsid = false, \
  }, \
  .LED = { \
    .enabled = true, \
    .idle_breathe = LED_PWM \
  }, \
  .RGBLED = { \
    .brightness = (RGB_ENABLED ? 0x7F : 0),  /* Half of max brightness or disabled if no RGB LED */ \
    .sid_to_use = (RGB_ENABLED ? 1 : -1), \
    .enabled = RGB_ENABLED, \
    .idle_breathe = RGB_ENABLED, \
  }, \
  .Cdc = { \
    .enabled = true \
  }, \
  .WebUSB = { \
    .enabled = true \
  }, \
  .Asid = { \
    .enabled = true, \
    /* .buffered = false */ \
  }, \
  .Midi = { \
    .enabled = true, \
    .emulator_app = 0, \
  }, \
  .FMOpl = { \
    .sidno = 0, \
    .enabled = false, \
  }, \
  .external_clock = false, \
  .lock_clockrate = false, \
  .stereo_en = false, \
  .lock_audio_sw = false, \
  .mirrored = false, \
  .flipped = false, \
  .mixed = false, \
  .need_confirmation = false, \
  .socket_change_detect = true, /* WARNING: This setting _can_ and _will_ fry your 9v SID if config is set to 6581 (12v) */ \
  .preset_auto_detect = PRESET_AUTO_DETECT_DEFAULT, /* Default disabled on v1.0~v1.3, default enabled on v1.5+ */ \
  .board_clock_link = false, /* Disabled by default */ \
  .rx_ring = true, /* Enabled by default */ \
} \

typedef struct RuntimeCFG {

  /* Number of SID's available */
  uint8_t sids_one;    /* config_socket.c:apply_socket_config() */
  uint8_t sids_two;    /* config_socket.c:apply_socket_config() */
  uint8_t numsids;     /* config_socket.c:apply_socket_config() */

  /* Chip & SID type access */
  uint8_t chip_one;    /* config_socket.c:apply_socket_config() */
  uint8_t chip_two;    /* config_socket.c:apply_socket_config() */
  uint8_t fmopl_sid;   /* config_socket.c:apply_fmopl_config() */

  /* RW, CS1, CS2 mask */
  uint8_t one;         /* config_bus.c:apply_bus_config() */
  uint8_t two;         /* config_bus.c:apply_bus_config() */
  uint8_t three;       /* config_bus.c:apply_bus_config() */
  uint8_t four;        /* config_bus.c:apply_bus_config() */

  /* Address mask */
  uint8_t one_mask;    /* config_bus.c:apply_bus_config() */
  uint8_t two_mask;    /* config_bus.c:apply_bus_config() */
  uint8_t three_mask;  /* config_bus.c:apply_bus_config() */
  uint8_t four_mask;   /* config_bus.c:apply_bus_config() */

  /* Max 4 sids {S1, S2, S3, S4} defaults to 'unknown' */
  uint8_t sidtype[4];  /* config_bus.c:apply_bus_config() -> used for RGB LED verification */
  /* contains the configured sid order to physical sid order relation */
  uint8_t ids[4];      /* config_bus.c:apply_bus_config() */
  /* contains the physical sid order */
  uint8_t sidid[4];    /* config_bus.c:apply_bus_config() */
  /* contains the sid address in configured sid order */
  uint8_t sidaddr[4];  /* config_bus.c:apply_bus_config() -> used in Midi handler! */
  /* contains the sid address masks */
  uint8_t sidmask[4];  /* config_bus.c:apply_bus_config() */
  uint8_t addrmask[4]; /* config_bus.c:apply_bus_config() */

  /* Check values */
  bool sock_one : 1;       /* config_socket.c:apply_socket_config() */
  bool sock_two : 1;       /* config_socket.c:apply_socket_config() */
  bool sock_one_dual : 1;  /* config_socket.c:apply_socket_config() */
  bool sock_two_dual : 1;  /* config_socket.c:apply_socket_config() */
  bool mirrored : 1;       /* config_socket.c:apply_socket_config() */
  bool flipped : 1;        /* config_socket.c:apply_socket_config() */
  bool mixed : 1;          /* config_socket.c:apply_socket_config() */
  bool fmopl_enabled : 1;  /* config_socket.c:apply_fmopl_config() */

} RuntimeCFG;

/* Firmware-only config sub-commands, protocol ids live in usbsid_protocol.h */
enum
{
  TEST_FN          = 0x99,  /* TODO: Remove before v1 release */
#if defined(ONBOARD_EMULATOR)
  TEST_FN2         = 0xEF,  /* TODO: Remove before v1 release */
#endif
};

/* Global variables from config.c */
extern Config        usbsid_config;
extern RuntimeCFG    cfg;
extern ConfigError   err;
extern volatile bool first_boot;
/* Global versioning variables */
extern const char    *us_product;
extern const char    *project_version;
extern const char    *pcb_version;

/**
 * @brief USBSID-Pico PCB and Firmware features packed in a single byte
 * 1 Byte == 8 bits == 8 features to define
 *   76543210
 * 0b00000000 = default at source, compilation defines what is and is not enabled
 * 0 = Pico type: 0 Pico1, 1 Pico2
 * 1 = Unused
 * 2 = RGB LED onboard: 0 No, 1 Yes
 * 3 = PIO Uart: 0 No, 1 Yes
 * 4 = Wifi & Bluetooth onboard: 0 No, 1 Yes
 * 5 = Network SID Device: 0 No, 1 Yes
 * 6 = SID Stream: 0 No, 1 Yes
 *     An extra Vendor interface is available for reading a SID register stream
 * 7 = Embedded SID player & Emulator: 0 No, 1 Yes
 */
static const uint8_t us_features = (
  0b00000000 /* 0 */
#if defined(PICO_RP2350)
  | (1 << 0)
#endif
  /* 1 Unused */
#if defined(USE_RGB)
  | (1 << 2)
#endif
#if defined(USE_PIO_UART)
  | (1 << 3)
#endif
#if defined(USE_NET) || defined(USE_USB_NET) /* Also enables WIFI_READ_IP command */
  | (1 << 4)
#endif
#if defined(USE_NSD) /* Also enables DISCOVERY_ENABLE command */
  | (1 << 5)
#endif
#if defined(USE_SID_STREAM)  /* SID register stream */
  | (1 << 6)
#endif
#if defined(ONBOARD_EMULATOR)
  | (1 << 7)
#endif
);

/* Const feature variables */
static const bool is_rp2350     = (us_features & US_FEATURE_RP2350);    /* 0 0b00000001 */
static const bool has_rgb_vu    = (us_features & US_FEATURE_RGBVU);     /* 2 0b00000100 */
static const bool has_pio_uart  = (us_features & US_FEATURE_PIOUART);   /* 3 0b00001000 */
static const bool has_net       = (us_features & US_FEATURE_NET);       /* 4 0b00010000 */
static const bool has_nsd       = (us_features & US_FEATURE_NSD);       /* 5 0b00100000 */
static const bool has_sidstream = (us_features & US_FEATURE_SIDSTREAM); /* 6 0b01000000 */
static const bool has_emulator  = (us_features & US_FEATURE_EMULATOR);  /* 7 0b10000000*/

/* Functions from config.c */
void        write_back_data(size_t buffersize);
bool        config_unacknowledged(void);
void        load_config(Config *config);
void        verify_midiconfig_offset(void);
void        save_config_ext(void);
void        handle_config_request(uint8_t *buffer, uint32_t size);
bool        config_is_query(uint8_t cmd);
void        print_config(void);
ConfigError apply_new_presetconfig(void);
ConfigError apply_config(bool at_boot);
void        detect_default_config(void);
int         return_clockrate(void);
void        apply_clockrate(int n_clock, bool suspend_sids);
void        save_load_config(void);
void        save_load_apply_config(bool at_boot);
void        verify_clockrate(void);

/* Defined in net_config.c, declared in this header since callers throughout this
 * file need it and net_config.h doesn't include config.h */
extern NetConfig net_cfg;

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_CONFIG_H_ */
