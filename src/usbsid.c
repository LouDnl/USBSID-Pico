/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * usbsid.c
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

#include "globals.h"
#include "config.h"
#include "usbsid.h"
#include "gpio.h"
#include "midi.h"  /* needed for struct midi_machine */
#include "sid.h"
#include "logging.h"

/* util.c */
extern long map(long x, long in_min, long in_max, long out_min, long out_max);
extern int randval(int min, int max);

/* Init external vars */
static semaphore_t core1_init;

/* Init vars ~ Do not change order to keep memory alignment! */
uint8_t __not_in_flash("usbsid_buffer") config_buffer[5];
uint8_t __not_in_flash("usbsid_buffer") sid_memory[(0x20 * 4)] __attribute__((aligned(2 * (0x20 * 4))));
uint8_t __not_in_flash("usbsid_buffer") write_buffer[MAX_BUFFER_SIZE] __attribute__((aligned(2 * MAX_BUFFER_SIZE)));
uint8_t __not_in_flash("usbsid_buffer") sid_buffer[MAX_BUFFER_SIZE] __attribute__((aligned(2 * MAX_BUFFER_SIZE)));
uint8_t __not_in_flash("usbsid_buffer") read_buffer[MAX_BUFFER_SIZE] __attribute__((aligned(2 * MAX_BUFFER_SIZE)));
int usb_connected = 0, usbdata = 0, pwm_value = 0, updown = 1;
uint32_t cdcread = 0, cdcwrite = 0, webread = 0, webwrite = 0;
uint8_t *cdc_itf = 0, *wusb_itf = 0;
uint16_t vu = 0;
uint32_t breathe_interval_ms = BREATHE_INTV;
uint32_t start_ms = 0;
char ntype = '0', dtype = '0', cdc = 'C', asid = 'A', midi = 'M', wusb = 'W';
bool web_serial_connected = false;
double cpu_mhz = 0, cpu_us = 0, sid_hz = 0, sid_mhz = 0, sid_us = 0;

/* Init var pointers for external use */
uint8_t (*write_buffer_p)[MAX_BUFFER_SIZE] = &write_buffer;

/* Config externals */
Config usbsid_config;
extern int sock_one, sock_two, sids_one, sids_two, numsids, act_as_one;
extern void default_config(Config * config);
extern void load_config(Config * config);
extern void save_config(const Config * config);
extern void handle_config_request(uint8_t * buffer);
extern void apply_config(void);
extern void detect_default_config(void);
extern void verify_clockrate(void);

/* GPIO externals */
extern void init_gpio(void);
extern void init_vu(void);
extern void setup_piobus(void);
extern void setup_dmachannels(void);
extern void sync_pios(void);
extern void init_sidclock(void);
extern int detect_clocksignal(void);
extern void pause_sid(void);
extern void pause_sid_withmute(void);
extern void mute_sid(void);
extern void unmute_sid(void);
extern void reset_sid(void);
extern void reset_sid_registers(void);
extern void enable_sid(void);
extern void disable_sid(void);
extern void clear_bus_all(void);
extern uint8_t __not_in_flash_func(bus_operation)(uint8_t command, uint8_t address, uint8_t data);
extern void __not_in_flash_func(cycled_bus_operation)(uint8_t address, uint8_t data, uint16_t cycles);

/* MCU externals */
extern void mcu_reset(void);
extern void mcu_jump_to_bootloader(void);

/* Midi externals */
midi_machine midimachine;
extern void process_stream(uint8_t *buffer, size_t size);

/* RGB LED */
#if defined(USE_RGB)
#define IS_RGBW false
PIO pio_rgb = pio1;
uint ws2812_sm, offset_ws2812;
int _rgb = 0;
uint8_t r_ = 0, g_ = 0, b_ = 0;
unsigned char o1 = 0, o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0, o7 = 0, o8 = 0, o9 = 0, o10 = 0, o11 = 0, o12 = 0;
const __not_in_flash("usbsid_data") unsigned char color_LUT[43][6][3] =
{
  /* Red Green Blue Yellow Cyan Purple*/
  {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
  {{6, 0, 0}, {0, 6, 0}, {0, 0, 6}, {6, 6, 0}, {0, 6, 6}, {6, 0, 6}},
  {{12, 0, 0}, {0, 12, 0}, {0, 0, 12}, {12, 12, 0}, {0, 12, 12}, {12, 0, 12}},
  {{18, 0, 0}, {0, 18, 0}, {0, 0, 18}, {18, 18, 0}, {0, 18, 18}, {18, 0, 18}},
  {{24, 0, 0}, {0, 24, 0}, {0, 0, 24}, {24, 24, 0}, {0, 24, 24}, {24, 0, 24}},
  {{30, 0, 0}, {0, 30, 0}, {0, 0, 30}, {30, 30, 0}, {0, 30, 30}, {30, 0, 30}},
  {{36, 0, 0}, {0, 36, 0}, {0, 0, 36}, {36, 36, 0}, {0, 36, 36}, {36, 0, 36}},
  {{42, 0, 0}, {0, 42, 0}, {0, 0, 42}, {42, 42, 0}, {0, 42, 42}, {42, 0, 42}},
  {{48, 0, 0}, {0, 48, 0}, {0, 0, 48}, {48, 48, 0}, {0, 48, 48}, {48, 0, 48}},
  {{54, 0, 0}, {0, 54, 0}, {0, 0, 54}, {54, 54, 0}, {0, 54, 54}, {54, 0, 54}},
  {{60, 0, 0}, {0, 60, 0}, {0, 0, 60}, {60, 60, 0}, {0, 60, 60}, {60, 0, 60}},
  {{66, 0, 0}, {0, 66, 0}, {0, 0, 66}, {66, 66, 0}, {0, 66, 66}, {66, 0, 66}},
  {{72, 0, 0}, {0, 72, 0}, {0, 0, 72}, {72, 72, 0}, {0, 72, 72}, {72, 0, 72}},
  {{78, 0, 0}, {0, 78, 0}, {0, 0, 78}, {78, 78, 0}, {0, 78, 78}, {78, 0, 78}},
  {{84, 0, 0}, {0, 84, 0}, {0, 0, 84}, {84, 84, 0}, {0, 84, 84}, {84, 0, 84}},
  {{90, 0, 0}, {0, 90, 0}, {0, 0, 90}, {90, 90, 0}, {0, 90, 90}, {90, 0, 90}},
  {{96, 0, 0}, {0, 96, 0}, {0, 0, 96}, {96, 96, 0}, {0, 96, 96}, {96, 0, 96}},
  {{102, 0, 0}, {0, 102, 0}, {0, 0, 102}, {102, 102, 0}, {0, 102, 102}, {102, 0, 102}},
  {{108, 0, 0}, {0, 108, 0}, {0, 0, 108}, {108, 108, 0}, {0, 108, 108}, {108, 0, 108}},
  {{114, 0, 0}, {0, 114, 0}, {0, 0, 114}, {114, 114, 0}, {0, 114, 114}, {114, 0, 114}},
  {{120, 0, 0}, {0, 120, 0}, {0, 0, 120}, {120, 120, 0}, {0, 120, 120}, {120, 0, 120}},
  {{126, 0, 0}, {0, 126, 0}, {0, 0, 126}, {126, 126, 0}, {0, 126, 126}, {126, 0, 126}},
  {{132, 0, 0}, {0, 132, 0}, {0, 0, 132}, {132, 132, 0}, {0, 132, 132}, {132, 0, 132}},
  {{138, 0, 0}, {0, 138, 0}, {0, 0, 138}, {138, 138, 0}, {0, 138, 138}, {138, 0, 138}},
  {{144, 0, 0}, {0, 144, 0}, {0, 0, 144}, {144, 144, 0}, {0, 144, 144}, {144, 0, 144}},
  {{150, 0, 0}, {0, 150, 0}, {0, 0, 150}, {150, 150, 0}, {0, 150, 150}, {150, 0, 150}},
  {{156, 0, 0}, {0, 156, 0}, {0, 0, 156}, {156, 156, 0}, {0, 156, 156}, {156, 0, 156}},
  {{162, 0, 0}, {0, 162, 0}, {0, 0, 162}, {162, 162, 0}, {0, 162, 162}, {162, 0, 162}},
  {{168, 0, 0}, {0, 168, 0}, {0, 0, 168}, {168, 168, 0}, {0, 168, 168}, {168, 0, 168}},
  {{174, 0, 0}, {0, 174, 0}, {0, 0, 174}, {174, 174, 0}, {0, 174, 174}, {174, 0, 174}},
  {{180, 0, 0}, {0, 180, 0}, {0, 0, 180}, {180, 180, 0}, {0, 180, 180}, {180, 0, 180}},
  {{186, 0, 0}, {0, 186, 0}, {0, 0, 186}, {186, 186, 0}, {0, 186, 186}, {186, 0, 186}},
  {{192, 0, 0}, {0, 192, 0}, {0, 0, 192}, {192, 192, 0}, {0, 192, 192}, {192, 0, 192}},
  {{198, 0, 0}, {0, 198, 0}, {0, 0, 198}, {198, 198, 0}, {0, 198, 198}, {198, 0, 198}},
  {{204, 0, 0}, {0, 204, 0}, {0, 0, 204}, {204, 204, 0}, {0, 204, 204}, {204, 0, 204}},
  {{210, 0, 0}, {0, 210, 0}, {0, 0, 210}, {210, 210, 0}, {0, 210, 210}, {210, 0, 210}},
  {{216, 0, 0}, {0, 216, 0}, {0, 0, 216}, {216, 216, 0}, {0, 216, 216}, {216, 0, 216}},
  {{222, 0, 0}, {0, 222, 0}, {0, 0, 222}, {222, 222, 0}, {0, 222, 222}, {222, 0, 222}},
  {{228, 0, 0}, {0, 228, 0}, {0, 0, 228}, {228, 228, 0}, {0, 228, 228}, {228, 0, 228}},
  {{234, 0, 0}, {0, 234, 0}, {0, 0, 234}, {234, 234, 0}, {0, 234, 234}, {234, 0, 234}},
  {{240, 0, 0}, {0, 240, 0}, {0, 0, 240}, {240, 240, 0}, {0, 240, 240}, {240, 0, 240}},
  {{246, 0, 0}, {0, 246, 0}, {0, 0, 246}, {246, 246, 0}, {0, 246, 246}, {246, 0, 246}},
  {{252, 0, 0}, {0, 252, 0}, {0, 0, 252}, {252, 252, 0}, {0, 252, 252}, {252, 0, 252}}
};
#endif

/* WebUSB Description URL */
static const tusb_desc_webusb_url_t desc_url =
{
  .bLength         = 3 + count_of(URL) - 1,
  .bDescriptorType = 3, // WEBUSB URL type
  .bScheme         = 1, // 0: http, 1: https
  .url             = URL
};


/* UTILS */

/* Log reset reason */
void reset_reason(void)
{
#if PICO_RP2040
  io_rw_32 *rr = (io_rw_32 *) (VREG_AND_CHIP_RESET_BASE + VREG_AND_CHIP_RESET_CHIP_RESET_OFFSET);
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS)
    DBG("[RESET] Caused by power on or brownout detection\n");
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS)
    DBG("[RESET] Caused by RUN pin trigger ~ manual or ISA RESET signal\n");
  if(*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS)
    DBG("[RESET] Caused by debug port\n");
#endif
  return;
}

#if defined(USE_RGB)
void put_pixel(uint32_t pixel_grb) {
  // pio_sm_put_blocking(pio_rgb, ws2812_sm, pixel_grb << 8u);
  pio_sm_put(pio_rgb, ws2812_sm, pixel_grb << 8u);
  return;
}

uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t) (r) << 8) | ((uint32_t) (g) << 16) | (uint32_t) (b);
}

uint8_t rgbb(double inp)
{
  int r = abs((inp / 255) * usbsid_config.RGBLED.brightness);
  return (uint8_t)r;
}
#endif


/* SETUP */

/* Initialize debug logging if enabled */
void init_logging(void)
{
  #if defined(USBSID_UART)
  stdio_uart_init_full(uart0, BAUD_RATE, TX, RX);
  sleep_ms(100);  /* leave time for uart to settle */
  stdio_flush();
  DBG("\n[%s]\n", __func__);
  #endif
  return;
}

/* Init the RGB LED pio */
void init_rgb(void)
{
  #if defined(USE_RGB)
  _rgb = randval(0, 5);
  r_ = g_ = b_ = 0;
  offset_ws2812 = pio_add_program(pio_rgb, &ws2812_program);
  ws2812_sm = 0;  /* PIO1 SM0 */
  pio_sm_claim(pio_rgb, ws2812_sm);
  ws2812_program_init(pio_rgb, ws2812_sm, offset_ws2812, 23, 800000, IS_RGBW);
  put_pixel(urgb_u32(0,0,0));
  #endif
  return;
}


/* USB TO HOST */

/* Write from device to host */
void cdc_write(uint8_t * itf, uint32_t n)
{ /* No need to check if write available with current driver code */
  tud_cdc_n_write(*itf, write_buffer, n);  /* write n bytes of data to client */
  tud_cdc_n_write_flush(*itf);
  vu = vu == 0 ? 100 : vu;  /* NOTICE: Testfix for core1 setting dtype to 0 */
  IODBG("[O] [%c] %02X:%02X\n", dtype, sid_buffer[1], write_buffer[0]);
  return;
}

/* Write from device to host */
void webserial_write(uint8_t * itf, uint32_t n)
{ /* No need to check if write available with current driver code */
  tud_vendor_write(write_buffer, n);
  tud_vendor_flush();
  vu = vu == 0 ? 100 : vu;  /* NOTICE: Testfix for core1 setting dtype to 0 */
  IODBG("[O] [%c] DAT[0x%02x] \n", dtype, write_buffer[0]);
  return;
}


/* BUFFER HANDLING */

/* Process received usb data */
void __not_in_flash_func(handle_buffer_task)(uint8_t * itf, uint32_t * n)
{
  usbdata = 1;
  uint8_t command = ((sid_buffer[0] & PACKET_TYPE) >> 6);
  uint8_t subcommand = (sid_buffer[0] & COMMAND_MASK);
  uint8_t n_bytes = (sid_buffer[0] & BYTE_MASK);

  if (command == CYCLED_WRITE) {
    // n_bytes = (n_bytes == 0) ? 4 : n_bytes; /* if byte count is zero, this is a single write packet */
    if (n_bytes == 0) {
      cycled_bus_operation(sid_buffer[1], sid_buffer[2], (sid_buffer[3] << 8 | sid_buffer[4]));
    } else {
      for (int i = 1; i <= n_bytes; i += 4) {
        usbdata = 1;
        if (sid_buffer[i] == 0xFF && sid_buffer[i + 1] == 0xF0 && sid_buffer[i + 2] == 0xFF && sid_buffer[i + 3] == 0xFF) {
          /* EXPERIMENTAL ~ NOT WORKING YET */
          /* reset_sid(); */
          /* return; */
          continue;
        };
        cycled_bus_operation(sid_buffer[i], sid_buffer[i + 1], (sid_buffer[i + 2] << 8 | sid_buffer[i + 3]));
      };
    }
    return;
  };
  if (command == WRITE) {
    // n_bytes = (n_bytes == 0) ? 2 : n_bytes; /* if byte count is zero, this is a single write packet */
    if (n_bytes == 0) {
      bus_operation(0x10, sid_buffer[1], sid_buffer[2]);  /* write the address and value to the SID */
      IODBG("[I] [%c] $%02X:%02X\n", dtype, sid_buffer[1], sid_buffer[2]);
    } else {
      IODBG("[I] [%c]", dtype);
      for (int i = 1; i <= n_bytes; i += 2) {
        IODBG(" $%02X:%02X", sid_buffer[i], sid_buffer[i + 1]);
        /* write the address and value to the SID with minimal 10 cycles in between ~ Thanks for the cycle amount erique! */
        cycled_bus_operation(sid_buffer[i], sid_buffer[i + 1], 10);
      };
      IODBG("\n");
    }
    return;
  };
  if (command == READ) {  /* READING CAN ONLY HANDLE ONE AT A TIME, PERIOD. */
    write_buffer[0] = bus_operation((0x10 | READ), sid_buffer[1], sid_buffer[2]);  /* write the address to the SID and read the data back */
    switch (dtype) {  /* write the result to the USB client */
      case 'C':
        cdc_write(itf, BYTES_TO_SEND);
        break;
      case 'W':
        webserial_write(itf, BYTES_TO_SEND);
        break;
      default:
        IODBG("[WRITE ERROR]%c\n", dtype);
        break;
    };
    return;
  };
  if (command == COMMAND) {
    switch (subcommand) {
      case PAUSE:
        DBG("[PAUSE_SID]\n");
        pause_sid();
        break;
      case MUTE:
        DBG("[MUTE_SID]\n");
        mute_sid();
        break;
      case UNMUTE:
        DBG("[UNMUTE_SID]\n");
        unmute_sid();
        break;
      case RESET_SID:
        if (sid_buffer[1] == 0) {
          DBG("[RESET_SID]\n");
          reset_sid();
        }
        if (sid_buffer[1] == 1) {
          DBG("[RESET_SID_REGISTERS]\n");
          reset_sid_registers();
        }
        break;
      case DISABLE_SID:
        DBG("[DISABLE_SID]\n");
        disable_sid();
        break;
      case ENABLE_SID:
        DBG("[ENABLE_SID]\n");
        enable_sid();
        break;
      case CLEAR_BUS:
        DBG("[CLEAR_BUS]\n");
        clear_bus_all();
        break;
      case CONFIG:
        DBG("[CONFIG]\n");
        memcpy(config_buffer, (sid_buffer + 1), 5);
        handle_config_request(config_buffer);
        memset(config_buffer, 0, count_of(config_buffer));
        break;
      case RESET_MCU:
        DBG("[RESET_MCU]\n");
        mcu_reset();
        break;
      case BOOTLOADER:
        DBG("[BOOTLOADER]\n");
        mcu_jump_to_bootloader();
        break;
      default:
        break;
      }
    return;
  };
  return;
}


/* LED SORCERERS */

/* It goes up and it goes down */
void led_vumeter_task(void)
{
  #if LED_PWM
  if (to_ms_since_boot(get_absolute_time()) - start_ms < breathe_interval_ms) {
    return;  /* not enough time */
  }
  start_ms = to_ms_since_boot(get_absolute_time());
  if (usbdata == 1 && dtype != ntype) {
    uint8_t osc1, osc2, osc3;
    osc1 = (sid_memory[0x00] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 1 */
    osc2 = (sid_memory[0x07] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 2 */
    osc3 = (sid_memory[0x0E] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 3 */

    #if defined(USE_RGB)
    if(usbsid_config.RGBLED.enabled) {
      r_ = 0, g_ = 0, b_ = 0, o1 = 0, o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0, o7 = 0, o8 = 0, o9 = 0, o10 = 0, o11 = 0, o12 = 0;
      uint8_t osc4, osc5, osc6, osc7, osc8, osc9, osc10, osc11, osc12;
      if (numsids >= 2) {
        osc4 = (sid_memory[0x00 + 0x20] * 0.596);  /* Frequency in Hz of SID2 @ $D420 Oscillator 1 */
        osc5 = (sid_memory[0x07 + 0x20] * 0.596);  /* Frequency in Hz of SID2 @ $D420 Oscillator 2 */
        osc6 = (sid_memory[0x0E + 0x20] * 0.596);  /* Frequency in Hz of SID2 @ $D420 Oscillator 3 */
      }
      if (numsids >= 3) {
        osc7 = (sid_memory[0x00 + 0x40] * 0.596);  /* Frequency in Hz of SID3 @ $D440 Oscillator 1 */
        osc8 = (sid_memory[0x07 + 0x40] * 0.596);  /* Frequency in Hz of SID3 @ $D440 Oscillator 2 */
        osc9 = (sid_memory[0x0E + 0x40] * 0.596);  /* Frequency in Hz of SID3 @ $D440 Oscillator 3 */
      }
      if (numsids == 4) {
        osc10 = (sid_memory[0x00 + 0x60] * 0.596);  /* Frequency in Hz of SID4 @ $D460 Oscillator 1 */
        osc11 = (sid_memory[0x07 + 0x60] * 0.596);  /* Frequency in Hz of SID4 @ $D460 Oscillator 2 */
        osc12 = (sid_memory[0x0E + 0x60] * 0.596);  /* Frequency in Hz of SID4 @ $D460 Oscillator 3 */
      }
      o1 = map(osc1, 0, 255, 0, 43);
      o2 = map(osc2, 0, 255, 0, 43);
      o3 = map(osc3, 0, 255, 0, 43);
      if (numsids >= 2) {
        o4 = map(osc4, 0, 255, 0, 43);
        o5 = map(osc5, 0, 255, 0, 43);
        o6 = map(osc6, 0, 255, 0, 43);
      }
      if (numsids >= 3) {
        o7 = map(osc7, 0, 255, 0, 43);
        o8 = map(osc8, 0, 255, 0, 43);
        o9 = map(osc9, 0, 255, 0, 43);
      }
      if (numsids == 4) {
        o10 = map(osc10, 0, 255, 0, 43);
        o11 = map(osc11, 0, 255, 0, 43);
        o12 = map(osc12, 0, 255, 0, 43);
      }
      switch (usbsid_config.RGBLED.sid_to_use) {
      case 1:
        r_ += color_LUT[o1][0][0] + color_LUT[o2][1][0] + color_LUT[o3][2][0] ;
        g_ += color_LUT[o1][0][1] + color_LUT[o2][1][1] + color_LUT[o3][2][1] ;
        b_ += color_LUT[o1][0][2] + color_LUT[o2][1][2] + color_LUT[o3][2][2] ;
        break;
      case 2:
        r_ += color_LUT[o4][3][0] + color_LUT[o5][4][0] + color_LUT[o6][5][0] ;
        g_ += color_LUT[o4][3][1] + color_LUT[o5][4][1] + color_LUT[o6][5][1] ;
        b_ += color_LUT[o4][3][2] + color_LUT[o5][4][2] + color_LUT[o6][5][2] ;
        break;
      case 3:
        r_ += color_LUT[o7][0][0] + color_LUT[o8][1][0] + color_LUT[o9][2][0] ;
        g_ += color_LUT[o7][0][1] + color_LUT[o8][1][1] + color_LUT[o9][2][1] ;
        b_ += color_LUT[o7][0][2] + color_LUT[o8][1][2] + color_LUT[o9][2][2] ;
        break;
      case 4:
        r_ += color_LUT[o10][3][0] + color_LUT[o11][4][0] + color_LUT[o12][5][0] ;
        g_ += color_LUT[o10][3][1] + color_LUT[o11][4][1] + color_LUT[o12][5][1] ;
        b_ += color_LUT[o10][3][2] + color_LUT[o11][4][2] + color_LUT[o12][5][2] ;
        break;
      case 0:
      default:
        r_ += color_LUT[o1][0][0] + color_LUT[o2][1][0] + color_LUT[o3][2][0] + color_LUT[o4][3][0] + color_LUT[o5][4][0] + color_LUT[o6][5][0] ;
        g_ += color_LUT[o1][0][1] + color_LUT[o2][1][1] + color_LUT[o3][2][1] + color_LUT[o4][3][1] + color_LUT[o5][4][1] + color_LUT[o6][5][1] ;
        b_ += color_LUT[o1][0][2] + color_LUT[o2][1][2] + color_LUT[o3][2][2] + color_LUT[o4][3][2] + color_LUT[o5][4][2] + color_LUT[o6][5][2] ;
        break;
      }
      put_pixel(urgb_u32(rgbb(r_), rgbb(g_), rgbb(b_)));

      /* Color LED debugging ~ uncomment for testing */
      // DBG("[%c][PWM]$%04x [R]%02x [G]%02x [B]%02x [O1]$%02x $%02d [O2]$%02x $%02d [O3]$%02x $%02d [O4]$%02x $%02d [O5]$%02x $%02d [O6]$%02x $%02d\n",
      //  dtype, vu, r_, g_, b_, osc1, o1, osc2, o2, osc3, o3, osc4, o4, osc5, o5, osc6, o6);
    }
    #endif

    vu = abs((osc1 + osc2 + osc3) / 3.0f);
    vu = map(vu, 0, HZ_MAX, 0, VU_MAX);
    if (usbsid_config.LED.enabled) {
      pwm_set_gpio_level(BUILTIN_LED, vu);
    }

    MDBG("[%c:%d][PWM]$%04x[V1]$%02X%02X$%02X%02X$%02X$%02X$%02X[V2]$%02X%02X$%02X%02X$%02X$%02X$%02X[V3]$%02X%02X$%02X%02X$%02X$%02X$%02X[FC]$%02x%02x$%02x[VOL]$%02x\n",
      dtype, usbdata, vu,
      sid_memory[0x01], sid_memory[0x00], sid_memory[0x03], sid_memory[0x02], sid_memory[0x04], sid_memory[0x05], sid_memory[0x06],
      sid_memory[0x08], sid_memory[0x07], sid_memory[0x0A], sid_memory[0x09], sid_memory[0x0B], sid_memory[0x0C], sid_memory[0x0D],
      sid_memory[0x0F], sid_memory[0x0E], sid_memory[0x11], sid_memory[0x10], sid_memory[0x12], sid_memory[0x13], sid_memory[0x14],
      sid_memory[0x16], sid_memory[0x15], sid_memory[0x17], sid_memory[0x18]);

  }
  return;
  #endif
}

/* Mouth breather! */
void led_breathe_task(void)
{
  #if LED_PWM
  if (usbdata == 0 && dtype == ntype) {
    if (to_ms_since_boot(get_absolute_time()) - start_ms < breathe_interval_ms) {
      return;  /* not enough time */
    }
    start_ms = to_ms_since_boot(get_absolute_time());

    if (pwm_value >= VU_MAX) {
      updown = 0;
    }
    if (pwm_value <= 0) {
      updown = 1;
      #ifdef USE_RGB
      _rgb = randval(0, 5);
      #endif
    }

    if (updown == 1 && pwm_value <= VU_MAX)
      pwm_value += BREATHE_STEP;

    if (updown == 0 && pwm_value >= 0)
      pwm_value -= BREATHE_STEP;
    if (usbsid_config.LED.enabled && usbsid_config.LED.idle_breathe) {
      pwm_set_gpio_level(BUILTIN_LED, pwm_value);
    }
    #if defined(USE_RGB)
    if (usbsid_config.RGBLED.enabled && usbsid_config.RGBLED.idle_breathe) {
      int rgb_ = map(pwm_value, 0, VU_MAX, 0, 43);
      r_ = color_LUT[rgb_][_rgb][0];
      g_ = color_LUT[rgb_][_rgb][1];
      b_ = color_LUT[rgb_][_rgb][2];
      put_pixel(urgb_u32(rgbb(r_),rgbb(g_),rgbb(b_)));
      /* Color LED debugging ~ uncomment for testing */
      /* DBG("[%c][PWM]$%04x [R]%02x [G]%02x [B]%02x\n", dtype, pwm_value, r_, g_, b_); */
    } else {
      put_pixel(urgb_u32(0,0,0));
    }
    #endif
  }
  return;
  #endif
}


/* USB CALLBACKS */

void tud_mount_cb(void)
{
  DBG("[%s]\n", __func__);
  usb_connected = 1;
}

void tud_umount_cb(void)
{
  usb_connected = 0, usbdata = 0, dtype = ntype;
  DBG("[%s]\n", __func__);
  disable_sid();  /* NOTICE: Testing if this is causing the random lockups */
}

void tud_suspend_cb(bool remote_wakeup_en)
{
  /* (void) remote_wakeup_en; */
  DBG("[%s] remote_wakeup_en:%d\n", __func__, remote_wakeup_en);
  usb_connected = 0, usbdata = 0, dtype = ntype;
}

void tud_resume_cb(void)
{
  DBG("[%s]\n", __func__);
  usb_connected = 1;
}


/* USB CDC CLASS CALLBACKS */

/* Read from host to device */
void tud_cdc_rx_cb(uint8_t itf)
{ /* No need to check available bytes for reading */
  cdc_itf = &itf;
  usbdata = 1, dtype = cdc;
  vu = vu == 0 ? 100 : vu;  /* NOTICE: Testfix for core1 setting dtype to 0 */
  cdcread = tud_cdc_n_read(*cdc_itf, &read_buffer, MAX_BUFFER_SIZE);  /* Read data from client */
  tud_cdc_n_read_flush(*cdc_itf);
  memcpy(sid_buffer, read_buffer, cdcread);
  handle_buffer_task(cdc_itf, &cdcread);
  memset(read_buffer, 0, count_of(read_buffer));
  memset(sid_buffer, 0, count_of(sid_buffer));
  return;
}

void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char)
{
  (void)itf;
  (void)wanted_char;
  /* DBG("[%s]\n", __func__); */  /* Disabled due to possible uart spam */
}

void tud_cdc_tx_complete_cb(uint8_t itf)
{
  (void)itf;
  /* DBG("[%s]\n", __func__); */ /* Disabled due to uart spam */
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  /* (void) itf; */
  /* (void) rts; */
  DBG("[%s] itf:%x, dtr:%d, rts:%d\n", __func__, itf, dtr, rts);

  if ( dtr ) {
    /* Terminal connected */
    usbdata = 1;
  }
  else
  {
    /* Terminal disconnected */
    usbdata = 0;
  }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
  /* (void)itf; */
  /* (void)p_line_coding; */
  DBG("[%s] itf:%x, bit_rate:%u, stop_bits:%u, parity:%u, data_bits:%u\n", __func__, itf, (int)p_line_coding->bit_rate, p_line_coding->stop_bits, p_line_coding->parity, p_line_coding->data_bits);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
  /* (void)itf; */
  /* (void)duration_ms; */
  DBG("[%s] its:%x, duration_ms:%x\n", __func__, itf, duration_ms);
}


/* USB MIDI CLASS CALLBACKS */

void tud_midi_rx_cb(uint8_t itf)
{
  if (tud_midi_n_mounted(itf)) {
    usbdata = 1;
    while (tud_midi_n_available(itf, 0)) {  /* Loop as long as there is data available */
      uint32_t available = tud_midi_n_stream_read(itf, 0, midimachine.usbstreambuffer, MAX_BUFFER_SIZE);  /* Reads all available bytes at once */
      process_stream(midimachine.usbstreambuffer, available);
    }
    /* Clear usb buffer after use */
    memset(midimachine.usbstreambuffer, 0, count_of(midimachine.usbstreambuffer));
    return;
  }
  return;
}


/* USB VENDOR CLASS CALLBACKS */

/* Read from host to device */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize)
{
  /* buffer contains incoming data, but of the previous read so we void it */
  (void)buffer;
  (void)bufsize;

  if (web_serial_connected) { /* vendor class has no connect check, thus use this */
    wusb_itf = &itf;
    usbdata = 1, dtype = wusb;
    webread = tud_vendor_n_read(*wusb_itf, &read_buffer, MAX_BUFFER_SIZE);
    tud_vendor_n_read_flush(*wusb_itf);
    memcpy(sid_buffer, read_buffer, webread);
    handle_buffer_task(wusb_itf, &webread);
    memset(read_buffer, 0, count_of(read_buffer));
    memset(sid_buffer, 0, count_of(sid_buffer));
    return;
  }
  return;
}

void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes)
{
  (void)itf;
  DBG("[%s] %lu\n", __func__, sent_bytes);
}


/* WEBUSB VENDOR CLASS CALLBACKS */

/* Handle incoming vendor and webusb data */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  DBG("[%s] stage:%x, rhport:%x, bRequest:0x%x, wValue:%d, wIndex:%x, wLength:%x, bmRequestType:%x, type:%x, recipient:%x, direction:%x\n",
    __func__, stage, rhport,
    request->bRequest, request->wValue, request->wIndex, request->wLength, request->bmRequestType,
    request->bmRequestType_bit.type, request->bmRequestType_bit.recipient, request->bmRequestType_bit.direction);
  /* Do nothing with IDLE (0), DATA (2) & ACK (3) stages */
  if (stage != CONTROL_STAGE_SETUP) return true;  /* Stage 1 */

  switch (request->bmRequestType_bit.type) { /* BitType */
    case TUSB_REQ_TYPE_STANDARD:  /* 0 */
      break;
    case TUSB_REQ_TYPE_CLASS:  /* 1 */
      if (request->bRequest == WEBUSB_COMMAND) {
        DBG("request->bRequest == WEBUSB_COMMAND\n");
        if (request->wValue == WEBUSB_RESET) {
          DBG("request->wValue == WEBUSB_RESET\n");
          // BUG: NO WURKY CURKY
          /* reset_sid_registers(); */ // BUG: Temporary disabled
          reset_sid();  // NOTICE: Temporary until fixed!
          /* unmute_sid(); */
        }
        if (request->wValue == RESET_SID) {
          DBG("request->wValue == RESET_SID\n");
          reset_sid();
        }
        if (request->wValue == PAUSE) {
          DBG("request->wValue == PAUSE\n");
          pause_sid();
          mute_sid();
        }
        if (request->wValue == WEBUSB_CONTINUE) {
          DBG("request->wValue == WEBUSB_CONTINUE\n");
          pause_sid();
          unmute_sid();
        }
      }
      if (request->bRequest == 0x22) {
        /* Webserial simulates the CDC_REQUEST_SET_CONTROL_LINE_STATE (0x22) to connect and disconnect */
        web_serial_connected = (request->wValue != 0);
        /* Respond with status OK */
        return tud_control_status(rhport, request);
      }
      break;
    case TUSB_REQ_TYPE_VENDOR:  /* 2 */
      switch (request->bRequest) {
        case VENDOR_REQUEST_WEBUSB:
          /* Match vendor request in BOS descriptor
           * Get landing page url and return it
           */
          // Disabled for now due to constant opening of url on plugin of device
          /* return tud_control_xfer(rhport, request, (void*)(uintptr_t) &desc_url, desc_url.bLength); */
          return tud_control_status(rhport, request);
        case VENDOR_REQUEST_MICROSOFT:
          if (request->wIndex == 7) {
            /* Get Microsoft OS 2.0 compatible descriptor */
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20+8, 2);
            return tud_control_xfer(rhport, request, (void*)(uintptr_t) desc_ms_os_20, total_len);
          } else {
            return false;
          }
        default:
          break;
      }
      break;
    case TUSB_REQ_TYPE_INVALID:  /* 3 */
      break;
    default:
      break;
  }

  /* Stall at unknown request */
  return false;
}


/* MAIN */

void core1_main(void)
{
  /* Set core locking for flash saving ~ note this makes SIO_IRQ_PROC1 unavailable */
  flash_safe_execute_core_init();
  /* Apply saved config to used vars */
  apply_config();
  /* Detect optional external crystal */
  if (detect_clocksignal() == 0) {
    usbsid_config.external_clock = false;
    gpio_deinit(PHI); /* Disable PHI as gpio */
    init_sidclock();
  } else {  /* Do nothing gpio acts as input detection */
    usbsid_config.external_clock = true;
    usbsid_config.clock_rate = CLOCK_DEFAULT;  /* Always 1MHz */
  }

  /* Init RGB LED */
  init_rgb();
  /* Clear the dirt */
  memset(sid_memory, 0, sizeof sid_memory);
  /* Start the VU */
  init_vu();

  /* Init cycled write buffer vars */
  cpu_mhz = (clock_get_hz(clk_sys) / 1000 / 1000);
  cpu_us = (1 / cpu_mhz);
  sid_hz = usbsid_config.clock_rate;
  sid_mhz = (sid_hz / 1000 / 1000);
  sid_us = (1 / sid_mhz);
  CFG("[BOOT PICO] %lu Hz, %.0f MHz, %.4f uS\n", clock_get_hz(clk_sys), cpu_mhz, cpu_us);
  CFG("[BOOT C64]  %.0f Hz, %.6f MHz, %.4f uS\n", sid_hz, sid_mhz, sid_us);

  /* Release semaphore when core 1 is started */
  sem_release(&core1_init);

  int n_checks = 0, m_now = 0;
  m_now = to_ms_since_boot(get_absolute_time());
  while (1) {
    usbdata == 1 ? led_vumeter_task() : led_breathe_task();
    if (to_ms_since_boot(get_absolute_time()) - m_now < CHECK_INTV) { /* 20 seconds */
      /* NOTICE: Testfix for core1 setting dtype to 0 */
      /* do nothing */
    } else {
      m_now = to_ms_since_boot(get_absolute_time());
      if (vu == 0 && usbdata == 1) {
        n_checks++;
        if (n_checks >= MAX_CHECKS)
        {
          n_checks = 0, usbdata = 0, dtype = ntype;  /* NOTE: This sets dtype to 0 which causes buffertask write to go to default and error out with many consecutive reads from the bus */
        }
      }
    }
  }
}

int main()
{
  (void)desc_url;  /* NOTE: Remove if going to use it */

  #if PICO_RP2040
  /* System clock @ 125MHz */
  set_sys_clock_pll(1500000000, 6, 2);
  #elif PICO_RP2350
  /* System clock @ 150MHz */
  set_sys_clock_pll(1500000000, 5, 2);
  #endif
  /* Init board */
  board_init();
  /* Init USB */
  tud_init(BOARD_TUD_RHPORT);
  /* Init logging */
  init_logging();
  /* Log reset reason */
  reset_reason();
  /* Load config before init of USBSID settings ~ NOTE: This cannot be run from Core 1! */
  load_config(&usbsid_config);
  /* Create a blocking semaphore to wait until init of core 1 is complete */
  sem_init(&core1_init, 0, 1);
  /* Init core 1 */
  multicore_launch_core1(core1_main);
  /* Wait for core 1 to finish */
  sem_acquire_blocking(&core1_init);
  /* Check for default config bit ~ NOTE: This cannot be run from Core 1! */
  detect_default_config();
  /* Verify the clockrare in the config is not out of bounds */
  verify_clockrate();
  /* Init GPIO */
  init_gpio();
  /* Init PIO */
  setup_piobus();
  /* Sync PIOS */
  sync_pios();
  /* Init DMA */
  setup_dmachannels();
  /* Init midi */
  midi_init();

  /* Loop IO tasks forever */
  while (1) {
    tud_task_ext(/* UINT32_MAX */0, false);  // equals tud_task();
  }

  /* Point of no return, this should never be reached */
  return 0;
}
