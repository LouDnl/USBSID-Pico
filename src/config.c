/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
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

/* SID externals */
extern uint8_t detect_sid_version(uint8_t start_addr);
extern void sid_test(int sidno, char test, char wf);

/* GPIO externals */
extern void restart_bus(void);

/* Midi externals */
extern void midi_bus_operation(uint8_t a, uint8_t b);

/* MCU externals */
extern void mcu_reset(void);

/* Pre declarations */
void apply_config(void);
void apply_socket_change(void);
void apply_clockrate(int n_clock);

/* Init local vars */
static uint8_t config_array[FLASH_PAGE_SIZE]; /* 256 MIN ~ FLASH_PAGE_SIZE & 4096 MAX ~ FLASH_SECTOR_SIZE  */
static uint8_t socket_config_array[10]; /* 10 bytes is enough for now */
static uint8_t p_version_array[MAX_BUFFER_SIZE];

/* Init vars */
int sock_one = 0, sock_two = 0, sids_one = 0, sids_two = 0, numsids = 0, act_as_one = 0;
uint8_t one = 0, two = 0, three = 0, four = 0;
uint8_t one_mask = 0, two_mask = 0, three_mask = 0, four_mask = 0;
const char* project_version = PROJECT_VERSION;

/* Init string vars for logging */
const char *sidtypes[5] = { "UNKNOWN", "N/A", "MOS8580", "MOS6581", "FMOpl" };
const char *chiptypes[2] = { "Real", "Clone" };
const char *clonetypes[6] = { "Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID" };
const char *int_ext[2] = { "Internal", "External" };
const char *en_dis[2] = { "Enabled", "Disabled" };
const char *true_false[2] = { "True", "False" };
const char *single_dual[2] = { "Dual SID", "Single SID" };

#define USBSID_DEFAULT_CONFIG_INIT { \
  .magic = MAGIC_SMOKE, \
  .default_config = 1, \
  .external_clock = false, \
  .clock_rate = DEFAULT, \
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
    .sid_to_use = 1, \
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
} \

static const Config usbsid_default_config = USBSID_DEFAULT_CONFIG_INIT;

void print_cfg(const uint8_t *buf, size_t len) {
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
    CFG("[READ SID1] [%02x %s]\n", sid1, sidtypes[sid1]);
  }
  if (numsids >= 2) {
    sid2 = detect_sid_version(0x20);
    CFG("[READ SID2] [%02x %s]\n", sid2, sidtypes[sid2]);
  }
  if (numsids >= 3) {
    sid3 = detect_sid_version(0x40);
    CFG("[READ SID3] [%02x %s]\n", sid3, sidtypes[sid3]);
  }
  if (numsids == 4) {
    sid4 = detect_sid_version(0x60);
    CFG("[READ SID4] [%02x %s]\n", sid4, sidtypes[sid4]);
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

  CFG("[SOCKET ONE SID1 TYPE] %s\n", sidtypes[usbsid_config.socketOne.sid1type]);
  CFG("[SOCKET ONE SID2 TYPE] %s\n", sidtypes[usbsid_config.socketOne.sid2type]);
  CFG("[SOCKET TWO SID1 TYPE] %s\n", sidtypes[usbsid_config.socketTwo.sid1type]);
  CFG("[SOCKET TWO SID2 TYPE] %s\n", sidtypes[usbsid_config.socketTwo.sid2type]);

  return;
}

void read_config(Config* config)
{
  memset(socket_config_array, 0, sizeof socket_config_array);  /* Make sure we don't send garbled old data */

  config_array[0] = READ_CONFIG; /* Initiator byte */
  config_array[1] = 0x7F; /* Verification byte */
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
  config_array[63] = 0xFF; // Terminator byte

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

void read_firmware_version()
{
  p_version_array[0] = USBSID_VERSION;  /* Initiator byte */
  p_version_array[1] = strlen(project_version);  /* Length of version string */
  memcpy(p_version_array+2, project_version, strlen(project_version));
  return;
}

void default_config(Config* config)
{
  memcpy(config, &usbsid_default_config, sizeof(Config));
  return;
}

void load_config(Config* config)
{
  CFG("[COPY CONFIG] [FROM]0x%x [TO]0x%x [SIZE]%u\n", XIP_BASE + FLASH_TARGET_OFFSET, (uint)&config, sizeof(Config));
  memcpy(config, (void *)(XIP_BASE + FLASH_TARGET_OFFSET), sizeof(Config));
  stdio_flush();
  if (config->magic != MAGIC_SMOKE) {
      default_config(config);
      return;
  }
  return;
}

void save_config_lowlevel(void* config_data)
{
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);  /* 4096 Bytes (sector aligend) erase as per SDK manual */
  flash_range_program(FLASH_TARGET_OFFSET, config_data, CONFIG_SIZE /* FLASH_PAGE_SIZE */);  /* 256 Bytes (page aligned) write as per SDK manual */
  restore_interrupts(ints);
  return;
}

void save_config(const Config* config)
{
  uint8_t config_data[CONFIG_SIZE] = {0};
  static_assert(sizeof(Config) < CONFIG_SIZE, "[CONFIG SAVE ERROR] Config struct doesn't fit inside CONFIG_SIZE");
  memcpy(config_data, config, sizeof(Config));
  int err = flash_safe_execute(save_config_lowlevel, config_data, 100);
  if (err) {
    CFG("[SAVE ERROR] %d\n", err);
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
    apply_config();
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
  CFG("[CFG BUFFER] %x %x %x %x %x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
  switch (buffer[0]) {
    case RESET_USBSID:
      CFG("[RESET_USBSID]\n");
      mcu_reset();
      break;
    case READ_CONFIG:
      CFG("[READ_CONFIG]\n");
      /* TODO: loads current config and sends it to the requester ~ when config is finished */
      /* ISSUE: Although 4 writes are performed, only the first 2 are received */
      CFG("[XIP_BASE]%u [FLASH_PAGE_SIZE]%u [FLASH_SECTOR_SIZE]%u [FLASH_TARGET_OFFSET]%u\n", XIP_BASE, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, FLASH_TARGET_OFFSET);
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
      CFG("[READ_SOCKETCFG]\n");
      read_socket_config(&usbsid_config);
      print_cfg(socket_config_array, count_of(socket_config_array));
      memset(write_buffer_p, 0, 64);
      memcpy(write_buffer_p, socket_config_array, count_of(socket_config_array));
      write_back_data(64);
      break;
    case APPLY_CONFIG:
      CFG("[APPLY_CONFIG]\n");
      apply_config();
      break;
    case RELOAD_CONFIG:
      CFG("[RELOAD_CONFIG]\n");
      load_config(&usbsid_config);
      apply_config();
      for (int i = 0; i < count_of(clockrates); i++) {
        if (clockrates[i] == usbsid_config.clock_rate) {
          apply_clockrate(i);
        }
      }
      break;
    case SET_CONFIG:
      CFG("[SET_CONFIG]\n");
      switch (buffer[1]) {
        case 0: /* clock_rate / mixed */
          usbsid_config.clock_rate = clockrates[(int)buffer[2]];
          break;
        case 1: /* socketOne */
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
                if (buffer[3] <= 3) {
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
        case 2: /* socketTwo */
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
              if (buffer[3] <= 3) {
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
        case 3: /* LED */
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
        case 4: /* RGBLED */
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
              if (buffer[3] <= 3) {
                usbsid_config.RGBLED.sid_to_use = buffer[3];
              };
              break;
            default:
              break;
          }
          break;
        case 5: /* CDC */
        case 6: /* WEBUSB */
        case 7: /* ASID */
        case 8: /* MIDI */
        default:
          break;
      };
      break;
    case SAVE_CONFIG:
      CFG("[SAVE_CONFIG] and RESET_MCU\n");
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      mcu_reset();
      break;
    case SAVE_NORESET:
      CFG("[SAVE_CONFIG]\n");
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      apply_config();
      break;
    case RESET_CONFIG:
      CFG("[RESET_CONFIG]\n");
      default_config(&usbsid_config);
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      apply_config();
      break;
    case WRITE_CONFIG:
      /* TODO: FINISH */
      CFG("[WRITE_CONFIG] NOT IMPLEMENTED YET!\n");
      break;
    case SINGLE_SID:
      CFG("[SINGLE_SID]\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, false, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case MIRRORED_SID:
      CFG("[MIRRORED_SID]\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, true, false, usbsid_config.socketTwo.chiptype, true);
      break;
    case DUAL_SID:
      CFG("[DUAL_SID]\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, true, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case DUAL_SOCKET1:
      CFG("[DUAL_SOCKET1]\n");
      set_socket_config(buffer[1], true, true, usbsid_config.socketOne.chiptype, false, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case DUAL_SOCKET2:
      CFG("[DUAL_SOCKET2]\n");
      set_socket_config(buffer[1], false, false, usbsid_config.socketOne.chiptype, true, true, usbsid_config.socketTwo.chiptype, false);
      break;
    case QUAD_SID:
      CFG("[QUAD_SID]\n");
      set_socket_config(buffer[1], true, true, 1, true, true, 1, false);
      break;
    case TRIPLE_SID:
      CFG("[TRIPLE_SID SOCKET 1]\n");
      set_socket_config(buffer[1], true, true, 1, true, false, usbsid_config.socketTwo.chiptype, false);
      break;
    case TRIPLE_SID_TWO:
      CFG("[TRIPLE_SID SOCKET 2]\n");
      set_socket_config(buffer[1], true, false, usbsid_config.socketOne.chiptype, true, true, 1, false);
      break;
    case LOAD_MIDI_STATE: /* Load from config into midimachine and apply to SIDs */
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
    case SAVE_MIDI_STATE: /* Save from midimachine into config and save to flash */
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
    case RESET_MIDI_STATE: /* Reset all settings to zero */
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
    case SET_CLOCK: /* Change SID clock frequency */
      CFG("[SET_CLOCK]\n");
      apply_clockrate((int)buffer[1]);
      break;
    case DETECT_SIDS:
      CFG("[DETECT_SIDS]\n");
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
      CFG("[TEST_ALLSIDS]\n");
      for (int s = 0; s < numsids; s++) {
        CFG("[START TEST SID %d]\n", s);
        sid_test(s, '1', 'A');
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
      int t = (buffer[1] == 1 ? '1'  /* All tests */
        : buffer[1] == 2 ? '2'  /* All waveforms test */
        : buffer[1] == 3 ? '3'  /* Filter tests */
        : buffer[1] == 4 ? '4'  /* Envelope tests */
        : buffer[1] == 5 ? '5'  /* Modulation tests */
        : '1');  /* Fallback to all tests */
      int wf = (buffer[2] == 0 ? 'A'  /* All */
        : buffer[2] == 1 ? 'T'  /* Triangle */
        : buffer[2] == 2 ? 'S'  /* Sawtooth */
        : buffer[2] == 3 ? 'P'  /* Pulse */
        : buffer[2] == 4 ? 'N'  /* Noise */
        : 'P');  /* Fallback to pulse waveform */
      CFG("[TEST_SID%d] TEST: %c WF: %c\n", (s + 1), t, wf);
      sid_test(s, t, wf);
      break;
    case USBSID_VERSION:
      CFG("[READ_FIRMWARE_VERSION]\n");
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
    case TEST_FN: /* TODO: Remove before v1 release */
      CFG("[TEST_FN]\n");
      CFG("[FLASH_TARGET_OFFSET]0x%x\n", FLASH_TARGET_OFFSET);
      CFG("[PICO_FLASH_SIZE_BYTES]0x%x\n", PICO_FLASH_SIZE_BYTES);
      CFG("[FLASH_SECTOR_SIZE]0x%x\n", FLASH_SECTOR_SIZE);
      CFG("[FLASH_PAGE_SIZE]0x%x\n", FLASH_PAGE_SIZE);
      CFG("[CONFIG_SIZE]0x%x\n", CONFIG_SIZE);
      CFG("A %x %x %d\n", usbsid_config.magic, MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
      CFG("A %x %x %d\n", (uint32_t)usbsid_config.magic, (uint32_t)MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
      CFG("A %d %d\n", (int)usbsid_config.magic, (int)MAGIC_SMOKE);
      CFG("A %d\n", usbsid_config.magic == MAGIC_SMOKE);

      CFG("[USBSID_SID_MEMORY]\n");
      print_cfg(sid_memory, (numsids * 0x20));
      break;
    default:
      break;
    }
  return;
}

void print_config_settings(void)
{
  /* Debug logging */
  CFG("[PICO] PICO_PIO_VERSION = %d\n", PICO_PIO_VERSION);  // pio.h PICO_PIO_VERSION
  #if defined(PICO_DEFAULT_LED_PIN)
  CFG("[PICO] PICO_DEFAULT_LED_PIN = %d\n", PICO_DEFAULT_LED_PIN);  // pio.h PICO_PIO_VERSION
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  CFG("[PICO] CYW43_WL_GPIO_LED_PIN = %d\n", CYW43_WL_GPIO_LED_PIN);  // pio.h PICO_PIO_VERSION
  #endif
  CFG("[PICO] LED_PWM = %d\n", LED_PWM);  // pio.h PICO_PIO_VERSION
  CFG("[CONFIG] PRINT SETTINGS START\n");
  CFG("[CONFIG] [USBSID VERSION] %s\n", project_version);

  CFG("[CONFIG] [CLOCK] %s @%d\n",
    ((int)usbsid_config.external_clock == 0 ? int_ext[0] : int_ext[1]),
    (int)usbsid_config.clock_rate);

  CFG("[CONFIG] [SOCKET ONE] %s as %s\n",
    ((int)usbsid_config.socketOne.enabled == 1 ? en_dis[0] : en_dis[1]),
    ((int)usbsid_config.socketOne.dualsid == 1 ? single_dual[0] : single_dual[1]));
  CFG("[CONFIG] [SOCKET ONE] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketOne.chiptype],
    clonetypes[usbsid_config.socketOne.clonetype]);
  CFG("[CONFIG] [SOCKET ONE] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketOne.sid1type],
    sidtypes[usbsid_config.socketOne.sid2type]);
  CFG("[CONFIG] [SOCKET TWO] %s as %s\n",
    ((int)usbsid_config.socketTwo.enabled == 1 ? en_dis[0] : en_dis[1]),
    ((int)usbsid_config.socketTwo.dualsid == 1 ? single_dual[0] : single_dual[1]));
  CFG("[CONFIG] [SOCKET TWO] CHIP TYPE: %s, CLONE TYPE: %s\n",
    chiptypes[usbsid_config.socketTwo.chiptype],
    clonetypes[usbsid_config.socketTwo.clonetype]);
  CFG("[CONFIG] [SOCKET TWO] SID 1 TYPE: %s, SID 2 TYPE: %s\n",
    sidtypes[usbsid_config.socketTwo.sid1type],
    sidtypes[usbsid_config.socketTwo.sid2type]);
  CFG("[CONFIG] [SOCKET TWO AS ONE] %s\n",
    ((int)usbsid_config.socketTwo.act_as_one == 1 ? true_false[0] : true_false[1]));

  CFG("[CONFIG] [LED] %s, Idle breathe? %s\n",
    ((int)usbsid_config.LED.enabled == 1 ? en_dis[0] : en_dis[1]),
    ((int)usbsid_config.LED.idle_breathe == 1 ? true_false[0] : true_false[1]));
  CFG("[CONFIG] [RGBLED] %s, Idle breathe? %s\n",
    ((int)usbsid_config.RGBLED.enabled == 1 ? en_dis[0] : en_dis[1]),
    ((int)usbsid_config.RGBLED.idle_breathe == 1 ? true_false[0] : true_false[1]));
  CFG("[CONFIG] [RGBLED SIDTOUSE] %d\n",
    (int)usbsid_config.RGBLED.sid_to_use);
  CFG("[CONFIG] [RGBLED BRIGHTNESS] %d\n",
    (int)usbsid_config.RGBLED.brightness);

  CFG("[CONFIG] [CDC] %s\n",
    ((int)usbsid_config.Cdc.enabled == 1 ? en_dis[0] : en_dis[1]));
  CFG("[CONFIG] [WebUSB] %s\n",
    ((int)usbsid_config.WebUSB.enabled == 1 ? en_dis[0] : en_dis[1]));
  CFG("[CONFIG] [Asid] %s\n",
    ((int)usbsid_config.Asid.enabled == 1 ? en_dis[0] : en_dis[1]));
  CFG("[CONFIG] [Midi] %s\n",
    ((int)usbsid_config.Midi.enabled == 1 ? en_dis[0] : en_dis[1]));
  CFG("[CONFIG] PRINT SETTINGS END\n");

  return;
}

void print_socket_config(void)
{
  CFG("[SOCK_ONE EN] %s [SOCK_TWO EN] %s [ACT_AS_ONE] %s\n[NO SIDS] [SOCK_ONE] #%d [SOCK_TWO] #%d [TOTAL] #%d\n",
    (sock_one ? true_false[0] : true_false[1]),
    (sock_two ? true_false[0] : true_false[1]),
    (act_as_one ? true_false[0] : true_false[1]),
    sids_one, sids_two, numsids);

  return;
}

void print_bus_config(void)
{
  CFG("[BUS]\n[ONE]   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[TWO]   %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[THREE] %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n[FOUR]  %02x 0b"PRINTF_BINARY_PATTERN_INT8"\n",
    one, PRINTF_BYTE_TO_BINARY_INT8(one),
    two, PRINTF_BYTE_TO_BINARY_INT8(two),
    three, PRINTF_BYTE_TO_BINARY_INT8(three),
    four, PRINTF_BYTE_TO_BINARY_INT8(four));
    return;
}

void verify_socket_settings(void)
{
  /* Pre applying default socket settings if needed */
  if (usbsid_config.socketOne.enabled == true) {
    if (usbsid_config.socketOne.dualsid == true) {
      if (usbsid_config.socketOne.chiptype != 1)
        usbsid_config.socketOne.chiptype = 1;  /* chiptype cannot be real with dualsid */
      if (usbsid_config.socketOne.clonetype == 0)
        usbsid_config.socketOne.clonetype = 1;  /* clonetype cannot be disabled with dualsid */
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
      if (usbsid_config.socketTwo.chiptype != 1)
        usbsid_config.socketTwo.chiptype = 1;  /* chiptype cannot be real with dualsid */
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
  /* one == 0x00, two == 0x20, three == 0x40, four == 0x60 */
  if (act_as_one) {                    /* act-as-one enabled overrules all settings */
    one = two = 0;                     /* CS1 low, CS2 low */
    three = four = 0;                  /* CS1 low, CS2 low */
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
      }
    }
  }
  return;
}

void apply_led_config(void)
{
  usbsid_config.RGBLED.sid_to_use /* Make sure all SIDs are used if sid to use is higher then the number of sids */
    = (usbsid_config.RGBLED.sid_to_use > 2)
      && (numsids <= 2)
        ? 0 : usbsid_config.RGBLED.sid_to_use;
  return;
}

void apply_config(void)
{
  CFG("[CONFIG APPLY] START\n");

  verify_socket_settings();
  CFG("[CONFIG] Applying socket settings\n");
  apply_socket_config();
  CFG("[CONFIG] Applying bus settings\n");
  apply_bus_config();
  CFG("[CONFIG] Applying RGBLED SID\n");
  apply_led_config();
  CFG("[CONFIG APPLY] FINISHED\n");

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
  CFG("[CONFIG DETECT DEFAULT] START\n");
  CFG("[IS DEFAULT CONFIG?] %s\n",
    (usbsid_config.default_config ? true_false[0] : true_false[1]));
  if(usbsid_config.default_config == 1) {
    CFG("[DETECTED DEFAULT CONFIG]\n");
    usbsid_config.default_config = 0;
    save_config(&usbsid_config);
  }
  CFG("[CONFIG DETECT DEFAULT] FINISHED\n");
  return;
}

void apply_clockrate(int n_clock)
{
  if (!usbsid_config.external_clock) {
    if (clockrates[n_clock] != usbsid_config.clock_rate) {
      CFG("[CLOCK FROM]%d [CLOCK TO]%d\n", usbsid_config.clock_rate, clockrates[n_clock]);
      usbsid_config.clock_rate = clockrates[n_clock];
      /* Cycled write buffer vars */
      sid_hz = usbsid_config.clock_rate;
      sid_mhz = (sid_hz / 1000 / 1000);
      sid_us = (1 / sid_mhz);
      CFG("[CFG PICO] %lu Hz, %.0f MHz, %.4f uS\n", clock_get_hz(clk_sys), cpu_mhz, cpu_us);
      CFG("[CFG C64]  %.0f Hz, %.6f MHz, %.4f uS\n", sid_hz, sid_mhz, sid_us);
      /* Start clock set */
      deinit_sidclock();
      init_sidclock();
      restart_bus();
      return;
    } else {
      CFG("[CLOCK FROM]%d AND [CLOCK TO]%d ARE THE SAME, SKIPPING SET_CLOCK\n", usbsid_config.clock_rate, clockrates[n_clock]);
      return;
    }
  }
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
        CFG("[CLOCK ERROR] Detected unconventional clockrate (%ld) error in config, revert to default\n", usbsid_config.clock_rate);
        usbsid_config.clock_rate = clockrates[0];
        save_config(&usbsid_config);
        load_config(&usbsid_config);
        mcu_reset();
        break;
    }
    return;
  }
  return;
}
