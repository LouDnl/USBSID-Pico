/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * mcu.h
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * BusPirate5 by DangerousPrototypes:
 * https://github.com/DangerousPrototypes/BusPirate5-firmware/blob/main/pirate/mcu.h
 *
 * Any licensing conditions from the above named source apply to this code
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
#ifndef _MCU_H_
#define _MCU_H_
#pragma once
#include "pico/stdlib.h"

/* https://forums.raspberrypi.com/viewtopic.php?p=1928868#p1928868 */
/* Reset rp2040 register */
#define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))
/* Reset rp2040 using the reset register */
#define RESET_PICO() AIRCR_Register = 0x5FA0004;

/* Retrieve the rp2040 mcu unique id */
uint64_t mcu_get_unique_id(void);
/* Reset the rp2040 mcu */
void mcu_reset(void);
/* Jump to rp2040 mcu bootloader */
void mcu_jump_to_bootloader(void);

#endif /* _MCU_H_ */
