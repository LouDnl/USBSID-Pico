/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config.c
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

#include <globals.h>
#include <usbsid.h>
#include <usbsid_constants.h>
#include <config.h>
#include <gpio.h>
#include <midi.h>
#include <midi_config.h>
#include <sid.h>
#include <bus.h>
#include <dma.h>
#include <pio.h>
#include <mcu.h>
#include <sid_detection.h>
#include <sid_cloneconfig.h>
#include <sid_tests.h>
#include <config_bus.h>
#include <config_socket.h>
#include <config_logging.h>
#include <logging.h>
#ifdef USE_NET
#include <net_wifi.h>
#include <net_config.h>
#include <bluetooth.h>
#ifdef USE_NSD
#include <nsd.h>
#endif /* USE_NSD */
#endif /* USE_NET */

/* SID player */
#if defined(ONBOARD_EMULATOR)
#include <sid_player.h>
#include <usplayer.h>
static int sidbytes_received = 0;
static bool receiving_sidfile = 0;
#endif /* ONBOARD_EMULATOR */

/* Declare variables */
static const Config usbsid_default_config = USBSID_DEFAULT_CONFIG_INIT;
Config usbsid_config = {0};
RuntimeCFG cfg = {0};
ConfigError err = 0;
volatile bool first_boot = false;
const char __in_flash("us_vars") *project_version = PROJECT_VERSION;
const char __in_flash("us_vars") *pcb_version = PCB_VERSION;
const char __in_flash("us_vars") *us_product = USBSID_PRODUCT;

/* Declare local variables */
/* 0x15 (16) max before starting at 0 flash sector erase */
static uint8_t config_saveid = 0;
/* 256 Bytes MAX == FLASH_PAGE_SIZE (Max storage size is 4096 bytes == FLASH_SECTOR_SIZE) */
static uint8_t config_array[FLASH_PAGE_SIZE] = {0};
/* 12 bytes and counting */
static uint8_t socket_config_array[SOCKET_BUFFER_SIZE];
/* 256 Bytes MAX == FLASH_PAGE_SIZE */
static uint8_t net_config_array[FLASH_PAGE_SIZE];
/* 64 byte array for copying the config version into */
static uint8_t p_version_array[MAX_BUFFER_SIZE] = {0};
/* Config magic verification storage */
static uint32_t cm_verification = 0;
/* Number of writes to host we need to do on a config read depending on the config_array size */
static int cfg_read_writes = 0;
/* Number of writes to host we need to do on a config read depending on the net_config_array size */
static int net_cfg_read_writes = 0;
/* Temporary buffer to store incoming data */
uint8_t * data_buffer = NULL;
/* Well, how big is it? */
volatile int data_buffer_size = 0;
/* Is it ok? */
bool midiconfig_offset_ok = false;


/**
 * @brief Write back data to USB CDC or Vendor
 *
 * @param size_t buffersize
 */
void write_back_data(size_t buffersize)
{
  switch (rtype) {
    case 'C':
      cdc_write(cdc_itf, buffersize);
      break;
    case 'W':
      webserial_write(wusb_itf, buffersize);
      break;
  }
  return;
}

/**
 * @brief Returns true if either of the variables is true
 *
 * @return bool
 */
bool config_unacknowledged(void)
{
#if PCB_VERSION_INT >= 15
  return (usbsid_config.need_confirmation || detected_sid_change);
#else
  return false;
#endif
}

/**
 * @brief Dispatch an incoming WRITE_CONFIG buffer to its config type handler
 *
 * @note 64 byte buffer before reaching this function:
 * @note { CMD, CFGCMD, INIT, VER, DATA ... 58 bytes max, VER, END }
 * @note 62 byte buffer reaching this function:
 * @note { INIT, VER, DATA ... 58 bytes max, VER, END }
 * @note if more then 64 bytes, VER and END are not in this packet
 * @note the FULL_CONFIG / SOCKET_CONFIG / MIDI_CONFIG / MIDI_CCVALUES cases
 *       are currently unimplemented stubs
 *
 * @param uint8_t * buffer
 * @param uint32_t size
 */
void handle_config_buffer(uint8_t * buffer, uint32_t size)
{
  print_cfg(buffer, size, false);
  /* TODO: Add something along the line
     like this: sidfile = (uint8_t*)calloc(1, 0x10000); */
  switch (buffer[1]) {
    case FULL_CONFIG: break;
    case SOCKET_CONFIG: break;
    case MIDI_CONFIG: break;
    case MIDI_CCVALUES: break;
    case NETWORK_CONFIG: break;
    default:
      break;
  }
  return;
}

/**
 * @brief Encode a Config struct into the 64 byte READ_CONFIG wire buffer
 *
 * Fills the static `config_array` with the fields the host expects for a
 * READ_CONFIG response (clock, socket one/two, LED, RGBLED, feature enable
 * flags, audio switch, mirrored/flipped/mixed bits, terminator bytes), and
 * scans the array for the 0x8F/0xFF end marker to compute `cfg_read_writes`,
 * the number of 64 byte USB writes needed to send it back.
 *
 * @param Config* config
 */
void read_config(Config* config)
{
  memset(config_array, 0, sizeof config_array);  /* Make sure we don't send garbled old data */

  usCFG("Reading configuration from 0x%x to 0x%x with size %u\n",
    (uint)config, &config_array, sizeof(Config));

  config_array[0] = READ_CONFIG;       /* Initiator byte */
  config_array[1] = VERIFICATION_BYTE; /* Verification byte */

  config_array[2] = (int)config_unacknowledged();      /* (PCB v1.5+) Need configuration confirmation */
  config_array[3] = (int)config->socket_change_detect; /* (PCB v1.5+) Socket change detection */
  config_array[4] = (int)(config->last_preset & 0x7f); /* The last applied preset */
  config_array[4] |=
    (int)((int)(config->preset_auto_detect << 7) & 0x80);   /* Socket auto detection during preset applying */

  /* Clockworx */
  config_array[5] = (int)config->lock_clockrate;
  config_array[6] = (int)config->external_clock;
  config_array[7] = (config->clock_rate >> 16) & BYTE;
  config_array[8] = (config->clock_rate >> 8) & BYTE;
  config_array[9] = config->clock_rate & BYTE;
  /* Socket One */
  config_array[10] = (int)config->socketOne.enabled;
  config_array[11] = (int)config->socketOne.dualsid;
  config_array[12] = config->socketOne.chiptype;
  config_array[13] = (config->socketOne.sid1.id | (config->socketOne.sid2.id << 4));
  config_array[14] = config->socketOne.sid1.type;
  config_array[15] = config->socketOne.sid2.type;

  config_array[16] = 0x00; /* Unused */
  config_array[17] = 0x00; /* Unused */
  config_array[18] = 0x00; /* Unused */
  config_array[19] = 0x00; /* Unused */

  /* Socket Two */
  config_array[20] = (int)config->socketTwo.enabled;
  config_array[21] = (int)config->socketTwo.dualsid;
  config_array[22] = (int)config->mirrored; /* NOTE: Pre v0.7.0 fw backwards compatibility */
  config_array[23] = config->socketTwo.chiptype;
  config_array[24] = (config->socketTwo.sid1.id | (config->socketTwo.sid2.id << 4));
  config_array[25] = config->socketTwo.sid1.type;
  config_array[26] = config->socketTwo.sid2.type;

  config_array[27] = 0x00; /* Unused */
  config_array[28] = 0x00; /* Unused */
  config_array[29] = 0x00; /* Unused */

  /* Bright light, bright light!! */
  config_array[30] = (int)config->LED.enabled;
  config_array[31] = (int)config->LED.idle_breathe;

  config_array[32] = 0x00; /* Unused */
  config_array[33] = 0x00; /* Unused */
  config_array[34] = 0x00; /* Unused */
  config_array[35] = 0x00; /* Unused */
  config_array[36] = 0x00; /* Unused */
  config_array[37] = 0x00; /* Unused */
  config_array[38] = 0x00; /* Unused */
  config_array[39] = 0x00; /* Unused */

  /* I can see colors! */
  config_array[40] = (int)config->RGBLED.enabled;
  config_array[41] = (int)config->RGBLED.idle_breathe;
  config_array[42] = config->RGBLED.brightness;
  config_array[43] = (int)config->RGBLED.sid_to_use;

  config_array[44] = 0x00; /* Unused */
  config_array[45] = 0x00; /* Unused */
  config_array[46] = 0x00; /* Unused */
  config_array[47] = 0x00; /* Unused */
  config_array[48] = 0x00; /* Unused */
  config_array[49] = 0x00; /* Unused */
  config_array[50] = 0x00; /* Unused */

  /* Stuffs */
  config_array[51] = (int)config->Cdc.enabled;
  config_array[52] = (int)config->WebUSB.enabled;
  config_array[53] = (int)config->Asid.enabled;
  config_array[54] = (int)config->Midi.enabled;

  /* Stuff for keyboard people */
  config_array[55] = (int)config->FMOpl.enabled;
  config_array[56] = config->FMOpl.sidno;

  /* Headphonez on! */
  config_array[57] = config->stereo_en;
  config_array[58] = config->lock_audio_sw;

  config_array[59] = 0x00; /* Unused */

  config_array[60] = ((int)config->mirrored | ((int)config->flipped << 1) | ((int)config->mixed << 2));

  config_array[61] = 0x00; /* Unused */

  config_array[62] = END_BYTE; /* Mark end of config */
  config_array[63] = TERMINATION_BYTE; /* Terminator byte */

  /* Yes, I know, I can assign cfg_read_writes manually, but I do not want to keep wondering if I need to change it */
  for (int n = 0; n < count_of(config_array); n++) { /* Find number of writes by terminator byte */
    static int endbyte = 0;
    if (config_array[n] == END_BYTE) { endbyte = n;
      if (config_array[n+1] == 0xFF) {
        /* n = 62, 62+2 = 64, 64/64 = 1 */
        /* n = 117, 117+2 = 119, 119/64 = 1 */
        cfg_read_writes = ROUND((n+2)/64);
        break;
      }
    }
  }

  return;
}

/**
 * @brief Encode a Config struct into the 12 byte READ_SOCKETCFG wire buffer
 *
 * Fills the static `socket_config_array` with the socket one/two enabled,
 * dualsid, chiptype and SID type/id fields plus the mirrored/flipped/mixed
 * bits, for the READ_SOCKETCFG response.
 *
 * @param Config* config
 */
void read_socket_config(Config* config)
{
  memset(socket_config_array, 0, sizeof socket_config_array);  /* Make sure we don't send garbled old data */

  socket_config_array[0]  = READ_SOCKETCFG;    /* Initiator byte */
  socket_config_array[1]  = VERIFICATION_BYTE; /* Verification byte */

  socket_config_array[2]  = ((int)config->socketOne.enabled << 4) | (int)config->socketOne.dualsid;
  socket_config_array[3]  = (int)config->socketOne.chiptype;
  socket_config_array[4]  = (config->socketOne.sid1.type << 4) | config->socketOne.sid2.type;

  socket_config_array[5]  = ((int)config->socketTwo.enabled << 4) | (int)config->socketTwo.dualsid;
  socket_config_array[6]  = (int)config->socketTwo.chiptype;
  socket_config_array[7]  = (config->socketTwo.sid1.type << 4) | config->socketTwo.sid2.type;

  socket_config_array[8]  = ((int)config->socketOne.sid1.id | ((int)config->socketOne.sid2.id << 4));
  socket_config_array[9]  = ((int)config->socketTwo.sid1.id | ((int)config->socketTwo.sid2.id << 4));

  socket_config_array[10] = ((int)config->mirrored | ((int)config->flipped << 1) | ((int)config->mixed << 2));

  socket_config_array[11] = TERMINATION_BYTE; /* Terminator byte */

  return;
}

/**
 * @brief Encode a NetConfig struct into the 64 byte READ_NETCFG wire buffer
 *
 * Fills the static `net_config_array` with the fields the host expects for a
 * READ_NETCFG response and scans the array for the 0x8F/0xFF end marker to
 * compute `net_cfg_read_writes`,the number of 64 byte USB writes needed to
 * send it back.
 *
 * @param NetConfig* config
 */
void read_net_config(NetConfig* config)
{
  memset(net_config_array, 0, sizeof net_config_array);  /* Make sure we don't send garbled old data */

  net_config_array[0]  = READ_NETCFG;       /* Initiator byte */
  net_config_array[1]  = VERIFICATION_BYTE; /* Verification byte */

  memcpy(&net_config_array[2], config->ssid, count_of(net_cfg.ssid)); /* 33 */
  memcpy(&net_config_array[35], config->hostname, count_of(net_cfg.hostname)); /* 24 */

  net_config_array[59] = (config->nsd_port & BYTE); /* Port Lo */
  net_config_array[60] = ((config->nsd_port >> 8) & BYTE); /* Port Hi */
  net_config_array[61]  = (
    config->flags.wifi_enabled
  | (config->flags.nsd_enabled << 1)
  | (config->flags.bt_nsd_enabled << 2)
  | (config->flags.discovery_enabled << 3));

  net_config_array[62] = END_BYTE; /* Mark end of net_config */
  net_config_array[63] = TERMINATION_BYTE; /* Terminator byte */

  /* Yes, I know, I can assign net_cfg_read_writes manually, but I do not want to keep wondering if I need to change it */
  for (int n = 0; n < count_of(net_config_array); n++) { /* Find number of writes by terminator byte */
    static int endbyte = 0;
    if (net_config_array[n] == END_BYTE) { endbyte = n;
      if (net_config_array[n+1] == 0xFF) {
        /* n = 62, 62+2 = 64, 64/64 = 1 */
        /* n = 117, 117+2 = 119, 119/64 = 1 */
        net_cfg_read_writes = ROUND((n+2)/64);
        break;
      }
    }
  }

  return;
}

/**
 * @brief Encode the firmware version string into `p_version_array`
 *
 * Writes { USBSID_VERSION, length, version string bytes } for the
 * USBSID_VERSION large-write response.
 */
void read_firmware_version(void)
{
  p_version_array[0] = USBSID_VERSION;  /* Initiator byte */
  p_version_array[1] = strlen(project_version);  /* Length of version string */
  memcpy(p_version_array+2, project_version, strlen(project_version));
  return;
}

/**
 * @brief Encode the PCB version string into `p_version_array`
 *
 * Writes { US_PCB_VERSION, length, version string bytes } for the
 * US_PCB_VERSION large-write response.
 */
void read_pcb_version(void)
{
  p_version_array[0] = US_PCB_VERSION;  /* Initiator byte */
  p_version_array[1] = strlen(pcb_version);  /* Length of version string */
  memcpy(p_version_array+2, pcb_version, strlen(pcb_version));
  return;
}

/**
 * @brief Reset a Config struct to the compiled-in default configuration
 *
 * Preserves the config's `config_saveid` across the reset so the flash
 * save slot bookkeeping stays intact.
 *
 * @param Config* config
 */
void __no_inline_not_in_flash_func(default_config)(Config* config)
{
  config_saveid = config->config_saveid;  /* Preserve config saveid */
  memcpy(config, &usbsid_default_config, sizeof(Config));
  config->config_saveid = config_saveid;  /* Copy saveid back into the default config */
  return;
}

/**
 * @brief Cross-check the MIDI partition's address against the existing
 *        Config partition's address
 *
 * `ADDR_CONFIG` (linker symbol, in the .ld scripts) and `FLASH_CONFIG_OFFSET`
 * (the macro above, derived at compile time from `PICO_FLASH_SIZE_BYTES`)
 * compute the identical address two different ways. They have to agree by
 * construction; this just says so out loud at boot, before anything ever
 * writes to the MIDI partition that sits directly after both of them. A
 * mismatch here would mean the two build-time views of the flash layout
 * have drifted apart, which is exactly the kind of thing that must be
 * caught before a flash_range_erase() runs anywhere near it.
 */
void verify_midiconfig_offset(void)
{
  uint32_t linker_config_offset = (uint32_t)ADDR_CONFIG - XIP_BASE;
  usNFO("\n");
  usCFG("MIDI storage:\n");
  usCFG("  FLASH_CONFIG_OFFSET = 0x%X\n", FLASH_CONFIG_OFFSET);
  usCFG("  linker ADDR_CONFIG offset = 0x%X\n", linker_config_offset);
  usCFG("  FLASH_MIDICONFIG_OFFSET = 0x%X\n", FLASH_MIDICONFIG_OFFSET);
  if (linker_config_offset != FLASH_CONFIG_OFFSET) {
    usERR("MIDI storage: ADDR_CONFIG (linker) 0x%X != FLASH_CONFIG_OFFSET (macro) 0x%X - refusing to trust MIDI flash offsets!\n",
      linker_config_offset, FLASH_CONFIG_OFFSET);
    midiconfig_offset_ok = false;
  } else {
    midiconfig_offset_ok = true;
  }
  return;
}

/**
 * @brief Load the most recently saved configuration from flash
 *
 * Walks the flash save slots (`FLASH_PAGE_SIZE` apart, up to 16 slots)
 * starting at slot 0, following the `config_saveid` chain until it finds a
 * slot whose stored id no longer matches its position; that means the
 * previous slot holds the latest saved config, which is then copied into
 * `*config`. Falls back to `default_config()` if the loaded config's magic
 * number does not match `MAGIC_SMOKE`.
 *
 * @note must not log directly after the flash memcpy without an
 *       stdio_flush() first, or the Pico will freeze
 *
 * @param Config* config
 */
void __no_inline_not_in_flash_func(load_config)(Config* config)
{
  print_cfg_addr();
  usNFO("\n");
  usCFG("Loading configuration from flash\n");
  uint8_t savelocationid = 0;  /* counter for finding the current save location */
AGAIN:
  Config temp_config;
  usCFG("  Trying flash position '%u'", savelocationid);
  /* NOTICE: Do not do any logging directly after memcpy without stdio_flush or the Pico will freeze! */
  memcpy(&temp_config, (void *)(XIP_BASE + (FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(Config));
  stdio_flush();
  usNFO(", flash contains config id '%u' (255 == empty slot)\n",
     temp_config.config_saveid);
  if ((temp_config.config_saveid >= 0) && (temp_config.config_saveid <= 0xF) /* Max 16 saves */
    && temp_config.config_saveid == savelocationid) { /* Found previously saved config */
    savelocationid++;  /* Increase id and try again */
    goto AGAIN; /* Always tries again if config is found */
  } else { /* They are not equal, that means this config is an empty save location or corrupted, load the previous config */
    savelocationid--; /* This will now be the previous config_saveid */
    usCFG("  Found %s configuration at position %u\n",
      ((savelocationid == 255) ? "empty" : "latest"), savelocationid);
    memcpy(config, (void *)(XIP_BASE + (FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(Config));
    stdio_flush();
  }
  /* Do not assign savelocationid here but he actual saveid from the restored config */
  config_saveid = usbsid_config.config_saveid;  /* copy saveid into global variable (for later saving use etc.) */
  usCFG("  %s loaded from position %u\n",
    (usbsid_config.config_saveid == 255 ? "Empty configuration" : "Configuration"),
    usbsid_config.config_saveid);

  usCFG("Copied Configuration:\n");
  usCFG("  From 0x%x\n",
    (FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * savelocationid)));
  usCFG("  To 0x%x\n",
    (uint)config);
  usCFG("  Size = %u\n",
     sizeof(Config));
  usCFG("Copy config addresses:\n");
  usCFG("  &usbsid_config = 0x%x\n",
    (uint)&usbsid_config);
  usCFG("  config = 0x%x\n",
    (uint)config);
  usCFG("  &config = 0x%x\n",
    (uint)&config);

  cm_verification = usbsid_config.magic;   /* Store the current magic for later */
  if (usbsid_config.magic != MAGIC_SMOKE) {  /* Verify the magic */
      usERR("MAGIC usbsid_config.magic: %u != MAGIC_SMOKE: %u\n", usbsid_config.magic, MAGIC_SMOKE);
      usCFG("Reset to default configuration!\n");
      default_config(config);
  }

  return;
}

/**
 * @brief Erase (if needed) and program one flash page with the config data
 *
 * Runs with interrupts disabled for the duration of the flash operation.
 * Erases the whole `FLASH_SECTOR_SIZE` sector only when `config_saveid` is
 * 0, to keep the number of flash erases low, then programs the
 * `FLASH_PAGE_SIZE` page at the current save slot.
 *
 * @note no logging in this function, to avoid errors while flash is busy
 * @note intended to be invoked via flash_safe_execute() from write_config()
 *
 * @param void* config_data
 */
void __no_inline_not_in_flash_func(write_config_lowlevel)(void* config_data)
{ /* No logging in this function to avoid errors */
  uint32_t ints = save_and_disable_interrupts();
  /* FLASH_SECTOR_SIZE = 4096 Bytes (sector aligend) erase as per SDK manual and
     only erase if saveid is zero to keep the number of flash erases low */
  if (config_saveid == 0x0) flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
  /* FLASH_PAGE_SIZE = 256 Bytes (page aligned) write as per SDK manual */
  flash_range_program((FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * config_saveid)), (uint8_t*)config_data, FLASH_PAGE_SIZE);
  restore_interrupts(ints);
  return;
}

/**
 * @brief Write a Config struct to flash via flash_safe_execute()
 *
 * Copies the config into a `CONFIG_SIZE` local buffer (statically asserted
 * to fit the whole struct), then runs write_config_lowlevel() through
 * flash_safe_execute() so the flash operation is safely coordinated with
 * core1, and sleeps 100ms afterwards.
 *
 * @param const Config* config
 */
void __no_inline_not_in_flash_func(write_config)(const Config* config)
{
  uint8_t config_data[CONFIG_SIZE] = {0};
  static_assert(sizeof(Config) < CONFIG_SIZE, "[CONFIG] SAVE ERROR: Config struct doesn't fit inside CONFIG_SIZE");
  memcpy(config_data, config, sizeof(Config));
  int err = flash_safe_execute(write_config_lowlevel, config_data, 100);
  if (err) {
    usERR("Saving configuration: %d\n", err);
  }
  sleep_ms(100);
  return;
}

/**
 * @brief Advance the save slot id and persist a Config struct to flash
 *
 * Only proceeds if `usbsid_config.config_saveid` and the local
 * `config_saveid` counter still agree; increments and wraps the id at 0xF
 * (16 slots), then calls write_config(). Logs an error and returns without
 * writing if the ids have gone out of sync.
 *
 * @param Config* config
 */
void __no_inline_not_in_flash_func(save_config)(Config* config)
{
  usNFO("\n");
  usCFG("Saving configuration:\n");
  /* Verify config saveid and increase */
  int noerr = (usbsid_config.config_saveid == config_saveid);
  if (noerr) {
    /* Only increase id's if both are equal */
    config_saveid++; /* If it was 255 this means first boot up, will automatically be increased to 0 */
    config_saveid = usbsid_config.config_saveid = (config_saveid <= 0xF ? config_saveid : 0);
    usCFG("  Config id = %d, saving to 0x%x (%u)\n", config_saveid,
      (FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * config_saveid)), (FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * config_saveid)));
  } else {
    usERR("Config save id's are not equal %d != %d. Unable to save configuration!!\n", usbsid_config.config_saveid, config_saveid);
    return;
  }
  write_config(config);
  usCFG("Configuration saved!\n");
  return;
}

/**
 * @brief Handles incoming config request buffers
 *
 * Dispatches on buffer[0] (the command byte) to the whole USB config/control
 * protocol: config read/write, presets, clock control, audio switch, SID
 * detection, clone chip config, SID player upload/transport, and test
 * commands. Two buffer layouts are used depending on the command:
 *
 * 5 bytes: Byte 0 command, Byte 1 struct setting (e.g. socketOne,
 * clock_rate) or additional command, Byte 2 setting entry (e.g. dualsid),
 * Byte 3 new value, Byte 4 reserved.
 *
 * >= 6 bytes: Byte 0 write command (e.g. WRITE_CONFIG), Byte 1 config type,
 * Byte 2..61 the data.
 *
 * @param uint8_t * buffer buffer of max 64 bytes
 * @param uint32_t size length of the buffer
 */
void handle_config_request(uint8_t * buffer, uint32_t size)
{
  if (buffer[0] < 0xD0) { /* Don't log incoming buffer to avoid spam above this region */
    usCFG("Incoming buffer: %x %x %x %x %x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
  }
  switch (buffer[0]) {
    case RESET_USBSID:
      usCFG("RESET_USBSID\n");
      mcu_reset();
      break;
    case READ_CONFIG:
      usCFG("READ_CONFIG\n");
      /* NOTICE: Vendor device (WebUSB) only:
         It should send 1 packet of 64 bytes (the size of the current config array),
         but sends 1 and a zero packet. This 0 packet must be accounted for in drivers
         */
      read_config(&usbsid_config);
      print_cfg(config_array, count_of(config_array), false);
      memset(write_buffer_p, 0, 64);
      /* Account for the Config array size with a loop */
      for (int i = 0; i < cfg_read_writes; i++) {
        usCFG("Write back config array part %d of %d\n", (i+1), cfg_read_writes); /* i+1 because humans count from 1 :) */
        memcpy(write_buffer_p, config_array + (i * 64), 64);
        write_back_data(64);
      }
      break;
    case READ_SOCKETCFG:
      usCFG("READ_SOCKETCFG\n");
      read_socket_config(&usbsid_config);
      print_cfg(socket_config_array, SOCKET_BUFFER_SIZE, false);
      memset(write_buffer_p, 0, 64);
      memcpy(write_buffer_p, socket_config_array, SOCKET_BUFFER_SIZE);
      write_back_data(SOCKET_BUFFER_SIZE);
      break;
    case READ_NETCFG:
    {
      usCFG("READ_NETCFG\n");
#ifdef USE_NET
      read_net_config(&net_cfg);
#else /* Write back default config (unchangeable ;) */
      static NetConfig ndc = NETCONFIG_DEFAULT_INIT;
      read_net_config(&ndc);
#endif /* USE_NET */
      print_cfg(net_config_array, count_of(net_config_array), false);
      memset(write_buffer_p, 0, 64);
      /* Account for the Config array size with a loop */
      for (int i = 0; i < net_cfg_read_writes; i++) {
        usCFG("Write back net config array part %d of %d\n", (i+1), net_cfg_read_writes); /* i+1 because humans count from 1 :) */
        memcpy(write_buffer_p, net_config_array + (i * 64), 64);
        write_back_data(64);
      }
      break;
    }
    case READ_NUMSIDS:
      usCFG("READ_NUMSIDS: %u\n", get_numsids());
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = get_numsids();
      write_back_data(1);
      break;
    case READ_FMOPLSID:
      usCFG("READ_FMOPLSID\n");
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = (uint8_t)cfg.fmopl_sid;
      write_back_data(1);
      break;
    case READ_CONFIGACK:
#if PCB_VERSION_INT >= 15
      usCFG("READ_CONFIGACK: %d\n", (int)config_unacknowledged());
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = (uint8_t)config_unacknowledged();
#else
      usCFG("READ_CONFIGACK Not supported\n");
      memset(write_buffer_p, 0, 64);
#endif
      write_back_data(1);
      break;
    case APPLY_CONFIG:
      usCFG("APPLY_CONFIG\n");
      apply_config(false); /* Not at boot */
      break;
    case RELOAD_CONFIG:
      usCFG("RELOAD_CONFIG\n");
      load_config(&usbsid_config);
      apply_config(false); /* Not at boot */
      for (uint i = 0; i < count_of(clockrates); i++) {
        if (clockrates[i] == usbsid_config.clock_rate) {
          apply_clockrate(i, true);
        }
      }
      break;
    case SET_CONFIG:
      usCFG("SET_CONFIG\n");
      switch (buffer[1]) {
        case BOARD_CLOCKRATE: /* clock_rate */
          /* will always be available to change the setting since it doesn't apply it */
          usbsid_config.clock_rate = clockrates[(int)buffer[2]];
          usbsid_config.refresh_rate = refreshrates[(int)buffer[2]]; /* Experimental */
          usbsid_config.raster_rate = rasterrates[(int)buffer[2]]; /* Experimental */
          if (buffer[3] == 0 || buffer[3] == 1) { /* Verify correct data */
            usbsid_config.lock_clockrate = (bool)buffer[3];
          }
          break;
        case SOCKET_ONE:      /* socketOne */
          switch (buffer[2]) {
            case CONFIG_ENABLED:  /* enabled */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.socketOne.enabled = (buffer[3] == 1) ? true : false;
              };
              break;
            case SOCKET_DUALSID:  /* dualsid */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.socketOne.dualsid = (buffer[3] == 1) ? true : false;
              };
              break;
            case SOCKET_CHIPTYPE: /* chiptype */
              if (buffer[3] < CHIP_COUNT) {
                usbsid_config.socketOne.chiptype = buffer[3];
              }
              break;
            case SOCKET_UNUSED:   /* UNUSED */
              break;
            case SOCKET_SID1TYPE: /* sid1.type */
              if (buffer[3] < SID_COUNT) {
                usbsid_config.socketOne.sid1.type = buffer[3];
                apply_fmopl_config(&cfg);  /* Keep FMOpl state in sync */
              }
              break;
            case SOCKET_SID2TYPE: /* sid2.type */
                if (buffer[3] < SID_COUNT) {
                  usbsid_config.socketOne.sid2.type = buffer[3];
                  apply_fmopl_config(&cfg);  /* Keep FMOpl state in sync */
                }
              break;
          };
          break;
        case SOCKET_TWO:      /* socketTwo */
          switch (buffer[2]) {
            case CONFIG_ENABLED:  /* enabled */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.socketTwo.enabled = (buffer[3] == 1) ? true : false;
              };
              break;
            case SOCKET_DUALSID:  /* dualsid */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.socketTwo.dualsid = (buffer[3] == 1) ? true : false;
              };
              break;
            case SOCKET_CHIPTYPE: /* chiptype */
              if (buffer[3] < CHIP_COUNT) {
                usbsid_config.socketTwo.chiptype = buffer[3];
              }
              break;
            case SOCKET_UNUSED:   /* UNUSED */
              break;
            case SOCKET_SID1TYPE: /* sid1.type */
              if (buffer[3] < SID_COUNT) {
                usbsid_config.socketTwo.sid1.type = buffer[3];
                apply_fmopl_config(&cfg);  /* Keep FMOpl state in sync */
              }
              break;
            case SOCKET_SID2TYPE: /* sid2.type */
              if (buffer[3] < SID_COUNT) {
                usbsid_config.socketTwo.sid2.type = buffer[3];
                apply_fmopl_config(&cfg);  /* Keep FMOpl state in sync */
              }
              break;
            case OLD_S2_MIRRORED: /* mirrored */ /* NOTE: Pre v0.7.0 fw backwards compatibility */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.mirrored = (buffer[3] == 1) ? true : false;
              };
              break;
          };
          break;
        case BOARD_LED:       /* LED */
          switch (buffer[2]) {
            case CONFIG_ENABLED: /* enabled */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.LED.enabled = (buffer[3] == 1) ? true : false;
              };
              break;
            case LED_IDLEBREATH: /* idle_breathe */
              if (buffer[3] <= 1) { /* 1 or 0 */
                if (LED_PWM) {
                  usbsid_config.LED.idle_breathe = (buffer[3] == 1) ? true : false;
                } else {
                  usbsid_config.LED.idle_breathe = false;  /* Always false, no PWM LED on PicoW :( */
                };
              };
              break;
            default:
              break;
          };
          break;
        case BOARD_RGBLED:    /* RGBLED */
          switch (buffer[2]) {
            case CONFIG_ENABLED: /* enabled */
              if (buffer[3] <= 1) { /* 1 or 0 */
                if (RGB_ENABLED) {
                  usbsid_config.RGBLED.enabled = (buffer[3] == 1) ? true : false;
                } else {
                  usbsid_config.RGBLED.enabled = false;  /* Always false if no RGB LED */
                };
              };
              break;
            case LED_IDLEBREATH: /* idle_breathe */
              if (buffer[3] <= 1) { /* 1 or 0 */
                if (RGB_ENABLED) {
                  usbsid_config.RGBLED.idle_breathe = (buffer[3] == 1) ? true : false;
                } else {
                  usbsid_config.RGBLED.idle_breathe = false;  /* Always false if no RGB LED */
                };
              };
              break;
            case LED_BRIGHTNESS: /* brightness */
              if (RGB_ENABLED) {
                usbsid_config.RGBLED.brightness = buffer[3];
              } else {
                usbsid_config.RGBLED.brightness = 0;  /* No brightness needed if no RGB LED */
              };
              break;
            case LED_SIDTOUSE:   /* sid_to_use */
              if (buffer[3] >= 1 && buffer[3] <= 4) {
                if (RGB_ENABLED) {
                  usbsid_config.RGBLED.sid_to_use = buffer[3];
                } else {
                  usbsid_config.RGBLED.sid_to_use = -1;
                };
              };
              break;
            default:
              break;
          }
          break;
        case BOARD_CDC:       /* CDC ~ Cannot be disabled */
            usNFO("[EASTER] Why you little!\n");
          break;
        case BOARD_WEBUSB:    /* WEBUSB ~ Cannot be disabled */
            usNFO("[EGG] H4ck3r b4by!\n");
          break;
        case BOARD_ASID:      /* ASID */
          usbsid_config.Asid.enabled = (bool)buffer[2];
          /* usbsid_config.Asid.buffered = (bool)buffer[3]; */
          break;
        case BOARD_MIDI:      /* MIDI */
          usbsid_config.Midi.enabled = (bool)buffer[2];
          break;
        case BOARD_FMOPL:     /* FMOpl */
          /* buffer[2] = enabled, buffer[3] = sidno 1~4 (optional when disabling)
           * Assigning the SID type is what enables FMOpl, the enabled flag on
           * its own is derived state and cannot be set independently */
          if (buffer[2] == 0) {
            set_fmopl_sidno(0);  /* Clear any assigned FMOpl SID */
          } else {
            set_fmopl_sidno((int)buffer[3]);
          }
          apply_fmopl_config(&cfg);
          break;
        case BOARD_AUSWITCH:  /* Audio switch */
          usbsid_config.stereo_en =
          (buffer[2] == 0 || buffer[2] == 1)
          ? (bool)buffer[2]
          : true;  /* Default to 1 ~ stereo if incorrect value */
          break;
        case BOARD_AULOCK:    /* Lock audio switch */
          usbsid_config.lock_audio_sw =
          (buffer[2] == 0 || buffer[2] == 1)
          ? (bool)buffer[2]
          : false;  /* Default to unlocked incorrect value */
          break;
        case BOARD_MIRRORED:  /* mirrored */
          if (buffer[2] <= 1) { /* 1 or 0 */
            usbsid_config.mirrored = (bool)buffer[2];
          };
          break;
        case BOARD_FLIPPED:   /* flipped */
          if (buffer[2] <= 1) { /* 1 or 0 */
            usbsid_config.flipped = (bool)buffer[2];
          };
          break;
        case BOARD_MIXED:     /* mixed */
          if (buffer[2] <= 1) { /* 1 or 0 */
            usbsid_config.mixed = (bool)buffer[2];
          };
          break;
        case BOARD_SDETECT:   /* Socket change autodetection */
          usbsid_config.socket_change_detect =
            (buffer[2] == 0 || buffer[2] == 1)
            ? (bool)buffer[2]
            : true;  /* Default to 1 ~ enabled, always autodetect */
          break;
        case BOARD_PADETECT:   /* Preset silent autodetection */
          usbsid_config.preset_auto_detect =
            (buffer[2] == 0 || buffer[2] == 1)
            ? (bool)buffer[2]
            : true;  /* Default to 1 ~ enabled, always autodetect */
          break;
        default:
          break;
      };
      break;
    case SAVE_CONFIG:
      usCFG("SAVE_CONFIG and RESET_MCU\n");
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      mcu_reset();
      break;
    case SAVE_NORESET:
      usCFG("SAVE_CONFIG no RESET\n");
      save_load_apply_config(false);
      break;
    case RESET_CONFIG:
      usCFG("RESET_CONFIG\n");
      default_config(&usbsid_config);
      save_config(&usbsid_config);
      if (buffer[1] != 0) {
        mcu_reset();
      } else {
        load_config(&usbsid_config);
        apply_config(false); /* Not at boot */
      }
      break;
    case WRITE_CONFIG: /* Incoming write from Host */ /* TODO: FINISH */
      /* Max size of incoming buffer = 61 */
      usCFG("WRITE_CONFIG\n");
      handle_config_buffer(buffer, size);
      break;
    case SINGLE_SID:
      int single_socket = (
        ((buffer[1] == 1)
        || (buffer[2] == 1)) /* NOTE: Pre v0.7.0 fw backwards compatibility */
        ? 2 : 0);
      usCFG("SINGLE_SID SOCKET %d\n", single_socket);
      if (single_socket == 2) {
        apply_preset_wrapper(PRESET_SINGLE_S2);
      } else {
        apply_preset_wrapper(PRESET_SINGLE_S1);
      }
      break;
    case MIRRORED_SID:
      usCFG("MIRRORED_SID: %s\n", ((buffer[1] == 0) ? "SingleSID" : "DualSID"));
      if (buffer[1] == 0) {
        apply_preset_wrapper(PRESET_MIRRORED);
      } else {
        apply_preset_wrapper(PRESET_MIRRORED_DUAL);
      }
      break;
    case DUAL_FLIPPED:
      usCFG("DUAL_FLIPPED\n");
      apply_preset_wrapper(PRESET_DUAL_FLIPPED);
      break;
    case QUAD_FLIPPED:
      usCFG("QUAD_FLIPPED\n");
      apply_preset_wrapper(PRESET_QUAD_FLIPPED);
      break;
    case QUAD_MIXED:
      usCFG("QUAD_MIXED\n");
      apply_preset_wrapper(PRESET_QUAD_MIXED);
      break;
    case QUAD_FLIPMIX:
      usCFG("QUAD_FLIPMIX\n");
      apply_preset_wrapper(PRESET_QUAD_FLIPMIX);
      break;
    case HOTFLIP_SOCKETS:
      usCFG("HOTFLIP_SOCKETS\n");
      flip_sockets();
      break;
    case DUAL_SID:
      usCFG("DUAL_SID\n");
      apply_preset_wrapper(PRESET_DUAL_BOTH);
      break;
    case DUAL_SOCKET1:
      usCFG("DUAL_SOCKET 1\n");
      apply_preset_wrapper(PRESET_DUAL_S1);
      break;
    case DUAL_SOCKET2:
      usCFG("DUAL_SOCKET 2\n");
      apply_preset_wrapper(PRESET_DUAL_S2);
      break;
    case QUAD_SID:
      usCFG("QUAD_SID\n");
      apply_preset_wrapper(PRESET_QUAD);
      break;
    case TRIPLE_SID:
      usCFG("TRIPLE_SID SOCKET 1\n");
      apply_preset_wrapper(PRESET_TRIPLE_S1);
      break;
    case TRIPLE_SID_TWO:
      usCFG("TRIPLE_SID SOCKET 2\n");
      apply_preset_wrapper(PRESET_TRIPLE_S2);
      break;
    case LOAD_MIDI_STATE:
      usCFG("LOAD_MIDI_STATE\n");
      midi_config_load();
      break;
    case SAVE_MIDI_STATE:
      usCFG("SAVE_MIDI_STATE\n");
      midi_config_save();
      break;
    case RESET_MIDI_STATE:
      usCFG("RESET_MIDI_STATE\n");
      midi_config_reset();
      break;
    case WIFI_STATUS: /* Not available on non _w, so always returns 0xFF 0xFF */
      usCFG("WIFI_STATUS\n");
      memset(write_buffer_p, 0, 64);
#ifdef USE_NET
      write_buffer_p[0] = (uint8_t)net_wifi_is_connected();
#else
      write_buffer_p[0] = 0xFF; /* Not built with WiFi/Bluetooth support */
#endif /* USE_NET */
#ifdef USE_NSD
      write_buffer_p[1] = (uint8_t)nsd_session_is_active();
#else
      write_buffer_p[1] = 0xFF;
#endif /* USE_NSD */
      write_back_data(2);
      break;
#ifdef USE_NET
    case WIFI_SET_SSID:
    {
      usCFG("WIFI_SET_SSID\n");
      uint8_t len = buffer[1];
      if (len > sizeof(net_cfg.ssid) - 1) len = sizeof(net_cfg.ssid) - 1;
      memset(net_cfg.ssid, 0, sizeof(net_cfg.ssid));
      memcpy(net_cfg.ssid, &buffer[2], len);
      break;
    }
    case WIFI_SET_PSK:
    {
      usCFG("WIFI_SET_PSK\n"); /* Never log or read the value itself */
      uint8_t len = buffer[1];
      if (len > sizeof(net_cfg.psk) - 1) len = sizeof(net_cfg.psk) - 1;
      memset(net_cfg.psk, 0, sizeof(net_cfg.psk));
      memcpy(net_cfg.psk, &buffer[2], len);
      break;
    }
    case WIFI_SET_HOSTNAME:
     {
      usCFG("WIFI_SET_HOSTNAME\n");
      uint8_t len = buffer[1];
      if (len > sizeof(net_cfg.hostname) - 1) len = sizeof(net_cfg.hostname) - 1;
      memset(net_cfg.hostname, 0, sizeof(net_cfg.hostname));
      memcpy(net_cfg.hostname, &buffer[2], len);
      break;
    }
    case WIFI_ENABLE:
      usCFG("WIFI_ENABLE: %u\n", buffer[1]);
      if (buffer[1] <= 1) net_cfg.flags.wifi_enabled = (bool)buffer[1];
      /* Doesn't require WIFI_APPLY to have been sent first */
      if (net_cfg.flags.wifi_enabled && net_cfg.ssid[0]) {
        net_wifi_set_credentials(net_cfg.ssid, net_cfg.psk);
        net_wifi_set_hostname(net_cfg.hostname);
        net_wifi_start();
      }
      break;
#ifdef USE_NSD
    case NSD_ENABLE:
      usCFG("NSD_ENABLE: %u\n", buffer[1]);
      if (buffer[1] <= 1) net_cfg.flags.nsd_enabled = (bool)buffer[1];
      break;
      case NSD_SET_PORT:
      usCFG("NSD_SET_PORT\n");
      net_cfg.nsd_port = (uint16_t)((buffer[1] << 8) | buffer[2]);
      break;
#endif /* USE_NSD */
    case WIFI_APPLY:
      usCFG("WIFI_APPLY\n");
      save_net_config(&net_cfg);
      net_wifi_set_credentials(net_cfg.ssid, net_cfg.psk);
      net_wifi_set_hostname(net_cfg.hostname);
      /* net_wifi_start() is one-way, one-shot power-on (no teardown yet) -
       * toggling WIFI_ENABLE back off after this does not power it down. */
      if (net_cfg.flags.wifi_enabled) {
        net_wifi_start();
      }
      break;
    case WIFI_FORGET:
      usCFG("WIFI_FORGET\n");
      default_net_config(&net_cfg);
      save_net_config(&net_cfg);
      net_wifi_set_credentials("", "");
      break;
    case BT_NSD_ENABLE:
      usCFG("BT_NSD_ENABLE: %u\n", buffer[1]);
      if (buffer[1] <= 1) net_cfg.flags.bt_nsd_enabled = (bool)buffer[1];
      save_net_config(&net_cfg); /* Save the config first */
      /* Doesn't require WIFI_APPLY to have been sent first */
      net_bt_set_power(net_cfg.flags.bt_nsd_enabled);
#endif /* USE_NET */
      break;
    case SET_CLOCK:         /* Change SID clock frequency by array id */
      usCFG("SET_CLOCK\n");
      /* locked clockrate check is done in apply_clockrate */
      bool suspend_sids = (buffer[2] == 1) ? true : false;  /* Set RES low while changing clock? */
      apply_clockrate((int)buffer[1], suspend_sids);
      break;
    case GET_CLOCK:         /* Returns the clockrate as array id in byte 0 */
      usCFG("GET_CLOCK\n");
      int clk_rate_id = return_clockrate();
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = clk_rate_id;
      write_back_data(1);
      break;
    case LOCK_CLOCK:        /* Locks the clockrate from being changed, saved in config */
      usCFG("LOCK_CLOCK\n");
      if (buffer[1] == 0 || buffer[1] == 1) { /* Verify correct data */
        usbsid_config.lock_clockrate = (bool)buffer[1];
      } else {
        usWRN("Incorrect value for locking: %d\n", buffer[1]);
      }
      if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
        usCFG("Saving config\n");
        save_load_apply_config(false);
      }
      break;
    case TOGGLE_AUDIO:      /* Toggle the audio state regardless of config setting */
      usCFG("TOGGLE_AUDIO\n");
      toggle_audio_switch();  /* if PCB_VERSION_INT >= 13 is not defined, this doesn't do anything */
      break;
    case SET_AUDIO:         /* Set the audio state from buffer setting (saves config if provided) */
      usCFG("SET_AUDIO\n");
      if (!usbsid_config.lock_audio_sw) {
        usbsid_config.stereo_en =
          (buffer[1] == 0 || buffer[1] == 1)
          ? (bool)buffer[1]
          : true;  /* Default to 1 ~ stereo if incorrect value */
        set_audio_switch(usbsid_config.stereo_en);
        if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
          usCFG("Saving config\n");
          save_load_apply_config(false);
        }
      } else {
        usCFG("Audio switch is %s, requested change to %d (%s)\n",
          monostereo_str((int)usbsid_config.stereo_en),
          buffer[1], monostereo_str(buffer[1]));
        return;
      }
      break;
    case GET_AUDIO: /* Returns the audio switch state */
#if PCB_VERSION_INT >= 13
      usCFG("GET_AUDIO: %d\n", (int)usbsid_config.stereo_en);
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = (uint8_t)usbsid_config.stereo_en;
#else
      usCFG("GET_AUDIO Not supported\n");
      memset(write_buffer_p, 0, 64);
#endif
      write_back_data(1);
      break;
    case LOCK_AUDIO:
      usCFG("LOCK_AUDIO\n");
      usbsid_config.lock_audio_sw =
        (buffer[1] == 0 || buffer[1] == 1)
        ? (bool)buffer[1]
        : false;  /* Default to false if incorrect value ~ don't lock */
      if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
        usCFG("Saving config\n");
        save_load_apply_config(false);
      }
      break;
    case CONFIG_ACK: /* Power to both SID sockets is switched off and read/writes are dropped (v1.5+) */
      usCFG("CONFIG_ACK\n");
#if PCB_VERSION_INT >= 15
      /* Can only acknowledge config, will save config _and_ turn power to both sockets on */
      bool ack_state = config_unacknowledged();
      usCFG("Configuration confirmed!\n");
      usbsid_config.need_confirmation = detected_sid_change = false;
      if (ack_state) {
        save_config(&usbsid_config);
        apply_socket_config_voltages();
        bus_drain();
        bus_resync();
        clear_dma_channels();
        reset_sid();
      }
#endif
      break;
    case SOCKET_DETECT: /* Enable/disable automatic socket change detection on v1.5+ */
      usCFG("SOCKET_DETECT\n");
#if PCB_VERSION_INT >= 15
      usbsid_config.socket_change_detect =
        (buffer[1] == 0 || buffer[1] == 1)
        ? (bool)buffer[1]
        : true;  /* Default to 1 ~ enabled, always autodetect */
      usCFG("Automatic socket change detection is now '%s'\n",
        switch_str((int)usbsid_config.socket_change_detect));
      save_config(&usbsid_config);
#endif
      break;
    case PRESET_DETECT: /* Enable/disable automatic socket change detection on v1.5+ */
      usCFG("PRESET_DETECT\n");
      usbsid_config.preset_auto_detect =
        (buffer[2] == 0 || buffer[2] == 1)
        ? (bool)buffer[2]
        : true;  /* Default to 1 ~ enabled, always autodetect */
      usCFG("Silent autodetection during preset change is now '%s'\n",
        switch_str((int)usbsid_config.preset_auto_detect));
      save_config(&usbsid_config);
      break;
    case DETECT_SIDS:       /* Detect SID types per socket */
      if (buffer[1] == 0) {
        usCFG("DETECT_SIDS (ALL)\n");
        usbsid_config.socketOne.sid1.type = detect_sidtype_at(usbsid_config.socketOne.sid1.addr, usbsid_config.socketOne.chiptype);
        if (usbsid_config.socketOne.dualsid) {
          usbsid_config.socketOne.sid2.type = detect_sidtype_at(usbsid_config.socketOne.sid2.addr, usbsid_config.socketOne.chiptype);
        }
        usbsid_config.socketTwo.sid1.type = detect_sidtype_at(usbsid_config.socketTwo.sid1.addr, usbsid_config.socketTwo.chiptype);
        if (usbsid_config.socketTwo.dualsid) {
          usbsid_config.socketTwo.sid2.type = detect_sidtype_at(usbsid_config.socketTwo.sid2.addr, usbsid_config.socketTwo.chiptype);
        }
        memset(write_buffer_p, 0 ,64);  /* Empty the write buffer pointer */
        read_config(&usbsid_config);    /* Read the config into the config buffer */
        memcpy(write_buffer_p, config_array, 64);  /* Copy the first 64 bytes from the buffer into the write buffer */
        write_back_data(64);
      } else if (buffer[1] == 1) { /* Only if 1, else just skip! */
        if (buffer[2] < 4) {
          usCFG("DETECT_SIDS @ $%02x\n", buffer[3]);
          switch (buffer[2]) {
            case 0:
              detect_sid_model(buffer[3]); /* 0 */
              break;
            case 1:
              detect_sid_version(buffer[3]); /* 1 */
              break;
            case 2:
              detect_sid_reflex(buffer[3]); /* 2 */
              break;
            case 3:
              detect_sid_version_skpico_deprecated(buffer[3]); /* 3 */
              break;
          }
        }
      }
      break;
    case DETECT_CLONES:
      usCFG("DETECT_CLONES\n");
      usbsid_config.socketOne.chiptype = detect_chiptype_at(usbsid_config.socketOne.sid1.addr);
      usbsid_config.socketTwo.chiptype = detect_chiptype_at(usbsid_config.socketTwo.sid1.addr);
      memset(write_buffer_p, 0 ,64);  /* Empty the write buffer pointer */
      read_config(&usbsid_config);  /* Read the config into the config buffer */
      memcpy(write_buffer_p, config_array, 64);  /* Copy the first 64 bytes from the buffer into the write buffer */
      write_back_data(64);
      break;
    case AUTO_DETECT:
      usCFG("AUTO_DETECT\n");
      err = sid_auto_detect(false); /* Not at boot */
#if PCB_VERSION_INT >= 15
      usbsid_config.need_confirmation = detected_sid_change = true; /* Force validation! */
      voltage_state_off(); /* Turn off regulators again */
#endif
      if (err != CFG_OK) {
        usERR("Auto detection failed: %s\n", config_error_str(err));
      }
      if (buffer[1] == 1) { /* Save and reboot */
        save_config_ext();
        mcu_reset(); /* Point of no return */
      } else {
        save_load_apply_config(true);
        memset(write_buffer_p, 0 ,64);  /* Empty the write buffer pointer */
        read_config(&usbsid_config);    /* Read the config into the config buffer */
        memcpy(write_buffer_p, config_array, 64);  /* Copy the first 64 bytes from the buffer into the write buffer */
        write_back_data(64);
      }
      break;
    case TEST_ALLSIDS:
      usCFG("TEST_ALLSIDS\n");
      running_tests = true;
      for (int s = 0; s < cfg.numsids; s++) {
        usCFG("Starting tests on SID %d]\n", s);
        if (running_tests) {
          sidtest_queue_entry_t s_entry = {sid_test, s, '1', 'A'};
          queue_try_add(&sidtest_queue, &s_entry);
        } else return;
      };
      break;
    case TEST_SID1 ... TEST_SID4:
      int s = (buffer[0] == TEST_SID1 ? 0
        : buffer[0] == TEST_SID2 ? 1
        : buffer[0] == TEST_SID3 ? 2
        : buffer[0] == TEST_SID4 ? 3
        : 0);  /* Fallback to SID 0 */
      char t = (buffer[1] == 1 ? '1'  /* All tests */
        : buffer[1] == 2 ? '2'  /* All waveforms test */
        : buffer[1] == 3 ? '3'  /* Filter tests */
        : buffer[1] == 4 ? '4'  /* Envelope tests */
        : buffer[1] == 5 ? '5'  /* Modulation tests */
        : '1');  /* Fallback to all tests */
      char wf = (buffer[2] == 0 ? 'A'  /* All */
        : buffer[2] == 1 ? 'T'  /* Triangle */
        : buffer[2] == 2 ? 'S'  /* Sawtooth */
        : buffer[2] == 3 ? 'P'  /* Pulse */
        : buffer[2] == 4 ? 'N'  /* Noise */
        : 'P');  /* Fallback to pulse waveform */
      usCFG("TEST_SID %d TEST: %c WF: %c\n", (s + 1), t, wf);
      running_tests = true;
      sidtest_queue_entry_t s_entry = {sid_test, s, t, wf};
      queue_try_add(&sidtest_queue, &s_entry);
      break;
    case STOP_TESTS:
      usCFG("STOP_TESTS\n");
      running_tests = false;
      break;
    case USBSID_VERSION:
      usCFG("READ_FIRMWARE_VERSION\n");
      if (buffer[1] == 0) {  /* Large write 64 bytes */
        memset(p_version_array, 0, count_of(p_version_array));
        read_firmware_version();
        memset(write_buffer_p, 0, MAX_BUFFER_SIZE);
        memcpy(write_buffer_p, p_version_array, MAX_BUFFER_SIZE);
        write_back_data(MAX_BUFFER_SIZE);
      } else {  /* Small write single byte */
        memset(write_buffer_p, 0, 64);
        int fwver = FW_VERSION_INT; // ISSUE: Not yet working correct
        write_buffer_p[0] = fwver;
        write_back_data(1);
      }
      break;
    case US_PCB_VERSION:
      usCFG("READ_PCB_VERSION\n");
      if (buffer[1] == 0) {  /* Large write 64 bytes */
        memset(p_version_array, 0, count_of(p_version_array));
        read_pcb_version();
        memset(write_buffer_p, 0, MAX_BUFFER_SIZE);
        memcpy(write_buffer_p, p_version_array, MAX_BUFFER_SIZE);
        write_back_data(MAX_BUFFER_SIZE);
      } else {  /* Small write single byte */
        memset(write_buffer_p, 0, 64);
        /* int pcbver = (strcmp(pcb_version, "1.3") == 0 ? 13 : 10); */
        int pcbver = PCB_VERSION_INT;
        write_buffer_p[0] = pcbver;
        write_back_data(1);
      }
      break;
    case US_FEATURES:
      usCFG("READ_FEATURES\n");
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = us_features;
      write_back_data(1);
      break;
    case RESTART_BUS:
      usCFG("RESTART_BUS\n");
      restart_bus();
      break;
    case RESTART_BUS_CLK:
     usCFG("RESTART_BUS_CLK\n");
      restart_bus_clocks();
      break;
    case SYNC_PIOS:
      usCFG("SYNC_PIOS\n");
      sync_pios(false);
      break;
    case TEST_FN:
      uint8_t st = 0xFF;
      if (buffer[1] == 0) {
        if(buffer[2] == 0) st = detect_skpico(buffer[3]);
        if(buffer[2] == 1) st = detect_armsid(buffer[3]);
        if(buffer[2] == 2) st = detect_fpgasid(buffer[3]);
        if(buffer[2] == 3) st = detect_pdsid(buffer[3]); // BUG: Causes dsync!
        if(buffer[2] == 4) st = detect_backsid(buffer[3]);
        if(buffer[2] == 5) st = detect_sidemu(buffer[3]);
        usCFG("Detect found: %02X\n", st);
      }
      if (buffer[1] == 1) {
        detect_fmopl(buffer[2]);
      }
      if (buffer[1] == 2) {
        usCFG("SAVE ID BEFORE: %d (%d)\n", config_saveid, usbsid_config.config_saveid);
        config_saveid = usbsid_config.config_saveid = 0;  /* reset saveid */
        usCFG("SAVE ID AFTER: %d (%d)\n", config_saveid, usbsid_config.config_saveid);
      }
      if (buffer[1] == 3)  {
        read_fpgasid_configuration(buffer[2]);
      }
      if (buffer[1] == 4)  {
        read_skpico_configuration(buffer[2], buffer[3]);
      }
      if (buffer[1] == 5)  {
        read_armsid_configuration(buffer[2]);
      }
      if (buffer[1] == 6)  {
        print_backsid_filter_type(buffer[2]);
        print_backsid_version(buffer[2]);
      }
      if (buffer[1] == 7) {
        set_backsid_filter_type(buffer[2],buffer[3]);
      }
      if (buffer[1] == 8) {
        cycled_write_operation(0x1d,0x50,6);
        cycled_delay_operation(100);
        cycled_write_operation(0x1e,0x44,6);
        cycled_delay_operation(100);
        uint8_t result = cycled_read_operation(0x1e,6);
        usNFO("PDSID: $%02x\n",result);
      }
      if (buffer[1] == 9) {
        if (buffer[2] == 0) {
          print_config_overview();
        } else if (buffer[2] == 1){
          print_config_summary();
          print_runtime_summary();
        } else {
          print_cfg_addr();
        }
      }
      if (buffer[1] == 10) {
        uint16_t dcyc = 1000;
        if (buffer[2] != 0 || buffer[3] != 0)
          dcyc = (buffer[2] << 8) | buffer[3];
        usCFG("DELAY TESTING FOR %ld US/CYCLES\n", dcyc);
        volatile uint64_t test_before = to_us_since_boot(get_absolute_time());
        sleep_ms(dcyc/1000);
        volatile uint64_t test_after = to_us_since_boot(get_absolute_time());
        usCFG("SLEEP_MS before: %lld after: %lld difference %lld\n", test_before, test_after, (test_after - test_before));
        test_before = to_us_since_boot(get_absolute_time());
        volatile uint16_t waited_cycles = cycled_delay_operation(dcyc);
        test_after = to_us_since_boot(get_absolute_time());
        usCFG("DELAY_CYCLES %u = %.4fµs, ACTUAL: %uµs (including get_absolute_time delay)\n",
          waited_cycles, (float)(waited_cycles * sid_us), (test_after - test_before));
        volatile uint32_t now, end;
        test_before = to_us_since_boot(get_absolute_time());
        now = end = clockcycles();
        do {
          end = clockcycles();
        } while (end < (now + dcyc));
        test_after = to_us_since_boot(get_absolute_time());
        usCFG("CPU START %u CPU END: %u CPU TARGET: %u FRAME START: %u FRAME END: %u \n",
          now, end, (now + dcyc),
          (uint)(now/usbsid_config.raster_rate),
          (uint)((now + dcyc)/usbsid_config.raster_rate)
        );
        usCFG("DELAY_CYCLES %u = %.4fµs, ACTUAL: %uµs (including get_absolute_time delay)\n",
          dcyc, (float)(dcyc * sid_us),
          (test_after - test_before)
        );
      }
      if (buffer[1] == 0x0b) {
        detect_socket_change();
        clear_dma_channels();
      }
      #if PCB_VERSION_INT >= 15
      if (buffer[1] == 0x0c) {
        switch (buffer[2]) {
          case 0:
            set_SID5v_state((bool)buffer[3]);
            break;
          case 1:
            set_SIDhv_state((bool)buffer[3]);
            break;
          case 2:
            set_SID1_highvoltage((bool)buffer[3]);
            break;
          case 3:
            set_SID2_highvoltage((bool)buffer[3]);
            break;
        }
      }
      #endif
      if (buffer[1] == 0x0d) {
        // set_sidemu_sidtype(buffer[2], buffer[3]);
        extern uint8_t get_pin_states(void);
        uint8_t pinstates = get_pin_states();
        usNFO("PINSTATES: 0b%04b\n",pinstates);
      }
      if (buffer[1] == 0x0e) {
        apply_sid_addresses();
      }
      if (buffer[1] == 0x0f) {
        usNFO("Printing SID memory\n");
        for (uint i = 0; i < SID_MEMORY_SIZE; i++) {
          if (i!=0 && i%0x20 == 0) { usNFO("\n");};
          usNFO("$%02X ", sid_memory[i]);
        }
        usNFO("\n");
      }
      if (buffer[1] == 0x10) {
        usNFO("Vendor fifos\n");
        bool vm = tud_vendor_n_mounted(WUSB_ITF);
        uint32_t wa = tud_vendor_n_write_available(WUSB_ITF);
        uint32_t ra = tud_vendor_n_available(WUSB_ITF);
        usNFO("tud_vendor_n_mounted: %d\n", vm);
        usNFO("tud_vendor_n_write_available: %u\n", wa);
        usNFO("tud_vendor_n_available: %u\n", ra);
        tud_vendor_n_write_flush(WUSB_ITF);
        tud_vendor_n_read_flush(WUSB_ITF);
      }
      break;
    case READ_CLONECHIP: /* Read configuration / data from clone Chips */
      usCFG("READ_CLONECHIP: $%02x @ $%02x\n", buffer[1], buffer[2]);
      if ((buffer[1]&0x0f) > CHIP_COUNT) break;
      memset(write_buffer_p, 0, MAX_BUFFER_SIZE);
      if (!read_chip_configuration(buffer[2], buffer[1], write_buffer_p)) {
        usNFO("READ ERROR!\n");
      }
      usNFO("WRITE BACK:\n");
      for (int i = 0; i < MAX_BUFFER_SIZE; i++) usNFO("%02x ", write_buffer_p[i]);
      usNFO("\n");
      usNFO("SENDING\n");
      write_back_data(MAX_BUFFER_SIZE);
      break;
    case WRITE_CLONECHIP: /* Write configuration / data to clone Chips */
      write_chip_configuration(buffer[2]);
      break;
    case PDSID: /* TODO: Deprecate and remove */
      if (buffer[1] == 0) {
        usCFG("Toggle PDSID type\n");
        reset_switch_pdsid_type();
        break;
      } else if (buffer[1] == 1) {
        usCFG("Set PDSID type: %d @ $%02x]\n",buffer[3],buffer[2]);
        if (!set_pdsid_sid_type(buffer[2],buffer[3])) {
          usERR("Failed to set PDSID type!\n");
        }
        break;
      } else if (buffer[1] == 2) {
        uint8_t result = read_pdsid_sid_type(buffer[2]);
        usCFG("Read PDSID result: %d @ $%02x]\n",result,buffer[2]);
        break;
      }
      break;
#if defined(ONBOARD_EMULATOR)
    case UPLOAD_SID_START:
      /* REVERT NOTE: this used to stage the incoming file in its own
       * calloc'd buffer (0x10000 on RP2350, 0x8000 on RP2040) here, then
       * hand it to load_prg()/load_sidtune() on core1, which copied it a
       * second time into usplayer's own tune buffer before freeing this
       * one - two copies of one file. usplayer_upload_start()/_feed() now
       * write straight into usplayer's buffer as packets arrive, so there
       * is nothing to allocate here any more. See usplayer.h's "Streaming
       * upload" block for the replacement API, and git history for this
       * file if the old staged-buffer approach ever needs to come back. */
      usCFG("UPLOAD_SID_START: %d\n",buffer[1]);
      playtime = 0; /* Reset playtime on upload to 0 */
      maxplaytime = 300000; /* Reset max playtime on upload back to 5 minutes in milliseconds */
      receiving_sidfile = true;
      sidbytes_received = 0;
      is_prg = ((buffer[1] == PRG_FILE) ? true : false);
      usplayer_upload_start();
      break;
    case UPLOAD_SID_DATA:
      if (sidbytes_received == 0) usCFG("UPLOAD_SID_DATA\n");
      if (receiving_sidfile) {
        /* Max buffer size minus command byte (config init byte is already
         * gone); straight into usplayer's own tune buffer, no local copy. */
        if (!usplayer_upload_feed(&buffer[1], 62)) {
          receiving_sidfile = false;
          usERR("More incoming data than usplayer has room for, aborting upload here!\n");
          break;
        }
        sidbytes_received += 62; /* kept for the log line below only */
      }
      break;
    case UPLOAD_SID_END:
      usCFG("UPLOAD_SID_END\n");
      usDBG("Received %u bytes\n", sidbytes_received);
      receiving_sidfile = false;
      sidbytes_received = 0;
      break;
    case UPLOAD_SID_SIZE:
      /* REVERT NOTE: used to be stored in sidfile_size and passed to
       * load_prg()/load_sidtune() as the byte count. Dropped: usplayer now
       * tracks the real count itself as bytes stream in, which is more
       * honest than this announced value ever was (see the removed "these
       * are never the same size" note this replaces). The command is still
       * accepted so the upload sequence on the wire is unchanged; the value
       * itself just goes nowhere now. */
      usCFG("UPLOAD_SID_SIZE\n");
      usDBG("Received file size announcement: %u (no longer used)\n",
        (unsigned)(buffer[1]<<8|buffer[2]));
      break;
    case UPLOAD_SID_PLAYTIME:
      usCFG("UPLOAD_SID_PLAYTIME\n");
      set_maxplaytime(buffer);
      break;
    case SID_PLAYER_TUNE:
      usCFG("SID_PLAYER_TUNE %d\n", buffer[1]);
      tuneno = buffer[2]; /* Should be 0 if not supplied */
      usCFG("Subtune %d\n", tuneno);
      unmute_sid(); /* Must unmute before play start or some tunes will be silent */
      sidplayer_init = true;
    case SID_PLAYER_START:
      usCFG("SID_PLAYER_START %d\n", sidplayer_init);
      if (sidplayer_init) {
        offload_ledrunner = true;
        sidplayer_start = true;
      }
      sidplayer_init = false;
      break;
    case SID_PLAYER_STOP:
      usCFG("SID_PLAYER_STOP\n");
      if (sidplayer_playing) {
        maxplaytime = 300000; /* Reset max playtime on play stop back to 5 minutes in milliseconds */
        sidplayer_stop = true;
      }
      /* Deinit all sidplayer variables */
      sidplayer_init = false;
      sidplayer_start = false;
      /* SID resets are handled in the emulator */
      break;
    case SID_PLAYER_PAUSE:
      usCFG("SID_PLAYER_PAUSE\n");
      sidplayer_playing = !sidplayer_playing;
      break;
    case SID_PLAYER_NEXT:
      usCFG("SID_PLAYER_NEXT\n");
      sidplayer_next = true;
      break;
    case SID_PLAYER_PREV:
      usCFG("SID_PLAYER_PREV\n");
      sidplayer_prev = true;
      break;
    case SID_PLAYER_TWO:
      usCFG("SID_PLAYER_TWO\n");
      force_socktwo();
      break;
    case SID_PLAYER_FFWD: /* Non functional */
      usCFG("SID_PLAYER_FFWD: %s\n",
        (buffer[1] ? "ON" : "OFF"));
      /* emu_ffwd((bool)buffer[1]); */
      break;
    case SID_PLAYER_RWND: /* Not implemented */
      break;
    case SID_PLAYER_MUTE:
      usCFG("SID_PLAYER_MUTE\n");
      set_mutestate(buffer);
      break;
    case SID_PLAYER_MUTED:
      usCFG("SID_PLAYER_MUTED\n");
      get_mutestate();
      break;
    case SID_PLAYER_TIME:
      usCFG("SID_PLAYER_TIME\n");
      get_playtime();
      break;
    case TEST_FN2:
      if (buffer[1] == 0x00) {
        usCFG("[USPLAYER @ 1000000] %u kcycles/s\n", usplayer_benchmark(1000000));
        uint32_t kc = usplayer_benchmark(985248);
        usCFG("[USPLAYER @ 985248] Emulation: %lu cycles/ms (realtime needs 986)\n", kc);
      }
      if (buffer[1] == 0x01) {
        usCFG("frames %lu  writes %lu  paced %llu  waited %llu\n",
          usplayer_frames(), usplayer_sid_writes(),
          usplayer_cycles_paced(), usplayer_cycles_waited());
      }
      break;
#endif
    default:
      break;
    }
  return;
}

/**
 * @brief Resolve which SID the RGBLED VU should follow
 *
 * If the configured `RGBLED.sid_to_use` is out of range (higher than
 * `cfg.numsids`) or points at a SID that is not type 2 or 3 (i.e. not an
 * 8580/6581), falls back to the first configured SID of type 2 or 3, or to
 * SID 1 if none is found. Otherwise leaves the configured value in place.
 */
void apply_rgbled_config()
{ /* if SID to use is higher then the number of sids, use first available SID */
  int sid = -1;
  int stou = (usbsid_config.RGBLED.sid_to_use - 1);
  for (int s = 0; s < 4; s++) {
    /* Find the first available SID that is either 2 or 3 */
    if (cfg.sidtype[s] == 2 || cfg.sidtype[s] == 3) {
      sid = (s + 1);
      break;
    }
  }
  usCFG("  RGBLED SID Requested: %d\n", usbsid_config.RGBLED.sid_to_use);
  /* check if requested sidno is actually configured  */
  usbsid_config.RGBLED.sid_to_use
    = (stou > cfg.numsids) /* if sid_to_use is higher then numsids */
    /* or */
    || ((cfg.sidtype[stou] != 2) /* if sidtype of sid_to_use is not 2 */
    && (cfg.sidtype[stou] != 3)) /* and if sidtype of sid_to_use is not 3 */
    ? (sid != -1)  /* If any above are true then, if sid is not -1 */
    ? sid  /* use the first SID that is either 8580 or 6581 */
    : 1    /* else default to SID 1 */
    : usbsid_config.RGBLED.sid_to_use;  /* Else use the programmed SID to use */
  usCFG("  RGBLED SID Calculated: %d\n", usbsid_config.RGBLED.sid_to_use);
  return;
}

/**
 * @brief Print the full current configuration and runtime state to the log
 *
 * Calls print_pico_features(), print_config_overview(),
 * print_config_summary() and print_runtime_summary() in sequence.
 */
void print_config(void)
{ /* The truth, and nothing but the truth! */
  print_pico_features();
  print_config_overview();
  print_config_summary();
  print_runtime_summary();
  return;
}

/**
 * @brief Abort in-flight bus DMA transfers and restart the bus clocks/PIOs
 *
 * @param bool silent suppress the log line when true
 */
void apply_busclock_settings(bool silent)
{
  if (!silent) usCFG("  Applying bus clock settings\n");
  abort_dma_bustransfers();
  restart_bus_clocks();
  sync_pios(false);
}

/**
 * @brief Validate and apply usbsid_config as a new preset, silently
 *
 * Validates the config, applying the fallback socket config on failure.
 * Builds a new RuntimeCFG from usbsid_config, applies the FMOpl config to
 * it, then atomically swaps it into the global `cfg` with interrupts
 * disabled, and applies the bus clock settings without logging.
 *
 * @return ConfigError CFG_OK on success, or the validation error
 */
ConfigError apply_new_presetconfig(void)
{
  /* Start with validation */
  err = validate_config();
  if (err != CFG_OK) {
    usERR("Preset config validation failed: %s\n", config_error_str(err));
    socket_config_fallback();  /* Apply fallback configuration */
    return err;
  }

  /* Apply values to Runtime Configuration */
  RuntimeCFG new_cfg;
  apply_runtime_config(&usbsid_config, &new_cfg);

  /* Find configured FMOpl ~ must apply to new_cfg, the swap below overwrites cfg */
  apply_fmopl_config(&new_cfg);

  /* Atomically swap runtime config IRQ safe */
  uint32_t irq = save_and_disable_interrupts();
  memcpy(&cfg, &new_cfg, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  /* Apply to hardware (unless boot) */
  apply_busclock_settings(true);

  return CFG_OK;
}

/**
 * @brief Validate and apply usbsid_config to the running configuration
 *
 * Validates the config, applying the fallback socket config on failure.
 * Builds a new RuntimeCFG from usbsid_config, applies the FMOpl config to
 * it, then atomically swaps it into the global `cfg` with interrupts
 * disabled. Restarts the PIO/bus clocks unless called at boot, applies the
 * RGBLED SID selection when RGB is available, and prints the full config
 * when not at boot.
 *
 * @param bool at_boot true when called during startup, skips hardware
 *        reapplication and the final config print
 * @return ConfigError always CFG_OK
 */
ConfigError apply_config(bool at_boot)
{
  usNFO("\n");
  usCFG("Starting config application (boot=%d)\n", at_boot);

  /* Start with validation */
  err = validate_config();
  if (err != CFG_OK) {
    usERR("  Validation failed: %s\n", config_error_str(err));
    socket_config_fallback();  /* Apply fallback configuration */
  }

  /* Apply values to Runtime Configuration */
  RuntimeCFG new_cfg;
  apply_runtime_config(&usbsid_config, &new_cfg);

  /* Find configured FMOpl ~ must apply to new_cfg, the swap below overwrites cfg */
  apply_fmopl_config(&new_cfg);

  /* Atomically swap runtime config IRQ safe */
  uint32_t irq = save_and_disable_interrupts();
  memcpy(&cfg, &new_cfg, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  /* Apply to hardware (unless boot) */
  if (!at_boot) {
    usCFG("  Restarting PIO\n");
    apply_busclock_settings(false);
  }

  usCFG("  Success: %d SIDs active\n", cfg.numsids);

  if (RGB_ENABLED) {
    usCFG("Applying RGBLED SID\n");
    apply_rgbled_config();
  } else {
    usCFG("  RGBLED not available\n");
  }

  /* Print config at end of apply if requested */
  if (!at_boot) {
    print_config();
  }

  return CFG_OK;
}

/**
 * @brief Save usbsid_config to flash then load it straight back
 */
void save_load_config(void)
{
  save_config(&usbsid_config);
  load_config(&usbsid_config);
  return;
}

/**
 * @brief Save, reload and apply usbsid_config, then verify socket voltages
 *
 * Calls save_load_config() and apply_config(), and on PCB v1.5+ also calls
 * verify_socket_config(), which applies the correct socket voltages if
 * needed.
 *
 * @param bool at_boot forwarded to apply_config()
 */
void save_load_apply_config(bool at_boot)
{
  save_load_config();
  apply_config(at_boot);
#if PCB_VERSION_INT >= 15
  verify_socket_config(); /* This will apply the correct voltages if needed */
#endif
  return;
}

/**
 * @brief Public wrapper for save_config(&usbsid_config)
 *
 * @note for saving the config from outside of config.c
 */
void save_config_ext(void)
{ /* For saving the config outside of config.c */
  save_config(&usbsid_config);
  return;
}

/**
 * @brief Run first-boot auto detection when the loaded config is the default
 *
 * If `usbsid_config.default_config` is set, clears the flag, sets
 * `first_boot` (so the link popup is sent once), runs sid_auto_detect() at
 * boot (which turns on the socket regulators on v1.5+), forces
 * `need_confirmation`/`detected_sid_change` and turns the regulators back
 * off again on v1.5+, and saves the resulting config.
 */
void detect_default_config(void)
{
  usNFO("\n");
  usCFG("Detecting default configuration\n");
  usCFG("  Is default? %s\n",
    boolean_str((int)usbsid_config.default_config));
  if(usbsid_config.default_config) {
    usCFG("  Default configuration detected!\n");
    usCFG("  MAGIC_SMOKE config_struct @ 0x%x cm_verification @ 0x%x\n",
      &usbsid_config.magic, &cm_verification);
    usCFG("  MAGIC_SMOKE verification config_struct = %u, header = %u, cm_verification = %u\n",
      usbsid_config.magic, MAGIC_SMOKE, cm_verification);
    usbsid_config.default_config = false;
    usCFG("  Default configuration state set to %s\n", boolean_str(usbsid_config.default_config));
    first_boot = true;  /* Only at first boot the link popup will be sent */
    /* default auto detect routine based on default config */
    sid_auto_detect(true); /* At boot, turns on regulators for v1.5+ */
#if PCB_VERSION_INT >= 15
    usbsid_config.need_confirmation = detected_sid_change = true; /* Force validation! */
    voltage_state_off(); /* Turn off regulators again */
#endif
    save_config(&usbsid_config);
  }
  return;
}

/**
 * @brief Look up the array index of the currently configured clock rate
 *
 * @return int index into `clockrates` matching `usbsid_config.clock_rate`,
 *         or 0 if not found
 */
int return_clockrate(void)
{
  for (uint i = 0; i < count_of(clockrates); i++) {
    if (clockrates[i] == usbsid_config.clock_rate) {
      return i;
    }
  }
  return 0;
}

/**
 * @brief Change the SID clock rate to the given clockrates table entry
 *
 * No-op if an external/locked clock, or already at the requested rate.
 *
 * @param int n_clock index into the `clockrates` table
 * @param bool suspend_sids disable/unmute SIDs around the clock change
 */
void apply_clockrate(int n_clock, bool suspend_sids)
{
  if (n_clock < 0 || n_clock >= (int)count_of(clockrates)) {
    usCFG("apply_clockrate: index %d out of range, ignoring\n", n_clock);
    return;
  }
  if (!usbsid_config.external_clock) {
    if (!usbsid_config.lock_clockrate) {
      if (clockrates[n_clock] != usbsid_config.clock_rate) {
        if (suspend_sids) {
          usCFG("Disabling SID's\n");
          disable_sid();
        }
        usCFG("Setting C64 SID Clock from %d to %d\n", usbsid_config.clock_rate, clockrates[n_clock]);
        usbsid_config.clock_rate = clockrates[n_clock];
        usbsid_config.refresh_rate = refreshrates[n_clock]; /* Used by ASID */
        usbsid_config.raster_rate = rasterrates[n_clock]; /* Used by the Vu and the ASID buffer */
        /* Cycled write buffer vars */
        sid_hz = usbsid_config.clock_rate;
        sid_mhz = (sid_hz / 1000 / 1000);
        sid_us = (1 / sid_mhz);
        usCFG("Clock information:\n");
        usCFG("  Pico Clock @ %lu Hz, %.0f MHz, %.4f uS\n",
          clock_get_hz(clk_sys), cpu_mhz, cpu_us);
        usCFG("  C64 SID Clock @ %.0f Hz, %.6f MHz, %.4f uS\n",
          sid_hz, sid_mhz, sid_us);
        /* bus_heavy_op_begin()/_end() bracket this DMA/PIO restart, see bus.h. */
        bus_heavy_op_begin();
        abort_dma_bustransfers();
        restart_bus_clocks();
        sync_pios(false);
        if (suspend_sids) {
          usCFG("Enable SID's and UnMute\n");
          enable_sid(true);
        }
        bus_heavy_op_end();
        // ISSUE: WHEN THE BUS IS RESTARTED THE CRACKLING ON CYCLE EXACT TUNES IS IMMENSE!
        // THIS IS AFTER PAL -> NTSC -> PAL
        // restart_bus();
        return;
      } else {
        usCFG("Requested C64 SID Clock from %d and to %d are equal, skipping SET_CLOCK\n",
          usbsid_config.clock_rate, clockrates[n_clock]);
        return;
      }
    } else {
      usCFG("Clockrate is locked, change from %d to %d not applied\n", usbsid_config.clock_rate, clockrates[n_clock]);
      return;
    }
  }
  return;
}

/**
 * @brief Sanity-check usbsid_config.clock_rate against the known clock values
 *
 * If not using an external clock and `clock_rate` is not one of
 * CLOCK_DEFAULT, CLOCK_PAL, CLOCK_NTSC or CLOCK_DREAN, resets the clock
 * rate/refresh rate/raster rate to the default entry, saves and reloads the
 * config, and resets the MCU.
 */
void verify_clockrate(void)
{
  if (!usbsid_config.external_clock) {
    switch (usbsid_config.clock_rate) {
      case CLOCK_DEFAULT:
      case CLOCK_PAL:
      case CLOCK_NTSC:
      case CLOCK_DREAN:
        break;
      default:
        usERR("Clock error: Detected unconventional clockrate (%ld) error in config, revert to default\n", usbsid_config.clock_rate);
        usbsid_config.clock_rate = clockrates[0];
        usbsid_config.refresh_rate = refreshrates[0]; /* Experimental */
        usbsid_config.raster_rate = rasterrates[0]; /* Experimental */
        save_config(&usbsid_config);
        load_config(&usbsid_config);
        mcu_reset();
        break;
    }
    return;
  }
  return;
}
