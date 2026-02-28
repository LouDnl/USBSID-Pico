/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * usbsid.c
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

#include "globals.h"
#include "config.h"
#include "usbsid.h"
#include "midi.h"
#include "sid.h"
#include "logging.h"


/* Double Tap */
extern int flagged;  /* doubletap check */

/* Declare variables ~ Do not change order to keep memory alignment! */
uint8_t __not_in_flash("usbsid_buffer") write_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE);  /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") sid_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE);    /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") read_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE);   /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") config_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE); /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") uart_buffer[64] __aligned(2 * MAX_BUFFER_SIZE); /* 64 Bytes, 128 bytes aligned */

#if defined(ONBOARD_EMULATOR) || defined(ONBOARD_SIDPLAYER)
/* Use full 64KB memory for C64 emulator and SID player */
uint8_t __not_in_flash("c64_memory") c64memory[0x10000] __aligned(128) = {0}; /* 64 Kilo Bytes, 128 bytes aligned */
/* Pointer to SID address range in memory */
uint8_t * sid_memory = &c64memory[0xd400]; /* Pointer to $d400 of 128 Bytes total */
#else
/* 128 Bytes 'Memory' storage for SID registers */
uint8_t __not_in_flash("usbsid_buffer") sid_memory[(0x20 * 4)] __aligned(2 * (0x20 * 4)); /* 256 Bytes */
#endif

volatile int usb_connected = 0, usbdata = 0;
volatile uint32_t cdcread = 0, cdcwrite = 0, webread = 0, webwrite = 0;
volatile uint8_t *cdc_itf = 0, *wusb_itf = 0;
/* nonetype, datatype, returntype */
volatile char ntype = '0', dtype = '0', rtype = '0';
const char cdc = 'C', asid = 'A', midi = 'M', sysex = 'S', wusb = 'W', uart = 'U';
static bool web_serial_connected = false;

volatile double cpu_mhz = 0, cpu_us = 0, sid_hz = 0, sid_mhz = 0, sid_us = 0;
volatile bool auto_config = false;
volatile bool offload_ledrunner = false;

/* Init var pointers for external use */
uint8_t (*write_buffer_p)[MAX_BUFFER_SIZE] = &write_buffer;

/* Config */
extern void load_config(Config * config);
extern void handle_config_request(uint8_t * buffer, uint32_t size);
extern void print_config(void);
extern void apply_config(bool at_boot, bool print_cfg);
extern void save_config_ext(void);
extern void detect_default_config();
extern Config usbsid_config;
extern RuntimeCFG cfg;
extern bool first_boot;

/* GPIO */
extern void init_gpio(void);
extern void setup_sidclock(void);
extern void setup_piobus(void);
extern void setup_dmachannels(void);
extern void sync_pios(bool at_boot);
extern void pause_sid(void);
extern void pause_sid_withmute(void);
extern void mute_sid(void);
extern void unmute_sid(void);
extern void reset_sid(void);
extern void reset_sid_registers(void);
extern void enable_sid(bool unmute);
extern void disable_sid(void);
extern void clear_bus_all(void);
extern uint16_t cycled_delay_operation(uint16_t cycles);
extern uint8_t cycled_read_operation(uint8_t address, uint16_t cycles);
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern bool is_muted;

/* Uart */
#ifdef USE_PIO_UART
extern void init_uart(void);
#endif

/* Vu */
extern volatile uint16_t vu;
extern void init_vu(void);
extern void led_runner(void);

/* MCU */
extern void mcu_reset(void);
extern void mcu_jump_to_bootloader(void);

/* SID */
extern bool get_reset_state(void);

/* SID tests */
extern bool running_tests;

/* SID detection */
extern void auto_detect_routine(void);

/* SID player */
#ifdef ONBOARD_EMULATOR
extern bool
  emulator_running,
  starting_emulator,
  stopping_emulator;
extern void start_cynthcart(void);
extern unsigned int run_cynthcart(void);
#endif /* ONBOARD_EMULATOR */
#if defined(ONBOARD_SIDPLAYER)
volatile bool sidplayer_init = false;
volatile bool sidplayer_start = false;
volatile bool sidplayer_playing = false;
volatile bool sidplayer_stop = false;
volatile bool sidplayer_next = false;
volatile bool sidplayer_prev = false;

extern uint8_t * sidfile; /* Temporary buffer to store incoming data */
extern volatile int sidfile_size;
extern volatile char tuneno;
extern volatile bool is_prg;
extern void load_prg(uint8_t * binary_, size_t binsize_, bool loop);
extern void load_sidtune(uint8_t * sidfile, int sidfilesize, char subt);
extern void init_sidplayer(void);
extern void start_sidplayer(bool loop);
extern void loop_sidplayer(void);
extern void stop_sidplayer(void);
extern void next_subtune(void);
extern void previous_subtune(void);
#endif /* ONBOARD_SIDPLAYER */

/* Midi */
extern void midi_init(void);
extern void process_stream(uint8_t *buffer, size_t size);

/* ASID */
extern void asid_init(void);

/* Midi init */
midi_machine midimachine;

/* Queues */
queue_t sidtest_queue;
queue_t logging_queue;

/* Multicore sync using atomic memory - avoids semaphore spin locks AND
 * FIFO (which is consumed by flash_safe_execute IRQ handler) */
static volatile uint32_t core_sync_state = 0;
#define SYNC_CORE1_STAGE1  0x11  /* Core 1 finished flash_safe_execute_core_init */
#define SYNC_CORE0_STAGE1  0x01  /* Core 0 finished config loading */
#define SYNC_CORE1_STAGE2  0x12  /* Core 1 finished queue/uart init */
#define SYNC_CORE0_STAGE2  0x02  /* Core 0 finished hardware init */

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
  usNFO("[RESET] Button double tapped? %d\n", flagged);
  io_rw_32 *rr = (io_rw_32 *) (VREG_AND_CHIP_RESET_BASE + VREG_AND_CHIP_RESET_CHIP_RESET_OFFSET);
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS)
    usNFO("[RESET] Caused by power-on reset or brownout detection\n");
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS)
    usNFO("[RESET] Caused by RUN pin trigger ~ manual or ISA RESET signal\n");
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS)
    usNFO("[RESET] Caused by debug port\n");
#elif PICO_RP2350
  /* io_rw_32 *rr = (io_rw_32 *) (POWMAN_BASE + POWMAN_CHIP_RESET_OFFSET); */
  usNFO("[RESET] Button double tapped? %d %X\n", flagged, powman_hw->chip_reset);
  if (/* *rr */ powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS)
    usNFO("[RESET] Caused by arm debugger\n");
  if (/* *rr */ powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_RESCUE_BITS)
    usNFO("[RESET] Caused by rescure reset from arm debugger\n");
  if (/* *rr */ powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS)
    usNFO("[RESET] Caused by RUN pin trigger ~ manual or ISA RESET signal\n");
  if (/* *rr */ powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS)
    usNFO("[RESET] Caused by brownout detection\n");
  if (/* *rr */ powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS)
    usNFO("[RESET] Caused by power-on reset\n");
#endif
  return;
}


/* SETUP */

/* Initialise debug logging if enabled */
void init_logging(void)
{
  #if defined(USBSID_UART)
  stdio_uart_init_full(uart0, BAUD_RATE, TX, RX);
  sleep_ms(100);  /* leave time for uart to settle */
  stdio_flush();
  usNFO("[NFO] Uart logging initialised\n");
  #endif
  return;
}


/* USB TO HOST */

/* Write from device to host */
void cdc_write(volatile uint8_t * itf, uint32_t n)
{ /* No need to check if write available with current driver code */
  usIO("[O %d] [%c] $%02X:%02X\n", n, dtype, sid_buffer[1], write_buffer[0]);
  tud_cdc_n_write(*itf, write_buffer, n);  /* write n bytes of data to client */
  tud_cdc_n_write_flush(*itf);
  return;
}

/* Write from device to host */
void webserial_write(volatile uint8_t * itf, uint32_t n)
{ /* No need to check if write available with current driver code */
  usIO("[O %d] [%c] $%02X:%02X\n", n, dtype, sid_buffer[1], write_buffer[0]);
  tud_vendor_write(write_buffer, n);
  tud_vendor_flush();
  return;
}


/* BUFFER HANDLING */

int __no_inline_not_in_flash_func(do_buffer_tick)(int top, int step)
{
  static int i = 1;
  if (i < 1) i = 1;  /* Guard: static init unreliable with -O3 */
  cycled_write_operation(sid_buffer[i], sid_buffer[i + 1], (step == 4 ? (sid_buffer[i + 2] << 8 | sid_buffer[i + 3]) : MIN_CYCLES));
  WRITEDBG(dtype, i, top, sid_buffer[i], sid_buffer[i + 1], (step == 4 ? (sid_buffer[i + 2] << 8 | sid_buffer[i + 3]) : MIN_CYCLES));
  usIO("[I %d] [%c] $%02X:%02X (%u)\n", i, dtype, sid_buffer[i], sid_buffer[i + 1], (step == 4 ? (sid_buffer[i + 2] << 8 | sid_buffer[i + 3]) : MIN_CYCLES));
  if (i+step >= top) {
    i = 1;
    return i;
  }
  i += step;
  return 0;
}

void __no_inline_not_in_flash_func(buffer_task)(int n_bytes, int step)
{
  int state = 0;
  do {
    usbdata = 1;
    state = do_buffer_tick(n_bytes, step);
  } while (state != 1);
}

/* Process received usb data */
void __no_inline_not_in_flash_func(process_buffer)(volatile uint8_t * itf, volatile uint32_t * n)
{
  usbdata = 1;
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  uint8_t command = ((sid_buffer[0] & PACKET_TYPE) >> 6);
  uint8_t subcommand = (sid_buffer[0] & COMMAND_MASK);
  uint8_t n_bytes = (sid_buffer[0] & BYTE_MASK);
  if __us_unlikely(get_reset_state() && (command != COMMAND)) { return; };  /* Drop incoming data if in reset state */

  if __us_likely(command == CYCLED_WRITE) {
    // n_bytes = (n_bytes == 0) ? 4 : n_bytes; /* if byte count is zero, this is a single write packet */
    if __us_unlikely(n_bytes == 0) {
      cycled_write_operation(sid_buffer[1], sid_buffer[2], (sid_buffer[3] << 8 | sid_buffer[4]));
      WRITEDBG(dtype, n_bytes, n_bytes, sid_buffer[1], sid_buffer[2], (sid_buffer[3] << 8 | sid_buffer[4]));
      usIO("[I %d] [%c] $%02X:%02X (%u)\n", n_bytes, dtype, sid_buffer[1], sid_buffer[2], (sid_buffer[3] << 8 | sid_buffer[4]));
    } else {
      buffer_task(n_bytes, 4);
    }
    return;
  };
  if __us_likely(command == WRITE) {
    // n_bytes = (n_bytes == 0) ? 2 : n_bytes; /* if byte count is zero, this is a single write packet */
    if __us_likely(n_bytes == 0) {
      cycled_write_operation(sid_buffer[1], sid_buffer[2], 6);  /* Add 6 cycles to each write for LDA(2) & STA(4) */
      WRITEDBG(dtype, n_bytes, n_bytes, sid_buffer[1], sid_buffer[2], 6);
      usIO("[I %d] [%c] $%02X:%02X (%u)\n", n_bytes, dtype, sid_buffer[1], sid_buffer[2], 6);
    } else {
      buffer_task(n_bytes, 2);
    }
    return;
  };
  if __us_unlikely(command == READ) {  /* READING CAN ONLY HANDLE ONE AT A TIME, PERIOD. */
    usIO("[I %d] [%c] $%02X:%02X\n", n_bytes, dtype, sid_buffer[1], sid_buffer[2]);
    write_buffer[0] = cycled_read_operation(sid_buffer[1], 0);  /* write the address to the SID and read the data back */
    switch (rtype) {  /* write the result to the USB client */
      case 'C':
        cdc_write(itf, BYTES_TO_SEND);
        break;
      case 'W':
        webserial_write(itf, BYTES_TO_SEND);
        break;
      default:
        usIO("[WRITE ERROR]%c\n", rtype);
        break;
    };
    vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
    return;
  };
  if (command == COMMAND) {
    switch (subcommand) {
      case CYCLED_READ:
        usIO("[I %d] [%c] $%02X %u\n", n_bytes, dtype, sid_buffer[1], (sid_buffer[2] << 8 | sid_buffer[3]));
        write_buffer[0] = cycled_read_operation(sid_buffer[1], (sid_buffer[2] << 8 | sid_buffer[3]));
        switch (rtype) {  /* write the result to the USB client */
          case 'C':
            cdc_write(itf, BYTES_TO_SEND);
            break;
          case 'W':
            webserial_write(itf, BYTES_TO_SEND);
            break;
          default:
            usIO("[WRITE ERROR]%c\n", rtype);
            break;
        };
        vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
        return;
      case DELAY_CYCLES:
        cycled_delay_operation((sid_buffer[1] << 8 | sid_buffer[2]));
        return;
      case PAUSE:
        usDBG("[PAUSE_SID]\n");
        pause_sid();
        break;
      case MUTE:
        usDBG("[MUTE_SID] %d\n",sid_buffer[1]);
        mute_sid();
        if (sid_buffer[1] == 1) is_muted = true;
        break;
      case UNMUTE:
        usDBG("[UNMUTE_SID] %d\n",sid_buffer[1]);
        if (sid_buffer[1] == 1) is_muted = false;
        unmute_sid();
        break;
      case RESET_SID:
        if (sid_buffer[1] == 0) {
          usDBG("[RESET_SID]\n");
          reset_sid();
        }
        if (sid_buffer[1] == 1) {
          usDBG("[RESET_SID_REGISTERS]\n");
          reset_sid_registers();
        }
        break;
      case DISABLE_SID:
        usDBG("[DISABLE_SID]\n");
        disable_sid();
        break;
      case ENABLE_SID:
        usDBG("[ENABLE_SID]\n");
        enable_sid(true);
        break;
      case CLEAR_BUS:
        usDBG("[CLEAR_BUS]\n");
        clear_bus_all();
        break;
      case CONFIG:
        if (sid_buffer[1] < 0xD0) { /* Don't log incoming buffer to avoid spam above this region */
          usDBG("[CONFIG]\n");
        }
        /* Copy incoming buffer ignoring the command byte */
        memcpy(config_buffer, (sid_buffer + 1), (int)*n - 1);
        handle_config_request(config_buffer, *n - 1);
        memset(config_buffer, 0, count_of(config_buffer));
        break;
      case RESET_MCU:
        usDBG("[RESET_MCU]\n");
        mcu_reset();
        break;
      case BOOTLOADER:
        usDBG("[BOOTLOADER]\n");
        mcu_jump_to_bootloader();
        break;
      default:
        break;
      }
    return;
  };
  return;
}


/* USB CALLBACKS */

void tud_mount_cb(void)
{
  usDBG("[%s]\n", __func__);
  usb_connected = 1;
}

void tud_umount_cb(void)
{
  usb_connected = 0, usbdata = 0, dtype = rtype = ntype;
  usDBG("[%s]\n", __func__);
  disable_sid();  /* NOTICE: Testing if this is causing the random lockups */
}

void tud_suspend_cb(bool remote_wakeup_en)
{
  /* (void) remote_wakeup_en; */
  usDBG("[%s] remote_wakeup_en:%d\n", __func__, remote_wakeup_en);
  usb_connected = 0, usbdata = 0, dtype = rtype = ntype;
}

void tud_resume_cb(void)
{
  usDBG("[%s]\n", __func__);
  usb_connected = 1;
}


/* USB MIDI CLASS TASK & CALLBACKS */

void midi_task(void) /* Disabled in loop ~ keeping for later use */
{ /* Same as the callback routine */
  if (tud_midi_n_mounted(MIDI_ITF)) {
    while (tud_midi_n_available(MIDI_ITF, MIDI_CABLE)) {  /* Loop as long as there is data available */
      usbdata = 1;
      uint32_t available = tud_midi_n_stream_read(MIDI_ITF, MIDI_CABLE, midimachine.usbstreambuffer, MAX_BUFFER_SIZE);  /* Reads all available bytes at once */
      process_stream(midimachine.usbstreambuffer, available);
    }
    /* Clear usb buffer after use ~ Disabled due to prematurely cut off tunes */
    /* memset(midimachine.usbstreambuffer, 0, count_of(midimachine.usbstreambuffer)); */
    return;
  }
  return;
}

void tud_midi_rx_cb(uint8_t itf)
{
  if (tud_midi_n_mounted(itf)) {
    while (tud_midi_n_available(itf, MIDI_CABLE)) {  /* Loop as long as there is data available */
      usbdata = 1;
      uint32_t available = tud_midi_n_stream_read(itf, MIDI_CABLE, midimachine.usbstreambuffer, MAX_BUFFER_SIZE);  /* Reads all available bytes at once */
      process_stream(midimachine.usbstreambuffer, available);
    }
    /* Clear usb buffer after use ~ Disabled due to prematurely cut off tunes */
    /* memset(midimachine.usbstreambuffer, 0, count_of(midimachine.usbstreambuffer)); */
    return;
  }
  return;
}


/* USB CDC CLASS TASK & CALLBACKS */

/* Read from host to device */
#ifndef USE_CDC_CALLBACK
void cdc_task(void)
{ /* Same as the callback routine */
  if (tud_cdc_n_connected(CDC_ITF)) {
    if (tud_cdc_n_available(CDC_ITF) > 0) {
      cdc_itf = CDC_ITF;
      usbdata = 1, dtype = cdc, rtype = cdc;
      cdcread = tud_cdc_n_read(CDC_ITF, &read_buffer, MAX_BUFFER_SIZE);  /* Read data from client */
      tud_cdc_n_read_flush(CDC_ITF);
      memcpy(sid_buffer, read_buffer, cdcread);
      process_buffer(cdc_itf, &cdcread);
      return;
    }
    return;
  }
  return;
}
#endif

void tud_cdc_rx_cb(uint8_t itf)
{ /* No need to check available bytes for reading */
#ifdef USE_CDC_CALLBACK
  if (itf == CDC_ITF) {
    cdc_itf = &itf;
    usbdata = 1, dtype = cdc, rtype = cdc;
    cdcread = tud_cdc_n_read(*cdc_itf, &read_buffer, MAX_BUFFER_SIZE);  /* Read data from client */
    tud_cdc_n_read_flush(*cdc_itf);
    memcpy(sid_buffer, read_buffer, cdcread);
    process_buffer(cdc_itf, &cdcread);
    return;
  }
#else
  (void)itf;
#endif
  return;
}

void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char)
{
  (void)itf;
  (void)wanted_char;
  /* usDBG("[%s]\n", __func__); */  /* Disabled due to possible uart spam */
}

void tud_cdc_tx_complete_cb(uint8_t itf)
{
  (void)itf;
  /* usDBG("[%s]\n", __func__); */ /* Disabled due to uart spam */
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  /* (void) itf; */
  /* (void) rts; */
  usDBG("[%s] itf:%x, dtr:%d, rts:%d\n", __func__, itf, dtr, rts);

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
  usDBG("[%s] itf:%x, bit_rate:%u, stop_bits:%u, parity:%u, data_bits:%u\n", __func__, itf, (int)p_line_coding->bit_rate, p_line_coding->stop_bits, p_line_coding->parity, p_line_coding->data_bits);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
  /* (void)itf; */
  /* (void)duration_ms; */
  usDBG("[%s] its:%x, duration_ms:%x\n", __func__, itf, duration_ms);
}


/* USB VENDOR CLASS CALLBACKS */

#ifdef USE_VENDOR_BUFFER
void vendor_task(void)
{ /* Same as the callback routine */
  /* If the fifo buffer is disabled, this function has no use */
  if (web_serial_connected) {
      wusb_itf = WUSB_ITF;
      usbdata = 1, dtype = wusb, rtype = wusb;
      webread = tud_vendor_n_read(WUSB_ITF, &read_buffer, MAX_BUFFER_SIZE);
      tud_vendor_n_read_flush(*wusb_itf);
      memcpy(sid_buffer, read_buffer, webread);
      process_buffer(wusb_itf, &webread);
    return;
  }
  return;
}
#endif

/* Read from host to device */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize)
{
  /* With the fifo buffer disabled the buffer contains the newest incoming data */
  /* If the fifo buffer is enabled the buffer contains data from the previous read */

  /* vendor class has no connect check, thus use this */
  if (web_serial_connected && itf == WUSB_ITF) {
      wusb_itf = &itf; /* Since there's only 1 vendor interface, we know it's 0 */
      usbdata = 1, dtype = wusb, rtype = wusb;
      webread = bufsize;
      memcpy(sid_buffer, buffer, bufsize);
      /* No need to flush since we have no fifo */
      process_buffer(wusb_itf, &webread);
    return;
  }
  return;
}

void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes)
{
  (void)itf;
  usDBG("[%s] %lu\n", __func__, sent_bytes);
}


/* WEBUSB VENDOR CLASS CALLBACKS */

/* Handle incoming vendor and webusb data */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  usDBG("[%s] stage:%x, rhport:%x, bRequest:0x%x, wValue:%d, wIndex:%x, wLength:%x, bmRequestType:%x, type:%x, recipient:%x, direction:%x\n",
    __func__, stage, rhport,
    request->bRequest, request->wValue, request->wIndex, request->wLength, request->bmRequestType,
    request->bmRequestType_bit.type, request->bmRequestType_bit.recipient, request->bmRequestType_bit.direction);
  /* Do nothing with IDLE (0), DATA (2) & ACK (3) stages */
  if (stage != CONTROL_STAGE_SETUP) return true;  /* Stage 1 */

  switch (request->bmRequestType_bit.type) { /* BitType */
    case TUSB_REQ_TYPE_STANDARD:  /* 0 */
      break;
    case TUSB_REQ_TYPE_CLASS:     /* 1 */
      if (request->bRequest == WEBUSB_COMMAND) {
        usDBG("request->bRequest == WEBUSB_COMMAND\n");
        if (request->wValue == WEBUSB_RESET) {
          usDBG("request->wValue == WEBUSB_RESET\n");
          // BUG: NO WURKY CURKY
          /* reset_sid_registers(); */ // BUG: Temporary disabled
          reset_sid();  // NOTICE: Temporary until fixed!
          /* unmute_sid(); */
        }
        if (request->wValue == RESET_SID) {
          usDBG("request->wValue == RESET_SID\n");
          reset_sid();
        }
        if (request->wValue == PAUSE) {
          usDBG("request->wValue == PAUSE\n");
          pause_sid();
          mute_sid();
        }
        if (request->wValue == WEBUSB_CONTINUE) {
          usDBG("request->wValue == WEBUSB_CONTINUE\n");
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
    case TUSB_REQ_TYPE_VENDOR:    /* 2 */
      switch (request->bRequest) {
        case VENDOR_REQUEST_WEBUSB:
          /* Match vendor request in BOS descriptor
           * Get landing page url and return it
           * if on default config first boot
           */
          if (first_boot) {
            return tud_control_xfer(rhport, request, (void*)(uintptr_t) &desc_url, desc_url.bLength);
            first_boot = false;
          } else {
            return tud_control_status(rhport, request);
          }
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

/* Multicore sync using atomic memory (avoids semaphore spin locks AND
 * FIFO which is consumed by flash_safe_execute IRQ handler)
 *
 * Core 0 -> launch core 1
 * Core 0 -> poll for SYNC_CORE1_STAGE1
 * Core 1 -> init flash safe execute
 * Core 1 -> set SYNC_CORE1_STAGE1
 * Core 1 -> poll for SYNC_CORE0_STAGE1
 * Core 0 -> load and apply config
 * Core 0 -> set SYNC_CORE0_STAGE1
 * Core 0 -> poll for SYNC_CORE1_STAGE2
 * Core 1 -> init queues and PIO uart
 * Core 1 -> set SYNC_CORE1_STAGE2
 * Core 1 -> poll for SYNC_CORE0_STAGE2
 * Core 0 -> init GPIO, SID clock, PIO, DMA, etc.
 * Core 0 -> boot finished uart log
 * Core 0 -> set SYNC_CORE0_STAGE2
 * Core 0 -> enter while loop
 * Core 1 -> enter while loop
 */
void core1_main(void)
{
  /* Set core locking for flash saving ~ note this makes SIO_IRQ_PROC1 unavailable */
  flash_safe_execute_core_init();  /* This needs to start before any flash actions take place! */

  /* Signal Core 0 we're ready (sync point 1) */
  usBOOT("<CORE 1> Signaling core0 ready ~ 1\n");
  __dmb();  /* Memory barrier before write */
  core_sync_state = SYNC_CORE1_STAGE1;
  __sev();  /* Signal event to wake Core 0 from WFE */

  /* Wait for Core 0 to finish config loading */
  usBOOT("<CORE 1> Waiting for core0 sync ~ 1\n");
  while (core_sync_state != SYNC_CORE0_STAGE1) {
    __wfe();  /* Wait for event - low power wait */
  }
  __dmb();  /* Memory barrier after read */

  /* Init queues */
  queue_init(&sidtest_queue, sizeof(sidtest_queue_entry_t), 1);  /* 1 entry deep */
  #ifdef WRITE_DEBUG  /* Only init this queue when needed */
  queue_init(&logging_queue, sizeof(writelogging_queue_entry_t), 16384);  /* 16384 entries deep so we don't skip any writes */
  #endif

  /* Initialise PIO Uart */
  #ifdef USE_PIO_UART
  init_uart();
  #endif

  /* Signal Core 0 we're ready (sync point 2) */
  usBOOT("<CORE 1> Signaling core0 ready ~ 2\n");
  __dmb();
  core_sync_state = SYNC_CORE1_STAGE2;
  __sev();

  /* Wait for Core 0 to finish hardware init */
  usBOOT("<CORE 1> Waiting for core0 sync ~ 2\n");
  while (core_sync_state != SYNC_CORE0_STAGE2) {
    __wfe();
  }
  __dmb();

  while (1) {

    /* Check SID test queue */
    if (running_tests) {
      sidtest_queue_entry_t s_entry;
      if (queue_try_remove(&sidtest_queue, &s_entry)) {
        s_entry.func(s_entry.s, s_entry.t, s_entry.wf);
      }
    }

    /* Blinky blinky? */
    if (!offload_ledrunner) {
      led_runner();
    }

#if ONBOARD_SIDPLAYER
    if (sidplayer_init) {
      sidplayer_init = false;
      sidplayer_start = false;
      sidplayer_playing = false;
      offload_ledrunner = true;
      if (is_prg) {
        load_prg(sidfile, sidfile_size, false); /* Load PRG without auto looping */
      } else {
        load_sidtune(sidfile, sidfile_size, tuneno);
      }
      sidplayer_start = true;
      free(sidfile);
      sidfile = NULL;
    }
    if (sidplayer_start) {
      sidplayer_init = false;
      sidplayer_start = false;
      sidplayer_playing = true;
      if (!is_prg) {
        init_sidplayer(); // WARNING: rp2040 insufficient memory!
        start_sidplayer(false); /* No auto loop */
      }
    }
    if (sidplayer_stop) {
      stop_sidplayer();
      sidplayer_stop = false;
      sidplayer_playing = false;
      offload_ledrunner = true;
    }
    if __us_unlikely (sidplayer_next && !sidplayer_playing) {
      next_subtune();
      sleep_us(20000);
      sidplayer_next = false;
      sidplayer_playing = true;
    }
    if __us_unlikely (!sidplayer_playing && sidplayer_prev) {
      previous_subtune();
      sidplayer_prev = false;
      sidplayer_playing = true;
    }
    if (sidplayer_playing) {
      loop_sidplayer();
      if __us_unlikely (sidplayer_next || sidplayer_prev) {
        sidplayer_playing = false;
      }
    }
#endif /* ONBOARD_SIDPLAYER */

#ifdef ONBOARD_EMULATOR
    if (!emulator_running && starting_emulator) {
      starting_emulator = false;
      emulator_running = true;
      start_cynthcart();
    }
    if (emulator_running && !starting_emulator) {
      run_cynthcart();
    }
#endif /* ONBOARD_EMULATOR */

#ifdef WRITE_DEBUG  /* Only run this queue when needed */
    if (usbdata == 1) {
      writelogging_queue_entry_t l_entry;
      if (queue_try_remove(&logging_queue, &l_entry)) {
        usDBG("[CORE2 %5u] [WRITE %c:%02d/%02d] $%02X:%02X %u\n",
          queue_get_level(&logging_queue),
          l_entry.dtype, l_entry.n, l_entry.s, l_entry.reg, l_entry.val, l_entry.cycles);
      }
    }
#endif

  }
  /* Point of no return, this should never be reached */
  return;
}

int main()
{
  /* Set system clockspeed */
#if PICO_RP2040
  /* System clock @ MAX SPEED!! ARRRR 200MHz */
  set_sys_clock_khz(200000, true);
#if 0
  /* System clock @ 125MHz */
  set_sys_clock_pll(1500000000, 6, 2);
#endif
#elif PICO_RP2350
  /* Onboard SID player requires atleast 200MHz! */
#if ONBOARD_SIDPLAYER
  /* System clock @ 200MHz */
  set_sys_clock_khz(200000, true);
#else
  /* System clock @ 150MHz */
  set_sys_clock_pll(1500000000, 5, 2);
#endif /* ONBOARD_SIDPLAYER */
#endif /* PICO_RP2350 */

  /* Init TinyUSB */
  tusb_rhport_init_t dev_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  /* Init logging */
  init_logging();
  /* Log reset reason */
  reset_reason();

  /* Clear flagged */
  if (flagged) { auto_config = true; flagged = 0; }  /* NOTE: Does not work on rp2350 */

  /* Launch Core 1 and wait for flash_safe_execute_core_init to complete */
  usBOOT("CORE0 Launching core1\n");
  multicore_launch_core1(core1_main);

  /* Wait for Core 1 to signal ready (sync point 1) */
  usBOOT("CORE0 Waiting for core1 ready ~ 1\n");
  while (core_sync_state != SYNC_CORE1_STAGE1) {
    __wfe();  /* Wait for event - low power wait */
  }
  __dmb();  /* Insert memory barrier after read */

  /* Load config before init of USBSID settings ~ NOTE: This cannot be run from Core 1! */
  load_config(&usbsid_config);
  /* Apply saved config to used vars */
  apply_config(true, false);

  /* Log boot CPU and C64 clock speeds */
  cpu_mhz = (clock_get_hz(clk_sys) / 1000 / 1000);
  cpu_us = (1 / cpu_mhz);
  sid_hz = usbsid_config.clock_rate;
  sid_mhz = (sid_hz / 1000 / 1000);
  sid_us = (1 / sid_mhz);
  if (!auto_config) {
    usNFO("\n");
    usNFO("[NFO] [PICO] %lu Hz, %.0f MHz, %.4f uS\n", clock_get_hz(clk_sys), cpu_mhz, cpu_us);
    usNFO("[NFO] [C64] %.0f Hz, %.6f MHz, %.4f uS\n", sid_hz, sid_mhz, sid_us);
    usNFO("[NFO] [C64] REFRESH_RATE %lu Cycles, RASTER_RATE %lu Cycles\n", usbsid_config.refresh_rate, usbsid_config.raster_rate);
  }

  /* Signal Core 1 to continue (sync point 1) */
  usBOOT("<CORE 0> Signaling core1 ~ 1\n");
  __dmb();  /* Insert memory barrier after read */
  core_sync_state = SYNC_CORE0_STAGE1;
  __sev();  /* Signal event to wake Core 1 from WFE */

  /* Wait for Core 1 to finish queue/uart init (sync point 2) */
  usBOOT("<CORE 0> Waiting for core1 ready ~ 2\n");
  while (core_sync_state != SYNC_CORE1_STAGE2) {
    __wfe();  /* Wait for event - low power wait */
  }
  __dmb();  /* Insert memory barrier after read */

  /* Init GPIO */
  usBOOT("Initializing GPIO\n");
  init_gpio();
  /* Start verification, detect and init sequence of SID clock */
  usBOOT("Setup SID clock\n");
  setup_sidclock();
  /* Init PIO */
  usBOOT("Setup PIO bus\n");
  setup_piobus();
  /* Sync PIOS */
  usBOOT("Synchronise PIO's\n");
  sync_pios(true);
  /* Init DMA */
  usBOOT("Setup DMA channels\n");
  setup_dmachannels();
  /* Start the VU */
  usBOOT("Initialise Vu\n");
  init_vu();
  /* Init midi */
  usBOOT("Initialise Midi\n");
  midi_init();
  /* Init ASID */
  usBOOT("Initialise ASID\n");
  asid_init();
  /* Enable SID chips */
  usBOOT("Enable SID chips\n");
  enable_sid(false);

  /* Clear the dirt */
#if !defined(ONBOARD_EMULATOR) && !defined(ONBOARD_SIDPLAYER)
  memset(sid_memory, 0, sizeof sid_memory);
#endif

  /* Check for default config bit
   * NOTE: This cannot be run from Core 1! */
  if (!auto_config) {
    detect_default_config();
  }
  if (auto_config) {  /* NOTE: Does not work on rp2350 */
    auto_detect_routine();  /* Double tap! */
    save_config_ext();
    auto_config = false;
    mcu_reset();
  }
  /* Print config once at end of boot routine */
  print_config();

  /* Reset SID chips */
  usBOOT("Reset SID chips\n");
  reset_sid(); /* WARNING: Might cause issues! */
  usBOOT("Reset SID registers\n");
  reset_sid_registers(); /* WARNING: Might cause issues! */

  {
    extern const char *us_product;
    extern const char *project_version;
    usNFO("\n");
#ifdef ONBOARD_EMULATOR
    usDBG("Firmware is compiled with Cynthcart support\n");
#endif
#ifdef ONBOARD_SIDPLAYER
    usDBG("Firmware is compiled with onboard SID player\n");
#endif
    usDBG("%s v%s Started successfully\n\n", us_product, project_version);
  }

  /* Signal Core 1 to enter main loop (sync point 2) */
  usBOOT("<CORE 0> Signaling core1 ~ 2\n");
  __dmb();  /* Memory barrier after read */
  core_sync_state = SYNC_CORE0_STAGE2;
  __sev();  /* Signal event to wake Core 1 from WFE */

  /* Loop IO tasks forever */
  while (1) {
    tud_task_ext(/* UINT32_MAX */0, false);  /* equals tud_task(); */
#ifndef USE_CDC_CALLBACK
    cdc_task();  /* Only use this if no callbacks */
#endif
#ifdef USE_VENDOR_BUFFER
    vendor_task();  /* Only use this if buffering and fifo are enabled */
#endif

    if (offload_ledrunner) {
      led_runner();
    }
  }

  /* Point of no return, this should never be reached */
  return 0;
}
