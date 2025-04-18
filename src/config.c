/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config.c
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


#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "globals.h"
#include "config.h"
#include "gpio.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* USBSID externals */
extern void init_sidclock(void);
extern void deinit_sidclock(void);
extern void cdc_write(uint8_t * itf, uint32_t n);
extern void webserial_write(uint8_t * itf, uint32_t n);
extern char dtype;
extern uint8_t *cdc_itf;
extern uint8_t *wusb_itf;
extern uint8_t *write_buffer_p;
extern double cpu_mhz, cpu_us;
extern double sid_hz, sid_mhz, sid_us;
extern uint8_t sid_memory[];
extern queue_t sidtest_queue;

/* SID externals */
extern uint8_t detect_sid_version(uint8_t start_addr);
extern void sid_test(int sidno, char test, char wf);
extern bool running_tests;

/* GPIO externals */
extern void reset_sid(void);
extern void restart_bus(void);
extern void restart_bus_clocks(void);
extern void stop_dma_channels(void);
extern void start_dma_channels(void);
extern void sync_pios(bool at_boot);
extern void enable_sid(bool unmute);
extern void disable_sid(void);
extern void mute_sid(void);
extern void reset_sid_registers(void);
extern void toggle_audio_switch(void);
extern void set_audio_switch(bool state);

/* Midi externals */
extern void midi_bus_operation(uint8_t a, uint8_t b);

/* MCU externals */
extern void mcu_reset(void);

/* Pre declarations */
int verify_fmopl_sidno(void);
void apply_config(bool at_boot);
void apply_socket_change(void);
int return_clockrate(void);
void apply_clockrate(int n_clock, bool suspend_sids);

/* Init config vars
 * Based on: https://community.element14.com/products/raspberry-pi/b/blog/posts/raspberry-pico-c-sdk-reserve-a-flash-memory-block-for-persistent-storage
 */
extern uint32_t ADDR_PERSISTENT[];  /* .section_persistent from the linker script */
#define ADDR_PERSISTENT_BASE_ADDR (ADDR_PERSISTENT)
#define FLASH_PERSISTENT_OFFSET ((uint32_t)ADDR_PERSISTENT_BASE_ADDR - XIP_BASE)  /* rp2040: 0x0x1FF000, rp2350: 0x3FF000 */
#define FLASH_PERSISTENT_SIZE (PICO_FLASH_SIZE_BYTES - FLASH_PERSISTENT_OFFSET)
#define FLASH_CONFIG_OFFSET (FLASH_PERSISTENT_OFFSET + ((FLASH_PERSISTENT_SIZE / 4) * 3))
#define CONFIG_SIZE (FLASH_SECTOR_SIZE / 16)  /* 256 Bytes ~ FLASH_PAGE_SIZE */

/* Init local vars */
static uint8_t config_array[FLASH_PAGE_SIZE]; /* 256 MIN ~ FLASH_PAGE_SIZE & 4096 MAX ~ FLASH_SECTOR_SIZE  */
static uint8_t socket_config_array[10]; /* 10 bytes is enough for now */
static uint8_t p_version_array[MAX_BUFFER_SIZE];
static uint32_t cm_verification; /* Config magic verification storage */
static int sidtype[4]; /* Max 4 sids {S1, S2, S3, S4} defaults to 'unknown' */

/* Init vars */
int sock_one = 0, sock_two = 0, sids_one = 0, sids_two = 0, numsids = 0, act_as_one = 0;
int fmopl_sid = 0;
bool fmopl_enabled = false;
uint8_t one = 0, two = 0, three = 0, four = 0;
uint8_t one_mask = 0, two_mask = 0, three_mask = 0, four_mask = 0;
bool first_boot = false;

/* Init string constants for logging */
const char* project_version = PROJECT_VERSION;
const char* pcb_version = PCB_VERSION;
const char *sidtypes[5] = { "UNKNOWN", "N/A", "MOS8580", "MOS6581", "FMOpl" };
const char *chiptypes[2] = { "Real", "Clone" };
const char *clonetypes[6] = { "Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID" };
const char *int_ext[2] = { "Internal", "External" };
const char *enabled[2] = { "Disabled", "Enabled" };
const char *true_false[2] = { "False", "True" };
const char *single_dual[2] = { "Dual SID", "Single SID" };
const char *mono_stereo[2] = { "Mono", "Stereo" };

#define USBSID_DEFAULT_CONFIG_INIT { \
  .magic = MAGIC_SMOKE, \
  .default_config = 1, \
  .external_clock = false, \
  .clock_rate = DEFAULT, \
  .raster_rate = R_DEFAULT, \
  .lock_clockrate = false, \
  .stereo_en = false, \
  .socketOne = { \
    .enabled = true, \
    .dualsid = false, \
    .chiptype = 0x0,  /* real */ \
    .clonetype = 0x0, /* disabled */ \
    .sid1type = 0x0,  /* unknown */ \
    .sid2type = 0x0,  /* unknown */ \
  }, \
  .socketTwo = { \
    .enabled = true, \
    .dualsid = false, \
    .act_as_one = false, \
    .chiptype = 0x0,  /* real */ \
    .clonetype = 0x0, /* disabled */ \
    .sid1type = 0x0,  /* unknown */ \
    .sid2type = 0x0,  /* unknown */ \
  }, \
  .LED = { \
    .enabled = true, \
    .idle_breathe = LED_PWM \
  }, \
  .RGBLED = { \
    .enabled = RGB_ENABLED, \
    .idle_breathe = RGB_ENABLED, \
    .brightness = (RGB_ENABLED ? 0x7F : 0),  /* Half of max brightness or disabled if no RGB LED */ \
    .sid_to_use = (RGB_ENABLED ? 1 : -1), \
  }, \
  .Cdc = { \
    .enabled = true \
  }, \
  .WebUSB = { \
    .enabled = true \
  }, \
  .Asid = { \
    .enabled = true \
  }, \
  .Midi = { \
    .enabled = true \
  }, \
  .FMOpl = { \
    .enabled = false, \
    .sidno = 0, \
  }, \
} \

static const Config usbsid_default_config = USBSID_DEFAULT_CONFIG_INIT;

void print_cfg_addr(void)
{
  CFG("[CONFIG] [XIP_BASE]0x%X (%u)\n\
[CONFIG] [PICO_FLASH_SIZE_BYTES]0x%X (%u)\n\
[CONFIG] [FLASH_PAGE_SIZE]0x%X (%u)\n\
[CONFIG] [FLASH_SECTOR_SIZE]0x%X (%u)\n\
[CONFIG] [ADDR_PERSISTENT_BASE_ADDR]0x%X (%u)\n\
[CONFIG] [FLASH_PERSISTENT_OFFSET]0x%X (%u)\n\
[CONFIG] [FLASH_PERSISTENT_SIZE]0x%X (%u)\n\
[CONFIG] [FLASH_CONFIG_OFFSET]0x%X (%u)\n\
[CONFIG] [CONFIG_SIZE]0x%X (%u)\n\
[CONFIG] [SIZEOF_CONFIG]0x%X (%u)\n",
    XIP_BASE, XIP_BASE, PICO_FLASH_SIZE_BYTES, PICO_FLASH_SIZE_BYTES,
    FLASH_PAGE_SIZE, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE,
    ADDR_PERSISTENT_BASE_ADDR, ADDR_PERSISTENT_BASE_ADDR,
    FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_OFFSET, FLASH_PERSISTENT_SIZE, FLASH_PERSISTENT_SIZE,
    FLASH_CONFIG_OFFSET, FLASH_CONFIG_OFFSET,
    CONFIG_SIZE, CONFIG_SIZE, sizeof(Config), sizeof(Config));
}

void print_cfg(const uint8_t *buf, size_t len)
{
  CFG("[PRINT CFG START]\n");
  for (size_t i = 0; i < len; ++i) {
    if (i == 0)
      CFG("[R%03d] ", i);
    CFG("%02x", buf[i]);
    if (i == (len - 1)) {
      CFG("\n");
    } else if ((i != 0) && (i % 16 == 15)) {
      CFG("\n[R%03d] ", i);
    } else {
      CFG(" ");
    }
  }
  CFG("[PRINT CFG END]\n");

  return;
}

void detect_sid_types(void)
{
  int sid1 = 0, sid2 = 0, sid3 = 0, sid4 = 0;

  if (numsids >= 1) {
    sid1 = detect_sid_version(0x00);
    CFG("[CONFIG] [READ SID1] [%02x %s]\n", sid1, sidtypes[sid1]);
  }
  if (numsids >= 2) {
    sid2 = detect_sid_version(0x20);
    CFG("[CONFIG] [READ SID2] [%02x %s]\n", sid2, sidtypes[sid2]);
  }
  if (numsids >= 3) {
    sid3 = detect_sid_version(0x40);
    CFG("[CONFIG] [READ SID3] [%02x %s]\n", sid3, sidtypes[sid3]);
  }
  if (numsids == 4) {
    sid4 = detect_sid_version(0x60);
    CFG("[CONFIG] [READ SID4] [%02x %s]\n", sid4, sidtypes[sid4]);
  }

  if (usbsid_config.socketOne.enabled && usbsid_config.socketTwo.enabled) {
    if (usbsid_config.socketOne.dualsid && usbsid_config.socketTwo.dualsid) {
      usbsid_config.socketOne.sid1type = sid1;
      usbsid_config.socketOne.sid2type = sid2;
      usbsid_config.socketTwo.sid1type = sid3;
      usbsid_config.socketTwo.sid2type = sid4;
    }
    if (!usbsid_config.socketOne.dualsid && usbsid_config.socketTwo.dualsid) {
      usbsid_config.socketOne.sid1type = sid1;
      usbsid_config.socketOne.sid2type = 1;
      usbsid_config.socketTwo.sid1type = sid2;
      usbsid_config.socketTwo.sid2type = sid3;
    }
    if (usbsid_config.socketOne.dualsid && !usbsid_config.socketTwo.dualsid) {
      usbsid_config.socketOne.sid1type = sid1;
      usbsid_config.socketOne.sid2type = sid2;
      usbsid_config.socketTwo.sid1type = sid3;
      usbsid_config.socketTwo.sid2type = 1;
    }
    if (!usbsid_config.socketOne.dualsid && !usbsid_config.socketTwo.dualsid) {
      usbsid_config.socketOne.sid1type = sid1;
      usbsid_config.socketOne.sid2type = 1;
      usbsid_config.socketTwo.sid1type = sid2;
      usbsid_config.socketTwo.sid2type = 1;
    }
  } else if (usbsid_config.socketOne.enabled && !usbsid_config.socketTwo.enabled) {
    usbsid_config.socketTwo.sid1type = 1;
    usbsid_config.socketTwo.sid2type = 1;
    if (usbsid_config.socketOne.dualsid) {
      usbsid_config.socketOne.sid1type = sid1;
      usbsid_config.socketOne.sid2type = sid2;
    } else if (!usbsid_config.socketOne.dualsid) {
      usbsid_config.socketOne.sid1type = sid1;
      usbsid_config.socketOne.sid2type = 1;
    }
  } else if (!usbsid_config.socketOne.enabled && usbsid_config.socketTwo.enabled) {
    usbsid_config.socketOne.sid1type = 1;
    usbsid_config.socketOne.sid2type = 1;
    if (usbsid_config.socketTwo.dualsid) {
      usbsid_config.socketTwo.sid1type = sid1;
      usbsid_config.socketTwo.sid2type = sid2;
    } else if (!usbsid_config.socketTwo.dualsid) {
      usbsid_config.socketTwo.sid1type = sid1;
      usbsid_config.socketTwo.sid2type = 1;
    }
  }

  CFG("[CONFIG] SOCKET ONE SID1 TYPE: %s\n", sidtypes[usbsid_config.socketOne.sid1type]);
  CFG("[CONFIG] SOCKET ONE SID2 TYPE: %s\n", sidtypes[usbsid_config.socketOne.sid2type]);
  CFG("[CONFIG] SOCKET TWO SID1 TYPE: %s\n", sidtypes[usbsid_config.socketTwo.sid1type]);
  CFG("[CONFIG] SOCKET TWO SID2 TYPE: %s\n", sidtypes[usbsid_config.socketTwo.sid2type]);

  return;
}

void read_config(Config* config)
{
  memset(config_array, 0, sizeof config_array);  /* Make sure we don't send garbled old data */

  CFG("[CONFIG] [READ CONFIG] [FROM]0x%X [TO]0x%X [SIZE]%u\n", (uint)config, &config_array, sizeof(Config));

  config_array[0] = READ_CONFIG;  /* Initiator byte */
  config_array[1] = 0x7F;  /* Verification byte */
  config_array[5] = (int)config->lock_clockrate;
  config_array[6] = (int)config->external_clock;
  config_array[7] = (config->clock_rate >> 16) & BYTE;
  config_array[8] = (config->clock_rate >> 8) & BYTE;
  config_array[9] = config->clock_rate & BYTE;
  config_array[10] = (int)config->socketOne.enabled;
  config_array[11] = (int)config->socketOne.dualsid;
  config_array[12] = config->socketOne.chiptype;
  config_array[13] = config->socketOne.clonetype;
  config_array[14] = config->socketOne.sid1type;
  config_array[15] = config->socketOne.sid2type;
  config_array[20] = (int)config->socketTwo.enabled;
  config_array[21] = (int)config->socketTwo.dualsid;
  config_array[22] = (int)config->socketTwo.act_as_one;
  config_array[23] = config->socketTwo.chiptype;
  config_array[24] = config->socketTwo.clonetype;
  config_array[25] = config->socketTwo.sid1type;
  config_array[26] = config->socketTwo.sid2type;
  config_array[30] = (int)config->LED.enabled;
  config_array[31] = (int)config->LED.idle_breathe;
  config_array[40] = (int)config->RGBLED.enabled;
  config_array[41] = (int)config->RGBLED.idle_breathe;
  config_array[42] = config->RGBLED.brightness;
  config_array[43] = (int)config->RGBLED.sid_to_use;
  config_array[51] = (int)config->Cdc.enabled;
  config_array[52] = (int)config->WebUSB.enabled;
  config_array[53] = (int)config->Asid.enabled;
  config_array[54] = (int)config->Midi.enabled;
  config_array[55] = (int)config->FMOpl.enabled;
  config_array[56] = config->FMOpl.sidno;
  config_array[57] = config->stereo_en;
  config_array[63] = 0xFF;  /* Terminator byte */

  return;
}

void read_socket_config(Config* config)
{
  memset(socket_config_array, 0, sizeof socket_config_array);  /* Make sure we don't send garbled old data */

  socket_config_array[0] = READ_SOCKETCFG; /* Initiator byte */
  socket_config_array[1] = 0x7F; /* Verification byte */

  socket_config_array[2] = ((int)config->socketOne.enabled << 4) | (int)config->socketOne.dualsid;
  socket_config_array[3] = (config->socketOne.chiptype << 4) | config->socketOne.clonetype;
  socket_config_array[4] = (config->socketOne.sid1type << 4) | config->socketOne.sid2type;

  socket_config_array[5] = ((int)config->socketTwo.enabled << 4) | (int)config->socketTwo.dualsid;
  socket_config_array[6] = (config->socketTwo.chiptype << 4) | config->socketTwo.clonetype;
  socket_config_array[7] = (config->socketTwo.sid1type << 4) | config->socketTwo.sid2type;

  socket_config_array[8] = (int)config->socketTwo.act_as_one;

  socket_config_array[9] = 0xFF; // Terminator byte

  return;
}

void read_firmware_version(void)
{
  p_version_array[0] = USBSID_VERSION;  /* Initiator byte */
  p_version_array[1] = strlen(project_version);  /* Length of version string */
  memcpy(p_version_array+2, project_version, strlen(project_version));
  return;
}

void read_pcb_version(void)
{
  p_version_array[0] = US_PCB_VERSION;  /* Initiator byte */
  p_version_array[1] = strlen(pcb_version);  /* Length of version string */
  memcpy(p_version_array+2, pcb_version, strlen(pcb_version));
  return;
}

void __no_inline_not_in_flash_func(default_config)(Config* config)
{
  memcpy(config, &usbsid_default_config, sizeof(Config));
  return;
}

void __no_inline_not_in_flash_func(load_config)(Config* config)
{
  CFG("[CONFIG] LOAD CONFIG START\n");
  print_cfg_addr();
  CFG("[CONFIG] COPY CONFIG:\n[CONFIG] [FROM]0x%X\n[CONFIG] [TO]0x%X\n[CONFIG] [SIZE]%u\n",
    (XIP_BASE + FLASH_CONFIG_OFFSET), (uint)config, sizeof(Config));
  CFG("[CONFIG] COPY CONFIG ADDRESSES:\n[CONFIG] [&usbsid_config]0x%X\n[CONFIG] [config]0x%X\n[CONFIG] [&config]0x%X\n",
    (uint)&usbsid_config, (uint)config, (uint)&config);
  /* NOTICE: Do not do any logging here after memcpy or the Pico will freeze! */
  memcpy(config, (void *)(XIP_BASE + FLASH_CONFIG_OFFSET), sizeof(Config));
  stdio_flush();
  cm_verification = config->magic;  /* Store the current magic for later */
  if (config->magic != MAGIC_SMOKE) {  /* Verify the magic */
      CFG("[CONFIG] [MAGIC ERROR] config->magic: %u != MAGIC_SMOKE: %u\n", config->magic, MAGIC_SMOKE);
      CFG("[CONFIG] RESET TO DEFAULT CONFIG!\n");
      default_config(config);
      return;
  }
  return;
}

void __no_inline_not_in_flash_func(save_config_lowlevel)(void* config_data)
{
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);  /* 4096 Bytes (sector aligend) erase as per SDK manual */
  flash_range_program(FLASH_CONFIG_OFFSET, (uint8_t*)config_data, FLASH_PAGE_SIZE);  /* 256 Bytes (page aligned) write as per SDK manual */
  restore_interrupts(ints);
  return;
}

void __no_inline_not_in_flash_func(save_config)(const Config* config)
{
  CFG("[CONFIG] SAVE CONFIG START\n"); /* TODO: Test if this doesn't cause a lockup */
  uint8_t config_data[CONFIG_SIZE] = {0};
  static_assert(sizeof(Config) < CONFIG_SIZE, "[CONFIG] SAVE ERROR: Config struct doesn't fit inside CONFIG_SIZE");
  memcpy(config_data, config, sizeof(Config));
  int err = flash_safe_execute(save_config_lowlevel, config_data, 100);
  if (err) {
    CFG("[CONFIG] SAVE ERROR: %d\n", err);
  }
  sleep_ms(100);
  return;
}

void write_back_data(size_t buffersize)
{
  switch (dtype) {
    case 'C':
      cdc_write(cdc_itf, buffersize);
      break;
    case 'W':
      webserial_write(wusb_itf, buffersize);
      break;
  }
  return;
}

void set_socket_config(uint8_t cmd, bool s1en, bool s1dual, uint8_t s1chip, bool s2en, bool s2dual, uint8_t s2chip, bool as_one)
{
  usbsid_config.socketOne.enabled = s1en;
  usbsid_config.socketOne.dualsid = s1dual;
  usbsid_config.socketOne.chiptype = s1chip;  /* Chiptype must be clone for dualsid to work! */
  usbsid_config.socketTwo.enabled = s2en;
  usbsid_config.socketTwo.dualsid = s2dual;
  usbsid_config.socketTwo.chiptype = s2chip;  /* Chiptype must be clone for dualsid to work! */
  usbsid_config.socketTwo.act_as_one = as_one;
  if (cmd == 0) {
    save_config(&usbsid_config);
    load_config(&usbsid_config);
    apply_config(false);
  } else if (cmd == 1) {
    apply_socket_change();
  }
  return;
}

void handle_config_request(uint8_t * buffer)
{ /* Incoming Config data buffer
   *
   * 5 bytes
   * Byte 0 ~ command
   * Byte 1 ~ struct setting e.g. socketOne, clock_rate or additional command
   * Byte 2 ~ setting entry e.g. dualsid
   * Byte 3 ~ new value
   * Byte 4 ~ reserved
   */
  CFG("[CONFIG BUFFER] %x %x %x %x %x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
  switch (buffer[0]) {
    case RESET_USBSID:
      CFG("[CMD] RESET_USBSID\n");
      mcu_reset();
      break;
    case READ_CONFIG:
      CFG("[CMD] READ_CONFIG\n");
      /* ISSUE: Although 4 writes are performed, only the first 2 are received */
      read_config(&usbsid_config);
      print_cfg(config_array, count_of(config_array));
      int writes = count_of(config_array) / 64;  /* ISSUE: It should send 4 packets of 64 bytes, but sends only 2 and a zero packet */
      memset(write_buffer_p, 0, 64);
      for (int i = 0; i < writes; i++) {
        memcpy(write_buffer_p, config_array + (i * 64), 64);
        write_back_data(64);
      }
      break;
    case READ_SOCKETCFG:
      CFG("[CMD] READ_SOCKETCFG\n");
      read_socket_config(&usbsid_config);
      print_cfg(socket_config_array, count_of(socket_config_array));
      memset(write_buffer_p, 0, 64);
      memcpy(write_buffer_p, socket_config_array, count_of(socket_config_array));
      write_back_data(64);
      break;
    case READ_NUMSIDS:
      CFG("[CMD] READ_NUMSIDS\n");
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = (uint8_t)numsids;
      write_back_data(1);
      break;
    case READ_FMOPLSID:
      CFG("[CMD] READ_FMOPLSID\n");
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = (uint8_t)fmopl_sid;
      write_back_data(1);
      break;
    case APPLY_CONFIG:
      CFG("[CMD] APPLY_CONFIG\n");
      apply_config(false);
      break;
    case RELOAD_CONFIG:
      CFG("[CMD] RELOAD_CONFIG\n");
      load_config(&usbsid_config);
      apply_config(false);
      for (int i = 0; i < count_of(clockrates); i++) {
        if (clockrates[i] == usbsid_config.clock_rate) {
          apply_clockrate(i, true);
        }
      }
      break;
    case SET_CONFIG:
      CFG("[CMD] SET_CONFIG\n");
      switch (buffer[1]) {
        case  0:  /* clock_rate */
          /* will always be available to change the setting since it doesn't apply it */
          usbsid_config.clock_rate = clockrates[(int)buffer[2]];
          usbsid_config.raster_rate = rasterrates[(int)buffer[2]]; /* Experimental */
          if (buffer[3] == 0 || buffer[3] == 1) { /* Verify correct data */
            usbsid_config.lock_clockrate = (bool)buffer[3];
          }
          break;
        case  1:  /* socketOne */
          switch (buffer[2]) {
              case 0: /* enabled */
                if (buffer[3] <= 1) { /* 1 or 0 */
                  usbsid_config.socketOne.enabled = (buffer[3] == 1) ? true : false;
                };
                break;
              case 1: /* dualsid */
                if (buffer[3] <= 1) { /* 1 or 0 */
                  usbsid_config.socketOne.dualsid = (buffer[3] == 1) ? true : false;
                };
                break;
              case 2: /* chiptype */
                if (buffer[3] <= 1) {
                  usbsid_config.socketOne.chiptype = buffer[3];
                }
                break;
              case 3: /* clonetype */
                if (buffer[3] <= 5) {
                  usbsid_config.socketOne.clonetype = buffer[3];
                }
                break;
              case 4: /* sid1type */
                if (buffer[3] <= 4) {
                  usbsid_config.socketOne.sid1type = buffer[3];
                }
                break;
              case 5: /* sid2type */
                if (buffer[3] <= 4) {
                  usbsid_config.socketOne.sid2type = buffer[3];
                }
                break;
          };
          break;
        case  2:  /* socketTwo */
          switch (buffer[2]) {
            case 0: /* enabled */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.socketTwo.enabled = (buffer[3] == 1) ? true : false;
              };
              break;
            case 1: /* dualsid */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.socketTwo.dualsid = (buffer[3] == 1) ? true : false;
              };
              break;
            case 2: /* chiptype */
              if (buffer[3] <= 1) {
                usbsid_config.socketTwo.chiptype = buffer[3];
              }
              break;
            case 3: /* clonetype */
              if (buffer[3] <= 5) {
                usbsid_config.socketTwo.clonetype = buffer[3];
              }
              break;
            case 4: /* sid1type */
              if (buffer[3] <= 4) {
                usbsid_config.socketTwo.sid1type = buffer[3];
              }
              break;
            case 5: /* sid2type */
              if (buffer[3] <= 4) {
                usbsid_config.socketTwo.sid2type = buffer[3];
              }
              break;
            case 6: /* act_as_one */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.socketTwo.act_as_one = (buffer[3] == 1) ? true : false;
              };
              break;
          };
          break;
        case  3:  /* LED */
          switch (buffer[2]) {
            case 0: /* enabled */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.LED.enabled = (buffer[3] == 1) ? true : false;
              };
              break;
            case 1: /* idle_breathe */
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
        case  4:  /* RGBLED */
          switch (buffer[2]) {
            case 0: /* enabled */
              if (buffer[3] <= 1) { /* 1 or 0 */
                if (RGB_ENABLED) {
                  usbsid_config.RGBLED.enabled = (buffer[3] == 1) ? true : false;
                } else {
                  usbsid_config.RGBLED.enabled = false;  /* Always false if no RGB LED */
                };
              };
              break;
            case 1: /* idle_breathe */
              if (buffer[3] <= 1) { /* 1 or 0 */
                if (RGB_ENABLED) {
                  usbsid_config.RGBLED.idle_breathe = (buffer[3] == 1) ? true : false;
                } else {
                  usbsid_config.RGBLED.idle_breathe = false;  /* Always false if no RGB LED */
                };
              };
              break;
            case 2: /* brightness */
              if (RGB_ENABLED) {
                usbsid_config.RGBLED.brightness = buffer[3];
              } else {
                usbsid_config.RGBLED.brightness = 0;  /* No brightness needed if no RGB LED */
              };
              break;
            case 3: /* sid_to_use */
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
        case  5:  /* CDC */
        case  6:  /* WEBUSB */
        case  7:  /* ASID */
        case  8:  /* MIDI */
          break;
        case  9:  /* FMOpl */
          usbsid_config.FMOpl.enabled = (bool)buffer[2];
          usbsid_config.FMOpl.sidno = verify_fmopl_sidno();
          break;
        case 10:  /* Audio switch */
          usbsid_config.stereo_en =
          (buffer[2] == 0 || buffer[2] == 1)
          ? (bool)buffer[2]
          : true;  /* Default to 1 ~ stereo if incorrect value */
          break;
        default:
          break;
      };
      break;
    case SAVE_CONFIG:
      CFG("[CMD] SAVE_CONFIG and RESET_MCU\n");
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      mcu_reset();
      break;
    case SAVE_NORESET:
      CFG("[CMD] SAVE_CONFIG no RESET\n");
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      apply_config(false);
      break;
    case RESET_CONFIG:
      CFG("[CMD] RESET_CONFIG\n");
      default_config(&usbsid_config);
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      apply_config(false);
      break;
    case WRITE_CONFIG:
      /* TODO: FINISH */
      CFG("[CMD] WRITE_CONFIG NOT IMPLEMENTED YET!\n");
      break;
    case SINGLE_SID:
      CFG("[CMD] SINGLE_SID\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, false, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case MIRRORED_SID:
      CFG("[CMD] MIRRORED_SID\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, true, false, usbsid_config.socketTwo.chiptype, true);
      break;
    case DUAL_SID:
      CFG("[CMD] DUAL_SID\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, true, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case DUAL_SOCKET1:
      CFG("[CMD] DUAL_SOCKET 1\n");
      set_socket_config(buffer[1], true, true, usbsid_config.socketOne.chiptype, false, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case DUAL_SOCKET2:
      CFG("[CMD] DUAL_SOCKET 2\n");
      set_socket_config(buffer[1], false, false, usbsid_config.socketOne.chiptype, true, true, usbsid_config.socketTwo.chiptype, false);
      break;
    case QUAD_SID:
      CFG("[CMD] QUAD_SID\n");
      set_socket_config(buffer[1], true, true, 1, true, true, 1, false);
      break;
    case TRIPLE_SID:
      CFG("[CMD] TRIPLE_SID SOCKET 1\n");
      set_socket_config(buffer[1], true, true, 1, true, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case TRIPLE_SID_TWO:
      CFG("[CMD] TRIPLE_SID SOCKET 2\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, true, true, 1, false);
      break;
    case LOAD_MIDI_STATE:   /* Load from config into midimachine and apply to SIDs */
      CFG("[LOAD_MIDI_STATE]\n");
      for (int i = 0; i < 4; i++) {
        CFG("[SID %d]", (i + 1));
        for (int j = 0; j < 32; j++) {
          /* ISSUE: this only loads 1 channel state and should actually save all channel states */
          midimachine.channel_states[curr_midi_channel][i][j] = usbsid_config.Midi.sid_states[i][j];
          midimachine.channelkey_states[curr_midi_channel][i][i] = 0;  /* Make sure extras is always initialized @ zero */
          CFG(" %02x", midimachine.channel_states[curr_midi_channel][i][j]);
          midi_bus_operation((0x20 * i) | j, midimachine.channel_states[curr_midi_channel][i][j]);
        }
        CFG("\n");
      }
      break;
    case SAVE_MIDI_STATE:   /* Save from midimachine into config and save to flash */
      CFG("[SAVE_MIDI_STATE]\n");
      for (int i = 0; i < 4; i++) {
        CFG("[SID %d]", (i + 1));
        for (int j = 0; j < 32; j++) {
          /* ISSUE: this only loads 1 channel state and should actually save all channel states */
          usbsid_config.Midi.sid_states[i][j] = midimachine.channel_states[curr_midi_channel][i][j];
          CFG(" %02x", usbsid_config.Midi.sid_states[i][j]);
        }
        CFG("\n");
      }
      save_config(&usbsid_config);
      break;
    case RESET_MIDI_STATE:  /* Reset all settings to zero */
      CFG("[RESET_MIDI_STATE]\n");
      for (int i = 0; i < 4; i++) {
        CFG("[SID %d]", (i + 1));
        for (int j = 0; j < 32; j++) {
          // BUG: this only resets 1 channel state
          usbsid_config.Midi.sid_states[i][j] = midimachine.channel_states[curr_midi_channel][i][j] = 0;
          CFG(" %02x", usbsid_config.Midi.sid_states[i][j]);
          midi_bus_operation((0x20 * i) | j, midimachine.channel_states[curr_midi_channel][i][j]);
        }
        CFG("\n");
      }
      save_config(&usbsid_config);
      /* mcu_reset(); */
      break;
    case SET_CLOCK:         /* Change SID clock frequency by array id */
      CFG("[CMD] SET_CLOCK\n");
      /* locked clockrate check is done in apply_clockrate */
      bool suspend_sids = (buffer[2] == 1) ? true : false;  /* Set RES low while changing clock? */
      apply_clockrate((int)buffer[1], suspend_sids);
      break;
    case GET_CLOCK:         /* Returns the clockrate as array id in byte 0 */
      CFG("[CMD] GET_CLOCK\n");
      int clk_rate_id = return_clockrate();
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = clk_rate_id;
      write_back_data(1);
      break;
    case LOCK_CLOCK:        /* Locks the clockrate from being changed, saved in config */
      CFG("[CMD] LOCK_CLOCK\n");
      if (buffer[1] == 0 || buffer[1] == 1) { /* Verify correct data */
        usbsid_config.lock_clockrate = (bool)buffer[1];
      } else {
        CFG("[LOCK_CLOCK] Received incorrect value of %d\n", buffer[1]);
      }
      if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
        CFG("[LOCK_CLOCK] SAVE_CONFIG\n");
        save_config(&usbsid_config);
        load_config(&usbsid_config);
        apply_config(false);
      }
      break;
    case TOGGLE_AUDIO:      /* Toggle the audio state regardless of config setting */
      CFG("[CMD] TOGGLE_AUDIO\n");
      toggle_audio_switch();  /* if HAS_AUDIOSWITCH is not defined, this doesn't do anything */
      break;
    case SET_AUDIO:         /* Set the audio state from buffer setting (saves config if provided) */
      CFG("[CMD] SET_AUDIO\n");
      usbsid_config.stereo_en =
        (buffer[1] == 0 || buffer[1] == 1)
        ? (bool)buffer[1]
        : true;  /* Default to 1 ~ stereo if incorrect value */
      set_audio_switch(usbsid_config.stereo_en);
      if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
        CFG("[SET_AUDIO] SAVE_CONFIG\n");
        save_config(&usbsid_config);
        load_config(&usbsid_config);
        apply_config(false);
      }
      break;
    case DETECT_SIDS:       /* Detect SID types per socket */
      CFG("[CMD] DETECT_SIDS\n");
      detect_sid_types();
      memset(write_buffer_p, 0 ,64);  /* Empty the write buffer pointer */
      read_config(&usbsid_config);  /* Read the config into the config buffer */
      memcpy(write_buffer_p, config_array, 64);  /* Copy the first 64 bytes from the buffer into the write buffer */
      switch (dtype) {
        case 'C':
          cdc_write(cdc_itf, 64);
          break;
        case 'W':
          webserial_write(wusb_itf, 64);
          break;
      }
      break;
    case TEST_ALLSIDS:  /* ISSUE: This must be run on Core 1 so we can actually stop it! */
      CFG("[CMD] TEST_ALLSIDS\n");
      running_tests = true;
      for (int s = 0; s < numsids; s++) {
        CFG("[START TEST SID %d]\n", s);
        if (running_tests) {
          sidtest_queue_entry_t s_entry = {sid_test, s, '1', 'A'};
          queue_try_add(&sidtest_queue, &s_entry);
          /* sid_test(s, '1', 'A'); */
        } else return;
      };
      break;
    /* ISSUE: This must be run on Core 1 so we can actually stop it! */
    case TEST_SID1:
    case TEST_SID2:
    case TEST_SID3:
    case TEST_SID4:
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
      CFG("[CMD] TEST_SID %d TEST: %c WF: %c\n", (s + 1), t, wf);
      running_tests = true;
      sidtest_queue_entry_t s_entry = {sid_test, s, t, wf};
      queue_try_add(&sidtest_queue, &s_entry);
      /* sid_test(s, t, wf); */
      break;
    case STOP_TESTS:
      CFG("[CMD] STOP_TESTS\n");
      running_tests = false;
      break;
    case USBSID_VERSION:
      CFG("[CMD] READ_FIRMWARE_VERSION\n");
      memset(p_version_array, 0, count_of(p_version_array));
      read_firmware_version();
      memset(write_buffer_p, 0, MAX_BUFFER_SIZE);
      memcpy(write_buffer_p, p_version_array, MAX_BUFFER_SIZE);
        switch (dtype) {
          case 'C':
            cdc_write(cdc_itf, MAX_BUFFER_SIZE);
            break;
          case 'W':
            webserial_write(wusb_itf, MAX_BUFFER_SIZE);
            break;
        }
      break;
    case US_PCB_VERSION:
      CFG("[CMD] READ_PCB_VERSION\n");
      if (buffer[1] == 0) {  /* Large write 64 bytes */
        memset(p_version_array, 0, count_of(p_version_array));
        read_pcb_version();
        memset(write_buffer_p, 0, MAX_BUFFER_SIZE);
        memcpy(write_buffer_p, p_version_array, MAX_BUFFER_SIZE);
          switch (dtype) {
            case 'C':
              cdc_write(cdc_itf, MAX_BUFFER_SIZE);
              break;
            case 'W':
              webserial_write(wusb_itf, MAX_BUFFER_SIZE);
              break;
          }
      } else {  /* Small write single byte */
        memset(write_buffer_p, 0, 64);
        int pcbver = (strcmp(pcb_version, "1.3") == 0 ? 13 : 10);
        write_buffer_p[0] = pcbver;
        write_back_data(1);
      }
      break;
    case RESTART_BUS:
      CFG("[CMD] RESTART_BUS\n");
      restart_bus();
      break;
    case RESTART_BUS_CLK:
     CFG("[CMD] RESTART_BUS_CLK\n");
      restart_bus_clocks();
      break;
    case SYNC_PIOS:
      CFG("[CMD] SYNC_PIOS\n");
      sync_pios(false);
      break;
    case TEST_FN: /* TODO: Remove before v1 release */
      CFG("[CMD] TEST_FN\n");
      CFG("[FLASH_CONFIG_OFFSET]0x%x\n", FLASH_CONFIG_OFFSET);
      CFG("[PICO_FLASH_SIZE_BYTES]0x%x\n", PICO_FLASH_SIZE_BYTES);
      CFG("[FLASH_SECTOR_SIZE]0x%x\n", FLASH_SECTOR_SIZE);
      CFG("[FLASH_PAGE_SIZE]0x%x\n", FLASH_PAGE_SIZE);
      CFG("[CONFIG_SIZE]0x%x\n", CONFIG_SIZE);
      CFG("A %x %x %d\n", usbsid_config.magic, MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
      CFG("A %x %x %d\n", (uint32_t)usbsid_config.magic, (uint32_t)MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
      CFG("A %d %d\n", (int)usbsid_config.magic, (int)MAGIC_SMOKE);
      CFG("A %d\n", usbsid_config.magic == MAGIC_SMOKE);


      CFG("[TEST_CONFIG_START]\n");
      CFG("[MAGIC_SMOKE ERROR?] config: %u header: %u cm_verification: %u\n", usbsid_config.magic, MAGIC_SMOKE, cm_verification);

      Config test_config;
      memcpy(&test_config, (void *)(XIP_BASE + FLASH_CONFIG_OFFSET), sizeof(Config));
      CFG("A %x %x %d\n", test_config.magic, MAGIC_SMOKE, test_config.magic != MAGIC_SMOKE);
      CFG("A %x %x %d\n", (uint32_t)test_config.magic, (uint32_t)MAGIC_SMOKE, test_config.magic != MAGIC_SMOKE);
      CFG("A %d %d\n", (int)test_config.magic, (int)MAGIC_SMOKE);
      CFG("A %d\n", test_config.magic == MAGIC_SMOKE);
      read_config(&test_config);
      print_cfg(config_array, count_of(config_array));
      CFG("[TEST_CONFIG_END]\n");

      // CFG("[USBSID_SID_MEMORY]\n");
      // print_cfg(sid_memory, (numsids * 0x20));
      // uint8_t st = detect_sid_model(buffer[1]);
      // CFG("[TEST FOUND] %u\n", st);
      break;
    default:
      break;
    }
  return;
}

void print_config_settings(void)
{
  /* Debug logging */
  CFG("[CONFIG] [PICO] PICO_PIO_VERSION = %d\n", PICO_PIO_VERSION);  // pio.h PICO_PIO_VERSION
  #if defined(PICO_DEFAULT_LED_PIN)
  CFG("[CONFIG] [PICO] PICO_DEFAULT_LED_PIN = %d\n", PICO_DEFAULT_LED_PIN);  // pio.h PICO_PIO_VERSION
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  CFG("[CONFIG] [PICO] CYW43_WL_GPIO_LED_PIN = %d\n", CYW43_WL_GPIO_LED_PIN);  // pio.h PICO_PIO_VERSION
  #endif
  CFG("[CONFIG] [PICO] LED_PWM = %d\n", LED_PWM);  // pio.h PICO_PIO_VERSION
  CFG("[CONFIG] PRINT SETTINGS START\n");
  CFG("[CONFIG] [USBSID PCB VERSION] %s\n", PCB_VERSION);
  CFG("[CONFIG] [USBSID FIRMWARE VERSION] %s\n", project_version);

  CFG("[CONFIG] [CLOCK] %s @%d\n",
    int_ext[(int)usbsid_config.external_clock],
    (int)usbsid_config.clock_rate);
  CFG("[CONFIG] [CLOCK RATE LOCKED] %s\n",
    true_false[(int)usbsid_config.lock_clockrate]);
  CFG("[CONFIG] [RASTER RATE] @%d\n",
    (int)usbsid_config.raster_rate);

  CFG("[CONFIG] [SOCKET ONE] %s as %s\n",
    enabled[(int)usbsid_config.socketOne.enabled],
    ((int)usbsid_config.socketOne.dualsid == 1 ? single_dual[0] : single_dual[1]));
  CFG("[CONFIG] [SOCKET ONE] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketOne.chiptype],
    clonetypes[usbsid_config.socketOne.clonetype]);
  CFG("[CONFIG] [SOCKET ONE] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketOne.sid1type],
    sidtypes[usbsid_config.socketOne.sid2type]);
  CFG("[CONFIG] [SOCKET TWO] %s as %s\n",
    enabled[(int)usbsid_config.socketTwo.enabled],
    ((int)usbsid_config.socketTwo.dualsid == 1 ? single_dual[0] : single_dual[1]));
  CFG("[CONFIG] [SOCKET TWO] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketTwo.chiptype],
    clonetypes[usbsid_config.socketTwo.clonetype]);
  CFG("[CONFIG] [SOCKET TWO] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketTwo.sid1type],
    sidtypes[usbsid_config.socketTwo.sid2type]);
  CFG("[CONFIG] [SOCKET TWO AS ONE] %s\n",
    true_false[(int)usbsid_config.socketTwo.act_as_one]);

  CFG("[CONFIG] [LED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.LED.enabled],
    true_false[(int)usbsid_config.LED.idle_breathe]);
  CFG("[CONFIG] [RGBLED] %s, Idle breathe? %s\n",
    enabled[(int)usbsid_config.RGBLED.enabled],
    true_false[(int)usbsid_config.RGBLED.idle_breathe]);
  CFG("[CONFIG] [RGBLED SIDTOUSE] %d\n",
    (int)usbsid_config.RGBLED.sid_to_use);
  CFG("[CONFIG] [RGBLED BRIGHTNESS] %d\n",
    (int)usbsid_config.RGBLED.brightness);

  CFG("[CONFIG] [CDC] %s\n",
    enabled[(int)usbsid_config.Cdc.enabled]);
  CFG("[CONFIG] [WebUSB] %s\n",
    enabled[(int)usbsid_config.WebUSB.enabled]);
  CFG("[CONFIG] [Asid] %s\n",
    enabled[(int)usbsid_config.Asid.enabled]);
  CFG("[CONFIG] [Midi] %s\n",
    enabled[(int)usbsid_config.Midi.enabled]);

  CFG("[CONFIG] [FMOpl] %s\n",
    enabled[(int)usbsid_config.FMOpl.enabled]);
  CFG("[CONFIG] [FMOpl] SIDno %d\n",
    usbsid_config.FMOpl.sidno);

  #if defined(HAS_AUDIOSWITCH)
  CFG("[CONFIG] [AUDIO_SWITCH] %s\n",
    mono_stereo[(int)usbsid_config.stereo_en]);
  #endif

  CFG("[CONFIG] PRINT SETTINGS END\n");

  return;
}

void print_socket_config(void)
{
  CFG("[CONFIG] SOCK_ONE EN: %s SOCK_TWO EN: %s\n[CONFIG] ACT_AS_ONE: %s\n[CONFIG] NO SIDS\n[CONFIG] SOCK_ONE #%d\n[CONFIG] SOCK_TWO #%d\n[CONFIG] TOTAL #%d\n",
    true_false[sock_one], true_false[sock_two], true_false[act_as_one],
    sids_one, sids_two, numsids);

  return;
}

void print_bus_config(void)
{
  CFG("[CONFIG] BUS\n[CONFIG] ONE:   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[CONFIG] TWO:   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[CONFIG] THREE: %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[CONFIG] FOUR:  %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n",
    one, PRINTF_BYTE_TO_BINARY_INT8(one),
    two, PRINTF_BYTE_TO_BINARY_INT8(two),
    three, PRINTF_BYTE_TO_BINARY_INT8(three),
    four, PRINTF_BYTE_TO_BINARY_INT8(four));
  CFG("[CONFIG] ADDRESS MASKS\n");
  CFG("[CONFIG] MASK_ONE:   0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", one_mask, PRINTF_BYTE_TO_BINARY_INT8(one_mask));
  CFG("[CONFIG] MASK_TWO:   0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", two_mask, PRINTF_BYTE_TO_BINARY_INT8(two_mask));
  CFG("[CONFIG] MASK_THREE: 0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", three_mask, PRINTF_BYTE_TO_BINARY_INT8(three_mask));
  CFG("[CONFIG] MASK_FOUR:  0x%02x 0b"PRINTF_BINARY_PATTERN_INT8"\n", four_mask, PRINTF_BYTE_TO_BINARY_INT8(four_mask));
  return;
}

int verify_fmopl_sidno(void)
{
  int fmoplsidno = -1;
  if (usbsid_config.FMOpl.enabled) {
    if (usbsid_config.socketOne.enabled) {
      if ((usbsid_config.socketOne.chiptype == 1) && (sids_one >= 1)) {
        if (usbsid_config.socketOne.sid1type == 4) {
          fmoplsidno = 1;
          return fmoplsidno;
        } else if ((usbsid_config.socketOne.sid2type == 4) && (sids_one == 2)) {
          fmoplsidno = 2;
          return fmoplsidno;
        }
      }
    }
    if (usbsid_config.socketTwo.enabled) {// && (fmoplsidno == -1)) {
      if ((usbsid_config.socketTwo.chiptype == 1) && (sids_two >= 1)) {
        if (usbsid_config.socketTwo.sid1type == 4) {
          fmoplsidno = (sids_one == 0)
          ? 1 : (sids_one == 1)
          ? 2 : (sids_one == 2)
          ? 3 : 0;
          return fmoplsidno;
        } else if (usbsid_config.socketTwo.sid2type == 4 && (sids_two == 2)) {
          fmoplsidno = (sids_one == 0)
          ? 2 : (sids_one == 1)
          ? 3 : (sids_one == 2)
          ? 4 : 0;
          return fmoplsidno;
        }
      }
    }
  }
  return fmoplsidno;
}

void verify_socket_settings(void)
{
  /* Pre applying default socket settings if needed */
  if (usbsid_config.socketOne.enabled == true) {
    if (usbsid_config.socketOne.dualsid == true) {
      if (usbsid_config.socketOne.chiptype != 1) {
        usbsid_config.socketOne.chiptype = 1;  /* chiptype cannot be real with dualsid */
      }
      if (usbsid_config.socketOne.clonetype == 0) {
        usbsid_config.socketOne.clonetype = 1;  /* clonetype cannot be disabled with dualsid */
      }
    } else {
      if (usbsid_config.socketOne.chiptype == 1) {
        if (usbsid_config.socketOne.clonetype == 0) {
          usbsid_config.socketOne.clonetype = 1;  /* clonetype cannot be disabled when chiptype is clone */
        }
      } else {
        usbsid_config.socketOne.clonetype = 0;  /* clonetype cannot be anything else when chiptype is real */
      }
    }
  }
  /* Pre applying default socket settings if needed */
  if (usbsid_config.socketTwo.enabled == true) {
    if (usbsid_config.socketTwo.dualsid == true) {
      if (usbsid_config.socketTwo.chiptype != 1) {
        usbsid_config.socketTwo.chiptype = 1;  /* chiptype cannot be real with dualsid */
      }
      if (usbsid_config.socketTwo.clonetype == 0) {
        usbsid_config.socketTwo.clonetype = 1;  /* clonetype cannot be disabled with dualsid */
      }
    } else {
      if (usbsid_config.socketTwo.chiptype == 1) {
        if (usbsid_config.socketTwo.clonetype == 0) {
          usbsid_config.socketTwo.clonetype = 1;  /* clonetype cannot be disabled when chiptype is clone */
        }
      } else {
        usbsid_config.socketTwo.clonetype = 0;  /* clonetype cannot be anything else when chiptype is real */
      }
    }
  }

  return;
}

void apply_fmopl_config(void)
{
  /* FMOpl */
  int fmoplsid = verify_fmopl_sidno();
  if (fmoplsid != -1) {
    fmopl_enabled = usbsid_config.FMOpl.enabled;
    usbsid_config.FMOpl.sidno = fmopl_sid = fmoplsid;
  } else {
    fmopl_enabled = usbsid_config.FMOpl.enabled = false;
    usbsid_config.FMOpl.sidno = fmopl_sid = -1;
  }
}

void apply_socket_config(void)
{
  act_as_one = usbsid_config.socketTwo.act_as_one;

  sock_one = usbsid_config.socketOne.enabled;
  sock_two = usbsid_config.socketTwo.enabled;

  sids_one = (sock_one == true) ? (usbsid_config.socketOne.dualsid == true) ? 2 : 1 : 0;
  sids_two = (sock_two == true) ? (usbsid_config.socketTwo.dualsid == true) ? 2 : 1 : 0;
  numsids = (sids_one + sids_two);

  return;
}

void apply_bus_config(void)
{  /* TODO: REWORK! */
  /* Apply defaults */
  sidtype[0] = usbsid_config.socketOne.sid1type;  /* SID1 */
  sidtype[1] = usbsid_config.socketOne.sid2type;  /* SID2 */
  sidtype[2] = usbsid_config.socketTwo.sid1type;  /* SID3 */
  sidtype[3] = usbsid_config.socketTwo.sid2type;  /* SID4 */
  /* one == 0x00, two == 0x20, three == 0x40, four == 0x60 */
  if (act_as_one) {                    /* act-as-one enabled overrules all settings */
    one = two = 0;                     /* CS1 low, CS2 low */
    three = four = 0;                  /* CS1 low, CS2 low */
    one_mask = two_mask = three_mask = four_mask = 0x1F;
    /* No changes to sidtypes */
  } else {
    if (sock_one && !sock_two) {       /* SocketOne enabled, SocketTwo disabled */
      one = 0b100;                     /* CS1 low, CS2 high */
      two = (sids_one == 2) ? 0b100    /* CS1 low, CS2 high */
        : 0b110;                       /* CS1 high, CS2 high */
      three = four = 0b110;            /* CS1 high, CS2 high */
      one_mask = 0x1F;
      two_mask = (sids_one == 2) ? 0x3F : 0x0;
      three_mask = 0x0;
      four_mask = 0x0;
      /* Apply differences */
      sidtype[1] = (sids_one == 2)  /* SID2 */
        ? usbsid_config.socketOne.sid2type : 1;
      sidtype[2] = 1;  /* SID3 */
      sidtype[3] = 1;  /* SID4 */
    }
    if (!sock_one && sock_two) {       /* SocketOne disabled, SocketTwo enabled */
      one = 0b010;                     /* CS1 high, CS2 low */
      two = (sids_two == 2) ? 0b010    /* CS1 high, CS2 low */
        : 0b110;                       /* CS1 high, CS2 high */
      three = four = 0b110;            /* CS1 high, CS2 high */
      one_mask = 0x1F;
      two_mask = (sids_two == 2) ? 0x3F : 0x0;
      three_mask = 0x0;
      four_mask = 0x0;
      /* Apply differences */
      sidtype[0] = usbsid_config.socketTwo.sid1type;  /* SID1 */
      sidtype[1] = (sids_two == 2)  /* SID2 */
        ? usbsid_config.socketTwo.sid2type : 1;
      sidtype[2] = 1;  /* SID3 */
      sidtype[3] = 1;  /* SID4 */
    }
    if (sock_one && sock_two) {        /* SocketOne enabled, SocketTwo enabled */
      /* TODO: Compact if else spiderweb */
      if (sids_one == 1 && sids_two == 1) {
        one   = 0b100;
        two   = 0b010;
        three = 0b110;
        four  = 0b110;
        one_mask = 0x1F;
        two_mask = 0x1F;
        three_mask = 0x0;
        four_mask = 0x0;
        /* Apply differences */
        sidtype[0] = usbsid_config.socketOne.sid1type;  /* SID1 */
        sidtype[1] = usbsid_config.socketTwo.sid1type;  /* SID2 */
        sidtype[2] = 1;  /* SID3 */
        sidtype[3] = 1;  /* SID4 */
      }
      if (sids_one == 2 && sids_two == 1) {
        one   = 0b100;
        two   = 0b100;
        three = 0b010;
        four  = 0b110;
        one_mask = 0x1F;
        two_mask = 0x3F;
        three_mask = 0x1F;
        four_mask = 0x0;
        /* Apply differences */
        sidtype[0] = usbsid_config.socketOne.sid1type;  /* SID1 */
        sidtype[1] = usbsid_config.socketOne.sid2type;  /* SID2 */
        sidtype[2] = usbsid_config.socketTwo.sid1type;  /* SID3 */
        sidtype[3] = 1;  /* SID4 */
      }
      if (sids_one == 1 && sids_two == 2) {
        one   = 0b100;
        two   = 0b010;
        three = 0b010;
        four  = 0b110;
        one_mask = 0x1F;
        two_mask = 0x1F;
        three_mask = 0x3F;
        four_mask = 0x0;
        /* Apply differences */
        sidtype[0] = usbsid_config.socketOne.sid1type;  /* SID1 */
        sidtype[1] = usbsid_config.socketTwo.sid2type;  /* SID2 */
        sidtype[2] = usbsid_config.socketTwo.sid2type;  /* SID3 */
        sidtype[3] = 1;  /* SID4 */
      }
      if (sids_one == 2 && sids_two == 2) {
        one   = 0b100;
        two   = 0b100;
        three = 0b010;
        four  = 0b010;
        one_mask = 0x1F;
        two_mask = 0x3F;
        three_mask = 0x1F;
        four_mask = 0x3F;
        /* No changes to sidtypes */
      }
    }
  }
  return;
}

void apply_led_config(void)
{ /* if SID to use is higher then the number of sids, use first available SID */
  int sid = -1;
  int stou = (usbsid_config.RGBLED.sid_to_use - 1);
  for (int s = 0; s < 4; s++) {
    if (sidtype[s] == 2 || sidtype[s] == 3) {
      sid = (s + 1);
      break;
    }
  }
  CFG("[CONFIG] RGBLED REQUESTED: %d\n", usbsid_config.RGBLED.sid_to_use);
  /* check if requested sidno is actually configured  */
  usbsid_config.RGBLED.sid_to_use
    = (stou > numsids)
    || (sidtype[stou] != 2)
    || (sidtype[stou] != 3)
    ? (sid != -1)  /* If not still -1 */
    ? sid  /* use the first SID that is either 8580 or 6581 */
    : 1    /* else default to SID 1 */
    : usbsid_config.RGBLED.sid_to_use;  /* Else use the programmed SID to use */
  CFG("[CONFIG] RGBLED CALCULATED: %d\n", usbsid_config.RGBLED.sid_to_use);
  return;
}

void apply_config(bool at_boot)
{
  CFG("[CONFIG APPLY] START\n");

  verify_socket_settings();
  CFG("[CONFIG] Applying socket settings\n");
  apply_socket_config();
  if (usbsid_config.FMOpl.enabled) {
    CFG("[CONFIG] Applying optional FMOpl settings\n");
    apply_fmopl_config();
  };
  CFG("[CONFIG] Applying bus settings\n");
  apply_bus_config();
  if (!at_boot) {
    CFG("[CONFIG] Applying bus clock settings\n");
    stop_dma_channels();
    restart_bus_clocks();
    sync_pios(false);
    start_dma_channels();
  }
  if (RGB_ENABLED) {
    CFG("[CONFIG] Applying RGBLED SID\n");
    apply_led_config();
    CFG("[CONFIG APPLY] FINISHED\n");
  } else {
    CFG("[CONFIG] RGBLED not available\n");
  }

  print_config_settings();
  print_socket_config();
  print_bus_config();
  return;
}

void apply_socket_change(void)
{
  verify_socket_settings();
  CFG("[CONFIG] Applying socket settings\n");
  apply_socket_config();
  CFG("[CONFIG] Applying bus settings\n");
  apply_bus_config();
  return;
}

void detect_default_config(void)
{
  CFG("[CONFIG] DETECT DEFAULT CONFIG START\n");
  CFG("[CONFIG] IS DEFAULT CONFIG? %s\n",
    true_false[usbsid_config.default_config]);
  if(usbsid_config.default_config == 1) {
    CFG("[CONFIG] DETECTED DEFAULT CONFIG!\n");
    CFG("[CONFIG] [MAGIC_SMOKE ADDRESS] config struct: 0x%X cm_verification: 0x%X\n",
      &usbsid_config.magic, &cm_verification);
    CFG("[CONFIG] [MAGIC_SMOKE VERIFICATION] config_struct: %u header: %u cm_verification: %u\n",
      usbsid_config.magic, MAGIC_SMOKE, cm_verification);
    usbsid_config.default_config = 0;
    CFG("[CONFIG] DEFAULT CONFIG STATE SET TO %d\n", usbsid_config.default_config);
    first_boot = true;  /* Only at first boot the link popup will be sent */
    save_config(&usbsid_config);
    CFG("[CONFIG] CONFIG SAVED\n");
  }
  CFG("[CONFIG] DETECT DEFAULT FINISHED\n");
  return;
}

int return_clockrate(void)
{
  for (int i = 0; i < count_of(clockrates); i++) {
    if (clockrates[i] == usbsid_config.clock_rate) {
      return i;
    }
  }
  return 0;
}

void apply_clockrate(int n_clock, bool suspend_sids)
{
  if (!usbsid_config.external_clock) {
    if (!usbsid_config.lock_clockrate) {
      if (clockrates[n_clock] != usbsid_config.clock_rate) {
        if (suspend_sids) {
          CFG("[CONFIG] [DISABLE SID]\n");
          disable_sid();
        }
        CFG("[CONFIG] [CLOCK FROM]%d [CLOCK TO]%d\n", usbsid_config.clock_rate, clockrates[n_clock]);
        usbsid_config.clock_rate = clockrates[n_clock];
        usbsid_config.raster_rate = rasterrates[n_clock]; /* Experimental */
        /* Cycled write buffer vars */
        sid_hz = usbsid_config.clock_rate;
        sid_mhz = (sid_hz / 1000 / 1000);
        sid_us = (1 / sid_mhz);
        CFG("[CONFIG] [CFG PICO] %lu Hz, %.0f MHz, %.4f uS\n", clock_get_hz(clk_sys), cpu_mhz, cpu_us);
        CFG("[CONFIG] [CFG C64]  %.0f Hz, %.6f MHz, %.4f uS\n", sid_hz, sid_mhz, sid_us);
        /* Start clock set */
        // ISSUE: EVEN THOUGH THIS IS BETTER IT DOES NOT SOLVE THE CRACKLING/BUS ROT ISSUE!
        restart_bus_clocks();
        stop_dma_channels();
        start_dma_channels();
        sync_pios(false);
        if (suspend_sids) {
          CFG("[ENABLE SID WITH UNMUTE]\n");
          enable_sid(true);
        }
        // ISSUE: WHEN THIS HAPPENS THE CRACKLING ON CYCLE EXACT TUNES IS IMMENSE!
        // THIS IS AFTER PAL -> NTSC -> PAL
        // restart_bus();
        return;
      } else {
        CFG("[CONFIG] [CLOCK FROM]%d AND [CLOCK TO]%d ARE THE SAME, SKIPPING SET_CLOCK\n", usbsid_config.clock_rate, clockrates[n_clock]);
        return;
      }
    } else {
      CFG("[CONFIG] [CLOCK] Clockrate is locked, change from %d to %d not applied\n", usbsid_config.clock_rate, clockrates[n_clock]);
      return;
    }
  }
  return;
}

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
        CFG("[CONFIG] [CLOCK ERROR] Detected unconventional clockrate (%ld) error in config, revert to default\n", usbsid_config.clock_rate);
        usbsid_config.clock_rate = clockrates[0];
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
