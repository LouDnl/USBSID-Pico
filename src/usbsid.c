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

#include <globals.h> /* Includes macros, sid_defs & usbsid_defs */
#include <usbsid.h>
#include <usbsid_constants.h>
#include <config.h>
#include <config_socket.h>
#include <gpio.h>
#include <gpio_defs.h>
#include <pio.h>
#include <dma.h>
#include <bus.h>
#include <uart.h>
#include <vu.h>
#include <mcu.h>
#include <sid.h>
#include <sid_tests.h>
#include <midi.h>
#include <midi_engine.h>
#include <midi_handler.h>
#include <asid.h>
#include <logging.h>
#ifdef USE_NET
#include <net_bluetooth.h>
#include <net_wifi.h>
#include <net_config.h>
#ifdef USE_NSD
#include <nsd.h>
#endif /* USE_NSD */
#endif /* USE_NET */
#if defined(ONBOARD_EMULATOR)
#include <sid_player.h>
#include <usplayer.h>
#include <cynthcart_embedded.h>
#endif


/* Declare variables ~ Do not change order to keep memory alignment! */
uint8_t __not_in_flash("usbsid_buffer") write_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE);  /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") sid_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE);    /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") read_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE);   /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") config_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE); /* 64 Bytes, 128 bytes aligned */
uint8_t __not_in_flash("usbsid_buffer") uart_buffer[MAX_BUFFER_SIZE] __aligned(2 * MAX_BUFFER_SIZE);   /* 64 Bytes, 128 bytes aligned */
uint8_t *write_buffer_p = write_buffer; /* Init pointer for external use */

/* 128 Bytes 'Memory' storage for SID registers */
uint8_t __not_in_flash("usbsid_buffer") sid_memory[SID_MEMORY_SIZE] __aligned(SID_MEMORY_SIZE) = {0}; /* 128 Bytes, 128 bytes aligned */

volatile static bool receivedata = false, sidwriting = false;
/**
 * @brief Check whether data has been received from a USB host
 *
 * @return bool current receivedata state
 */
volatile bool is_receivedata(void) { return receivedata; };
/**
 * @brief Set the received-data flag
 *
 * @param bool state
 */
volatile void set_receivedata(bool state) { receivedata = state; };
/**
 * @brief Check whether a SID write is currently in progress
 *
 * @return bool current sidwriting state
 */
volatile bool is_sidwriting(void) { return sidwriting; };
/**
 * @brief Set the SID-writing-in-progress flag
 *
 * @param bool state
 */
volatile void set_sidwriting(bool state) { sidwriting = state; };;
volatile uint32_t cdcread = 0, cdcwrite = 0, webread = 0, webwrite = 0;
volatile uint8_t *cdc_itf = 0, *wusb_itf = 0;
/* nonetype, datatype, returntype */
volatile char ntype = '0', dtype = '0', rtype = '0';
const char cdc = 'C', asid = 'A', midi = 'M', sysex = 'S', wusb = 'W', uart = 'U';
static bool web_serial_connected = false;

volatile double cpu_mhz = 0, cpu_us = 0, sid_hz = 0, sid_mhz = 0, sid_us = 0;
volatile bool offload_ledrunner = false;
#if PCB_VERSION_INT >= 15
volatile bool detected_sid_change = false;
volatile bool sid_change_unacknowledged = true;
#else
const bool detected_sid_change = false;
#endif

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

/**
 * @brief Log the reason for the last chip reset over the debug channel
 *
 * Reads the platform reset-cause register (VREG_AND_CHIP_RESET on RP2040,
 * POWMAN chip_reset on RP2350) and prints which bit(s) caused the reset.
 */
void reset_reason(void)
{
#if PICO_RP2040
  io_rw_32 *rr = (io_rw_32 *) (VREG_AND_CHIP_RESET_BASE + VREG_AND_CHIP_RESET_CHIP_RESET_OFFSET);
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS)
    usNFO("[RESET] Caused by power-on reset or brownout detection\n");
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS)
    usNFO("[RESET] Caused by RUN pin trigger ~ manual or ISA RESET signal\n");
  if (*rr & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS)
    usNFO("[RESET] Caused by debug port\n");
#elif PICO_RP2350
  /* io_rw_32 *rr = (io_rw_32 *) (POWMAN_BASE + POWMAN_CHIP_RESET_OFFSET); */
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

/**
 * @brief Initialise debug logging if enabled
 *
 * Sets up UART stdio for debug output when USBSID_UART is compiled in;
 * no-op otherwise.
 */
void init_logging(void)
{
#if defined(USBSID_UART)
  stdio_uart_init_full(uart0, BAUD_RATE, TX, RX);
  sleep_ms(100);  /* leave time for uart to settle */
  stdio_flush();
  usNFO("\n[NFO] Uart logging initialised\n");
#endif
  return;
}


/* USB TO HOST */

/**
 * @brief Write from device to CDC host
 *
 * @param itf
 * @param n
 */
void cdc_write(volatile uint8_t * itf, uint32_t n)
{ /* No need to check if write available with current driver code */
  usIO("[O %d] [%c] $%02X:%02X\n", n, dtype, sid_buffer[1], write_buffer[0]);
  tud_cdc_n_write(*itf, write_buffer, n);  /* write n bytes of data to client */
  tud_cdc_n_write_flush(*itf);
  return;
}

/**
 * @brief Write from device to Vendor host
 *
 * @note n is dropped and we always write MAX_BUFFER_SIZE back to the Vendor interfaec
 * @note for some unknown reason the Vendor ITF has another 0 byte length packet
 *       waiting in the fifo. send a 0 byte length read to account for it.
 *
 * @param itf
 * @param n
 */
void webserial_write(volatile uint8_t * itf, uint32_t n)
{
  usIO("[O %d] [%c] $%02X:%02X\n", n, dtype, sid_buffer[1], write_buffer[0]);
  /* ADDED: pending is what is still unsent in the TX fifo. Non zero means this
   * reply is about to be merged with the previous one into a single packet, which
   * is what produced a 14 byte reply to a 1 byte question on the host. */
  uint32_t pending = CFG_TUD_VENDOR_TX_BUFSIZE - tud_vendor_n_write_available(*itf);
  uint32_t wrote = tud_vendor_n_write(*itf, write_buffer, MAX_BUFFER_SIZE);
  usIO("[VDR] TXQ want:%lu wrote:%lu pending_before:%lu\n", n, wrote, pending);
  tud_vendor_n_write_flush(*itf);
  return;
}


/* BUFFER HANDLING */

/**
 * @brief Perform a single write step from sid_buffer and advance the cursor
 *
 * Called repeatedly by buffer_task to walk sid_buffer in steps of 2 (plain
 * write: register, value) or 4 (cycled write: register, value, cycle count)
 * bytes, issuing one cycled_write_operation per call. Keeps its walk index
 * in a function-local static so successive calls continue where the last
 * one left off.
 *
 * @param int top
 * @param int step
 * @return int 1 when the buffer index wrapped back to the start (done), 0 otherwise
 */
int __no_inline_not_in_flash_func(do_buffer_tick)(int top, int step)
{
  static int i = 1;
  if (i < 1) i = 1;  /* Guard: static init unreliable with -O3 */
  if ((i + step) > MAX_BUFFER_SIZE) { i = 1; return i; } /* Guard: Cannot step outside of the maximum buffer range */
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

/**
 * @brief Drive do_buffer_tick until the whole incoming buffer has been written
 *
 * @param int n_bytes
 * @param int step
 */
void __no_inline_not_in_flash_func(buffer_task)(int n_bytes, int step)
{
  int state = 0;
  do {
    set_vu_action(); /* Keep that shiny Vu blinking! */
    state = do_buffer_tick(n_bytes, step);
  } while (state != 1);
}

/**
 * @brief Process received USB data and dispatch it to the SID bus
 *
 * Decodes the command/subcommand/byte-count header from sid_buffer[0] and
 * routes to the appropriate handler: CYCLED_WRITE and WRITE issue one or
 * more cycled_write_operation calls (single write inline, multi-byte via
 * buffer_task), READ performs a single cycled_read_operation and replies
 * over the originating interface, and COMMAND dispatches to the various
 * SID/config/MCU control subcommands (PAUSE, MUTE, RESET_SID, CONFIG,
 * RESET_MCU, BOOTLOADER, etc). Incoming data is dropped while the bus is
 * in reset state, except for COMMAND, CYCLED_READ and DELAY_CYCLES.
 *
 * @param volatile uint8_t * itf
 * @param volatile uint32_t * n
 */
void __no_inline_not_in_flash_func(process_buffer)(volatile uint8_t * itf, volatile uint32_t * n)
{
  bus_try_claim(BUS_OWNER_USB); /* USB always wins the claim, see bus.c */
  set_vu_action(); /* Keep that shiny Vu blinking! */
  uint8_t command = ((sid_buffer[0] & PACKET_TYPE) >> 6);
  uint8_t subcommand = (sid_buffer[0] & COMMAND_MASK);
  uint8_t n_bytes = (sid_buffer[0] & BYTE_MASK);
  if __us_unlikely(get_reset_state()
    && (command != COMMAND)
    && ((subcommand != CYCLED_READ)
      && (subcommand != DELAY_CYCLES))) { return; };  /* Drop incoming data if in reset state */

  if __us_unlikely(config_unacknowledged()) {
    goto SIDCHANGEDETECTED; /* Skip to command sequence of unacknowledged */
  }
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
    return;
  };
SIDCHANGEDETECTED:;
  if __us_likely(command == COMMAND) {
    if __us_unlikely(config_unacknowledged()
     && (subcommand != CONFIG) && (subcommand != RESET_MCU) && (subcommand != BOOTLOADER)) return;
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
            usERR("While writing to '%c'\n", rtype);
            break;
        };
        return;
      case DELAY_CYCLES:
        cycled_delay_operation((sid_buffer[1] << 8 | sid_buffer[2]));
        return;
      case PAUSE:
        usDBG("PAUSE_SID\n");
        pause_sid();
        break;
      case MUTE:
        usDBG("MUTE_SID %d\n",sid_buffer[1]);
        mute_sid();
        if (sid_buffer[1] == 1) set_muted_state(true);
        break;
      case UNMUTE:
        usDBG("UNMUTE_SID %d\n",sid_buffer[1]);
        if (sid_buffer[1] == 1) set_muted_state(false);
        unmute_sid();
        break;
      case RESET_SID:
        if (sid_buffer[1] == 0) {
          usDBG("RESET_SID\n");
          reset_sid();
        }
        if (sid_buffer[1] == 1) {
          usDBG("RESET_SID_REGISTERS\n");
          reset_sid_registers();
        }
        break;
      case DISABLE_SID:
        usDBG("DISABLE_SID\n");
        disable_sid();
        break;
      case ENABLE_SID:
        usDBG("ENABLE_SID\n");
        enable_sid(true);
        break;
      case CLEAR_BUS:
        usDBG("CLEAR_BUS\n");
        clear_bus_all();
        break;
      case CONFIG: /* Don't log message about config to avoid spam when uploading */
        /* Copy incoming buffer ignoring the command byte */
        memcpy(config_buffer, (sid_buffer + 1), (int)*n - 1);
        handle_config_request(config_buffer, *n - 1);
        memset(config_buffer, 0, count_of(config_buffer));
        break;
      case RESET_MCU:
        usDBG("RESET_MCU\n");
        mcu_reset();
        break;
      case BOOTLOADER:
        usDBG("BOOTLOADER\n");
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

/**
 * @brief TinyUSB callback fired when the device is mounted by the host
 */
void tud_mount_cb(void)
{
  /* usDBG("[%s]\n", __func__); */
  usNFO("[CDC] Mount\n");
}

/**
 * @brief TinyUSB callback fired when the device is unmounted by the host
 *
 * Clears the received-data flag and resets dtype/rtype to the "none" type.
 */
void tud_umount_cb(void)
{
  set_receivedata(false), dtype = rtype = ntype;
  /* usDBG("[%s]\n", __func__); */
  usNFO("[CDC] Unmount\n");
}

/**
 * @brief TinyUSB callback fired when the host suspends the bus
 *
 * Clears the received-data flag and resets dtype/rtype to the "none" type.
 *
 * @param bool remote_wakeup_en
 */
void tud_suspend_cb(bool remote_wakeup_en)
{
  /* (void) remote_wakeup_en; */
  /* usDBG("[%s] remote_wakeup_en:%d\n", __func__, remote_wakeup_en); */
  usNFO("[CDC] remote_wakeup_en:%d\n", remote_wakeup_en);
  set_receivedata(false), dtype = rtype = ntype;
}

/**
 * @brief TinyUSB callback fired when the host resumes the bus
 */
void tud_resume_cb(void)
{
  /* usDBG("[%s]\n", __func__); */
}


/* USB MIDI CLASS TASK & CALLBACKS */

/**
 * @brief TinyUSB callback fired when MIDI data is available on an interface
 *
 * Drains all complete 4-byte MIDI packets from the given interface and
 * hands each to process_usb_midi_packet.
 *
 * @param uint8_t itf
 */
void tud_midi_rx_cb(uint8_t itf)
{
  if (tud_midi_n_mounted(itf)) {
    uint8_t packet[4];
    while (tud_midi_n_packet_read(itf, packet)) {  /* Loop as long as there are full packets available */
      set_receivedata(true);
      process_usb_midi_packet(packet);
    }
    return;
  }
  return;
}


/* USB CDC CLASS TASKS & CALLBACKS */

#if 0
/**
 * @brief Poll the CDC interface for available data and process it
 * NOTE: Deprecated but kept as historical information
 *
 * Same as the tud_cdc_rx_cb callback routine. Used in the Core0 main
 * loop if tud_cdc_rx_cb is not defined.
 * Reads available bytes into read_buffer, copies them into sid_buffer
 * and hands off to process_buffer.
 */
void __us_deprecated cdc_task(void)
{ /* Same as the callback routine */
  if (tud_cdc_n_connected(CDC_ITF1)) {
    if (tud_cdc_n_available(CDC_ITF1) > 0) {
      cdc_itf = CDC_ITF1;
      set_receivedata(true), dtype = cdc, rtype = cdc;
      cdcread = tud_cdc_n_read(CDC_ITF1, &read_buffer, MAX_BUFFER_SIZE);  /* Read data from client */
      tud_cdc_n_read_flush(CDC_ITF1);
      memcpy(sid_buffer, read_buffer, cdcread);
      process_buffer(cdc_itf, &cdcread);
      return;
    }
    return;
  }
  return;
}
#endif

/**
 * @brief TinyUSB callback fired when CDC data is available from the host
 *
 * Read from host to device. No need to check available bytes for reading.
 * Reads up to MAX_BUFFER_SIZE bytes into read_buffer, copies them into
 * sid_buffer, and hands off to process_buffer.
 *
 * @param uint8_t itf
 */
void tud_cdc_rx_cb(uint8_t itf)
{ /* No need to check available bytes for reading */
  if __us_likely((itf == CDC1_ITF) || (itf == CDC2_ITF)) {
    if (tud_cdc_n_available(itf)) {
      cdc_itf = &itf;
      set_receivedata(true), dtype = cdc, rtype = cdc;
      cdcread = tud_cdc_n_read(*cdc_itf, &read_buffer, MAX_BUFFER_SIZE);  /* Read data from client */
      tud_cdc_n_read_flush(*cdc_itf);
      memcpy(sid_buffer, read_buffer, cdcread);
      process_buffer(cdc_itf, &cdcread);
      return;
    }
  }
  return;
}

/**
 * @brief TinyUSB callback fired when the CDC "wanted char" is received
 *
 * @note debug logging left disabled here to avoid possible uart spam
 *
 * @param uint8_t itf
 * @param char wanted_char
 */
void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char)
{
  (void)itf;
  (void)wanted_char;
  /* usDBG("[%s]\n", __func__); */  /* Disabled due to possible uart spam */
}

/**
 * @brief TinyUSB callback fired when a queued CDC transmit completes
 *
 * @note debug logging left disabled here to avoid uart spam
 *
 * @param uint8_t itf
 */
void tud_cdc_tx_complete_cb(uint8_t itf)
{
  (void)itf;
  /* usDBG("[%s]\n", __func__); */  /* Disabled due to uart spam */
}

/**
 * @brief TinyUSB callback fired when the CDC line state (DTR/RTS) changes
 *
 * Sets the received-data flag when DTR indicates a terminal connected,
 * clears it when DTR indicates disconnect.
 *
 * @param uint8_t itf
 * @param bool dtr
 * @param bool rts
 */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  /* (void) itf; */
  /* (void) rts; */
  /* usDBG("[%s] itf:%x, dtr:%d, rts:%d\n", __func__, itf, dtr, rts); */
  usNFO("[CDC] Line state itf:%x, dtr:%d, rts:%d\n", itf, dtr, rts);

  if ( dtr ) {
    /* Terminal connected */
    set_receivedata(true);
  }
  else
  {
    /* Terminal disconnected */
    set_receivedata(false);
  }
}

/**
 * @brief TinyUSB callback fired when the CDC line coding changes
 *
 * @param uint8_t itf
 * @param cdc_line_coding_t const* p_line_coding
 */
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
  /* (void)itf; */
  /* (void)p_line_coding; */
  usNFO("[CDC] Line coding itf:%x, bit_rate:%u, stop_bits:%u, parity:%u, data_bits:%u\n",
    itf, (int)p_line_coding->bit_rate, p_line_coding->stop_bits, p_line_coding->parity, p_line_coding->data_bits);
}

/**
 * @brief TinyUSB callback fired when a CDC break condition is sent
 *
 * @param uint8_t itf
 * @param uint16_t duration_ms
 */
void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
  /* (void)itf; */
  /* (void)duration_ms; */
  usNFO("[CDC] Break its:%x, duration_ms:%x\n", itf, duration_ms);
}


/* WEBUSB VENDOR CLASS TASKS & CALLBACKS */

#if 0
/**
 * @brief Poll the WebUSB vendor interface for available data and process it
 * NOTE: Deprecated but kept as historical information
 *
 * Same as the tud_vendor_rx_cb callback routine, used in main loop instead
 * of interrupt callback. If the fifo buffer is disabled this function has
 * no use. Reads available bytes into read_buffer, copies them into sid_buffer,
 * and hands off to process_buffer.
 */
void __us_deprecated vendor_task(void)
{ /* Same as the callback routine */
  /* If the fifo buffer is disabled, this function has no use */
  if (web_serial_connected) {
      wusb_itf = WUSB_ITF;
      set_receivedata(true), dtype = wusb, rtype = wusb;
      webread = tud_vendor_n_read(WUSB_ITF, &read_buffer, MAX_BUFFER_SIZE);
      tud_vendor_n_read_flush(*wusb_itf);
      memcpy(sid_buffer, read_buffer, webread);
      process_buffer(wusb_itf, &webread);
    return;
  }
  return;
}
#endif

/**
 * @brief TinyUSB callback fired when WebUSB vendor data is available
 *
 * Read from host to device. With the fifo buffer disabled the buffer
 * contains the newest incoming data; with the fifo buffer enabled the
 * buffer contains data from the previous read. The vendor class has no
 * connect check built in, so web_serial_connected is used as a makeshift
 * check.
 *
 * @param uint8_t itf
 * @param uint8_t const* buffer
 * @param uint16_t bufsize
 */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize)
{
  /* With the fifo buffer disabled the buffer contains the newest incoming data */
  /* If the fifo buffer is enabled the buffer contains data from the previous read */

  /* vendor class has no connect check, so we use a makeshift check */
  if __us_likely(itf == WUSB_ITF && web_serial_connected) {
      wusb_itf = &itf; /* Since there's only 1 vendor interface, we know it's 0 */
      set_receivedata(true), dtype = wusb, rtype = wusb;
      webread = bufsize;
      /* Flush the fifo */
      tud_vendor_n_read_flush(*wusb_itf);
      memcpy(sid_buffer, buffer, bufsize);
      process_buffer(wusb_itf, &webread);
    return;
  }
  return;
}

/**
 * @brief TinyUSB callback fired when a queued vendor transmit completes
 *
 * @param uint8_t itf
 * @param uint32_t sent_bytes
 */
void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes)
{
  (void)itf;
  usNFO("[VDR] TX %lu\n", sent_bytes);
}

/**
 * @brief Handle incoming vendor and WebUSB control transfer requests
 *
 * Only the CONTROL_STAGE_SETUP stage is handled; other stages are
 * acknowledged with no action. Handles the CDC-style SET_CONTROL_LINE_STATE
 * class request (0x22) used by WebSerial to signal connect/disconnect,
 * the WebUSB landing-page URL vendor request (returns desc_url on first
 * boot or when configuration confirmation/SID-change acknowledgement is
 * pending), and the Microsoft OS 2.0 compatible descriptor vendor request.
 * Any other request stalls the endpoint.
 *
 * @param uint8_t rhport
 * @param uint8_t stage
 * @param tusb_control_request_t const * request
 * @return bool true if the request was handled/acknowledged, false to stall
 */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  usNFO("[VDR] XFER stage:%x, rhport:%x, bRequest:0x%x, wValue:%d, wIndex:%x, wLength:%x, bmRequestType:%x, type:%x, recipient:%x, direction:%x\n",
    stage, rhport,
    request->bRequest, request->wValue, request->wIndex, request->wLength, request->bmRequestType,
    request->bmRequestType_bit.type, request->bmRequestType_bit.recipient, request->bmRequestType_bit.direction);
  /* Do nothing with IDLE (0), DATA (2) & ACK (3) stages */
  if (stage != CONTROL_STAGE_SETUP) return true;  /* Stage 1 */

  switch (request->bmRequestType_bit.type) { /* BitType */
    case TUSB_REQ_TYPE_STANDARD:  /* 0 */
      break;
    case TUSB_REQ_TYPE_CLASS:     /* 1 */
      if (request->bRequest == 0x22) { /* On connection */
        /* Webserial simulates the CDC_REQUEST_SET_CONTROL_LINE_STATE (0x22) to connect and disconnect */
        web_serial_connected = (request->wValue != 0);
        /* Flush any data still in the fifo */
        if (web_serial_connected) {
          tud_vendor_n_read_flush(WUSB_ITF);
          tud_vendor_n_write_flush(WUSB_ITF);
        }
        /* Respond with status OK */
        return tud_control_status(rhport, request);
      }
      break;
    case TUSB_REQ_TYPE_VENDOR:    /* 2 */
      switch (request->bRequest) {
        case VENDOR_REQUEST_WEBUSB: /* 1 */
          /* Match vendor request in BOS descriptor
           * Get landing page url and return it
           * if on default config first boot
           */
          if (first_boot || usbsid_config.need_confirmation || detected_sid_change) {
            return tud_control_xfer(rhport, request, (void*)(uintptr_t) &desc_url, desc_url.bLength);
            first_boot = false;
          } else {
            return tud_control_status(rhport, request);
          }
        case VENDOR_REQUEST_MICROSOFT: /* 2 */
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


/* MAIN LOOPS */

/**
 * @brief Core0 loop de loop.
 * Runs the following tasks in a while loop:
 * - TinyUSB polling task
 * - LED runner when core 1 is busy
 * - LED fast blink for v1.5+ boards
 * - WiFi/Bluetooth polling
 *
 * @note Runs on core 0, never returns.
 *
 *
 */
void __us_noreturn core0_loop(void)
{
  while (1) {
    tud_task_ext(0, false);  /* equals tud_task(); timout_ms already at 0 and is _always_ discarded in osal_none.h */
    #if 0
    cdc_task();  /* Only use this if no callbacks */
    #endif
    #if 0
    vendor_task();  /* Only use this if buffering and fifo are enabled */
    #endif

    if (offload_ledrunner) {
      led_runner();
    }

    #if defined(USE_NET) && (PCB_VERSION_INT >= 15)
    /* On pico_w and pico2_w the LED is controlled via the WiFi module and
     * the wifi SPI control expects single threaded calling. This forces the
     * firmware to blink via Core0 */
    if (detected_sid_change) led_fast_blink();
    #endif

    #if defined(USE_NET)
    /* LED tracks actual WiFi or Bluetooth connection status
     * - off while neither is connected
     * - on once WiFi is associated with an IP, or a Bluetooth SPP channel is open
     * Skipped while detected_sid_change is true so it never fights led_fast_blink()'s
     * priority indicator over the same GPIO.
     * Only touches the GPIO on an actual state change, not every loop tick
     * - it's a real SPI transaction to the CYW43439
     * - always runs on this core so there's no cross-core race */
    if (!detected_sid_change) {
      static bool wifi_led_on = false;
      bool want_on = usbsid_config.LED.enabled
        && (net_wifi_is_connected() || net_bt_is_connected());
      if (want_on != wifi_led_on) {
        wifi_led_on = want_on;
        cyw43_arch_gpio_put(BUILTIN_LED, wifi_led_on);
      }
    }
    #endif

    /* Poll the cyw43/update the station state machine, unconditionally */
    #if defined(USE_NET)
    if (!bus_heavy_op_active()) {
      cyw43_arch_poll();
      net_wifi_update();
    }
    #endif
  }

  /* Point of no return, this should never be reached */
  __builtin_unreachable();
}

/**
 * @brief Core1 loop de loop
 * @note Runs on core 1, never returns.
 * Runs the following tasks in a while loop:
 * - LED runner
 * - SID test event queue
 * - Midi engine event queue
 * - Network SID Device event queue
 * - Embedded USBSID-Player
 * - Embedded Cynthcart
 * - Write queue debug logging
 *
 */
void __us_noreturn core1_loop(void)
{
  while(1) {
    /* No continue if in reset */
    if __us_unlikely(get_reset_state()) continue;

    /* Blinky blinky? Maybe warning? */
    if (!offload_ledrunner) {
      led_runner();
    }

    #if PCB_VERSION_INT >= 15
    /* No continue if warning */
    if __us_unlikely(detected_sid_change) continue;
    #endif

    /* Drain the SID test queue when runnign tests */
    if __us_unlikely(running_tests) {
      sidtest_queue_entry_t s_entry;
      if (queue_try_remove(&sidtest_queue, &s_entry)) {
        s_entry.func(s_entry.s, s_entry.t, s_entry.wf);
      }
    }

    /* Drain the MIDI event ring.
     * Core1 handles SID bus writes for MIDI input
     * so USB callbacks on Core0 are not interrupted
     */
    #ifdef ONBOARD_EMULATOR
    /* Skip if embedded Cynthcart is running  */
    if __us_likely(!emulator_running) {
    #endif
      midi_engine_task();
    #ifdef ONBOARD_EMULATOR
    }
    #endif

    #ifdef USE_NSD
    /* Drain the NSD write ring.
     * Core1 handles SID bus writes for NSD input.
     * The WiFi/Bluetooth transports are polled from core 0.
     */
    nsd_drain_task();
    #endif

    /* The Embedded SID player is completely run by Core1 and
     * is mutually exclusive with Cynthcart.
     * There can be only 1 ;-)
     */
    #ifdef ONBOARD_EMULATOR
    if (sidplayer_init && !emulator_running) {
      sidplayer_init = false;
      sidplayer_start = false;
      sidplayer_playing = false;
      offload_ledrunner = true;
      if (is_prg) {
        usplayer_upload_finish_prg(false); /* Load PRG without auto looping */
      } else {
        usplayer_upload_finish_tune(tuneno);
      }
      sidplayer_start = true;
    }
    if (sidplayer_start  && !emulator_running) {
      sidplayer_init = false;
      sidplayer_start = false;
      sidplayer_playing = true;
      if (!is_prg) {
        init_sidplayer(); /* Initialise */
        usplayer_set_sid_config(cfg.numsids,cfg.sids_one,cfg.sids_two,cfg.fmopl_sid); /* Provide board SID config */
        start_sidplayer(false); /* No auto loop */
      }
    }
    if (sidplayer_stop  && !emulator_running) {
      stop_sidplayer();
      sidplayer_stop = false;
      sidplayer_playing = false;
      offload_ledrunner = true;
    }
    if __us_unlikely ((sidplayer_next && !sidplayer_playing) && !emulator_running) {
      next_subtune();
      sleep_us(20000);
      sidplayer_next = false;
      sidplayer_playing = true;
    }
    if __us_unlikely ((!sidplayer_playing && sidplayer_prev) && !emulator_running) {
      previous_subtune();
      sidplayer_prev = false;
      sidplayer_playing = true;
    }
    if __us_likely(sidplayer_playing && !emulator_running) {
      if (bus_try_claim(BUS_OWNER_PLAYER)) { /* USB still wins if it's active, see bus.c */
        loop_sidplayer(); /* INFO: Cycle exact tunes are too demanding to play nice on rp2040 */
        bus_release(BUS_OWNER_PLAYER);
      }
      playtime = usplayer_playtime_ms();
      if __us_unlikely(sidplayer_next || sidplayer_prev) {
        sidplayer_playing = false;
      }
      if __us_unlikely((playtime >= maxplaytime) && !emulator_running) {
        sidplayer_stop = true;
        /* Deinit all sidplayer variables */
        sidplayer_init = false;
        sidplayer_start = false;
        /* Reset max playtime back to 5 minutes in milliseconds */
        maxplaytime = 300000;
      }
    }
    /* The Cynthcart emulation uses the Embedded SID player as
     * platform to run on. It is only available if the player is
     * also compiled in as feature.
     */
    if ((!emulator_running && starting_emulator) && !sidplayer_playing) {
      starting_emulator = false;
      emulator_running = true;
      offload_ledrunner = true;
      start_cynthcart();
    }
    if ((emulator_running && !starting_emulator) && !sidplayer_playing) {
      if (bus_try_claim(BUS_OWNER_PLAYER)) { /* USB still wins if it's active, see bus.c */
        run_cynthcart();
        bus_release(BUS_OWNER_PLAYER);
      }
    }
    #endif /* ONBOARD_EMULATOR */

    #ifdef WRITE_DEBUG  /* Only run this queue when needed */
    if (is_receivedata()) {
      writelogging_queue_entry_t l_entry;
      if (queue_try_remove(&logging_queue, &l_entry)) {
        usDBG("[CORE2 %5u] [WRITE %c:%02d/%02d] $%02X:%02X %u\n",
          queue_get_level(&logging_queue),
          l_entry.dtype, l_entry.n, l_entry.s,
          l_entry.reg, l_entry.val, l_entry.cycles);
      }
    }
    #endif
  }

  /* Point of no return, this should never be reached */
  __builtin_unreachable();
}


/** MAIN INIT
 *
 * Multicore sync using atomic memory (avoids semaphore spin locks and the
 * hardware FIFO, which is consumed by the flash_safe_execute IRQ handler).
 * Boot sequence:
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
 *
 * After sync, the main loop runs the LED runner, drains the SID test queue,
 * drains the MIDI event ring (midi_engine_task), and drives the onboard
 * SID player / Cynthcart emulator state machines when those features are
 * compiled in.
 *
 */

/**
 * @brief Core 1 entry point: boot sync with core0, then the core1 main loop
 *
 * - Enables core to be locked when saving to flash
 * - Initialises SID test and Write debug queues
 * - Initialises PIO Uart
 * -
 *
 * @note runs on core 1, hands control over to `core1_loop` after init.
 *
 */
void core1_main(void)
{
  /* Set core locking for flash saving ~ note this makes SIO_IRQ_PROC1 unavailable */
  flash_safe_execute_core_init();  /* This needs to start before any flash actions take place! */

  /* Signal Core 0 we're ready (sync point 1) */
  usBOOT("<CORE 1> Signaling core0 ready ~ 1\n");
  core_sync_state = SYNC_CORE1_STAGE1;
  __dsb();  /* Data Synchronisation Barrier - ensures store completes before SEV */
  __sev();  /* Signal event to wake Core 0 from WFE */

  /* Wait for Core 0 to finish config loading */
  usBOOT("<CORE 1> Waiting for core0 sync ~ 1\n");
  while (true) {
    __wfe();  /* Wait for event - low power wait */
    __dmb();  /* Data Memory Barrier */
    if (core_sync_state == SYNC_CORE0_STAGE1) break;
  }
  __dmb();  /* Data Memory Barrier after read */

  /* Init SID test queue, a single entry deep */
  queue_init(&sidtest_queue, sizeof(sidtest_queue_entry_t), 1);
  #ifdef WRITE_DEBUG  /* Only init this queue when needed */
  /* Init Write logging queue, 16384 entries deep so we don't skip any writes */
  queue_init(&logging_queue, sizeof(writelogging_queue_entry_t), 16384);
  #endif

  /* Initialise PIO Uart */
  #ifdef USE_PIO_UART
  init_uart();
  #endif

  /* Signal Core 0 we're ready (sync point 2) */
  usBOOT("<CORE 1> Signaling core0 ready ~ 2\n");
  core_sync_state = SYNC_CORE1_STAGE2;
  __dsb(); /* Data Synchronisation Barrier - ensures store completes before SEV */
  __sev(); /* Signal event to wake Core 0 from WFE */

  /* Wait for Core 0 to finish hardware init */
  usBOOT("<CORE 1> Waiting for core0 sync ~ 2\n");
  while (true) {
    __wfe();  /* Wait for event - low power wait */
    __dmb();  /* Data Memory Barrier */
    if (core_sync_state == SYNC_CORE0_STAGE2) break;
  }
  __dmb();  /* Data Memory Barrier after read */

  core1_loop();

  /* Point of no return, this should never be reached */
  return;
}

/**
 * @brief Firmware entry point: boot core0, launch core1, then IO task loop
 *
 * - Sets the system clock speed
 * - Initialises TinyUSB (kept disconnected from the host until hardware is ready)
 * - Launches core1 and runs the core0 side of the two-stage multicore boot sync (see core1_main)
 * - Loads and appliesthe persisted config(s)
 * - Sets up the SID clock, bus, PIO, DMA, VU, MIDI, ASID and SID state detection
 * - Runs default-config/socket-config verification
 * - Finally allows the host to enumerate via tud_connect()
 *
 * @note runs on core 0, hands control over to `core0_loop` after init.
 *
 * @return int never actually returns; present for the standard C signature
 */
int main()
{
  /* Set system clockspeed */
  #if (defined(ONBOARD_EMULATOR) && ONBOARD_EMULATOR) \
    || (defined(USE_NET) && USE_NET)
    /* System clock overclocked @ 250MHz */
    set_sys_clock_khz(250000, true); /* Boo fucking hoo, still too slow for rp2040!! */
  #else /* Set default speeds if non of the above */
    #if PICO_RP2040
    /* System clock @ MAX SPEED!! ARRRR 200MHz */
    set_sys_clock_khz(200000, true);
    #elif PICO_RP2350
    /* System clock @ 150MHz */
    // set_sys_clock_pll(1500000000, 5, 2);
    set_sys_clock_khz(150000, true);
    #endif
  #endif

  /* Init TinyUSB */
  tusb_rhport_init_t dev_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  tud_disconnect();  /* Keep USB invisible to host during boot - set_base_voltages can take some time */
  /* Init logging */
  init_logging();
  /* Log reset reason */
  reset_reason();

  /* Launch Core 1 and wait for flash_safe_execute_core_init to complete */
  usBOOT("CORE0 Launching core1\n");
  multicore_launch_core1(core1_main);

  /* Wait for Core 1 to signal ready (sync point 1) */
  usBOOT("CORE0 Waiting for core1 ready ~ 1\n");
  while (true) {
    __wfe();  /* Wait for event - low power wait */
    __dmb();  /* Data Memory Barrier */
    if (core_sync_state == SYNC_CORE1_STAGE1) break;
  }
  __dmb();  /* Data Memory Barrier before write */

  /* Load config before init of USBSID settings ~ NOTE: This cannot be run from Core 1! */
  load_config(&usbsid_config);

  /* must run before anything ever touches the MIDI flash partition */
  verify_midiconfig_offset();

  /* Apply saved config to used vars */
  err = apply_config(true); /* At boot */
  if (err != CFG_OK) {
    usERR("%s\n", config_error_str(err));
  };

  /* Log boot CPU and C64 clock speeds */
  cpu_mhz = (clock_get_hz(clk_sys) / 1000 / 1000);
  cpu_us = (1 / cpu_mhz);
  sid_hz = usbsid_config.clock_rate;
  sid_mhz = (sid_hz / 1000 / 1000);
  sid_us = (1 / sid_mhz);
  usNFO("\n");
  usNFO("[NFO] Clock information:\n");
  usNFO("[NFO]   Pico Clock @ %lu Hz, %.0f MHz, %.4f uS\n",
    clock_get_hz(clk_sys), cpu_mhz, cpu_us);
  usNFO("[NFO]   C64 SID Clock @ %.0f Hz, %.6f MHz, %.4f uS\n",
    sid_hz, sid_mhz, sid_us);
  usNFO("[NFO]   C64 Refresh Rate = %lu Cycles\n",
    usbsid_config.refresh_rate);
  usNFO("[NFO]   C64 Raster Rate = %lu Cycles\n",
    usbsid_config.raster_rate);

  /* Signal Core 1 to continue (sync point 1) */
  usBOOT("<CORE 0> Signaling core1 ~ 1\n");
  core_sync_state = SYNC_CORE0_STAGE1;
  __dsb();  /* Data Synchronisation Barrier - ensures store completes before SEV */
  __sev();  /* Signal event to wake Core 1 from WFE */

  /* Wait for Core 1 to finish queue/uart init (sync point 2) */
  usBOOT("<CORE 0> Waiting for core1 ready ~ 2\n");
  while (true) {
    __wfe();  /* Wait for event - low power wait */
    __dmb();  /* Data Memory Barrier */
    if (core_sync_state == SYNC_CORE1_STAGE2) break;
  }
  __dmb();  /* Data Memory Barrier after read */

  /* Start verification, detect and init sequence of SID clock */
  usBOOT("Setup SID clock\n");
  setup_sidclock();

  /* Init C64 bus */
  usBOOT("Initializing C64 bus\n");
  init_bus_control();

  /* Init voltage control */
  #if PCB_VERSION_INT >= 15
  usBOOT("Initializing voltage control\n");
  init_vccvdd_control();
  #endif

  /* Init audio switch */
  #if PCB_VERSION_INT >= 13
  usBOOT("Initializing audio switch\n");
  init_audio_switch();
  #endif

  /* Init PIO */
  usBOOT("Setup PIO bus\n");
  setup_piobus();

  /* Sync PIOS */
  usBOOT("Synchronise PIO's\n");
  sync_pios(true);

  /* Init DMA */
  usBOOT("Setup DMA channels\n");
  setup_dmachannels();

  /* Claim the bus spinlock before core1 is released to its main loop, so it
   * exists before anything could contend on it */
  usBOOT("Setup bus lock\n");
  bus_lock_init();

  /* Start the VU */
  usBOOT("Initialise Vu\n");
  init_vu();

  /* Init midi */
  usBOOT("Initialise Midi\n");
  midi_init();

  /* Init ASID */
  usBOOT("Initialise ASID\n");
  asid_init();

  #ifdef USE_NET
  /* Initialise Bluetooth */
  setup_bluetooth();
  /* Init WiFi network interface */
  net_wifi_init();
  /* Loads net_cfg, applies WiFi and Bluetooth power state live from it.
   * Run exactly once, unconditionally, on core 0 at boot. */
  start_net();
  #endif /* USE_NET */

  /* Init SID states */
  usBOOT("Init SID states\n");
  init_sid_states(); /* INFO: Detecting SID types require 9v to be enabled for all MOS SID types */

  /* Check for default config bit */
  #if PCB_VERSION_INT >= 15
  /* Only run autodetect sequence if not already waiting for confirmation */
  if (!usbsid_config.need_confirmation) {
    detect_default_config(); /* Saves config, always */
  }
  #else
  detect_default_config(); /* Saves config, always */
  #endif

  /* No need to reset SID registers or resetting the SID's
   * at this point during boot on (released) pre v1.5 boards
   */
  #if PCB_VERSION_INT >= 15
  verify_socket_config();
  #endif


  /* cfg.numsids is authoritatively set by detect_default_config() /
   * verify_socket_config() during boot.
   * midi_config_init() during midi_init() in the boot sequence ran
   * before that and does not know MAX_SIDS yet. The host has not
   * been allowed to enumerate yet, tud_connect() runs just before the
   * main loop, so there is no MIDI traffic this could race against.
   */
  midi_config_sync_poly_limits();

  /* Print config once at end of boot routine.
   * detected_sid_change is always false on pre v1.5 boards
   */
  if (!detected_sid_change) print_config();

  { /* Separate code block */
    usNFO("\n");
    usDBG("Firmware for %s compiled with:\n",
      (is_rp2350 ? "rp2350" : "rp2040"));
    #ifdef PICO_DEFAULT_LED_PIN
    usDBG("  - LED Vu meter\n");
    if (has_rgb_vu)    usDBG("  - RGB LED Vu meter\n");
    #else
    usDBG("  - LED Status indicator\n");
    #endif
    if (has_pio_uart)  usDBG("  - PIO Uart\n");
    if (has_net)       usDBG("  - WiFi & Bluetooth\n");
    if (has_nsd)       usDBG("    - with Network SID Device\n");
    if (has_emulator)  usDBG("  - Embedded USBSID-Player\n");
    if (has_emulator)  usDBG("    - with Cynthcart\n");
  }

  { /* Separate code block */
    usNFO("\n");
    if (!detected_sid_change) {
      usDBG("%s v%s Started successfully\n\n", us_product, project_version);
    } else {
      usDBG("%s v%s\n", us_product, project_version);
      usWRN("Please verify socket configuration before further use!\n\n");
    }
  }

  /* Signal Core 1 to enter main loop (sync point 2) */
  usBOOT("<CORE 0> Signaling core1 ~ 2\n");
  core_sync_state = SYNC_CORE0_STAGE2;
  __dsb();  /* Data Synchronisation Barrier - ensures store completes before SEV */
  __sev();  /* Signal event to wake Core 1 from WFE */

  /* All hardware ready - allow host to enumerate */
  if (!tud_connect()) usERR("!! USB CONNECTION ERROR !!");

  /* Loop IO tasks forever */
  core0_loop();

  /* Point of no return, this should never be reached */
  return 0;
}
