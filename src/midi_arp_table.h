/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * midi_arp_table.h
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

#ifndef _USBSID_MIDI_ARP_TABLE_H_
#define _USBSID_MIDI_ARP_TABLE_H_
#pragma once

#ifdef __cplusplus
  extern "C" {
#endif

#include <stdint.h>


/* GoatTracker/SidWizard-style arp/chord table - a small numbered sequence of
 * relative semitone offsets applied against the arpeggiator's held-note
 * root, replacing the fixed Up/Down/Up-Down/Random/As-Played shapes with an
 * author-defined one. Reuses arp_tick()'s existing stepping engine entirely
 * (free-run phase accumulator or MIDI-clock-synced division, see
 * midi_handler.c's arp_advance_table()) - only "which relative offset does
 * step N produce" is new.
 *
 * 16 steps matches MIDI_ARP_MAX_NOTES-scale storage costs - this is a
 * tracker-style arp shape, not a full sequencer track. */
#define MIDI_ARP_TABLE_STEPS 16
#define MIDI_ARP_TABLE_COUNT 16

typedef struct {
  int8_t  offsets[MIDI_ARP_TABLE_STEPS];  /* relative semitones from the arp's root note */
  uint8_t loop_start;                      /* step index the table returns to after step_count-1,
                                               so a table can have a distinct intro before its
                                               repeating tail, e.g. an attack transient */
  uint8_t step_count;                      /* 0 = empty/unauthored, arp_advance_table() no-ops */
} midi_arp_table_t;

extern midi_arp_table_t arp_tables[MIDI_ARP_TABLE_COUNT];

/* Table 0 is a plain repeating major triad (0, 4, 7), a safe non-silent
 * default to land on; tables 1-15 start empty (step_count 0), same "inert
 * rather than surprising" convention midi_patch_init() uses for its own
 * unauthored slots. */
void midi_arp_table_init(void);


#ifdef __cplusplus
  }
#endif

#endif /* _USBSID_MIDI_ARP_TABLE_H_ */
