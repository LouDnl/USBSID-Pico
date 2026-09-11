/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * bluetooth.c
 * Bluetooth Classic SPP transport for the Network SID Device (NSD) protocol
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

#include <globals.h>
#include <nsd.h>
#include <logging.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "btstack.h"

#define RFCOMM_SERVER_CHANNEL 1
#define BT_LOCAL_NAME         "USBSID-Pico"
/* NSD responses are all a handful of bytes (the longest is GET_CONFIG_INFO's
 * "USBSID-Pico (8580)\0", ~22 bytes total) - this is generous headroom, not
 * sized to any protocol maximum. */
#define BT_SEND_BUF_SIZE 132

static uint16_t rfcomm_channel_id = 0;
static uint8_t spp_service_buffer[150];
static btstack_packet_callback_registration_t hci_event_callback_registration;
#ifdef BT_DIAG_DEBUG
static btstack_packet_callback_registration_t hci_diag_callback_registration;
#endif

/* Deferred send: BTstack requires waiting for RFCOMM_EVENT_CAN_SEND_NOW
 * before calling rfcomm_send(). NSD is strict request/response, so a
 * single pending slot is always enough. */
static uint8_t bt_send_buf[BT_SEND_BUF_SIZE];
static uint16_t bt_send_len = 0;
static volatile bool bt_send_pending = false;

/* Generated fresh every boot instead of a fixed published PIN - see
 * setup_bluetooth() below. Only used if a legacy (pre-2.1, non-SSP) client
 * forces a PIN-code pairing; a modern SSP client never triggers this path. */
static uint32_t bt_legacy_pin = 0;

static void bt_transport_send(void *ctx, const uint8_t *data, uint16_t len)
{
  (void)ctx;
  if (!rfcomm_channel_id || len == 0) return;
  if (bt_send_pending) {
    /* Should not happen given NSD's strict request/response shape - if it
     * ever does, dropping a response is safer than corrupting bt_send_buf
     * out from under an in-flight rfcomm_send(). */
    usBTH("previous response still pending, dropping NSD response\n");
    return;
  }
  if (len > sizeof(bt_send_buf)) len = sizeof(bt_send_buf); /* defensive, should never trigger */
  memcpy(bt_send_buf, data, len);
  bt_send_len = len;
  bt_send_pending = true;
  rfcomm_request_can_send_now_event(rfcomm_channel_id);
}

static void bt_transport_close(void *ctx)
{
  (void)ctx;
  if (rfcomm_channel_id) {
    rfcomm_disconnect(rfcomm_channel_id);
  }
}

static nsd_transport_t bt_transport = {
  .send = bt_transport_send,
  .close = bt_transport_close,
  .ctx = NULL,
};

/* BTstack packet handler: connection lifecycle plus the NSD data path */
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  bd_addr_t event_addr;
  uint8_t status;

  switch (packet_type) {
    case HCI_EVENT_PACKET:
      switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_COMMAND_COMPLETE:
          /* Confirms whether the controller accepted HCI_Write_Scan_Enable
           * (page+inquiry scan); status 0x00 = success. */
          if (hci_event_command_complete_get_command_opcode(packet) == hci_write_scan_enable.opcode) {
            uint8_t scan_status = hci_event_command_complete_get_return_parameters(packet)[0];
            usBTH("HCI_Write_Scan_Enable status 0x%02x\n", scan_status);
          }
          break;

        case HCI_EVENT_PIN_CODE_REQUEST: {
          /* Legacy (non-SSP) pairing fallback only - see bt_legacy_pin's
           * comment. Modern clients pair via Just Works (see
           * gap_ssp_set_io_capability() in setup_bluetooth()) and never
           * reach this. */
          hci_event_pin_code_request_get_bd_addr(packet, event_addr);
          char pin[5];
          snprintf(pin, sizeof(pin), "%04lu", (unsigned long)(bt_legacy_pin % 10000));
          usBTH("legacy PIN-code pairing requested, PIN is %s\n", pin);
          gap_pin_code_response(event_addr, (uint8_t *)pin);
          break;
        }

        case RFCOMM_EVENT_INCOMING_CONNECTION: {
          uint16_t incoming_cid = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);
          if (nsd_session_is_active()) {
            /* Single NSD session at a time - a WiFi TCP client (or another
             * SPP one) already has it, refuse before accepting the channel. */
            usBTH("NSD session already active, declining SPP connection\n");
            rfcomm_decline_connection(incoming_cid);
            break;
          }
          rfcomm_accept_connection(incoming_cid);
          break;
        }

        case RFCOMM_EVENT_CHANNEL_OPENED:
          status = rfcomm_event_channel_opened_get_status(packet);
          if (status != ERROR_CODE_SUCCESS) {
            usBTH("SPP channel open failed, status 0x%02x\n", status);
            break;
          }
          rfcomm_channel_id = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
          if (!nsd_session_open(&bt_transport)) {
            /* Lost a race against another transport between the incoming-
             * connection check above and the channel actually opening. */
            usBTH("NSD session claimed elsewhere, closing SPP channel\n");
            rfcomm_disconnect(rfcomm_channel_id);
            rfcomm_channel_id = 0;
            break;
          }
          usBTH("SPP NSD session open (cid 0x%04x)\n", rfcomm_channel_id);
          break;

        case RFCOMM_EVENT_CHANNEL_CLOSED:
          usBTH("SPP channel closed\n");
          nsd_session_close();
          rfcomm_channel_id = 0;
          bt_send_pending = false;
          break;

        case RFCOMM_EVENT_CAN_SEND_NOW:
          if (bt_send_pending) {
            rfcomm_send(rfcomm_channel_id, bt_send_buf, bt_send_len);
            bt_send_pending = false;
          }
          break;

        default:
          break;
      }
      break;

    case RFCOMM_DATA_PACKET:
      if (channel == rfcomm_channel_id) {
        nsd_feed(packet, size);
      }
      break;

    default:
      break;
  }
}

#ifdef BT_DIAG_DEBUG
/* Standalone diagnostic handler: logs every classic connection/pairing
 * lifecycle event packet_handler() above silently drops. Floods the log
 * in normal operation, so opt-in only. */
static void bt_diag_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET) return;

  bd_addr_t addr;
  switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_CONNECTION_REQUEST:
      hci_event_connection_request_get_bd_addr(packet, addr);
      usBTH("connection request from %s\n", bd_addr_to_str(addr));
      break;
    case HCI_EVENT_CONNECTION_COMPLETE:
      usBTH("connection complete, status 0x%02x, handle 0x%04x\n",
        hci_event_connection_complete_get_status(packet),
        hci_event_connection_complete_get_connection_handle(packet));
      break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
      usBTH("disconnection complete, reason 0x%02x\n",
        hci_event_disconnection_complete_get_reason(packet));
      break;
    case HCI_EVENT_AUTHENTICATION_COMPLETE:
      usBTH("authentication complete, status 0x%02x\n",
        hci_event_authentication_complete_get_status(packet));
      break;
    case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
      usBTH("SSP pairing complete, status 0x%02x\n",
        hci_event_simple_pairing_complete_get_status(packet));
      break;
    case HCI_EVENT_USER_CONFIRMATION_REQUEST:
      usBTH("SSP user confirmation requested (Just Works auto-accepts)\n");
      break;
    case HCI_EVENT_ENCRYPTION_CHANGE:
      usBTH("encryption change, status 0x%02x, enabled %u\n",
        hci_event_encryption_change_get_status(packet),
        hci_event_encryption_change_get_encryption_enabled(packet));
      break;
    case HCI_EVENT_LINK_KEY_REQUEST:
      usBTH("link key requested\n");
      break;
    case HCI_EVENT_LINK_KEY_NOTIFICATION:
      usBTH("link key notification\n");
      break;
    default:
      usBTH("unhandled HCI event 0x%02x\n", hci_event_packet_get_type(packet));
      break;
  }
}
#endif /* BT_DIAG_DEBUG */

/**
 * @brief Bring up the Bluetooth Classic SPP transport for NSD
 *
 * Runs on core 0, before net_wifi_init() (usbsid.c). Sole cyw43_arch_init()
 * owner under USE_NET; net_wifi_init() never calls it.
 * The LED is turned off by net_wifi_init().
 */
void setup_bluetooth(void)
{
  usNFO("\n");
  usBTH("Init bluetooth\n");
  if (cyw43_arch_init()) {
    usBTH("cyw43_arch_init failed\n");
    return;
  }

  /* Boot-random PIN, not a fixed one - only matters for the legacy
   * pairing fallback; the primary path is SSP Just Works (no PIN). */
  bt_legacy_pin = get_rand_32();

  l2cap_init();
  rfcomm_init();
  rfcomm_register_service(packet_handler, RFCOMM_SERVER_CHANNEL, 0xffff);

  sdp_init();
  memset(spp_service_buffer, 0, sizeof(spp_service_buffer));
  spp_create_sdp_record(spp_service_buffer, sdp_create_service_record_handle(),
    RFCOMM_SERVER_CHANNEL, BT_LOCAL_NAME);
  sdp_register_service(spp_service_buffer);

  gap_discoverable_control(1);
  /* Discoverable alone only enables inquiry scan (so a client can find the
   * device); incoming connections need page scan too, gated separately by
   * connectable - without this, hci_write_scan_enable never sets the page
   * scan bit and every BR/EDR connect attempt times out at the baseband
   * level even though inquiry/pairing lookup succeeds (see hci.c's
   * new_scan_enable_value = (connectable << 1) | discoverable). */
  gap_connectable_control(1);
  /* Headless device, no display or buttons to confirm/enter a passkey -
   * Just Works is the only SSP association model that makes sense here. */
  gap_ssp_set_io_capability(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  gap_set_local_name(BT_LOCAL_NAME);

  hci_event_callback_registration.callback = &packet_handler;
  hci_add_event_handler(&hci_event_callback_registration);

#ifdef BT_DIAG_DEBUG
  hci_diag_callback_registration.callback = &bt_diag_packet_handler;
  hci_add_event_handler(&hci_diag_callback_registration);
#endif

  hci_power_control(HCI_POWER_ON);
  usBTH("SPP NSD transport active, discoverable as '%s'\n", BT_LOCAL_NAME);
}

/**
 * @brief True once an SPP RFCOMM channel is open to a client
 */
bool net_bt_is_connected(void)
{
  return rfcomm_channel_id != 0;
}
