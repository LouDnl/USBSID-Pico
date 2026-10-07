/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_config.c
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
#include <config.h>
#include <logging.h>
#include <net_wifi.h>
#include <net_bluetooth.h>

/* WiFi/Bluetooth config, kept out of Config on purpose - see config.h's
 * FLASH_NET_OFFSET comment and WIFI_SET_PSK's write-only handling below. */
static const NetConfig net_default_config = NETCONFIG_DEFAULT_INIT;
NetConfig net_cfg = {0};

/* Same 16-slot wear-levelling counter as config_saveid, but for net_cfg's
 * own flash page at FLASH_NET_OFFSET. */
static uint8_t net_config_saveid = 0;


/**
 * @brief Set default network (WiFi/Bluetooth) configuration
 *
 */
void __no_inline_not_in_flash_func(default_net_config)(NetConfig* config)
{
  net_config_saveid = config->save_id;  /* Preserve save id, same as default_config() */
  memcpy(config, &net_default_config, sizeof(NetConfig));
  config->save_id = net_config_saveid;
  return;
}

/**
 * @brief Load the persisted network configuration, same 16-slot wear-levelling
 *        scan as load_config() but at FLASH_NET_OFFSET, entirely separate
 *        from Config's own flash page.
 *
 * NOTE: core-0-only, same constraint as load_config()
 */
void __no_inline_not_in_flash_func(load_net_config)(NetConfig* config)
{
  usNFO("\n");
  usCFG("Loading network configuration from flash\n");
  uint8_t savelocationid = 0;
NET_AGAIN:
  NetConfig temp_config;
  usCFG("  Trying net flash position '%u'", savelocationid);
  memcpy(&temp_config, (void *)(XIP_BASE + (FLASH_NET_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(NetConfig));
  stdio_flush();
  usNFO(", flash contains net config id '%u' (255 == empty slot)\n", temp_config.save_id);
  if ((temp_config.save_id <= 0xF) && (temp_config.save_id == savelocationid)) {
    savelocationid++;
    goto NET_AGAIN;
  } else {
    savelocationid--;
    usCFG("  Found %s network configuration at position %u\n",
      ((savelocationid == 255) ? "empty" : "latest"), savelocationid);
    memcpy(config, (void *)(XIP_BASE + (FLASH_NET_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(NetConfig));
    stdio_flush();
  }
  net_config_saveid = config->save_id;

  if (config->magic != MAGIC_SMOKE) {
    usERR("MAGIC net_cfg.magic: %u != MAGIC_SMOKE: %u\n", config->magic, MAGIC_SMOKE);
    usCFG("Reset to default network configuration!\n");
    default_net_config(config);
  }
  return;
}

/**
 * @brief Low level flash writing
 *
 */
static void __no_inline_not_in_flash_func(write_net_config_lowlevel)(void* config_data)
{ /* No logging in this function to avoid errors, mirrors write_config_lowlevel() */
  uint32_t ints = save_and_disable_interrupts();
  if (net_config_saveid == 0x0) flash_range_erase(FLASH_NET_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program((FLASH_NET_OFFSET + (FLASH_PAGE_SIZE * net_config_saveid)), (uint8_t*)config_data, FLASH_PAGE_SIZE);
  restore_interrupts(ints);
  return;
}

/**
 * @brief Write config to flash with safe execution
 *
 */
static void __no_inline_not_in_flash_func(write_net_config)(const NetConfig* config)
{
  uint8_t config_data[NETCONFIG_SIZE] = {0};
  static_assert(sizeof(NetConfig) < NETCONFIG_SIZE, "[NETCONFIG] SAVE ERROR: NetConfig struct doesn't fit inside NETCONFIG_SIZE");
  memcpy(config_data, config, sizeof(NetConfig));
  int err = flash_safe_execute(write_net_config_lowlevel, config_data, 100);
  if (err) {
    usERR("Saving network configuration: %d\n", err);
  }
  sleep_ms(100);
  return;
}

/**
 * @brief Save the network config to flash
 *
 */
void __no_inline_not_in_flash_func(save_net_config)(NetConfig* config)
{
  usNFO("\n");
  usCFG("Saving network configuration:\n");
  int noerr = (net_cfg.save_id == net_config_saveid);
  if (noerr) {
    net_config_saveid++;
    net_config_saveid = net_cfg.save_id = (net_config_saveid <= 0xF ? net_config_saveid : 0);
    config->save_id = net_config_saveid;
    usCFG("  Net config id = %d, saving to 0x%x\n", net_config_saveid,
      (FLASH_NET_OFFSET + (FLASH_PAGE_SIZE * net_config_saveid)));
  } else {
    usERR("Net config save id's are not equal %d != %d. Unable to save configuration!!\n", net_cfg.save_id, net_config_saveid);
    return;
  }
  write_net_config(config);
  usCFG("Network configuration saved!\n");
  return;
}

/**
 * @brief Load net_cfg and apply WiFi and Bluetooth power state live from it
 *
 */
void start_net(void)
{
#ifdef USE_NET
  load_net_config(&net_cfg);
  net_wifi_set_credentials(net_cfg.ssid, net_cfg.psk);
  net_wifi_set_hostname(net_cfg.hostname);
  /* Powering the WiFi radio measurably disrupts SID chip/model detection
   * on real hardware for as long as it stays on, so only bring it up if
   * the user opted in; a freshly flashed board (wifi_enabled defaults to
   * false) must behave identically to a non-WiFi build. */
  if (net_cfg.flags.wifi_enabled) {
    net_wifi_start();
  }
  /* net_cfg is only loaded above, not yet when setup_bluetooth() ran
   * earlier in boot - this is the first point bt_nsd_enabled is known. */
  net_bt_set_power(net_cfg.flags.bt_nsd_enabled);
#endif
}
