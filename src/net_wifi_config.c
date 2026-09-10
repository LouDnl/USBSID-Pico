/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_wifi_config.c
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

/* WiFi credentials, kept out of Config on purpose - see config.h's
 * FLASH_WIFI_OFFSET comment and WIFI_SET_PSK's write-only handling below. */
static const WifiConfig wifi_default_config = WIFICONFIG_DEFAULT_INIT;
WifiConfig wifi_cfg = {0};

/* Same 16-slot wear-levelling counter as config_saveid, but for wifi_cfg's
 * own flash page at FLASH_WIFI_OFFSET. */
static uint8_t wifi_config_saveid = 0;


/**
 * @brief Set WiFi default configuration
 *
 */
void __no_inline_not_in_flash_func(default_wifi_config)(WifiConfig* config)
{
  wifi_config_saveid = config->save_id;  /* Preserve save id, same as default_config() */
  memcpy(config, &wifi_default_config, sizeof(WifiConfig));
  config->save_id = wifi_config_saveid;
  return;
}

/**
 * @brief Load the persisted WiFi credentials, same 16-slot wear-levelling
 *        scan as load_config() but at FLASH_WIFI_OFFSET, entirely separate
 *        from Config's own flash page.
 *
 * NOTE: core-0-only, same constraint as load_config()
 */
void __no_inline_not_in_flash_func(load_wifi_config)(WifiConfig* config)
{
  usNFO("\n");
  usCFG("Loading WiFi configuration from flash\n");
  uint8_t savelocationid = 0;
WIFI_AGAIN:
  WifiConfig temp_config;
  usCFG("  Trying wifi flash position '%u'", savelocationid);
  memcpy(&temp_config, (void *)(XIP_BASE + (FLASH_WIFI_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(WifiConfig));
  stdio_flush();
  usNFO(", flash contains wifi config id '%u' (255 == empty slot)\n", temp_config.save_id);
  if ((temp_config.save_id <= 0xF) && (temp_config.save_id == savelocationid)) {
    savelocationid++;
    goto WIFI_AGAIN;
  } else {
    savelocationid--;
    usCFG("  Found %s WiFi configuration at position %u\n",
      ((savelocationid == 255) ? "empty" : "latest"), savelocationid);
    memcpy(config, (void *)(XIP_BASE + (FLASH_WIFI_OFFSET + (FLASH_PAGE_SIZE * savelocationid))), sizeof(WifiConfig));
    stdio_flush();
  }
  wifi_config_saveid = config->save_id;

  if (config->magic != MAGIC_SMOKE) {
    usERR("MAGIC wifi_cfg.magic: %u != MAGIC_SMOKE: %u\n", config->magic, MAGIC_SMOKE);
    usCFG("Reset to default WiFi configuration!\n");
    default_wifi_config(config);
  }
  return;
}

/**
 * @brief Low level flash writing
 *
 */
static void __no_inline_not_in_flash_func(write_wifi_config_lowlevel)(void* config_data)
{ /* No logging in this function to avoid errors, mirrors write_config_lowlevel() */
  uint32_t ints = save_and_disable_interrupts();
  if (wifi_config_saveid == 0x0) flash_range_erase(FLASH_WIFI_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program((FLASH_WIFI_OFFSET + (FLASH_PAGE_SIZE * wifi_config_saveid)), (uint8_t*)config_data, FLASH_PAGE_SIZE);
  restore_interrupts(ints);
  return;
}

/**
 * @brief Write config to flash with safe execution
 *
 */
static void __no_inline_not_in_flash_func(write_wifi_config)(const WifiConfig* config)
{
  uint8_t config_data[WIFICONFIG_SIZE] = {0};
  static_assert(sizeof(WifiConfig) < WIFICONFIG_SIZE, "[WIFICONFIG] SAVE ERROR: WifiConfig struct doesn't fit inside WIFICONFIG_SIZE");
  memcpy(config_data, config, sizeof(WifiConfig));
  int err = flash_safe_execute(write_wifi_config_lowlevel, config_data, 100);
  if (err) {
    usERR("Saving WiFi configuration: %d\n", err);
  }
  sleep_ms(100);
  return;
}

/**
 * @brief Save the wifi config to flash
 *
 */
void __no_inline_not_in_flash_func(save_wifi_config)(WifiConfig* config)
{
  usNFO("\n");
  usCFG("Saving WiFi configuration:\n");
  int noerr = (wifi_cfg.save_id == wifi_config_saveid);
  if (noerr) {
    wifi_config_saveid++;
    wifi_config_saveid = wifi_cfg.save_id = (wifi_config_saveid <= 0xF ? wifi_config_saveid : 0);
    config->save_id = wifi_config_saveid;
    usCFG("  WiFi config id = %d, saving to 0x%x\n", wifi_config_saveid,
      (FLASH_WIFI_OFFSET + (FLASH_PAGE_SIZE * wifi_config_saveid)));
  } else {
    usERR("WiFi config save id's are not equal %d != %d. Unable to save configuration!!\n", wifi_cfg.save_id, wifi_config_saveid);
    return;
  }
  write_wifi_config(config);
  usCFG("WiFi configuration saved!\n");
  return;
}

/**
 * @brief Start WiFi with configured credentials and hostname
 *
 */
void start_wifi(void)
{
#ifdef USE_WIFI
  load_wifi_config(&wifi_cfg);
  net_wifi_set_credentials(wifi_cfg.ssid, wifi_cfg.psk);
  net_wifi_set_hostname(wifi_cfg.hostname);
  /* Powering the WiFi radio measurably disrupts SID chip/model detection
   * on real hardware for as long as it stays on, so only bring it up if
   * the user opted in; a freshly flashed board (wifi_enabled defaults to
   * false) must behave identically to a non-WiFi build. */
  if (wifi_cfg.flags.wifi_enabled) {
    net_wifi_start();
  }
#endif
}
