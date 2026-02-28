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

/* Hardware api's */
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "globals.h"
#include "config.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"
#include "config_constants.h"


/* usbsid.c */
extern void cdc_write(volatile uint8_t * itf, uint32_t n);
extern void webserial_write(volatile uint8_t * itf, uint32_t n);
extern char rtype;
extern uint8_t *cdc_itf;
extern uint8_t *wusb_itf;
extern uint8_t *write_buffer_p;
extern double cpu_mhz, cpu_us;
extern double sid_hz, sid_mhz, sid_us;
#if defined(ONBOARD_EMULATOR) || defined(ONBOARD_SIDPLAYER)
extern uint8_t *sid_memory;
#else
extern uint8_t sid_memory[];
#endif
extern queue_t sidtest_queue;
extern bool auto_config;
#if defined(ONBOARD_EMULATOR) || defined(ONBOARD_SIDPLAYER)
/* Offload Vu to Core1 when running C64 on Core2 */
extern volatile bool offload_ledrunner;
#endif

/* dma.c */
extern void stop_dma_channels(void);
extern void start_dma_channels(void);

/* pio.c */
extern void init_sidclock(void);
extern void deinit_sidclock(void);
extern void sync_pios(bool at_boot);
extern void restart_bus_clocks(void);

/* sid.c */
extern void enable_sid(bool unmute);
extern void disable_sid(void);
extern void mute_sid(void);
extern void unmute_sid(void);
extern void reset_sid(void);
extern void reset_sid_registers(void);

/* bus.c */
extern uint8_t cycled_read_operation(uint8_t address, uint16_t cycles);
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern uint16_t cycled_delay_operation(uint16_t cycles);
extern void restart_bus(void);

/* gpio.c */
extern void toggle_audio_switch(void);
extern void set_audio_switch(bool state);

/* mcu.c */
extern void mcu_reset(void);

/* midi.c */
extern void midi_bus_operation(uint8_t a, uint8_t b);

/* sid_detection.c */
extern ConfigError sid_auto_detect(bool at_boot);
extern ChipType detect_chiptype_at(uint8_t base_address);
extern SIDType detect_sidtype_at(uint8_t base_address, uint8_t chiptype);
extern bool detect_fmopl(uint8_t base_address);
extern void auto_detect_routine(void);
extern uint8_t detect_sid_model(uint8_t start_addr);
extern uint8_t detect_sid_version(uint8_t start_addr);
extern uint8_t detect_sid_reflex(uint8_t start_addr);
extern uint8_t detect_sid_version_skpico_deprecated(uint8_t start_addr);

/* sid_tests.c */
extern void sid_test(int sidno, char test, char wf);
extern bool running_tests;

/* sid_cloneconfig.c */
extern void read_fpgasid_configuration(uint8_t base_address);
extern void read_skpico_configuration(uint8_t base_address, uint8_t profile);
extern void reset_switch_pdsid_type(void);
extern uint8_t read_pdsid_sid_type(uint8_t base_address);
extern bool set_pdsid_sid_type(uint8_t base_address, uint8_t type);

/* config_bus.c */
extern void apply_runtime_config(const Config *config, RuntimeCFG *rt);

/* config_socket.c */
ConfigError validate_config(void);
extern void socket_config_fallback(void);
extern void apply_fmopl_config(void);
void apply_preset_wrapper(SocketPreset preset);

/* config_logging.c */
extern void print_cfg(const uint8_t *buf, size_t len, bool newline);
extern void print_cfg_addr(void);
extern void print_pico_features(void);
extern void print_config_overview(void);
extern void print_config_summary(void);
extern void print_runtime_summary(void);

/* Config logging locals */
void (*config_print[2])(void) = { print_cfg_addr, print_pico_features };

/* Pre declarations */
ConfigError apply_config(bool at_boot);
void save_load_apply_config(bool at_boot);
void save_config_ext(void);
int return_clockrate(void);
void apply_clockrate(int n_clock, bool suspend_sids);
ConfigError err = 0;

/* SID player */
#ifdef ONBOARD_EMULATOR
extern bool stop_cynthcart(void); /* TODO: Remove double declaration */
extern void set_logging(int logid);
extern void unset_logging(int logid);
bool
  emulator_running,
  starting_emulator,
  stopping_emulator;
#endif /* ONBOARD_EMULATOR */
#if defined(ONBOARD_SIDPLAYER)
extern volatile bool
  sidplayer_init,
  sidplayer_start,
  sidplayer_playing,
  sidplayer_stop,
  sidplayer_next,
  sidplayer_prev;
/* SID player locals */
uint8_t * sidfile = NULL; /* Temporary buffer to store incoming data */
volatile int sidfile_size = 0;
volatile char tuneno = 0;
volatile bool is_prg = false; /* Default to SID file */
static int sidbytes_received = 0;
static bool receiving_sidfile = 0;
#endif /* ONBOARD_SIDPLAYER */

/* Declare variables */
Config usbsid_config = {0};
RuntimeCFG cfg = {0};
volatile bool first_boot = false;
const char __in_flash("us_vars") *project_version = PROJECT_VERSION;
const char __in_flash("us_vars") *pcb_version = PCB_VERSION;
const char __in_flash("us_vars") *us_product = USBSID_PRODUCT;

/* Declare local variables */
/* 0x15 (16) max before starting at 0 flash sector erase */
static uint8_t config_saveid = 0;
/* 256 Bytes MAX == FLASH_PAGE_SIZE (Max storage size is 4096 bytes == FLASH_SECTOR_SIZE) */
static uint8_t config_array[FLASH_PAGE_SIZE] = {0};
/* 10 bytes is enough for now */
static uint8_t socket_config_array[10];
/* 64 byte array for copying the config version into */
static uint8_t p_version_array[MAX_BUFFER_SIZE] = {0};
/* Config magic verification storage */
static uint32_t cm_verification = 0;


void read_config(Config* config)
{
  memset(config_array, 0, sizeof config_array);  /* Make sure we don't send garbled old data */

  usCFG("Reading configuration from 0x%x to 0x%x with size %u\n", (uint)config, &config_array, sizeof(Config));

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
  config_array[13] = 0xff; /* unused */
  config_array[14] = config->socketOne.sid1.type;
  config_array[15] = config->socketOne.sid2.type;
  config_array[20] = (int)config->socketTwo.enabled;
  config_array[21] = (int)config->socketTwo.dualsid;
  config_array[22] = (int)config->mirrored;
  config_array[23] = config->socketTwo.chiptype;
  config_array[24] = 0xff; /* unused */
  config_array[25] = config->socketTwo.sid1.type;
  config_array[26] = config->socketTwo.sid2.type;
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
  config_array[58] = config->lock_audio_sw;
  config_array[63] = 0xFF;  /* Terminator byte */

  return;
}

void read_socket_config(Config* config)
{
  memset(socket_config_array, 0, sizeof socket_config_array);  /* Make sure we don't send garbled old data */

  socket_config_array[0] = READ_SOCKETCFG; /* Initiator byte */
  socket_config_array[1] = 0x7F; /* Verification byte */

  socket_config_array[2] = ((int)config->socketOne.enabled << 4) | (int)config->socketOne.dualsid;
  socket_config_array[3] = (config->socketOne.chiptype << 4) /* | config->socketOne.clonetype */;
  socket_config_array[4] = (config->socketOne.sid1.type << 4) | config->socketOne.sid2.type;

  socket_config_array[5] = ((int)config->socketTwo.enabled << 4) | (int)config->socketTwo.dualsid;
  socket_config_array[6] = (config->socketTwo.chiptype << 4) /* | config->socketTwo.clonetype */;
  socket_config_array[7] = (config->socketTwo.sid1.type << 4) | config->socketTwo.sid2.type;

  socket_config_array[8] = (int)config->mirrored;

  socket_config_array[9] = 0xFF;  /* Terminator byte */

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
  config_saveid = config->config_saveid;  /* Preserve config saveid */
  memcpy(config, &usbsid_default_config, sizeof(Config));
  config->config_saveid = config_saveid;  /* Copy saveid back into the default config */
  return;
}

void __no_inline_not_in_flash_func(load_config)(Config* config)
{
  print_cfg_addr();
  usNFO("\n");
  usCFG("Loading configuration from flash\n");
  int savelocationid = 0;  /* counter for finding the current save location */
AGAIN:
  Config temp_config;
  /* NOTICE: Do not do any logging here after memcpy or the Pico will freeze! */
  memcpy(&temp_config, (void *)(XIP_BASE + (FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(Config));
  stdio_flush();
  usCFG("  Flash position = %d (Saved config id = %d)\n", savelocationid, temp_config.config_saveid);
  if (/* (temp_config.config_saveid >= 0) &&  */(temp_config.config_saveid <= 0xF) /* Max 16 saves */
    && temp_config.config_saveid == savelocationid) { /* Found previously saved config */
    savelocationid++;  /* Increase id and try again */
    goto AGAIN;
  } else { /* They are not equal, that means this config is an empty save location or corrupted, load the previous config */
    usCFG("  Found latest configuration at position %d (255 == empty slot)\n", (savelocationid-1));
    savelocationid--;
    memcpy(config, (void *)(XIP_BASE + (FLASH_CONFIG_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(Config));
    stdio_flush();
  }
  config_saveid = usbsid_config.config_saveid;  /* copy saveid into variable */
  usCFG("  Configuration loaded from position %d\n", usbsid_config.config_saveid);

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

void __no_inline_not_in_flash_func(save_config)(Config* config)
{
  usNFO("\n");
  usCFG("Saving configuration:\n");
  /* Verify config saveid and increase */
  int noerr = (usbsid_config.config_saveid == config_saveid);
  if (noerr) {
    /* Only increase id's if both are equal */
    config_saveid++;
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
 * @brief Handles incoming config request buffers
 *
 * @param uint8_t buffer of max 64 bytes
 * @param uint32_t size length of the buffer
 *
 * 5 bytes
 * Byte 0 ~ command
 * Byte 1 ~ struct setting e.g. socketOne, clock_rate or additional command
 * Byte 2 ~ setting entry e.g. dualsid
 * Byte 3 ~ new value
 * Byte 4 ~ reserved
 * >= 6 bytes
 * Byte 0 ~ write command e.g. WRITE_CONFIG
 * Byte 1 ~ config type or byte 1
 * Byte 2 ... 61 the data
 */
void handle_config_request(uint8_t * buffer, uint32_t size)
{
  if (buffer[0] < 0xD0) { /* Don't log incoming buffer to avoid spam above this region */
    usCFG("[CONFIG BUFFER] %x %x %x %x %x\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
  }
  switch (buffer[0]) {
    case RESET_USBSID:
      usCFG("[CMD] RESET_USBSID\n");
      mcu_reset();
      break;
    case READ_CONFIG:
      usCFG("[CMD] READ_CONFIG\n");
      /* ISSUE: Although 4 writes are performed, only the first 2 are received */
      read_config(&usbsid_config);
      print_cfg(config_array, count_of(config_array), false);
      int writes = count_of(config_array) / 64;  /* ISSUE: It should send 4 packets of 64 bytes, but sends only 2 and a zero packet */
      memset(write_buffer_p, 0, 64);
      for (int i = 0; i < writes; i++) {
        memcpy(write_buffer_p, config_array + (i * 64), 64);
        write_back_data(64);
      }
      break;
    case READ_SOCKETCFG:
      usCFG("[CMD] READ_SOCKETCFG\n");
      read_socket_config(&usbsid_config);
      print_cfg(socket_config_array, 10, false);
      memset(write_buffer_p, 0, 64);
      memcpy(write_buffer_p, socket_config_array, 10);
      write_back_data(10);
      break;
    case READ_NUMSIDS:
      usCFG("[CMD] READ_NUMSIDS: %u\n", (uint8_t)cfg.numsids);
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = (uint8_t)cfg.numsids;
      write_back_data(1);
      break;
    case READ_FMOPLSID:
      usCFG("[CMD] READ_FMOPLSID\n");
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = (uint8_t)cfg.fmopl_sid;
      write_back_data(1);
      break;
    case APPLY_CONFIG:
      usCFG("[CMD] APPLY_CONFIG\n");
      apply_config(false);
      break;
    case RELOAD_CONFIG:
      usCFG("[CMD] RELOAD_CONFIG\n");
      load_config(&usbsid_config);
      apply_config(false);
      for (uint i = 0; i < count_of(clockrates); i++) {
        if (clockrates[i] == usbsid_config.clock_rate) {
          apply_clockrate(i, true);
        }
      }
      break;
    case SET_CONFIG:
      usCFG("[CMD] SET_CONFIG\n");
      switch (buffer[1]) {
        case  0:  /* clock_rate */
          /* will always be available to change the setting since it doesn't apply it */
          usbsid_config.clock_rate = clockrates[(int)buffer[2]];
          usbsid_config.refresh_rate = refreshrates[(int)buffer[2]]; /* Experimental */
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
                if (buffer[3] < CHIP_COUNT) {
                  usbsid_config.socketOne.chiptype = buffer[3];
                }
                break;
              case 3: /* UNUSED */
                break;
              case 4: /* sid1.type */
                if (buffer[3] <= 4) {
                  usbsid_config.socketOne.sid1.type = buffer[3];
                }
                break;
              case 5: /* sid2.type */
                if (buffer[3] <= 4) {
                  usbsid_config.socketOne.sid2.type = buffer[3];
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
              if (buffer[3] < CHIP_COUNT) {
                usbsid_config.socketTwo.chiptype = buffer[3];
              }
              break;
            case 3: /* UNUSED */
              break;
            case 4: /* sid1.type */
              if (buffer[3] <= 4) {
                usbsid_config.socketTwo.sid1.type = buffer[3];
              }
              break;
            case 5: /* sid2.type */
              if (buffer[3] <= 4) {
                usbsid_config.socketTwo.sid2.type = buffer[3];
              }
              break;
            case 6: /* mirrored */
              if (buffer[3] <= 1) { /* 1 or 0 */
                usbsid_config.mirrored = (buffer[3] == 1) ? true : false;
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
        case  5:  /* CDC ~ Cannot be disabled */
        case  6:  /* WEBUSB ~ Cannot be disabled */
          break;
        case  7:  /* ASID */
          usbsid_config.Asid.enabled = (bool)buffer[2];
          /* usbsid_config.Asid.buffered = (bool)buffer[3]; */
          break;
        case  8:  /* MIDI */
          usbsid_config.Midi.enabled = (bool)buffer[2];
          break;
        case  9:  /* FMOpl */
          usbsid_config.FMOpl.enabled = (bool)buffer[2];
          apply_fmopl_config();
          break;
        case 10:  /* Audio switch */
          usbsid_config.stereo_en =
          (buffer[2] == 0 || buffer[2] == 1)
          ? (bool)buffer[2]
          : true;  /* Default to 1 ~ stereo if incorrect value */
          break;
        case 11:  /* Lock audio switch */
          usbsid_config.lock_audio_sw =
          (buffer[2] == 0 || buffer[2] == 1)
          ? (bool)buffer[2]
          : false;  /* Default to unlocked incorrect value */
          break;
        default:
          break;
      };
      break;
    case SAVE_CONFIG:
      usCFG("[CMD] SAVE_CONFIG and RESET_MCU\n");
      save_config(&usbsid_config);
      load_config(&usbsid_config);
      mcu_reset();
      break;
    case SAVE_NORESET:
      usCFG("[CMD] SAVE_CONFIG no RESET\n");
      save_load_apply_config(false);
      break;
    case RESET_CONFIG:
      usCFG("[CMD] RESET_CONFIG\n");
      default_config(&usbsid_config);
      save_config(&usbsid_config);
      if (buffer[1] != 0) {
        mcu_reset();
      } else {
        load_config(&usbsid_config);
        apply_config(false);
      }
      break;
    case WRITE_CONFIG:  /* TODO: FINISH */
      /* Max size of incoming buffer = 61 */
      usCFG("[CMD] WRITE_CONFIG\n");
      print_cfg(buffer, size, false);
      switch (buffer[1]) {
        case FULL_CONFIG:
        case SOCKET_CONFIG:
        case MIDI_CONFIG:
        case MIDI_CCVALUES:
        default:
          break;
      }
      break;
    case SINGLE_SID:
      int single_socket = ((buffer[2] == 1) ? 2 : 0);
      usCFG("[CMD] SINGLE_SID SOCKET %d\n", single_socket);
      if (single_socket == 2) {
        apply_preset_wrapper(PRESET_SINGLE_S2);
      } else {
        apply_preset_wrapper(PRESET_SINGLE_S1);
      }
      break;
    case FLIP_SOCKETS:
      usCFG("[CMD] FLIP_SOCKETS\n");
      extern void flip_sockets(void);
      flip_sockets();
      break;
    case MIRRORED_SID:
      usCFG("[CMD] MIRRORED_SID\n");
      apply_preset_wrapper(PRESET_MIRRORED);
      break;
    case DUAL_SID:
      usCFG("[CMD] DUAL_SID\n");
      apply_preset_wrapper(PRESET_DUAL_BOTH);
      break;
    case DUAL_SOCKET1:
      usCFG("[CMD] DUAL_SOCKET 1\n");
      apply_preset_wrapper(PRESET_DUAL_S1);
      break;
    case DUAL_SOCKET2:
      usCFG("[CMD] DUAL_SOCKET 2\n");
      apply_preset_wrapper(PRESET_DUAL_S2);
      break;
    case QUAD_SID:
      usCFG("[CMD] QUAD_SID\n");
      apply_preset_wrapper(PRESET_QUAD);
      break;
    case TRIPLE_SID:
      usCFG("[CMD] TRIPLE_SID SOCKET 1\n");
      apply_preset_wrapper(PRESET_TRIPLE_S1);
      break;
    case TRIPLE_SID_TWO:
      usCFG("[CMD] TRIPLE_SID SOCKET 2\n");
      apply_preset_wrapper(PRESET_TRIPLE_S2);
      break;
    case LOAD_MIDI_STATE:   /* Load from config into midimachine and apply to SIDs */
      // usCFG("[CMD] LOAD_MIDI_STATE\n");
      // for (int i = 0; i < 4; i++) {
      //   usCFG("[SID %d]", (i + 1));
      //   for (int j = 0; j < 32; j++) {
      //     /* ISSUE: this only loads 1 channel state and should actually save all channel states */
      //     midimachine.channel_states[curr_midi_channel][i][j] = usbsid_config.Midi.sid_states[i][j];
      //     midimachine.channelkey_states[curr_midi_channel][i][i] = 0;  /* Make sure extras is always initialised @ zero */
      //     usCFG(" %02x", midimachine.channel_states[curr_midi_channel][i][j]);
      //     midi_bus_operation((0x20 * i) | j, midimachine.channel_states[curr_midi_channel][i][j]);
      //   }
      //   usCFG("\n");
      // }
      break;
    case SAVE_MIDI_STATE:   /* Save from midimachine into config and save to flash */
      // usCFG("[CMD] SAVE_MIDI_STATE\n");
      // for (int i = 0; i < 4; i++) {
      //   usCFG("[SID %d]", (i + 1));
      //   for (int j = 0; j < 32; j++) {
      //     /* ISSUE: this only loads 1 channel state and should actually save all channel states */
      //     usbsid_config.Midi.sid_states[i][j] = midimachine.channel_states[curr_midi_channel][i][j];
      //     usCFG(" %02x", usbsid_config.Midi.sid_states[i][j]);
      //   }
      //   usCFG("\n");
      // }
      // save_config(&usbsid_config);
      break;
    case RESET_MIDI_STATE:  /* Reset all settings to zero */
      // usCFG("[CMD] RESET_MIDI_STATE\n");
      // for (int i = 0; i < 4; i++) {
      //   usCFG("[SID %d]", (i + 1));
      //   for (int j = 0; j < 32; j++) {
      //     // BUG: this only resets 1 channel state
      //     usbsid_config.Midi.sid_states[i][j] = midimachine.channel_states[curr_midi_channel][i][j] = 0;
      //     usCFG(" %02x", usbsid_config.Midi.sid_states[i][j]);
      //     midi_bus_operation((0x20 * i) | j, midimachine.channel_states[curr_midi_channel][i][j]);
      //   }
      //   usCFG("\n");
      // }
      // save_config(&usbsid_config);
      /* mcu_reset(); */
      break;
    case SET_CLOCK:         /* Change SID clock frequency by array id */
      usCFG("[CMD] SET_CLOCK\n");
      /* locked clockrate check is done in apply_clockrate */
      bool suspend_sids = (buffer[2] == 1) ? true : false;  /* Set RES low while changing clock? */
      apply_clockrate((int)buffer[1], suspend_sids);
      break;
    case GET_CLOCK:         /* Returns the clockrate as array id in byte 0 */
      usCFG("[CMD] GET_CLOCK\n");
      int clk_rate_id = return_clockrate();
      memset(write_buffer_p, 0, 64);
      write_buffer_p[0] = clk_rate_id;
      write_back_data(1);
      break;
    case LOCK_CLOCK:        /* Locks the clockrate from being changed, saved in config */
      usCFG("[CMD] LOCK_CLOCK\n");
      if (buffer[1] == 0 || buffer[1] == 1) { /* Verify correct data */
        usbsid_config.lock_clockrate = (bool)buffer[1];
      } else {
        usCFG("[LOCK_CLOCK] Received incorrect value of %d\n", buffer[1]);
      }
      if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
        usCFG("[LOCK_CLOCK] SAVE_CONFIG\n");
        save_load_apply_config(false);
      }
      break;
    case TOGGLE_AUDIO:      /* Toggle the audio state regardless of config setting */
      usCFG("[CMD] TOGGLE_AUDIO\n");
      toggle_audio_switch();  /* if HAS_AUDIOSWITCH is not defined, this doesn't do anything */
      break;
    case SET_AUDIO:         /* Set the audio state from buffer setting (saves config if provided) */
      usCFG("[CMD] SET_AUDIO\n");
      if (!usbsid_config.lock_audio_sw) {
        usbsid_config.stereo_en =
          (buffer[1] == 0 || buffer[1] == 1)
          ? (bool)buffer[1]
          : true;  /* Default to 1 ~ stereo if incorrect value */
        set_audio_switch(usbsid_config.stereo_en);
        if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
          usCFG("[SET_AUDIO] SAVE_CONFIG\n");
          save_load_apply_config(false);
        }
      } else {
        usCFG("Audio switch is %s, requested change to %d (%s)\n",
          monostereo_str((int)usbsid_config.stereo_en), buffer[1], monostereo_str(buffer[1]));
        return;
      }
      break;
    case GET_AUDIO: /* TODO: Finish */
      break;
    case LOCK_AUDIO:
      usCFG("[CMD] LOCK_AUDIO\n");
      usbsid_config.lock_audio_sw =
        (buffer[1] == 0 || buffer[1] == 1)
        ? (bool)buffer[1]
        : false;  /* Default to false if incorrect value ~ don't lock */
      if (buffer[2] == 1) {  /* Save and apply if set to a 1 */
        usCFG("[SET_AUDIO] SAVE_CONFIG\n");
        save_load_apply_config(false);
      }
      break;
    case DETECT_SIDS:       /* Detect SID types per socket */
      if (buffer[1] == 0) {
        usCFG("[CMD] DETECT_SIDS (ALL)\n");
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
        switch (rtype) {
          case 'C':
            cdc_write(cdc_itf, 64);
            break;
          case 'W':
            webserial_write(wusb_itf, 64);
            break;
        }
      } else if (buffer[1] == 1) { /* Only if 1, else just skip! */
        if (buffer[2] < 4) {
          usCFG("[CMD] DETECT_SIDS @ $%02x\n", buffer[3]);
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
      usCFG("[CMD] DETECT_CLONES\n");
      usbsid_config.socketOne.chiptype = detect_chiptype_at(usbsid_config.socketOne.sid1.addr);
      usbsid_config.socketTwo.chiptype = detect_chiptype_at(usbsid_config.socketTwo.sid1.addr);
      memset(write_buffer_p, 0 ,64);  /* Empty the write buffer pointer */
      read_config(&usbsid_config);  /* Read the config into the config buffer */
      memcpy(write_buffer_p, config_array, 64);  /* Copy the first 64 bytes from the buffer into the write buffer */
      switch (rtype) {
        case 'C':
          cdc_write(cdc_itf, 64);
          break;
        case 'W':
          webserial_write(wusb_itf, 64);
          break;
      }
      break;
    case AUTO_DETECT:
      usCFG("[CMD] AUTO_DETECT\n");
      err = sid_auto_detect(false);
      if (err != CFG_OK) {
        usERR("[CMD] Auto detection failed: %s\n", config_error_str(err));
      }
      if (buffer[1] == 1) { /* Save and reboot */
        save_config_ext();
        mcu_reset(); /* Point of no return */
      } else {
        save_load_apply_config(true);
        memset(write_buffer_p, 0 ,64);  /* Empty the write buffer pointer */
        read_config(&usbsid_config);    /* Read the config into the config buffer */
        memcpy(write_buffer_p, config_array, 64);  /* Copy the first 64 bytes from the buffer into the write buffer */
        switch (rtype) {
          case 'C':
            cdc_write(cdc_itf, 64);
            break;
          case 'W':
            webserial_write(wusb_itf, 64);
            break;
        }
      }
      break;
    case TEST_ALLSIDS:
      usCFG("[CMD] TEST_ALLSIDS\n");
      running_tests = true;
      for (int s = 0; s < cfg.numsids; s++) {
        usCFG("[START TEST SID %d]\n", s);
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
      usCFG("[CMD] TEST_SID %d TEST: %c WF: %c\n", (s + 1), t, wf);
      running_tests = true;
      sidtest_queue_entry_t s_entry = {sid_test, s, t, wf};
      queue_try_add(&sidtest_queue, &s_entry);
      break;
    case STOP_TESTS:
      usCFG("[CMD] STOP_TESTS\n");
      running_tests = false;
      break;
    case USBSID_VERSION:
      usCFG("[CMD] READ_FIRMWARE_VERSION\n");
      memset(p_version_array, 0, count_of(p_version_array));
      read_firmware_version();
      memset(write_buffer_p, 0, MAX_BUFFER_SIZE);
      memcpy(write_buffer_p, p_version_array, MAX_BUFFER_SIZE);
        switch (rtype) {
          case 'C':
            cdc_write(cdc_itf, MAX_BUFFER_SIZE);
            break;
          case 'W':
            webserial_write(wusb_itf, MAX_BUFFER_SIZE);
            break;
        }
      break;
    case US_PCB_VERSION:
      usCFG("[CMD] READ_PCB_VERSION\n");
      if (buffer[1] == 0) {  /* Large write 64 bytes */
        memset(p_version_array, 0, count_of(p_version_array));
        read_pcb_version();
        memset(write_buffer_p, 0, MAX_BUFFER_SIZE);
        memcpy(write_buffer_p, p_version_array, MAX_BUFFER_SIZE);
          switch (rtype) {
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
      usCFG("[CMD] RESTART_BUS\n");
      restart_bus();
      break;
    case RESTART_BUS_CLK:
     usCFG("[CMD] RESTART_BUS_CLK\n");
      restart_bus_clocks();
      break;
    case SYNC_PIOS:
      usCFG("[CMD] SYNC_PIOS\n");
      sync_pios(false);
      break;
    case TEST_FN: /* TODO: Remove before v1 release */
      if (buffer[1] == 0) {
        usCFG("[CMD] TEST_FN\n");
        usCFG("[FLASH_CONFIG_OFFSET]0x%x\n", FLASH_CONFIG_OFFSET);
        usCFG("[PICO_FLASH_SIZE_BYTES]0x%x\n", PICO_FLASH_SIZE_BYTES);
        usCFG("[FLASH_SECTOR_SIZE]0x%x\n", FLASH_SECTOR_SIZE);
        usCFG("[FLASH_PAGE_SIZE]0x%x\n", FLASH_PAGE_SIZE);
        usCFG("[CONFIG_SIZE]0x%x\n", CONFIG_SIZE);
        usCFG("A %x %x %d\n", usbsid_config.magic, MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
        usCFG("A %x %x %d\n", (uint32_t)usbsid_config.magic, (uint32_t)MAGIC_SMOKE, usbsid_config.magic != MAGIC_SMOKE);
        usCFG("A %d %d\n", (int)usbsid_config.magic, (int)MAGIC_SMOKE);
        usCFG("A %d\n", usbsid_config.magic == MAGIC_SMOKE);

        usCFG("[TEST_CONFIG_START]\n");
        usCFG("[MAGIC_SMOKE ERROR?] config: %u header: %u cm_verification: %u\n", usbsid_config.magic, MAGIC_SMOKE, cm_verification);

        Config test_config;
        memcpy(&test_config, (void *)(XIP_BASE + FLASH_CONFIG_OFFSET), sizeof(Config));
        usCFG("A %x %x %d\n", test_config.magic, MAGIC_SMOKE, test_config.magic != MAGIC_SMOKE);
        usCFG("A %x %x %d\n", (uint32_t)test_config.magic, (uint32_t)MAGIC_SMOKE, test_config.magic != MAGIC_SMOKE);
        usCFG("A %d %d\n", (int)test_config.magic, (int)MAGIC_SMOKE);
        usCFG("A %d\n", test_config.magic == MAGIC_SMOKE);
        read_config(&test_config);
        print_cfg(config_array, count_of(config_array), false);
        usCFG("[TEST_CONFIG_END]\n");
      }
      if (buffer[1] == 1) {
        usCFG("[USBSID_SID_MEMORY]\n");
        print_cfg(sid_memory, (cfg.numsids * 0x20), false);
      }
      break;
    case TEST_FN2:
      uint8_t st = 0xFF;
      #ifdef ONBOARD_EMULATOR
      if (buffer[1] == 0) {
        usCFG("START EMULATOR\n");
        emulator_running = false;
        offload_ledrunner = true;
        starting_emulator = true;
      }
      if (buffer[1] == 1) {
        usCFG("STOP EMULATOR\n");
        stopping_emulator = true;
        stop_cynthcart();
      }
      if (buffer[1] == 2) { /* 9A 2 n on/off(0) */
        if (buffer[3] == 0) {
          unset_logging((int)buffer[2]);
        } else {
          set_logging((int)buffer[2]);
        }
      }
      #endif
      if (buffer[1] == 3) {
        extern bool detect_armsid(uint8_t base_address);
        st = detect_armsid(buffer[2]);
        usCFG("[TEST FOUND] %02X\n", st);
      }
      if (buffer[1] == 5) {
        extern uint32_t clockcycles(void);
        uint16_t dcyc = 1000;
        if (buffer[2] != 0 || buffer[3] != 0)
          dcyc = (buffer[2] << 8) | buffer[3];
        usCFG("[CFG] DELAY TESTING FOR %ld US/CYCLES\n", dcyc);
        volatile uint64_t test_before = to_us_since_boot(get_absolute_time());
        sleep_ms(dcyc/1000);
        volatile uint64_t test_after = to_us_since_boot(get_absolute_time());
        usCFG("[CFG] SLEEP_MS before: %lld after: %lld difference %lld\n", test_before, test_after, (test_after - test_before));
        test_before = to_us_since_boot(get_absolute_time());
        volatile uint16_t waited_cycles = cycled_delay_operation(dcyc);
        test_after = to_us_since_boot(get_absolute_time());
        usCFG("[CFG] DELAY_CYCLES %u = %.4fµs, ACTUAL: %uµs (including get_absolute_time delay)\n",
          waited_cycles, (float)(waited_cycles * sid_us), (test_after - test_before));
        volatile uint32_t now, end;
        test_before = to_us_since_boot(get_absolute_time());
        now = end = clockcycles();
        do {
          end = clockcycles();
        } while (end < (now + dcyc));
        test_after = to_us_since_boot(get_absolute_time());
        usCFG("[CFG] CPU START %u CPU END: %u CPU TARGET: %u FRAME START: %u FRAME END: %u \n",
          now, end, (now + dcyc),
          (uint)(now/usbsid_config.raster_rate),
          (uint)((now + dcyc)/usbsid_config.raster_rate)
        );
        usCFG("[CFG] DELAY_CYCLES %u = %.4fµs, ACTUAL: %uµs (including get_absolute_time delay)\n",
          dcyc, (float)(dcyc * sid_us),
          (test_after - test_before)
        );
      }
      if (buffer[1] == 6) {
        detect_fmopl(buffer[2]);
      }
      if (buffer[1] == 7) {
        usCFG("SAVE ID BEFORE: %d (%d)\n", config_saveid, usbsid_config.config_saveid);
        config_saveid = usbsid_config.config_saveid = 0;  /* reset saveid */
        usCFG("SAVE ID AFTER: %d (%d)\n", config_saveid, usbsid_config.config_saveid);
      }
      if (buffer[1] == 8)  {
        read_fpgasid_configuration(buffer[2]);
      }
      if (buffer[1] == 9)  {
        read_skpico_configuration(buffer[2], buffer[3]);
      }
      if (buffer[1] == 0xA)  {
        extern void print_backsid_filter_type(uint8_t base_address);
        extern void print_backsid_version(uint8_t base_address);
        print_backsid_filter_type(buffer[2]);
        print_backsid_version(buffer[2]);
      }
      if (buffer[1] == 0xB) {
        extern void set_backsid_filter_type(uint8_t base_address, uint8_t type);
        set_backsid_filter_type(buffer[2],buffer[3]);
      }
      if (buffer[1] == 0xC) {
        cycled_write_operation(0x1d,0x50,6);
        cycled_delay_operation(100);
        cycled_write_operation(0x1e,0x44,6);
        cycled_delay_operation(100);
        uint8_t result = cycled_read_operation(0x1e,6);
        usNFO("[PDSID] $%02x\n",result);
      }
      if (buffer[1] == 0xD) {
        if(buffer[2] == 0) {
          print_config_overview();
        } else {
          print_config_summary();
          print_runtime_summary();
        }
      }
      if (buffer[1] == 0xE) {
        config_print[buffer[2]]();
      }
      break;
    case TEST_FN3:
      usNFO("[SID MEMORY]\n");
      for (uint i = 0; i < SID_MEMORY_SIZE; i++) {
        if (i!=0 && i%0x20 == 0) { usNFO("\n");};
        usNFO("$%02X ", sid_memory[i]);
      }
      usNFO("\n");
      break;
    case FPGASID: break; /* Reserved for config implementation */
    case SKPICO: break;  /* Reserved for config implementation */
    case ARMSID: break;  /* Reserved for config implementation */
    case PDSID:
      if (buffer[1] == 0) {
        usCFG("[TOGGLE PDSID TYPE]\n");
        reset_switch_pdsid_type();
        break;
      } else if (buffer[1] == 1) {
        usCFG("[SET PDSID TYPE %d @ $%02x]\n",buffer[3],buffer[2]);
        if (!set_pdsid_sid_type(buffer[2],buffer[3])) {
          usERR("Failed to set PDSID type!!\n");
        }
        break;
      } else if (buffer[1] == 2) {
        uint8_t result = read_pdsid_sid_type(buffer[2]);
        usCFG("[READ PDSID RESULT %d @ $%02x]\n",result,buffer[2]);
        break;
      }
      break;
    #if defined(ONBOARD_SIDPLAYER)
    case UPLOAD_SID_START:
      usCFG("[UPLOAD_SID_START %d]\n",buffer[1]);
      receiving_sidfile = true;
      sidbytes_received = 0;
      is_prg = ((buffer[1] == PRG_FILE) ? true : false);
      sidfile = (uint8_t*)calloc(1, 0x10000); /* allocate 64KB */
      if (sidfile == NULL) {
        /*
        * Handle out-of-memory error
        * NOTE: RP2040 has 264KB RAM; 64KB is ~25% of total.
        */
      }
      break;
    case UPLOAD_SID_DATA:
      if (sidbytes_received == 0) usCFG("[UPLOAD_SID_DATA]\n");
      if (receiving_sidfile) {
        for (int i = 1; i < 63; i++) { /* Max buffer size minus command byte (config init byte is already gone) */
          sidfile[sidbytes_received] = buffer[i];
          sidbytes_received++;
        }
      }
      break;
    case UPLOAD_SID_END:
      usCFG("[UPLOAD_SID_END]\n");
      usDBG("Received %u bytes\n", sidbytes_received);
      receiving_sidfile = false;
      /* ISSUE: These are never the same size */
      /* sidfile_size = sidbytes_received; */
      sidbytes_received = 0;
      break;
    case UPLOAD_SID_SIZE:
      usCFG("[UPLOAD_SID_SIZE]\n");
      sidfile_size = (buffer[1]<<8|buffer[2]);
      usDBG("Received SID file size: %u\n", sidfile_size);
      break;
    case SID_PLAYER_TUNE:
      usCFG("[SID_PLAYER_TUNE] %d\n", buffer[1]);
      usCFG("[CONFIG BUFFER SIZE] %d\n", count_of(buffer));
      tuneno = buffer[2]; /* Should be 0 if not supplied */
      usCFG("[SUBTUNE] %d\n", tuneno);
      unmute_sid(); /* Must unmute before play start or some tunes will be silent */
      sidplayer_init = true;
    case SID_PLAYER_START:
      usCFG("[SID_PLAYER_START] %d\n", sidplayer_init);
      if (sidplayer_init) {
        offload_ledrunner = true;
        sidplayer_start = true;
      }
      sidplayer_init = false;
      break;
    case SID_PLAYER_STOP:
      usCFG("[SID_PLAYER_STOP]\n");
      if (sidplayer_playing) {
        sidplayer_stop = true;
      }
      /* Deinit all sidplayer variables */
      sidplayer_init = false;
      sidplayer_start = false;
      /* SID resets are handled in the emulator */
      break;
    case SID_PLAYER_PAUSE:
    usCFG("[SID_PLAYER_PAUSE]\n");
      sidplayer_playing = !sidplayer_playing;
      break;
    case SID_PLAYER_NEXT:
    usCFG("[SID_PLAYER_NEXT]\n");
      sidplayer_next = true;
      break;
    case SID_PLAYER_PREV:
    usCFG("[SID_PLAYER_PREV]\n");
      sidplayer_prev = true;
      break;
    #endif
    default:
      break;
    }
  return;
}

void apply_led_config(void)
{ /* if SID to use is higher then the number of sids, use first available SID */
  int sid = -1;
  int stou = (usbsid_config.RGBLED.sid_to_use - 1);
  for (int s = 0; s < 4; s++) {
    if (cfg.sidtype[s] == 2 || cfg.sidtype[s] == 3) {
      sid = (s + 1);
      break;
    }
  }
  usCFG("  RGBLED SID Requested: %d\n", usbsid_config.RGBLED.sid_to_use);
  /* check if requested sidno is actually configured  */
  usbsid_config.RGBLED.sid_to_use
    = (stou > cfg.numsids)
    || (cfg.sidtype[stou] != 2)
    || (cfg.sidtype[stou] != 3)
    ? (sid != -1)  /* If not still -1 */
    ? sid  /* use the first SID that is either 8580 or 6581 */
    : 1    /* else default to SID 1 */
    : usbsid_config.RGBLED.sid_to_use;  /* Else use the programmed SID to use */
  usCFG("  RGBLED SID Calculated: %d\n", usbsid_config.RGBLED.sid_to_use);
  return;
}

void print_config(void)
{ /* The truth, and nothing but the truth!   */
  print_pico_features();
  print_config_overview();
  print_config_summary();
  print_runtime_summary();
  return;
}

void apply_busclock_settings(void)
{
  usCFG("Applying bus clock settings\n");
  stop_dma_channels();
  restart_bus_clocks();
  sync_pios(false);
  start_dma_channels();
}

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
  // apply_runtime_config(&usbsid_config, &cfg);

  /* Find configured FMOpl */
  apply_fmopl_config();

  /* Atomically swap runtime config IRQ safe */
  uint32_t irq = save_and_disable_interrupts();
  memcpy(&cfg, &new_cfg, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  /* Apply to hardware (unless boot) */
  if (!at_boot) {
    usCFG("  Restarting PIO\n");
    apply_busclock_settings();
  }

  usCFG("  Success: %d SIDs active\n", cfg.numsids);

  if (RGB_ENABLED) {
    usCFG("  Applying RGBLED SID\n");
    apply_led_config();
  } else {
    usCFG("  RGBLED not available\n");
  }

  /* Print config at end of apply if requested */
  if (!at_boot && !auto_config) print_config();

  return CFG_OK;
}

void save_load_apply_config(bool at_boot)
{
  save_config(&usbsid_config);
  load_config(&usbsid_config);
  apply_config(at_boot);
  return;
}

void save_config_ext(void)
{ /* For saving the config outside of config.c */
  save_config(&usbsid_config);
  return;
}

void detect_default_config(void)
{
  usNFO("\n");
  usCFG("Detecting default configuration\n");
  usCFG("  Is default? %s\n",
    boolean_str((int)usbsid_config.default_config));
  if(usbsid_config.default_config == 1) {
    usCFG("  Default configuration detected!\n");
    usCFG("  MAGIC_SMOKE config_struct @ 0x%x cm_verification @ 0x%x\n",
      &usbsid_config.magic, &cm_verification);
    usCFG("  MAGIC_SMOKE verification config_struct = %u, header = %u, cm_verification = %u\n",
      usbsid_config.magic, MAGIC_SMOKE, cm_verification);
    usbsid_config.default_config = 0;
    usCFG("  Default configuration state set to %s\n", boolean_str(usbsid_config.default_config));
    first_boot = true;  /* Only at first boot the link popup will be sent */
    // auto_detect_routine(); /* default auto detect routine based on default config */
    sid_auto_detect(true); /* default auto detect routine based on default config */
    save_config(&usbsid_config);
  }
  return;
}

int return_clockrate(void)
{
  for (uint i = 0; i < count_of(clockrates); i++) {
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
        /* Start clock set */
        // ISSUE: EVEN THOUGH THIS IS BETTER IT DOES NOT SOLVE THE CRACKLING/BUS ROT ISSUE!
        stop_dma_channels();
        restart_bus_clocks();
        start_dma_channels();
        sync_pios(false);
        if (suspend_sids) {
          usCFG("Enable SID's and UnMute\n");
          enable_sid(true);
        }
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
