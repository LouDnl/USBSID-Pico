/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * stream.c
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

#ifdef USE_SID_STREAM

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/sync.h"

#include <globals.h> /* Includes macros, sid_defs & usbsid_defs */
#include <usbsid.h>
#include <config.h>
#include <bus.h>
#include <sid.h>
#include <stream.h>

#include "device/usbd_pvt.h"  /* Application class driver API */


/* Frames buffered for the IN endpoint, 4 ticks of 4 SIDs */
#define STREAM_RING_FRAMES 16
#define STREAM_EP_SIZE     64

/* Main interface packets processed, incremented by process_buffer() */
volatile uint32_t stream_rx_count = 0;

/* Stream state, core0 only. Ticks run from stream_task() (core0 loop),
 * control requests from tud_task() */
static bool     stream_running = false;
static uint16_t stream_rate = 0;
static uint32_t stream_period_us = 0;
static uint32_t stream_next_tick = 0;
static uint8_t  stream_sid_mask = 0;     /* 0 = all configured SIDs */
static uint16_t stream_seq = 0;          /* Current tick, advances on every elapsed tick */
static uint8_t  stream_session = 0;
static uint32_t stream_dropped = 0;

/* Frame ring: head written by stream_tick(), tail by the USB IRQ on
 * transfer complete. The frame at tail is in flight while tx_busy. */
static uint8_t  stream_rhport = 0;
static volatile bool     stream_opened = false;
static volatile bool     stream_tx_busy = false;
static volatile uint32_t stream_head = 0;
static volatile uint32_t stream_tail = 0;
static uint8_t stream_ring[STREAM_RING_FRAMES][STREAM_EP_SIZE] __aligned(4);
static uint8_t stream_out_buf[STREAM_EP_SIZE] __aligned(4);

static uint8_t stream_ctrl[STREAM_START_LEN];
static uint8_t stream_info_buf[STREAM_INFO_LEN];


/**
 * @brief Store a 16 bit value little endian
 *
 * @param uint8_t * p destination
 * @param uint16_t v value
 */
static inline void put_le16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

/**
 * @brief Store a 32 bit value little endian
 *
 * @param uint8_t * p destination
 * @param uint32_t v value
 */
static inline void put_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)(v >> 24);
}

/**
 * @brief Check a stream rate against the supported set
 *
 * @param uint16_t rate rate in Hz
 * @return bool true for 50, 60, 250, 500 or 1000
 */
static bool stream_rate_valid(uint16_t rate)
{
  switch (rate) {
    case 50:
    case 60:
    case 250:
    case 500:
    case 1000:
      return true;
    default:
      return false;
  }
}

/**
 * @brief Resolve the SID mask against the configured SID count
 *
 * @return uint8_t bit n set for each SID n+1 to stream
 */
static uint8_t __no_inline_not_in_flash_func(stream_active_mask)(void)
{
  uint8_t n = cfg.numsids;
  if (n == 0) n = 1;
  if (n > 4) n = 4;
  uint8_t configured = (uint8_t)((1u << n) - 1);
  return (uint8_t)((stream_sid_mask ? stream_sid_mask : 0x0F) & configured);
}

/**
 * @brief Start the IN transfer of the frame at tail if the endpoint is idle
 *
 * @note Call with interrupts disabled or from the USB IRQ
 */
static void __no_inline_not_in_flash_func(stream_kick)(void)
{
  if (stream_tx_busy || !stream_opened) return;
  if (stream_head == stream_tail) return;
  stream_tx_busy = true;
  if (!usbd_edpt_xfer(stream_rhport, STREAM_EP_IN,
        stream_ring[stream_tail % STREAM_RING_FRAMES], STREAM_FRAME_SIZE)) {
    stream_tx_busy = false;
  }
}

/**
 * @brief Drop all queued frames, keep the one in flight
 */
static void stream_ring_clear(void)
{
  uint32_t irq = save_and_disable_interrupts();
  stream_head = stream_tail + (stream_tx_busy ? 1u : 0u);
  restore_interrupts(irq);
}

/**
 * @brief Start or restart the stream
 *
 * Resets sequence, drop counter and queued frames, increments the session.
 *
 * @param uint16_t rate rate in Hz, 0 for STREAM_DEFAULT_RATE
 * @param uint8_t sid_mask bit n = SID n+1, 0 for all
 * @return bool false for an unsupported rate
 */
static bool stream_start(uint16_t rate, uint8_t sid_mask)
{
  if (rate == 0) rate = STREAM_DEFAULT_RATE;
  if (!stream_rate_valid(rate)) return false;
  stream_running = false;
  stream_ring_clear();
  stream_rate = rate;
  stream_period_us = (1000000u / rate);
  stream_sid_mask = (sid_mask & 0x0F);
  stream_seq = 0xFFFF;  /* First tick is 0 */
  stream_dropped = 0;
  stream_session++;
  stream_next_tick = time_us_32();
  stream_running = true;
  return true;
}

/**
 * @brief Stop the stream, frames already queued still go out
 */
void stream_stop(void)
{
  stream_running = false;
}

/**
 * @brief Fill stream_info_buf with the STREAM_REQUEST_INFO reply
 *
 * [0] format version, [1] frame size, [2-3] max rate LE, [4] SID count,
 * [5] bit 0 running, [6-7] rate LE, [8] active SID mask, [9] reserved,
 * [10] session, [11] reserved, [12-15] frames dropped LE
 */
static void stream_info(void)
{
  memset(stream_info_buf, 0, sizeof(stream_info_buf));
  stream_info_buf[0] = STREAM_FORMAT_VERSION;
  stream_info_buf[1] = STREAM_FRAME_SIZE;
  put_le16(&stream_info_buf[2], STREAM_MAX_RATE);
  stream_info_buf[4] = cfg.numsids;
  stream_info_buf[5] = (stream_running ? 0x01 : 0x00);
  put_le16(&stream_info_buf[6], stream_rate);
  stream_info_buf[8] = stream_active_mask();
  stream_info_buf[10] = stream_session;
  put_le32(&stream_info_buf[12], stream_dropped);
}

/**
 * @brief Build one SID frame into a ring slot
 *
 * Copies sid_memory[] without the bus spinlock: a write holds that lock for
 * its whole cycle delay. Single byte loads are atomic, a frame can mix
 * registers from before and after a concurrent write.
 *
 * @param uint8_t * frame ring slot, STREAM_EP_SIZE bytes
 * @param uint8_t sid SID index 0-3
 * @param uint8_t flags STREAM_FLAG_* bits for frame[1]
 * @param uint32_t stamp PHI1 cycle counter of the snapshot
 */
static void __no_inline_not_in_flash_func(stream_build_frame)(uint8_t *frame, uint8_t sid, uint8_t flags, uint32_t stamp)
{
  memset(frame, 0, STREAM_EP_SIZE);
  frame[0] = STREAM_FRAME_SID;
  frame[1] = (uint8_t)((sid & 0x03) | flags);
  put_le16(&frame[2], stream_seq);
  put_le32(&frame[4], stamp);
  put_le32(&frame[8], stream_rx_count);
  memcpy(&frame[12], &sid_memory[(sid & 0x03) * 0x20], 0x20);
  frame[48] = stream_session;
}

/**
 * @brief Build and queue the due tick
 *
 * Advances the sequence by every elapsed tick. Ticks missed entirely count
 * as dropped. Queues one frame per active SID in SID order when the ring
 * has room for all of them, else drops the tick, then starts the IN
 * transfer if the endpoint is idle.
 *
 * @param uint32_t now time_us_32() at the call, at or past the due time
 */
static void __no_inline_not_in_flash_func(stream_tick)(uint32_t now)
{
  uint8_t mask = stream_active_mask();
  uint32_t nsids = (uint32_t)__builtin_popcount(mask);
  uint32_t missed = ((now - stream_next_tick) / stream_period_us);
  stream_seq = (uint16_t)(stream_seq + missed + 1);
  stream_dropped += (missed * nsids);
  stream_next_tick += ((missed + 1) * stream_period_us);

  uint32_t used = (stream_head - stream_tail);
  if (stream_opened && ((STREAM_RING_FRAMES - used) >= nsids)) {
    uint8_t flags = 0;
    bus_owner_t owner = bus_owner_peek();
    if ((owner != BUS_OWNER_NONE) && (owner != BUS_OWNER_USB)) flags |= STREAM_FLAG_OTHER_OWNER;
    uint32_t head = stream_head;
    for (uint8_t sid = 0; sid < 4; sid++) {
      if (!(mask & (1u << sid))) continue;
      stream_build_frame(stream_ring[head % STREAM_RING_FRAMES], sid, flags, clockcycles());
      head++;
    }
    __compiler_memory_barrier();  /* Frames complete before the IRQ can see them */
    uint32_t irq = save_and_disable_interrupts();
    stream_head = head;
    stream_kick();
    restore_interrupts(irq);
  } else {
    stream_dropped += nsids;
  }
}

/**
 * @brief Run a due stream tick from the core0 loop
 */
void stream_task(void)
{
  if __us_likely(!stream_running) return;
  uint32_t now = time_us_32();
  if __us_likely((int32_t)(now - stream_next_tick) < 0) return;
  stream_tick(now);
}

/**
 * @brief Check whether a control request belongs to the stream
 *
 * @param tusb_control_request_t const * request
 * @return bool true for a vendor STREAM_REQUEST_*
 */
bool stream_is_request(tusb_control_request_t const * request)
{
  return (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR)
    && (request->bRequest >= STREAM_REQUEST_START)
    && (request->bRequest <= STREAM_REQUEST_INFO);
}

/**
 * @brief Handle a stream vendor control request, all stages
 *
 * @param uint8_t rhport
 * @param uint8_t stage CONTROL_STAGE_*
 * @param tusb_control_request_t const * request
 * @return bool false to stall
 */
bool stream_control_xfer(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  switch (request->bRequest) {
    case STREAM_REQUEST_START:
      if (stage == CONTROL_STAGE_SETUP) {
        if (request->bmRequestType_bit.direction != TUSB_DIR_OUT) return false;
        if (request->wLength == 0) {
          if (!stream_start(0, 0)) return false;
          return tud_control_status(rhport, request);
        }
        if (request->wLength != STREAM_START_LEN) return false;
        return tud_control_xfer(rhport, request, stream_ctrl, STREAM_START_LEN);
      }
      if (stage == CONTROL_STAGE_DATA) {
        return stream_start((uint16_t)(stream_ctrl[0] | (stream_ctrl[1] << 8)),
          stream_ctrl[2]);
      }
      return true;
    case STREAM_REQUEST_STOP:
      if (stage == CONTROL_STAGE_SETUP) {
        stream_stop();
        return tud_control_status(rhport, request);
      }
      return true;
    case STREAM_REQUEST_INFO:
      if (stage == CONTROL_STAGE_SETUP) {
        if (request->bmRequestType_bit.direction != TUSB_DIR_IN) return false;
        stream_info();
        uint16_t len = (request->wLength < STREAM_INFO_LEN) ? request->wLength : STREAM_INFO_LEN;
        return tud_control_xfer(rhport, request, stream_info_buf, len);
      }
      return true;
    default:
      return false;
  }
}


/* SID STREAM CLASS DRIVER */

/**
 * @brief Clear driver state, endpoints closed
 */
static void stream_driver_init(void)
{
  stream_stop();
  stream_opened = false;
  stream_tx_busy = false;
  stream_head = stream_tail = 0;
}

/**
 * @brief Deinitialize the driver
 *
 * @return bool always true
 */
static bool stream_driver_deinit(void)
{
  stream_driver_init();
  return true;
}

/**
 * @brief Bus reset: endpoints are gone, stop the stream
 *
 * @param uint8_t rhport
 */
static void stream_driver_reset(uint8_t rhport)
{
  (void)rhport;
  stream_driver_init();
}

/**
 * @brief Claim the SID stream interface and open its bulk endpoints
 *
 * @param uint8_t rhport
 * @param tusb_desc_interface_t const * desc_itf
 * @param uint16_t max_len bytes left in the configuration descriptor
 * @return uint16_t descriptor bytes consumed, 0 for any other interface
 */
static uint16_t stream_driver_open(uint8_t rhport, tusb_desc_interface_t const * desc_itf, uint16_t max_len)
{
  TU_VERIFY(desc_itf->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC, 0);
  TU_VERIFY(desc_itf->bInterfaceNumber == STREAM_ITF_NUM, 0);
  uint16_t const len = (uint16_t)(sizeof(tusb_desc_interface_t)
    + (desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t)));
  TU_VERIFY(max_len >= len, 0);

  uint8_t ep_out = 0, ep_in = 0;
  TU_ASSERT(usbd_open_edpt_pair(rhport, tu_desc_next(desc_itf), desc_itf->bNumEndpoints,
    TUSB_XFER_BULK, &ep_out, &ep_in), 0);
  TU_ASSERT((ep_out == STREAM_EP_OUT) && (ep_in == STREAM_EP_IN), 0);

  stream_rhport = rhport;
  stream_tx_busy = false;
  stream_head = stream_tail = 0;
  stream_opened = true;
  usbd_edpt_xfer(rhport, STREAM_EP_OUT, stream_out_buf, STREAM_EP_SIZE);  /* OUT data is discarded */
  return len;
}

/**
 * @brief Reset both endpoints to DATA0 on SET_INTERFACE
 *
 * Stall and clear stall drops the armed buffers and resets the data toggle.
 * Queued frames are dropped, the stream stops, OUT is re-armed.
 *
 * @param uint8_t rhport
 */
static void stream_endpoints_reset(uint8_t rhport)
{
  uint32_t irq = save_and_disable_interrupts();
  stream_stop();
  usbd_edpt_stall(rhport, STREAM_EP_OUT);
  usbd_edpt_clear_stall(rhport, STREAM_EP_OUT);
  usbd_edpt_stall(rhport, STREAM_EP_IN);
  usbd_edpt_clear_stall(rhport, STREAM_EP_IN);
  stream_tx_busy = false;
  stream_tail = stream_head;
  restore_interrupts(irq);
  usbd_edpt_xfer(rhport, STREAM_EP_OUT, stream_out_buf, STREAM_EP_SIZE);
}

/**
 * @brief Standard and class requests addressed to the SID stream interface
 *
 * @note Vendor requests (STREAM_REQUEST_*) arrive through
 *       tud_vendor_control_xfer_cb(), see stream_control_xfer()
 *
 * @param uint8_t rhport
 * @param uint8_t stage CONTROL_STAGE_*
 * @param tusb_control_request_t const * request
 * @return bool false to stall or to let usbd answer GET/SET_INTERFACE
 */
static bool stream_driver_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  if (stage != CONTROL_STAGE_SETUP) return true;
  if ((request->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD)
    && (request->bRequest == TUSB_REQ_SET_INTERFACE)) {
    stream_endpoints_reset(rhport);
    return tud_control_status(rhport, request);
  }
  return false;
}

/**
 * @brief Transfer complete in the USB IRQ: send the following frame directly
 *
 * @param uint8_t rhport
 * @param uint8_t ep_addr
 * @param xfer_result_t result
 * @param uint32_t xferred_bytes
 * @return bool true when handled, false defers to stream_driver_xfer_cb()
 */
static bool __no_inline_not_in_flash_func(stream_driver_xfer_isr)(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
  (void)rhport;
  (void)result;
  (void)xferred_bytes;
  if (ep_addr != STREAM_EP_IN) return false;
  stream_tail++;  /* Sent or failed, the slot is free either way */
  stream_tx_busy = false;
  stream_kick();
  return true;
}

/**
 * @brief Transfer complete in task context: discard OUT data, re-arm
 *
 * @param uint8_t rhport
 * @param uint8_t ep_addr
 * @param xfer_result_t result
 * @param uint32_t xferred_bytes
 * @return bool always true
 */
static bool stream_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
  (void)result;
  (void)xferred_bytes;
  if (ep_addr == STREAM_EP_OUT) {
    usbd_edpt_xfer(rhport, STREAM_EP_OUT, stream_out_buf, STREAM_EP_SIZE);
  }
  return true;
}

static usbd_class_driver_t const stream_driver = {
  .name            = "SIDSTREAM",
  .init            = stream_driver_init,
  .deinit          = stream_driver_deinit,
  .reset           = stream_driver_reset,
  .open            = stream_driver_open,
  .control_xfer_cb = stream_driver_control_xfer_cb,
  .xfer_cb         = stream_driver_xfer_cb,
  .xfer_isr        = stream_driver_xfer_isr,
  .sof             = NULL,
};

/**
 * @brief Register the SID stream class driver with TinyUSB
 *
 * @param uint8_t * driver_count set to the number of drivers returned
 * @return usbd_class_driver_t const * driver array
 */
usbd_class_driver_t const * usbd_app_driver_get_cb(uint8_t * driver_count)
{
  *driver_count = 1;
  return &stream_driver;
}

#endif /* USE_SID_STREAM */
