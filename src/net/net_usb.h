/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * net_usb.h
 * USB network (CDC-NCM) interface header file
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

#ifndef _USBSID_NET_USB_H_
#define _USBSID_NET_USB_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>


/* Addressing modes (NetConfig.flags.usbnet_ip_mode): DHCP server (factory
 * default, serves the USB link only), DHCP client (retries forever, needs a
 * host bridge to the LAN), static (needs host routing, proxy ARP or bridge).
 * Do not bridge the USB network interface into a LAN that has its own DHCP
 * server while in DHCP server mode. */

/* DHCP server mode address: USB_NET_SUBNET_A.B.C.1/24, DHCP pool .2 and .3,
 * no router, no DNS */
#ifndef USB_NET_SUBNET_A
#define USB_NET_SUBNET_A 192
#endif
#ifndef USB_NET_SUBNET_B
#define USB_NET_SUBNET_B 168
#endif
#ifndef USB_NET_SUBNET_C
#define USB_NET_SUBNET_C 65
#endif

/* net_usb_link_state() codes, same values as the WIFI_READ_IP state byte */
enum {
  NET_USB_CODE_DOWN       = 1,  /* USB not mounted or suspended */
  NET_USB_CODE_CONNECTING = 2,  /* Link up, no IP address yet */
  NET_USB_CODE_CONNECTED  = 3,  /* Link up with IP: lease, static or server */
};

/**
 * @brief Fill `tud_network_mac_address` from the MCU unique id
 *
 * Locally administered unicast MAC 02:xx:xx:xx:xx:xx, stable per board.
 * Repeated calls keep the first result. Core 0 only, no return value.
 */
void net_usb_mac_setup(void);

/**
 * @brief Initialize the USB network transport, core 0 only
 *
 * Load net_cfg, take its IP config as the applied IP config, set up the MAC,
 * lwIP and the netif, open the NSD listener and discovery per net_cfg. Call
 * after config load, before tud_connect().
 */
void net_usb_init(void);

/**
 * @brief Service the USB network transport, core 0 loop after tud_task()
 *
 * Track USB link state, feed queued RX frames to lwIP, flush the TX FIFO,
 * run lwIP timers, DHCP lease log, DHCP server retry and listener bind retry.
 */
void net_usb_task(void);

/**
 * @brief Apply net_cfg live, core 0 only
 *
 * Apply nsd_enabled, nsd_port and discovery_enabled. Take usbnet_ip_mode and
 * the static addresses as the applied IP config, the only one link up
 * addressing uses. A changed usbnet_ip_mode, or changed static addresses in
 * static mode, drops the NSD TCP session and restarts addressing.
 */
void net_usb_apply(void);

/**
 * @brief Apply net_cfg NSD settings live, core 0 only
 *
 * Apply nsd_enabled, nsd_port and discovery_enabled. IP mode and netif
 * addressing stay unchanged.
 */
void net_usb_apply_nsd(void);

/**
 * @brief Return the USB network state
 *
 * @return uint8_t NET_USB_CODE_DOWN, _CONNECTING or _CONNECTED
 */
uint8_t net_usb_link_state(void);

/**
 * @brief Return the IPv4 address of the USB netif
 *
 * @return uint32_t address in network byte order, 0 unless NET_USB_CODE_CONNECTED
 */
uint32_t net_usb_ip_addr(void);

#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_NET_USB_H_ */
