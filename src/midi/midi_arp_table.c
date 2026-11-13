/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_arp_table.c
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

#include <string.h>
#include <midi_arp_table.h>


midi_arp_table_t arp_tables[MIDI_ARP_TABLE_COUNT];

/**
 * @brief Initialise the MIDI arpeggiator tables to their default content
 */
void midi_arp_table_init(void)
{
  memset(arp_tables, 0, sizeof(arp_tables));

  /* Table 0: repeating major triad (root, major third, fifth). */
  arp_tables[0].offsets[0] = 0;
  arp_tables[0].offsets[1] = 4;
  arp_tables[0].offsets[2] = 7;
  arp_tables[0].loop_start = 0;
  arp_tables[0].step_count = 3;

  /* Tables 1-15 stay zeroed: step_count 0, arp_advance_table() no-ops. */
  return;
}
