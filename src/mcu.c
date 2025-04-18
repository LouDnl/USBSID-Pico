/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * mcu.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * BusPirate5 by DangerousPrototypes:
 * https://github.com/DangerousPrototypes/BusPirate5-firmware/blob/main/pirate/mcu.c
 *
 * Any licensing conditions from the above named source automatically apply to this code
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

#include <stdio.h>

#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/bootrom.h"

#include "logging.h"


uint64_t mcu_get_unique_id(void)
{
  static_assert(sizeof(pico_unique_board_id_t) == sizeof(uint64_t), "pico_unique_board_id_t is not 64 bits (but is cast to uint64_t)");
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  return *((uint64_t*)(id.id));
};

void mcu_reset(void)
{
  CFG("[MCU_RESET]\n");
  sleep_ms(100);  // sleep some ms to let commands prior to reset settle or finish
  /* watchdog_enable(1, 1); */
  watchdog_reboot(0, 0, 0);
  while(1);
}

void mcu_jump_to_bootloader(void)
{
  /* \param usb_activity_gpio_pin_mask 0 No pins are used as per a cold boot. Otherwise a single bit set indicating which
  *                               GPIO pin should be set to output and raised whenever there is mass storage activity
  *                               from the host.
  * \param disable_interface_mask value to control exposed interfaces
  *  - 0 To enable both interfaces (as per a cold boot)
  *  - 1 To disable the USB Mass Storage Interface
  *  - 2 To disable the USB PICOBOOT Interface
  */
  reset_usb_boot(0x00,0x00);
}
