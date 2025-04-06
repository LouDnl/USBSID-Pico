/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
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


/* Init external vars */
static semaphore_t core0_init, core1_init;

/* Init vars ~ Do not change order to keep memory alignment! */
uint8_t __not_in_flash("usbsid_buffer") config_buffer[5];
uint8_t __not_in_flash("usbsid_buffer") sid_memory[(0x20 * 4)] __attribute__((aligned(2 * (0x20 * 4))));
uint8_t __not_in_flash("usbsid_buffer") write_buffer[MAX_BUFFER_SIZE] __attribute__((aligned(2 * MAX_BUFFER_SIZE)));
uint8_t __not_in_flash("usbsid_buffer") sid_buffer[MAX_BUFFER_SIZE] __attribute__((aligned(2 * MAX_BUFFER_SIZE)));
uint8_t __not_in_flash("usbsid_buffer") read_buffer[MAX_BUFFER_SIZE] __attribute__((aligned(2 * MAX_BUFFER_SIZE)));
int usb_connected = 0, usbdata = 0;
uint32_t cdcread = 0, cdcwrite = 0, webread = 0, webwrite = 0;
uint8_t *cdc_itf = 0, *wusb_itf = 0;
char ntype = '0', dtype = '0', cdc = 'C', asid = 'A', midi = 'M', wusb = 'W';
bool web_serial_connected = false;
double cpu_mhz = 0, cpu_us = 0, sid_hz = 0, sid_mhz = 0, sid_us = 0;

/* Init var pointers for external use */
uint8_t (*write_buffer_p)[MAX_BUFFER_SIZE] = &write_buffer;

/* Config externals */
Config usbsid_config;
extern int sock_one, sock_two, sids_one, sids_two, numsids, act_as_one;
extern bool first_boot;
extern void load_config(Config * config);
extern void handle_config_request(uint8_t * buffer);
extern void apply_config(bool at_boot);
extern void detect_default_config(void);

/* GPIO externals */
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
extern uint8_t __not_in_flash_func(bus_operation)(uint8_t command, uint8_t address, uint8_t data);
extern void __not_in_flash_func(cycled_bus_operation)(uint8_t address, uint8_t data, uint16_t cycles);

/* Vu externals */
extern uint16_t vu;
extern void init_vu(void);
extern void led_runner(void);

/* MCU externals */
extern void mcu_reset(void);
extern void mcu_jump_to_bootloader(void);

/* SID externals */
extern bool running_tests;

/* Midi externals */
extern void midi_init(void);
extern void process_stream(uint8_t *buffer, size_t size);

/* Midi init */
midi_machine midimachine;

/* Queues */
queue_t sidtest_queue;
queue_t logging_queue;

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
    DBG("[RESET] Caused by power-on reset or brownout detection\n");
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS)
    DBG("[RESET] Caused by RUN pin trigger ~ manual or ISA RESET signal\n");
  if(*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS)
    DBG("[RESET] Caused by debug port\n");
#elif PICO_RP2350
  io_rw_32 *rr = (io_rw_32 *) (POWMAN_BASE + POWMAN_CHIP_RESET_OFFSET);
  if(*rr & POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS)
    DBG("[RESET] Caused by arm debugger\n");
  if(*rr & POWMAN_CHIP_RESET_HAD_RESCUE_BITS)
    DBG("[RESET] Caused by rescure reset from arm debugger\n");
  if (*rr & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS)
    DBG("[RESET] Caused by RUN pin trigger ~ manual or ISA RESET signal\n");
  if (*rr & POWMAN_CHIP_RESET_HAD_BOR_BITS)
    DBG("[RESET] Caused by brownout detection\n");
  if (*rr & POWMAN_CHIP_RESET_HAD_POR_BITS)
    DBG("[RESET] Caused by power-on reset\n");
#endif
  return;
}


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


/* USB TO HOST */

/* Write from device to host */
void cdc_write(uint8_t * itf, uint32_t n)
{ /* No need to check if write available with current driver code */
  IODBG("[O %d] [%c] $%02X:%02X\n", n, dtype, sid_buffer[1], write_buffer[0]);
  tud_cdc_n_write(*itf, write_buffer, n);  /* write n bytes of data to client */
  tud_cdc_n_write_flush(*itf);
  return;
}

/* Write from device to host */
void webserial_write(uint8_t * itf, uint32_t n)
{ /* No need to check if write available with current driver code */
  IODBG("[O %d] [%c] $%02X:%02X\n", n, dtype, sid_buffer[1], write_buffer[0]);
  tud_vendor_write(write_buffer, n);
  tud_vendor_flush();
  return;
}


/* BUFFER HANDLING */

int __not_in_flash_func(do_buffer_tick)(int top, int step)
{
  static int i = 1;
  cycled_bus_operation(sid_buffer[i], sid_buffer[i + 1], (step == 4 ? (sid_buffer[i + 2] << 8 | sid_buffer[i + 3]) : MIN_CYCLES));
  WRITEDBG(dtype, i, sid_buffer[i], sid_buffer[i + 1], (step == 4 ? (sid_buffer[i + 2] << 8 | sid_buffer[i + 3]) : MIN_CYCLES));
  if (i+step >= top) {
    i = 1;
    return i;
  }
  i += step;
  return 0;
}

void __not_in_flash_func(buffer_task)(int n_bytes, int step)
{
  int state = 0;
  do {
    usbdata = 1;
    state = do_buffer_tick(n_bytes, step);
  } while (state != 1);
}

/* Process received usb data */
void __not_in_flash_func(process_buffer)(uint8_t * itf, uint32_t * n)
{
  usbdata = 1;
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  uint8_t command = ((sid_buffer[0] & PACKET_TYPE) >> 6);
  uint8_t subcommand = (sid_buffer[0] & COMMAND_MASK);
  uint8_t n_bytes = (sid_buffer[0] & BYTE_MASK);

  if (command == CYCLED_WRITE) {
    // n_bytes = (n_bytes == 0) ? 4 : n_bytes; /* if byte count is zero, this is a single write packet */
    if (n_bytes == 0) {
      cycled_bus_operation(sid_buffer[1], sid_buffer[2], (sid_buffer[3] << 8 | sid_buffer[4]));
      WRITEDBG(dtype, n_bytes, sid_buffer[1], sid_buffer[2], (sid_buffer[3] << 8 | sid_buffer[4]));
    } else {
      buffer_task(n_bytes, 4);
    }
    return;
  };
  if (command == WRITE) {
    // n_bytes = (n_bytes == 0) ? 2 : n_bytes; /* if byte count is zero, this is a single write packet */
    if (n_bytes == 0) {
      bus_operation(0x10, sid_buffer[1], sid_buffer[2]);  /* Leave this on non cycled, errornous playback otherwise! */
      WRITEDBG(dtype, 0, sid_buffer[1], sid_buffer[2], 0);
    } else {
      buffer_task(n_bytes, 2);
    }
    return;
  };
  if (command == READ) {  /* READING CAN ONLY HANDLE ONE AT A TIME, PERIOD. */
    IODBG("[I %d] [%c] $%02X:%02X\n", n_bytes, dtype, sid_buffer[1], sid_buffer[2]);
    write_buffer[0] = bus_operation((0x10 | READ), sid_buffer[1], sid_buffer[2]);  /* write the address to the SID and read the data back */
    // write_buffer[0] = bus_operation((0x10 | READ), sid_buffer[1], 0x0);  /* write the address to the SID and read the data back */
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
    vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
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
        enable_sid(true);
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


/* USB MIDI CLASS CALLBACKS */

void tud_midi_rx_cb(uint8_t itf)
{
  if (tud_midi_n_mounted(itf)) {
    usbdata = 1;
    while (tud_midi_n_available(itf, 0)) {  /* Loop as long as there is data available */
      uint32_t available = tud_midi_n_stream_read(itf, 0, midimachine.usbstreambuffer, MAX_BUFFER_SIZE);  /* Reads all available bytes at once */
      process_stream(midimachine.usbstreambuffer, available);
    }
    /* Clear usb buffer after use ~ Disable due to prematurely cut off tunes */
    /* memset(midimachine.usbstreambuffer, 0, count_of(midimachine.usbstreambuffer)); */
    return;
  }
  return;
}


/* USB CDC CLASS CALLBACKS */

/* Read from host to device */
void tud_cdc_rx_cb(uint8_t itf)
{ /* No need to check available bytes for reading */
  cdc_itf = &itf;
  usbdata = 1, dtype = cdc;
  cdcread = tud_cdc_n_read(*cdc_itf, &read_buffer, MAX_BUFFER_SIZE);  /* Read data from client */
  tud_cdc_n_read_flush(*cdc_itf);
  memcpy(sid_buffer, read_buffer, cdcread);
  process_buffer(cdc_itf, &cdcread);
  /* memset(read_buffer, 0, count_of(read_buffer)); */
  /* memset(sid_buffer, 0, count_of(sid_buffer)); */
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


/* USB VENDOR CLASS CALLBACKS */

/* Read from host to device */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize)
{
  /* With the fifo buffer disabled the buffer contains the newest incoming data */
  /* If the fifo buffer is enabled the buffer contains data from the previous read */

  /* vendor class has no connect check, thus use this */
  if (web_serial_connected && itf == WUSB_ITF) {
      wusb_itf = &itf; /* Since there's only 1 vendor interface, we know it's 0 */
      usbdata = 1, dtype = wusb;
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
    case TUSB_REQ_TYPE_CLASS:     /* 1 */
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

/* Semaphore ping pong
 *
 * Core 0 -> init semapohre core1_init
 * Core 0 -> launch core 1
 * Core 0 -> wait for core1_init signal
 * Core 1 -> init flash safe execute
 * Core 1 -> release semaphore core1_init
 * Core 0 -> continue to load and apply config
 * Core 0 -> init SID clock
 * Core 1 -> init semaphore core0_init
 * Core 1 -> wait for core0_init signal
 * Core 0 -> release semaphore core0_init
 * Core 0 -> wait for core1_init signal
 * Core 1 -> continue startup RGB & Vu
 * Core 1 -> release semaphore core1_init
 * Core 1 -> wait for core0_init signal
 * Core 0 -> continue startup
 * Core 0 -> release semaphore core0_init
 * Core 0 -> enter while loop
 * Core 1 -> continue startup
 * Core 1 -> enter while loop
 */

void core1_main(void)
{
  /* Set core locking for flash saving ~ note this makes SIO_IRQ_PROC1 unavailable */
  flash_safe_execute_core_init();  /* This needs to start before any flash actions take place! */

  /* Workaround to make sure flash_safe_execute is executed
   * before everything else if a default config is detected
   * This just ping pongs bootup around with Core 0
   *
   * Release core 1 semaphore */
  sem_release(&core1_init);

  /* Create a blocking semaphore with max 1 permit */
  sem_init(&core0_init, 0, 1);
  /* Wait for core 0 to signal back */
  sem_acquire_blocking(&core0_init);

  /* Clear the dirt */
  memset(sid_memory, 0, sizeof sid_memory);

  /* Start the VU */
  init_vu();

  /* Init queues */
  queue_init(&sidtest_queue, sizeof(sidtest_queue_entry_t), 1);  /* 1 entry deep */
  #ifdef WRITE_DEBUG  /* Only init this queue when needed */
  queue_init(&logging_queue, sizeof(writelogging_queue_entry_t), 16);  /* 16 entries deep */
  #endif

  /* Release semaphore when core 1 is started */
  sem_release(&core1_init);

  /* Wait for core 0 to signal boot finished */
  sem_acquire_blocking(&core0_init);

  while (1) {
    /* Check SID test queue */
    if (running_tests) {
      sidtest_queue_entry_t s_entry;
      if (queue_try_remove(&sidtest_queue, &s_entry)) {
        s_entry.func(s_entry.s, s_entry.t, s_entry.wf);
      }
    }

    #ifdef WRITE_DEBUG  /* Only run this queue when needed */
    if (usbdata == 1) {
      writelogging_queue_entry_t l_entry;
      if (queue_try_remove(&logging_queue, &l_entry)) {
        DBG("[CORE2] [WRITE %c:%02d] $%02X:%02X %u\n", l_entry.dtype, l_entry.n, l_entry.reg, l_entry.val, l_entry.cycles);
      }
    }
    #endif

    led_runner();
  }
  /* Point of no return, this should never be reached */
  return;
}

int main()
{
  /* Set system clockspeed */
  #if PICO_RP2040
  /* System clock @ 125MHz */
  set_sys_clock_pll(1500000000, 6, 2);
  #elif PICO_RP2350
  /* System clock @ 150MHz */
  set_sys_clock_pll(1500000000, 5, 2);
  #endif

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

  /* Workaround to make sure flash_safe_execute is executed
   * before everything else if a default config is detected
   * This just ping pongs bootup around with Core 1
   *
   * Create a blocking semaphore with max 1 permit */
  sem_init(&core1_init, 0, 1);
  /* Init core 1 */
  multicore_launch_core1(core1_main);
  /* Wait for core 1 to signal back */
  sem_acquire_blocking(&core1_init);

  /* Load config before init of USBSID settings ~ NOTE: This cannot be run from Core 1! */
  load_config(&usbsid_config);
  /* Apply saved config to used vars */
  apply_config(true);
  /* Check for default config bit
   * NOTE: This cannot be run from Core 1! */
  detect_default_config();

  /* Log boot CPU and C64 clock speeds */
  cpu_mhz = (clock_get_hz(clk_sys) / 1000 / 1000);
  cpu_us = (1 / cpu_mhz);
  sid_hz = usbsid_config.clock_rate;
  sid_mhz = (sid_hz / 1000 / 1000);
  sid_us = (1 / sid_mhz);
  CFG("[BOOT PICO] %lu Hz, %.0f MHz, %.4f uS\n", clock_get_hz(clk_sys), cpu_mhz, cpu_us);
  CFG("[BOOT C64]  %.0f Hz, %.6f MHz, %.4f uS\n", sid_hz, sid_mhz, sid_us);

  /* Release core 0 semaphore */
  sem_release(&core0_init);
  /* Wait for core one to finish startup */
  sem_acquire_blocking(&core1_init);

  /* Init GPIO */
  init_gpio();
  /* Start verification, detect and init sequence of SID clock */
  setup_sidclock();
  /* Init PIO */
  setup_piobus();
  /* Sync PIOS */
  sync_pios(true);
  /* Init DMA */
  setup_dmachannels();
  /* Init midi */
  midi_init();
  /* Enable SID chips */
  // reset_sid_registers();  // Disable for now, this also enables the sid chips lol :)
  enable_sid(false);

  /* Release core 0 semaphore to signal boot finished */
  sem_release(&core0_init);

  /* Loop IO tasks forever */
  while (1) {
    tud_task_ext(/* UINT32_MAX */0, false);  /* equals tud_task(); */
  }

  /* Point of no return, this should never be reached */
  return 0;
}
