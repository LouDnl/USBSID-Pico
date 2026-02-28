/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * asid.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Huge thanks to Thomas Jansson for all his ASID improvement work (https://github.com/thomasj)
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
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

#include "stdbool.h"

#include "sid.h"
#include "midi.h"
#include "asid.h"
#include "globals.h"
#include "config.h"
#include "logging.h"


/* config.c */
extern Config usbsid_config;
extern RuntimeCFG cfg;
extern char *sidtypes[5];

/* gpio.c */
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern void pause_sid(void);
extern void reset_sid(void);
extern void reset_sid_registers(void);

/* vu.c */
extern uint16_t vu;

/* asid_buffer.c */
extern uint32_t track_asid_arrival(void);
extern void adjust_buffer_rate_dynamic(uint32_t target_rate);
extern void reset_arrival_tracking(void);
extern void ring_buffer_reset_size(void);
extern void set_buffer_rate(uint16_t rate);

/* Some locals, rural and such */
static bool buffer_started = false;
static bool default_order = false;
static bool default_order_on_start = false;
bool write_ordered = false;
/* thanks to thomasj */
struct asid_regpair_t {
  uint8_t index;
  uint8_t wait_us;
};
/* thanks to thomasj ~ SF2 Driver 11 no waits */
struct asid_regpair_t asid_to_writeorder[NO_SID_REGISTERS_ASID] = {};

/**
 * @brief Resets SID write order back to defaults
 */
void reset_asid_to_writeorder(void)
{
  usASID("Reset writeorder registers\n");
  write_ordered = false;
  for (int i = 0; i < NO_SID_REGISTERS_ASID; i++) {
    asid_to_writeorder[i].index = i;
    asid_to_writeorder[i].wait_us = 10; /* 10 cycles as default for rp2350 being too fast */
  }
  default_order_on_start = (default_order == false ? true : default_order);
  default_order = true;
  reset_arrival_tracking();
  ring_buffer_reset_size();
  return;
}

/**
 * @brief Initialise the asid buffer
 *
 * @param uint16_t framedelta_us ~ the rate to set the buffer to
 */
void init_asid_buffer(uint16_t framedelta_us)
{
  if (!buffer_started) {
    usASID("Init buffer queue, timer and irq\n");
    extern void init_buffer_pio(void);
    extern void asid_ring_init(void);

    asid_ring_init();
    init_buffer_pio();
    buffer_started = true;
  }
  /* Always set frame buffer timeout */
  if (framedelta_us != 0) { /* Above zero means always use framedelta_us */
    set_buffer_rate(framedelta_us);
  } else {
    set_buffer_rate(usbsid_config.refresh_rate);
  }
  return;
}

/**
 * @brief De-initialise the asid buffer
 */
void deinit_asid_buffer(void)
{
  if (buffer_started) {
    usASID("De-init buffer queue, timer and irq\n");
    reset_asid_to_writeorder();
    extern void stop_buffer_pio(void);
    extern void asid_ring_deinit(void);
    asid_ring_deinit();
    stop_buffer_pio();
    buffer_started = false;
  }
}

/**
 * @brief Initialise ASID on boot
 */
void asid_init(void)
{
  if (!default_order_on_start) reset_asid_to_writeorder();  /* Set defaults once on boot */
  else { ring_buffer_reset_size(); reset_arrival_tracking(); }
  usNFO("[ASID] Initialized\n");
}

/* Pling, plong, ploink!? */
void handle_asid_fmoplmessage(uint8_t* buffer)
{ /* Assumes byte 0-2 are not included in the buffer */
  uint8_t ndata_in_buffer = (buffer[0] + 1) << 1;
  uint8_t nmask_bytes = (ndata_in_buffer - 1) / 7 + 1;
  uint8_t data_index = nmask_bytes + 1;
  uint8_t data, field;
  uint8_t asid_fm_register_index = 0;

  static uint8_t fm_registers[MAX_FM_REG_PAIRS * 2];

  for (uint8_t mask = 0; mask < nmask_bytes; mask++) {
    field = 0x01;
    for (uint8_t bit = 0; (bit < 7) && (asid_fm_register_index < ndata_in_buffer); bit++) {
      data = buffer[data_index++];
      if ((buffer[1 + mask] & field) == field) {
        data += 0x80;
      }
      fm_registers[asid_fm_register_index++] = data;
      field <<= 1;
    }
  }
  uint8_t addr = ((cfg.fmopl_sid << 5) - 0x20);
  for (uint8_t reg = 0; reg < asid_fm_register_index; reg++) {
    dtype = asid;  /* Set data type to asid */
    /* Pico 2 requires at least 10 cycles between writes
     * or it will be too damn fast! So we do this for other
     * Pico's too */
    if((reg % 2 == 0)) {
      cycled_write_operation((addr | OPL_REG_ADDRESS), fm_registers[reg], 10);
      WRITEDBG(dtype, reg, asid_fm_register_index, (addr | OPL_REG_ADDRESS), fm_registers[reg], 10);
    } else {
      cycled_write_operation((addr | OPL_REG_DATA), fm_registers[reg], 10);
      WRITEDBG(dtype, reg, asid_fm_register_index, (addr | OPL_REG_DATA), fm_registers[reg], 10);
    }
  }
  midimachine.fmopl = 0;
  return;
}

/**
 * @brief Well, it does what it does
 * @note deprecated!
 *
 * @param uint8_t sid     ~ the sidnumber
 * @param uint8_t* buffer ~ the buffer to process
 * @param int size        ~ the size of the buffer
 */
void handle_complete_asid_buffer(uint8_t sid, uint8_t* buffer, int size)
{ /* Assumes byte 0-2 are included in the buffer and skips these */
  (void)size;  /* Stop calling me fat, I'm just big boned! */
  unsigned int reg = 0;
  for (uint8_t mask = 0; mask < 4; mask++) {  /* no more then 4 masks */
    for (uint8_t bit = 0; bit < 7; bit++) {  /* each packet has 7 bits ~ stoopid midi */
      if (buffer[mask + 3] & (1 << bit)) {  /* 3 byte message, skip 3 each round and add the bit */
        uint8_t register_value = buffer[reg + 11];  /* get the value to write from the buffer */
        if(buffer[mask + 7] & (1 << bit)) {  /* if anything higher then 0 */
          register_value |= 0x80;  /* the register_value needs its 8th MSB bit */
        }
        uint8_t address = asid_sid_registers[mask * 7 + bit];
        dtype = asid;  /* Set data type to asid */
        /* Pico 2 requires at least 10 cycles between writes
         * or it will be too damn fast! So we do this for other
         * Pico's too */
        cycled_write_operation((address |= sid), register_value, 10);
        WRITEDBG(dtype, reg, size, (address |= sid), register_value, 10);
        reg++;
      }
    }
  }
  return;
}

/**
 * @brief And this does the same
 * @note deprecated!
 *
 * @param uint8_t sid     ~ the sidnumber
 * @param uint8_t* buffer ~ the buffer to process
 */
void handle_asid_message(uint8_t sid, uint8_t* buffer)
{ /* Assumes byte 0-2 are not included in the buffer */
  unsigned int reg = 0;
  for (uint8_t mask = 0; mask < 4; mask++) {  /* no more then 4 masks */
    for (uint8_t bit = 0; bit < 7; bit++) {  /* each packet has 7 bits ~ stoopid midi */
      if (buffer[mask] & (1 << bit)) {  /* 3 byte message */
        uint8_t register_value = buffer[reg + 8];  /* get the value to write from the buffer */
        if(buffer[mask + 4] & (1 << bit)) {  /* if anything higher then 0 */
          register_value |= 0x80;  /* the register_value needs its 8th MSB bit */
        }
        uint8_t address = asid_sid_registers[mask * 7 + bit];
        dtype = asid;  /* Set data type to asid */
        /* Pico 2 requires at least 10 cycles between writes
         * or it will be too damn fast! So we do this for other
         * Pico's too */
        cycled_write_operation((address |= sid), register_value, 10);
        WRITEDBG(dtype, reg, 28, (address |= sid), register_value, 10);
        reg++;
      }
    }
  }
  return;
}

/**
 * @brief But this one lost track of time
 *
 * @param uint8_t sid     ~ the sidnumber
 * @param uint8_t* buffer ~ the buffer to process
 */
void handle_writeordered_asid_message(uint8_t sid, uint8_t* buffer)
{ /* Assumes byte 0-2 are not included in the buffer */
  int chip;
  switch (sid) {
    case 96: chip = 3; break;
    case 64: chip = 2; break;
    case 32: chip = 1; break;
    default: chip = 0; break;
  };

  struct asid_regpair_local_t {
    uint8_t reg;
    uint8_t data;
    uint8_t wait_us;
  };
  static struct asid_regpair_local_t writeOrder[USBSID_MAX_SIDS][NO_SID_REGISTERS_ASID];
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  unsigned int reg = 0;
  for (uint8_t mask = 0; mask < 4; mask++) {  /* no more then 4 masks */
    for (uint8_t bit = 0; bit < 7; bit++) {  /* each packet has 7 bits ~ stoopid midi */
      if (buffer[mask] & (1 << bit)) {  /* 3 byte message */
        uint8_t register_value = buffer[reg + 8];  /* get the value to write from the buffer */
        if(buffer[mask + 4] & (1 << bit)) {  /* if anything higher then 0 */
          register_value |= 0x80;  /* the register_value needs its 8th MSB bit */
        }
        writeOrder[chip][asid_to_writeorder[reg].index].reg = asid_sid_registers[mask * 7 + bit];
        writeOrder[chip][asid_to_writeorder[reg].index].data = register_value;
        /* Pico 2 requires at least 10 cycles between writes
         * or it will be too damn fast! So if wait_us is lower then 10 we use 10 cycles
         * and do this for other Pico's aswell */
        writeOrder[chip][asid_to_writeorder[reg].index].wait_us = asid_to_writeorder[reg].wait_us;
        /* usASID("[%d] $%02X:%02X %u\n", asid_to_writeorder[reg].index, (writeOrder[chip][asid_to_writeorder[reg].index].reg |= sid), writeOrder[chip][asid_to_writeorder[reg].index].data, writeOrder[chip][asid_to_writeorder[reg].index].wait_us); */
        dtype = asid;  /* Set data type to asid again */
        reg++;
      }
    }
  }
  /* inline extern as only used here */
  extern void asid_ring_write(uint8_t reg, uint8_t val, uint16_t c);
  for (size_t pos = 0; pos < NO_SID_REGISTERS_ASID; pos++) {
    if (writeOrder[chip][pos].wait_us != 0xff) {
      /* Push data to ASID ringbuffer */
      asid_ring_write(
        (writeOrder[chip][pos].reg |= sid),
        writeOrder[chip][pos].data,
        /* Account for 1 cycle overhead in PIO bus */
        // ((writeOrder[chip][pos].wait_us >= 0) ? (writeOrder[chip][pos].wait_us - 1) : 0));
        writeOrder[chip][pos].wait_us);
        // (write_ordered ? writeOrder[chip][pos].wait_us : 10)); /* Default write order has 10 cycles per write */
      WRITEDBG(dtype, pos, NO_SID_REGISTERS_ASID, (writeOrder[chip][pos].reg |= sid), writeOrder[chip][pos].data, writeOrder[chip][pos].wait_us);
    } else {
      asid_ring_write(0xffu,0xffu,0xffffu);
    }
  }
  for (size_t pos = 0; pos < NO_SID_REGISTERS_ASID; pos++) {
    writeOrder[chip][pos].wait_us = 0xff;  /* indicate not used */
  }
  return;
}

/**
 * @brief Process received writeorder configuration
 *
 * @param uint8_t* buffer ~ the buffer containing the configuration
 */
void handle_asid_writeorder_config(uint8_t* buffer)
{ /* thanks to thomasj */
  uint8_t data;
  int cycles;
  for (int i = 0; i < NO_SID_REGISTERS_ASID; i++) {
    data = buffer[i << 1];
    asid_to_writeorder[i].index = data & 0x1f;
    /* Regular handling removes 7us, so adjust */
    cycles = ((data & 0x40) << 1) + buffer[(i << 1) + 1];
    asid_to_writeorder[i].wait_us = MAX(1, cycles); /* USP has 1 cycle overhead */
    /* asid_to_writeorder[i].wait_us = cycles; */
    usASID("[WO%2d] {%02u,%02u}\n", i, asid_to_writeorder[i].index, asid_to_writeorder[i].wait_us);
  }
  default_order = false;
  return;
}

/**
 * @brief Set ASID environment
 *
 * @param int refresh_rate
 * @param int speed_multiplier
 * @param int custom_speed
 * @param int buffering
 * @param uint16_t framedelta_us
 */
inline void set_asid_env(int refresh_rate, int speed_multiplier, int custom_speed, int buffering, uint16_t framedelta_us)
{
  (void)speed_multiplier; /* Not used */
  (void)custom_speed;     /* Not used */
  (void)buffering;        /* Not used */

  { /* apply the new clock_rate based on the refresh_rate if applicable */
    extern void apply_clockrate(int n_clock, bool suspend_sids);
    apply_clockrate(
      ((refresh_rate == 0)
      ? 1   /* PAL */
      : 2), /* NTSC */
      true);
  }
  reset_sid_registers();
  reset_sid();

  init_asid_buffer(framedelta_us); /* Init on env set */
  ring_buffer_reset_size();  /* Reset buffer to default size for new tune */

  /* DON'T call reset_arrival_tracking() here */
  /* Instead, just accept that calculated rate will converge to framedelta */
  return;
}

/**
 * @brief Process received environment
 *
 * @param uint8_t* buffer ~ the buffer containing the environment
 */
void handle_asid_envmessage(uint8_t* buffer)
{ /* SID environment is only logged and not used for now */
  /* Incoming buffer skips first 3 bytes and
     starts at SETTINGS
     incoming message build up:
     0xF0, 0x2D, 0x32, SETTINGS, FRAMEDELTA1, FRAMEDELTA2, FRAMEDELTA3, 0x7F
  */
  /*
    data0: settings
    bit0    : 0 = PAL, 1 = NTSC
    bits4-1 : speed, 1x to 16x (value 0-15)
    bit5    : 1 = custom speed (only framedelta valid)
    bit6    : 1 = buffering requested by user
  */
  int refresh_rate = (buffer[0] & 0b1);
  int speed_multiplier = (buffer[0] & 0b11110) >> 1;
  int custom_speed = (buffer[0] & 0b100000) >> 5;
  int buffering = (buffer[0] & 0b1000000) >> 6;
  usASID("Settings: refresh rate: %s, speed multiplier: %d, custom speed: %d, buffering: %d\n",
    (refresh_rate == 0 ? "PAL" : "NTSC"),
    speed_multiplier, custom_speed, buffering);
  /*
    data1: framedelta uS, total 7+7+2=16 bits, slowest time = 65535us = 15Hz
    bits6-0: framedelta uS (LSB)

    data2:
    bits6-0: framedelta uS

    data3:
    bits1-0: framedelta uS (MSB)
    bits6-2: 5 bits (reserved)
  */
  uint16_t framedelta_us = ((buffer[1] & 0x7F) | (buffer[2] & 0x7F) << 7 | ((buffer[3] & 0x03) << 14));
  usASID("Framedelta: %d\n", framedelta_us);
  set_asid_env(refresh_rate, speed_multiplier, custom_speed, buffering, framedelta_us);
  return;
}

void handle_asid_typemessage(uint8_t* buffer)
{ /* SID type is only logged and not used for now */
  /* Incoming buffer skips first 3 bytes and
     starts at SIDNO
     incoming message build up:
     0xF0, 0x2D, 0x32, SIDNO, SIDTYPE, 0x7F
  */
  int sidtype = ((buffer[1] == 0) ? 3 : (buffer[1] == 1) ? 2 : 0);
  usASID("Tune SID %d type: %s\n", buffer[0], sidtypes[sidtype]);
  return;
}

/* Spy vs Spy ? */
void decode_asid_message(uint8_t* buffer, int size)
{
  static uint32_t rate; /* Tracks the rate of ASID messages */
  switch(buffer[2]) {
    case 0x4C:  /* Play start */
      usASID("Play start\n");
      if (!buffer_started) init_asid_buffer(usbsid_config.refresh_rate); /* Start buffer on play start */
      reset_arrival_tracking();  /* Reset timing on play start */
      ring_buffer_reset_size();  /* Reset buffer to default size for new tune */
      midimachine.bus = CLAIMED;
      break;
    case 0x4D:  /* Play stop */
      usASID("Play stop\n");
      reset_sid_registers();
      if (!default_order) reset_asid_to_writeorder();
      set_buffer_rate(usbsid_config.refresh_rate);
      ring_buffer_reset_size();  /* Reset buffer to default size */
      if (buffer_started) deinit_asid_buffer(); /* Stop buffer on play stop */
      midimachine.bus = FREE;
      break;
    case 0x30:  /* Write order timing (order and delay between individual SID register writes, to closely match the C64 driver used) */
      write_ordered = true;
      handle_asid_writeorder_config(&buffer[3]);
      break;
    case 0x31:  /* SID environment - PAL/NTSC, speed, frame delta time */
      handle_asid_envmessage(&buffer[3]);
      break;
    case 0x32:  /* SID chip info */
      handle_asid_typemessage(&buffer[3]);
      break;
    case 0x4F:  /* Display characters */
      break;
    case 0x4E:  /* SID 1 */
      if (!buffer_started) init_asid_buffer(usbsid_config.refresh_rate); /* Start buffer on first write */
      if (!default_order_on_start) reset_asid_to_writeorder();  /* Set defaults once on first write if not set yet */
      /* Only track arrival timing for SID1 - represents actual frame rate.
       * Multi-SID tunes send multiple messages per frame (one per SID). */
      rate = track_asid_arrival();
      /* Adjust rate based on buffer state (called for all SIDs) */
      adjust_buffer_rate_dynamic(rate);
      handle_writeordered_asid_message(0, &buffer[3]);
      break;
    case 0x50:  /* SID 2 */
      /* Update SID count and adjust rate - tracking only on SID1 for timing */
      extern void update_sid_count(uint8_t sid_num);
      update_sid_count(2);
      adjust_buffer_rate_dynamic(0);
      handle_writeordered_asid_message(32, &buffer[3]);
      break;
    case 0x51:  /* SID 3 */
      update_sid_count(3);
      adjust_buffer_rate_dynamic(0);
      handle_writeordered_asid_message(64, &buffer[3]);
      break;
    case 0x52:  /* SID 4 */
      update_sid_count(4);
      adjust_buffer_rate_dynamic(0);
      handle_writeordered_asid_message(96, &buffer[3]);
      break;
    case 0x60:  /* FMOpl */
      if (cfg.fmopl_enabled) {  /* Only if FMOpl is enabled, drop otherwise */
        midimachine.fmopl = 1;
        handle_asid_fmoplmessage(&buffer[3]);  /* Skip first 3 bytes */
      };
    default:
      break;
  }
  return;
}
