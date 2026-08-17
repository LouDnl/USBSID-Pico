/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sl_interface.cpp
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * Contents of this file come from USBSID-Player (https://github.com/LouDnl/USBSID-Player)
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

#include <file/songlengths.h>
#include <util/md5.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace usbsid;
using data_t = unsigned char; // Or uint8_t

char db_path[1024];

/* What to play when the database has never heard of a song.
 *
 * Five minutes. A tune with no entry used to play until interrupted, which
 * means an unattended run stops at the first such tune for ever and a playlist
 * never reaches the end. Five minutes is longer than most SIDs and short enough
 * that sitting through one is not a punishment; the same figure is used by the
 * browser player, so the two behave alike.
 *
 * Zero when song lengths are switched off altogether, which still means "play
 * until stopped": --no-songlengths is a request for exactly that.
 */
constexpr uint32_t kDefaultSongMs = 5u * 60u * 1000u;

static uint32_t song_length_ms(const usbsid::SongLengths & lengths,
                               uint16_t song, bool use_songlengths)
{
  if (!use_songlengths) return 0;
  const uint32_t ms = lengths.valid ? lengths.for_song(song) : 0;
  return (ms > 0) ? ms : kDefaultSongMs;
}

bool read_file(const char * path, std::vector<data_t> & out)
{
  FILE * f = fopen(path, "rb");
  if (f == nullptr) return false;
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) { fclose(f); return false; }
  out.resize(static_cast<size_t>(size));
  const size_t got = fread(out.data(), 1, out.size(), f);
  fclose(f);
  return got == out.size();
}

extern "C" uint32_t find_songlenth_db(bool is_sid, int tune_no, const char * filename, const char * songlengths_path)
{
  std::vector<data_t> bytes;
  if (!read_file(filename, bytes)) {
    printf("cannot read %s\n", filename);
    return 1;
  }

  SongLengths lengths;
  if (songlengths_find_file(songlengths_path, db_path, sizeof(db_path))) {
    std::vector<char> db;
    FILE * dbf = fopen(db_path, "rb");
    if (dbf != nullptr) {
      fseek(dbf, 0, SEEK_END);
      const long dbn = ftell(dbf);
      fseek(dbf, 0, SEEK_SET);
      if (dbn > 0) {
        db.resize(static_cast<size_t>(dbn));
        if (fread(db.data(), 1, db.size(), dbf) != db.size()) db.clear();
      }
      fclose(dbf);
    }
    char key[33];
    songlengths_key(bytes.data(), bytes.size(), key);
    if (!db.empty()) lengths = songlengths_lookup(db.data(), db.size(), key);
    if (lengths.valid) {
      const uint32_t ms = lengths.for_song(tune_no);
      printf("  length   : %u:%02u.%03u for this song, %u in the database\n",
             ms / 60000u, (ms / 1000u) % 60u, ms % 1000u, lengths.count);
      return ms;
    } else {
      printf("  length   : not in %s, using %u:%02u\n", db_path,
             kDefaultSongMs / 60000u, (kDefaultSongMs / 1000u) % 60u);
    }
  } else if (is_sid) {
    /* A path given on the command line that is not there is a mistake, and
     * saying "none found" about it would hide which of the two happened. */
    if (songlengths_path != nullptr) {
      printf("  length   : cannot read %s\n", songlengths_path);
    } else {
      printf("  length   : no Songlengths database found. Point --songlengths at "
             "one, or set $SONGLENGTHS or $HVSCROOT\n");
    }
  }
  return 300000u;
}
