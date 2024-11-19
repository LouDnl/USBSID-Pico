/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * config.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024 LouD
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

#include <math.h>

#include "globals.h"
#include "config.h"
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

/* SID externals */
extern uint8_t detect_sid_version(uint8_t start_addr);
extern void sid_test(int sidno, char test, char wf);

/* GPIO externals */
extern void restart_bus(void);

/* Midi externals */
extern void midi_bus_operation(uint8_t a, uint8_t b);
extern int curr_midi_channel;

/* MCU externals */
extern void mcu_reset(void);

/* Init vars */
void apply_config(void);
static uint8_t config_array[FLASH_PAGE_SIZE]; /* 256 MIN ~ FLASH_PAGE_SIZE & 4096 MAX ~ FLASH_SECTOR_SIZE  */
int sock_one, sock_two, sids_one, sids_two, numsids, act_as_one;
uint8_t one, two, three, four;
const char* project_version = PROJECT_VERSION;
static uint8_t p_version_array[MAX_BUFFER_SIZE];

// static const Config usbsid_default_config = {
#define USBSID_DEFAULT_CONFIG_INIT { \
  .magic = MAGIC_SMOKE, \
  .default_config = 1, \
  .external_clock = false, \
  .clock_rate = DEFAULT, \
  .socketOne = { \
    .enabled = true, \
    .dualsid = false, \
    .sidtype = 0x0,  /* empty */ \
  }, \
  .socketTwo = { \
    .enabled = true, \
    .dualsid = false, \
    .act_as_one = false, \
    .sidtype = 0x0,  /* empty */ \
  }, \
  .LED = { \
    .enabled = true, \
    .idle_breathe = true \
  }, \
  .RGBLED = { \
    .enabled = RGB_ENABLED, \
    .idle_breathe = true, \
    .brightness = 0x7F,  /* Half of max brightness */ \
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
      CFG("[R%d] ", i);
    CFG("%02x", buf[i]);
    if (i == (len - 1)) {
      CFG("\n");
    } else if (i % 16 == 15) {
      CFG("\n[R%d] ", i);
    } else {
      CFG(" ");
    }
  }
  CFG("[PRINT CFG END]\n");
}

void detect_sid_types(void)
{
  const char *sidtypes[4] = {"NONE", "CLONE", "MOS8580", "MOS6581"};
  int sid1 = 0, sid2 = 0;
  if (usbsid_config.socketOne.enabled) {
    sid1 = detect_sid_version(0x00);
    usbsid_config.socketOne.sidtype = sid1;
  } else {
    usbsid_config.socketOne.sidtype = 0;
  }
  if (usbsid_config.socketTwo.enabled) {
    if (usbsid_config.socketOne.dualsid) {
      sid2 = detect_sid_version(0x40);
      usbsid_config.socketTwo.sidtype = sid2;
    } else {
      sid2 = detect_sid_version(0x20);
      usbsid_config.socketTwo.sidtype = sid2;
    }
  } else {
    usbsid_config.socketTwo.sidtype = 0;
  }

  CFG("[SOCKET ONE] %s\n", sidtypes[usbsid_config.socketOne.sidtype]);
  CFG("[SOCKET TWO] %s\n", sidtypes[usbsid_config.socketTwo.sidtype]);
  CFG("[READ SID1] [%02x %s]\n", sid1, sidtypes[sid1]);
  CFG("[READ SID2] [%02x %s]\n", sid2, sidtypes[sid2]);
}

void read_config(Config* config)
{

  config_array[0] = READ_CONFIG; /* Initiator byte */
  config_array[1] = 0x7F; /* Verification byte */
  config_array[6] = (int)config->external_clock;
  config_array[7] = (config->clock_rate >> 16) & BYTE;
  config_array[8] = (config->clock_rate >> 8) & BYTE;
  config_array[9] = config->clock_rate & BYTE;
  config_array[10] = (int)config->socketOne.enabled;
  config_array[11] = (int)config->socketOne.dualsid;
  config_array[12] = config->socketOne.sidtype;
  config_array[20] = (int)config->socketTwo.enabled;
  config_array[21] = (int)config->socketTwo.dualsid;
  config_array[22] = config->socketTwo.sidtype;
  config_array[23] = (int)config->socketTwo.act_as_one;
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
}

void read_firmware_version()
{
  p_version_array[0] = USBSID_VERSION;  /* Initiator byte */
  p_version_array[1] = strlen(project_version);  /* Length of version string */
  memcpy(p_version_array+2, project_version, strlen(project_version));
}

void default_config(Config* config)
{
  memcpy(config, &usbsid_default_config, sizeof(Config));
}

void load_config(Config* config)
{
  CFG("[COPY CONFIG] [FROM]0x%x [TO]0x%x  [SIZE]%u\n", XIP_BASE + FLASH_TARGET_OFFSET, (uint)&config, sizeof(Config));
  memcpy(config, (void *)(XIP_BASE + FLASH_TARGET_OFFSET), sizeof(Config));
  stdio_flush();
  if (config->magic != MAGIC_SMOKE) {
      default_config(config);
      return;
  }
}

void save_config_lowlevel(void* config_data)
{
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);  /* 4096 Bytes (sector aligend) erase as per SDK manual */
  flash_range_program(FLASH_TARGET_OFFSET, config_data, CONFIG_SIZE /* FLASH_PAGE_SIZE */);  /* 256 Bytes (page aligned) write as per SDK manual */
  restore_interrupts(ints);
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
}

void handle_config_request(uint8_t * buffer)
{ /* Incoming Config data buffer
   *
   * 5 bytes
   * Byte 0 ~ command
   * Byte 1 ~ struct setting e.g. socketOne or clock_rate
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
      CFG("[XIP_BASE]%u [FLASH_PAGE_SIZE]%u [FLASH_SECTOR_SIZE]%u [FLASH_TARGET_OFFSET]%u\r\n", XIP_BASE, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, FLASH_TARGET_OFFSET);
      CFG("[>]\r\n");
      read_config(&usbsid_config);
      print_cfg(config_array, count_of(config_array));
      CFG("[<]\r\n");
      int writes = count_of(config_array) / 64;  /* BUG: It should send 4 packets of 64 bytes, but sends only 2 and a zero packet */
      CFG("[>] SEND %d WRITES OF 64 BYTES TO %c [<]\n", writes, dtype);
      memset(write_buffer_p, 0, 64);
      for (int i = 0; i < writes; i++) {
        memcpy(write_buffer_p, config_array + (i * 64), 64);
        CFG("[>] SEND WRITE %d OF %d [<]\n", i, writes);
        CFG("[>]");
        for (int i = 0; i < 64; i++) CFG(" %x", write_buffer_p[i]);
        CFG(" [<]\n");
        switch (dtype) {
          case 'C':
            cdc_write(cdc_itf, 64);
            break;
          case 'W':
            webserial_write(wusb_itf, 64);
            break;
        }
      }
      break;
    case APPLY_CONFIG:  /* loads config and applies it */
      CFG("[APPLY_CONFIG]\n");
      load_config(&usbsid_config);
      apply_config();
      break;
    case STORE_CONFIG:
      CFG("[STORE_CONFIG]\n");
      switch (buffer[1]) {
        case 0: /* clock_rate / mixed */
          usbsid_config.clock_rate = clockrates[(int)buffer[2]];
          break;
        case 1: /* socketOne */
          break;
        case 2: /* socketTwo */
          break;
        case 3: /* LED */
          switch (buffer[2]) {
            case 0: /* enabled */
              if (buffer[3] <= 1)  /* 1 or 0 */
                usbsid_config.LED.enabled = (buffer[3] == 1) ? true : false;
              break;
            case 1: /* idle_breathe */
              if (buffer[3] <= 1)  /* 1 or 0 */
                usbsid_config.LED.idle_breathe = (buffer[3] == 1) ? true : false;
              break;
            default:
              break;
            }
          break;
        case 4: /* RGBLED */
          switch (buffer[2]) {
            case 0: /* enabled */
              if (buffer[3] <= 1)  /* 1 or 0 */
                usbsid_config.RGBLED.enabled = (buffer[3] == 1) ? true : false;
              break;
            case 1: /* idle_breathe */
              if (buffer[3] <= 1)  /* 1 or 0 */
                usbsid_config.RGBLED.idle_breathe = (buffer[3] == 1) ? true : false;
              break;
            case 2: /* brightness */
              usbsid_config.RGBLED.brightness = buffer[3];
              break;
            case 3: /* sid_to_use */
              if (buffer[3] <= 3)
                usbsid_config.RGBLED.sid_to_use = buffer[3];
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
      }
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
    case SINGLE_SID:
      CFG("[SINGLE_SID]\n");
      usbsid_config.socketOne.enabled = true;
      usbsid_config.socketTwo.enabled = false;
      usbsid_config.socketOne.dualsid = false;
      usbsid_config.socketTwo.dualsid = false;
      usbsid_config.socketOne.sidtype = 0;
      usbsid_config.socketTwo.sidtype = 0;
      usbsid_config.socketTwo.act_as_one = true;
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      apply_config();
      break;
    case DUAL_SID:
      CFG("[DUAL_SID]\n");
      usbsid_config.socketOne.enabled = true;
      usbsid_config.socketTwo.enabled = true;
      usbsid_config.socketOne.dualsid = false;
      usbsid_config.socketTwo.dualsid = false;
      usbsid_config.socketOne.sidtype = 1;
      usbsid_config.socketTwo.sidtype = 1;
      usbsid_config.socketTwo.act_as_one = false;
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      apply_config();
      break;
    case QUAD_SID:
      CFG("[QUAD_SID]\n");
      usbsid_config.socketOne.enabled = true;
      usbsid_config.socketTwo.enabled = true;
      usbsid_config.socketOne.dualsid = true;
      usbsid_config.socketTwo.dualsid = true;
      usbsid_config.socketOne.sidtype = 1;
      usbsid_config.socketTwo.sidtype = 1;
      usbsid_config.socketTwo.act_as_one = false;
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      apply_config();
      break;
    case TRIPLE_SID:
      CFG("[TRIPLE_SID]\n");
      usbsid_config.socketOne.enabled = true;
      usbsid_config.socketTwo.enabled = true;
      usbsid_config.socketOne.dualsid = true;
      usbsid_config.socketTwo.dualsid = false;
      usbsid_config.socketOne.sidtype = 1;
      usbsid_config.socketTwo.sidtype = 1;
      usbsid_config.socketTwo.act_as_one = false;
      save_config(&usbsid_config);
      mcu_reset();
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
      if (!usbsid_config.external_clock) {
        if (clockrates[(int)buffer[1]] != usbsid_config.clock_rate) {
          CFG("[CLOCK FROM]%d [CLOCK TO]%d\n", usbsid_config.clock_rate, clockrates[(int)buffer[1]]);
          usbsid_config.clock_rate = clockrates[(int)buffer[1]];
          deinit_sidclock();
          init_sidclock();
          /* restart_bus(); */  /* TODO: Enable after bus_clock change */
        } else {
          CFG("[CLOCK FROM]%d AND [CLOCK TO]%d ARE THE SAME, SKIPPING SET_CLOCK\n", usbsid_config.clock_rate, clockrates[(int)buffer[1]]);
        }

      }
      break;
    case DETECT_SIDS:
      CFG("[DETECT_SIDS]\n");
      detect_sid_types();
      break;
    case TEST_ALLSIDS:
      CFG("[TEST_ALLSIDS]\n");
      for (int s = 0; s < numsids; s++) {
        CFG("[START TEST SID %d]\n", s);
        sid_test(s, '1', 'A');
      };
      break;
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
      printf("[TEST_FN]\n");
      printf("[FLASH_TARGET_OFFSET]0x%x\n", FLASH_TARGET_OFFSET);
      printf("[PICO_FLASH_SIZE_BYTES]0x%x\n", PICO_FLASH_SIZE_BYTES);
      printf("[FLASH_SECTOR_SIZE]0x%x\n", FLASH_SECTOR_SIZE);
      printf("[FLASH_PAGE_SIZE]0x%x\n", FLASH_PAGE_SIZE);
      printf("[CONFIG_SIZE]0x%x\n", CONFIG_SIZE);
      printf("A %x %x %d\n", usbsid_config.magic, MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
      printf("A %x %x %d\n", (uint32_t)usbsid_config.magic, (uint32_t)MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
      printf("A %d %d\n", (int)usbsid_config.magic, (int)MAGIC_SMOKE);
      printf("A %d\n", usbsid_config.magic == MAGIC_SMOKE);
      break;
    default:
      break;
    }
}

void apply_config(void)
{
  /* Debug logging */
  CFG("[CONFIG LOG START]\n");
  CFG("[USBSID VERSION]%s\n", project_version);
  CFG("[CLOCK] %s @%d\n", ((int)usbsid_config.external_clock == 0 ? "Internal" : "External"), (int)usbsid_config.clock_rate);
  CFG("[SOCKET ONE] %s as %s\n",((int)usbsid_config.socketOne.enabled == 1 ? "Enabled" : "Disabled"), ((int)usbsid_config.socketOne.dualsid == 1 ? "Dual SID" : "Single SID"));
  CFG("[SOCKET TWO] %s as %s\n",((int)usbsid_config.socketTwo.enabled == 1 ? "Enabled" : "Disabled"), ((int)usbsid_config.socketTwo.dualsid == 1 ? "Dual SID" : "Single SID"));
  CFG("[SOCKET TWO AS ONE] %s\n", ((int)usbsid_config.socketTwo.act_as_one == 1 ? "True" : "False"));
  CFG("[LED] %s, Idle breathe? %s\n", ((int)usbsid_config.LED.enabled == 1 ? "Enabled" : "Disabled"), ((int)usbsid_config.LED.idle_breathe == 1 ? "True" : "False"));
  CFG("[RGBLED] %s, Idle breathe? %s\n", ((int)usbsid_config.RGBLED.enabled == 1 ? "Enabled" : "Disabled"), ((int)usbsid_config.RGBLED.idle_breathe == 1 ? "True" : "False"));
  CFG("[RGBLED BRIGHTNESS] %d\n", (int)usbsid_config.RGBLED.brightness);
  CFG("[CDC] %s\n",((int)usbsid_config.Cdc.enabled == 1 ? "Enabled" : "Disabled"));
  CFG("[WebUSB] %s\n",((int)usbsid_config.WebUSB.enabled == 1 ? "Enabled" : "Disabled"));
  CFG("[Asid] %s\n",((int)usbsid_config.Asid.enabled == 1 ? "Enabled" : "Disabled"));
  CFG("[Midi] %s\n",((int)usbsid_config.Midi.enabled == 1 ? "Enabled" : "Disabled"));
  CFG("[CONFIG LOG END]\n");

  CFG("[APPLY CONFIG START]\n");
  sock_one = usbsid_config.socketOne.enabled;
  sock_two = usbsid_config.socketTwo.enabled;
  act_as_one = usbsid_config.socketTwo.act_as_one;
  sids_one = (sock_one == true) ? (usbsid_config.socketOne.dualsid == true) ? 2 : 1 : 0;
  sids_two = (sock_two == true) ? (usbsid_config.socketTwo.dualsid == true) ? 2 : 1 : 0;
  numsids = (sids_one + sids_two);

  usbsid_config.RGBLED.sid_to_use /* Make sure all SIDs are used if sid to use is higher then the number of sids */
    = (usbsid_config.RGBLED.sid_to_use > 2)
      && (numsids <= 2)
        ? 0 : usbsid_config.RGBLED.sid_to_use;

  /* TODO: Not perfect yet ~ missing remaps for triple sid config settings */
  one = (act_as_one)
    ? 0          // CS1 low, CS2 low
    : (1 << 2);  // CS1 low, CS2 high
  two = (sock_two && numsids == 2)
    ? (1 << 1)   // CS1 high, CS2 low
    : (1 << 2);  // CS1 low, CS2 high
  three = (numsids >= 3) || (sock_two && numsids == 2)
    ? (1 << 1)              // CS1 high, CS2 low
    : (1 << 1) | (1 << 2);  // CS1 high, CS2 high
  four = (numsids == 4)
    ? (1 << 1)              // CS1 high, CS2 low
    : (1 << 1) | (1 << 2);  // CS1 high, CS2 high

  CFG("[CONFIG APPLIED]\n[ONE]%02x [TWO]%02x [THREE]%02x [FOUR]%02x\n[SOCK_ONE EN]%s [SOCK_TWO EN]%s [ACT_AS_ONE]%s\n[NO SIDS][S1#]%d [S2#]%d [T#]%d\n",
    one, two, three, four,
    (sock_one ? "True" : "False"),
    (sock_two ? "True" : "False"),
    (act_as_one ? "True" : "False"),
    sids_one, sids_two, numsids);
}

void detect_default_config(void)
{
  CFG("[IS DEFAULT CONFIG?] %s\n", (usbsid_config.default_config ? "True" : "False"));
  if(usbsid_config.default_config == 1) {
    CFG("[DETECTED DEFAULT CONFIG]\n");
    usbsid_config.default_config = 0;
    save_config(&usbsid_config);
  }
  CFG("[CONFIG LOAD FINISHED]\n");
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
  }
}
