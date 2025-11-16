/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * interface.cpp
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Any licensing conditions from the above named source automatically
 * apply to this code
  *
 * Copyright (c) 2024-2025 LouD
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

#include <iomanip>
#include <iostream>
#include <string.h>

#include <interface.h>
#include <c64.h>
#include <memory.h>

#ifdef ONBOARD_SIDPLAYER
#include <sidfile.h>
static const char *chiptype_s[4] = {"Unknown", "MOS6581", "MOS8580", "MOS6581 and MOS8580"};
static const char *clockspeed_s[5] = {"Unknown", "PAL", "NTSC", "PAL and NTSC", "DREAN"};
extern uint8_t c64memory[0x10000];
#endif

#ifdef ANALYSE_STACKUSAGE
extern "C" {
#include "tool_heapusage.c"
extern void heapbefore(void);
extern void heapafter(void);
extern void getFreeStack(void);
}
#else
extern void heapbefore(void){};
extern void heapafter(void){};
extern void getFreeStack(void){};
#endif

extern "C" {
  #include "logging.h"
}

#ifdef ONBOARD_EMULATOR
/* constants */
static const uint16_t kBasicPrgStart = 0x0801;
static const uint16_t kBasicTxtTab   = 0x002b;
static const uint16_t kBasicVarTab   = 0x002d;
static const uint16_t kBasicAryTab   = 0x002f;
static const uint16_t kBasicStrEnd   = 0x0031;

/* Static variables */
static bool nosdl_     = true;
static bool isbinary_  = false;
static bool havecart_  = false;
static bool midi_      = false;
/* Static logging variables */
static bool logmemrw   = false; /* log _all_ memory reads and writes */
static bool logcia1rw  = false; /* log cia1 memory reads and writes */
static bool logcia2rw  = false; /* log cia2 memory reads and writes */
static bool logiorw    = false; /* log io memory reads and writes */
static bool logplarw   = false; /* log pla memory reads and writes */
static bool logcrtrw   = false; /* log cart memory reads and writes */
static bool logsidrw   = false; /* log sid memory reads and writes */
static bool logsidiorw = false; /* log sid io writes */
static bool bankswlog  = false; /* log bank switching */
/* Static IO/Memory variables */
static uint16_t load_addr, init_addr; /* Zero as signal it hasn't been changed */
static uint16_t aptr; /* pointer for counting addresses */
#endif

#ifdef ONBOARD_SIDPLAYER
static uint16_t pl_loadaddr;
static uint16_t pl_lastloadaddr;
static uint16_t pl_initaddr;
static uint16_t pl_playaddr;
static uint16_t pl_datalength;
static uint8_t *pl_databuffer;
static uint16_t pl_dataoffset;
static uint32_t pl_sidspeed;
static uint8_t pl_start_page;
static uint8_t pl_max_pages;
static uint16_t playerstart;
static uint8_t p_hi;
static uint8_t p_lo;
static int pl_songs;
static int pl_song_number;
static int pl_sidflags;
static int pl_curr_sidspeed;
static int pl_chiptype;
static int pl_clockspeed;
static int pl_sidversion;
static int pl_clock_speed;
static int pl_raster_lines;
static int pl_rasterrow_cycles;
static int pl_frame_cycles;
static int pl_refresh_rate;
/* static */ int pl_isrsid;
int subtune = 0;
uint8_t * binary_p;
#endif

/* Machine */
#ifdef ONBOARD_EMULATOR
C64 *c64 = nullptr;
#endif
#ifdef ONBOARD_SIDPLAYER
SidFile * sidfile_ = nullptr;
#endif

using namespace std;


#ifdef ONBOARD_EMULATOR
/**
 * @brief read short from little endian as big endian
 */
static uint16_t read_short_le(uint8_t * ptr, uint8_t pos)
{
  char b;
  uint16_t v = 0;
  v |= (ptr[pos]);
  v |= (ptr[(pos+1)] << 8);
  return v;
}

 /**
  * @brief read short from big endian as big endian
  */
static uint16_t read_short_be(uint8_t * ptr, uint8_t pos)
{
  char b;
  uint16_t v = 0;
  v |= (ptr[(pos)] << 8);
  v |= (ptr[pos+1]);
  return v;
}

/**
 * @brief enable specific machine logging
 */
extern "C" void logging_enable(int logid)
{
  switch(logid) {
    case 0: logmemrw    = true; break;
    case 1: logcia1rw   = true; break;
    case 2: logcia2rw   = true; break;
    case 3: logiorw     = true; break;
    case 4: logplarw    = true; break;
    case 5: logcrtrw    = true; break;
    case 6: logsidrw    = true; break;
    case 7: logsidiorw  = true; break;
    default: break;
  }
  return;
}

/**
 * @brief disable specific machine logging
 */
extern "C" void logging_disable(int logid)
{
  switch(logid) {
    case 0: logmemrw    = false; break;
    case 1: logcia1rw   = false; break;
    case 2: logcia2rw   = false; break;
    case 3: logiorw     = false; break;
    case 4: logplarw    = false; break;
    case 5: logcrtrw    = false; break;
    case 6: logsidrw    = false; break;
    case 7: logsidiorw  = false; break;
    default: break;
  }
  return;
}

/**
 * @brief disables all logging on emulator stop
 */
static void disable_all_logging(void)
{
 logmemrw   = false;
 logcia1rw  = false;
 logcia2rw  = false;
 logiorw    = false;
 logplarw   = false;
 logcrtrw   = false;
 logsidrw   = false;
 logsidiorw = false;
 bankswlog  = false;
 return;
}

/**
 * @brief set c64 logging
 */
extern "C" void _set_logging(void)
{
  if(logmemrw)   {c64->mem_->setlogrw(0);} else {c64->mem_->unsetlogrw(0);};
  if(logcia1rw)  {c64->mem_->setlogrw(1);} else {c64->mem_->unsetlogrw(1);};
  if(logcia2rw)  {c64->mem_->setlogrw(2);} else {c64->mem_->unsetlogrw(2);};
  if(logiorw)    {c64->mem_->setlogrw(3);} else {c64->mem_->unsetlogrw(3);};
  if(logplarw)   {c64->mem_->setlogrw(4);} else {c64->mem_->unsetlogrw(4);};
  if(logcrtrw)   {c64->mem_->setlogrw(5);} else {c64->mem_->unsetlogrw(5);};
  if(logsidrw)   {c64->mem_->setlogrw(6);} else {c64->mem_->unsetlogrw(6);};
  if(logsidiorw) {c64->mem_->setlogrw(7);} else {c64->mem_->unsetlogrw(7);};
}

/**
 * @brief Start Emudore with cynthcart loaded
 * will run continuously if set to true
 */
extern "C" void start_emudore_cynthcart(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  bool run_continuously
)
{
  if (c64 != nullptr) {
    D("Emudore already running!\n");
    return;
  }
  (void) heapbefore();
  midi_ = true;
  c64 = new C64(
    nosdl_,isbinary_,havecart_,bankswlog,midi_,
    basic_,chargen_,kernal_,binary_
  );
  C64::is_cynthcart = true;
  (void) heapafter();

  _set_logging(); /* Set c64 logging */

  unsigned short binsize = 13166;

  load_addr = aptr = read_short_le(binary_,0);
  size_t pos = 2; /* pos starts at 2 after reading the load address at 0 and 1 */
  (void) heapbefore();
  while(pos <= binsize) {
    // D("ADDR: $%04x POS: %d VAL: %02X\n",aptr,pos,binary_[pos]);
    c64->mem_->write_byte_no_io(aptr++,binary_[pos++]);
  }

  /* basic-tokenized prg */
  if(load_addr == kBasicPrgStart)
  {
    /* make BASIC happy */
    c64->mem_->write_word_no_io(kBasicTxtTab,load_addr);
    c64->mem_->write_word_no_io(kBasicVarTab,aptr);
    c64->mem_->write_word_no_io(kBasicAryTab,aptr);
    c64->mem_->write_word_no_io(kBasicStrEnd,aptr);
    if (init_addr == 0x0) {
      init_addr = ((c64->mem_->read_byte_no_io(load_addr)|c64->mem_->read_byte_no_io(load_addr+0x1)<<8)+0x2);
      D("1. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
      c64->cpu_->pc(init_addr);
    } else {
      D("2. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
      c64->cpu_->pc(init_addr); /* Example: Cynthcart start/init address is 0x80D */
    }
  }
  else {
    init_addr = ((c64->mem_->read_byte_no_io(load_addr)|c64->mem_->read_byte_no_io(load_addr+0x1)<<8)+0x2);
    D("3. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
    c64->cpu_->pc(init_addr);
  }

  (void) heapafter();
  if (run_continuously) {c64->start();};
  return;
}

extern "C" unsigned int run_emulation_cycle(void)
{
  return c64->emulate();
}

extern "C" unsigned int run_specified_cycle(
  bool cpu, bool cia1, bool cia2,
  bool vic, bool io,   bool cart)
{
  return c64->emulate_specified(cpu,cia1,cia2,vic,io,cart);
}

/**
 * @brief stop Emudore and free up memory
 */
extern "C" bool stop_emudore(void)
{
  D("Stopping emudore\n");
  (void) heapbefore();
  if (c64 != nullptr) {
    if (c64->is_looping()) {
      D("Disabling emudore loop\n");
      c64->disable_looping();
    }
    D("Stopping c64\n");
    disable_all_logging();
    C64::log_timings = false;
    C64::is_cynthcart = false;
    C64::is_rsid = false;
    _set_logging();
    delete c64;
    c64 = nullptr;
    (void) heapafter();
    memset(c64memory, 0, 0x10000);
    return true;
  }
  (void) heapafter();
  return false;
}

extern "C" bool is_looping(void)
{
  if (c64 != nullptr)
    return c64->is_looping();
  else return false;
}

extern "C" bool disable_looping(void)
{
  if (c64 != nullptr)
    return c64->disable_looping();
  else return false;
}

#ifdef ONBOARD_SIDPLAYER
static void print_sid_info(void)
{
  cout << "\n< Sid Info >" << endl;
  cout << "---------------------------------------------" << endl;
  cout << "SID Title          : " << sidfile_->GetModuleName() << endl;
  cout << "Author Name        : " << sidfile_->GetAuthorName() << endl;
  cout << "Release & (C)      : " << sidfile_->GetCopyrightInfo() << endl;
  cout << "---------------------------------------------" << endl;
  cout << "SID Type           : " << sidfile_->GetSidType() << endl;
  cout << "SID Format version : " << dec << pl_sidversion << endl;
  cout << "---------------------------------------------" << endl;
  // cout << "SID Flags          : 0x" << hex << sidflags << " 0b" << bitset<8>{sidflags} << endl;
  cout << "Chip Type          : " << chiptype_s[pl_chiptype] << endl;
  if (pl_sidversion == 3 || pl_sidversion == 4)
      cout << "Chip Type 2        : " << chiptype_s[sidfile_->GetChipType(2)] << endl;
  if (pl_sidversion == 4)
      cout << "Chip Type 3        : " << chiptype_s[sidfile_->GetChipType(3)] << endl;
  cout << "Clock Type         : " << hex << clockspeed_s[pl_clockspeed] << endl;
  cout << "Clock Speed        : " << dec << pl_clock_speed << endl;
  cout << "Raster Lines       : " << dec << pl_raster_lines << endl;
  cout << "Rasterrow Cycles   : " << dec << pl_rasterrow_cycles << endl;
  cout << "Frame Cycles       : " << dec << pl_frame_cycles << endl;
  cout << "Refresh Rate       : " << dec << pl_refresh_rate << endl;
  if (pl_sidversion == 3 || pl_sidversion == 4 || pl_sidversion == 78) {
      cout << "---------------------------------------------" << endl;
      cout << "SID 2 $addr        : $d" << hex << sidfile_->GetSIDaddr(2) << "0" << endl;
      if (pl_sidversion == 4 || pl_sidversion == 78)
          cout << "SID 3 $addr        : $d" << hex << sidfile_->GetSIDaddr(3) << "0" << endl;
      if (pl_sidversion == 78)
          cout << "SID 4 $addr        : $d" << hex << sidfile_->GetSIDaddr(4) << "0" << endl;
  }
  cout << "---------------------------------------------" << endl;
  cout << "Data Offset        : $" << setfill('0') << setw(4) << hex << sidfile_->GetDataOffset() << endl;
  cout << "Image length       : $" << hex << sidfile_->GetInitAddress() << " - $" << hex << pl_lastloadaddr << endl;
  cout << "Load Address       : $" << hex << sidfile_->GetLoadAddress() << endl;
  cout << "Init Address       : $" << hex << sidfile_->GetInitAddress() << endl;
  cout << "Play Address       : $" << hex << sidfile_->GetPlayAddress() << endl;
  cout << "Start Page         : $" << hex << sidfile_->GetStartPage() << endl;
  cout << "Max Pages          : $" << hex << sidfile_->GetMaxPages() << endl;
  cout << "---------------------------------------------" << endl;
  cout << "Song Speed(s)      : $" << hex << pl_curr_sidspeed << " $0x" << hex << pl_sidspeed << endl;//" 0b" << bitset<32>{pl_sidspeed} << endl;
  cout << "Timer              : " << (pl_curr_sidspeed == 1 ? "CIA1" : "Clock") << endl;
  cout << "Selected Sub-Song  : " << dec << pl_song_number + 1 << " / " << dec << sidfile_->GetNumOfSongs() << endl;
}

/**
 * @brief start emudore PRG tune player
 */
extern "C" void start_emudore_prgtuneplayer(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  size_t binsize_, bool run_continuously)
{
  if (c64 != nullptr) {
    D("Emudore already running!\n");
    return;
  }
  (void) heapbefore();
  midi_ = false; /* No need when playing PRG SID tunes! ;-) */
  c64 = new C64(
    nosdl_,isbinary_,havecart_,bankswlog,midi_,
    basic_,chargen_,kernal_,binary_
  );
  (void) heapafter();

  _set_logging(); /* Set c64 logging */

  unsigned short binsize = 13166;

  load_addr = aptr = read_short_le(binary_,0);
  size_t pos = 2; /* pos starts at 2 after reading the load address at 0 and 1 */
  (void) heapbefore();
  while(pos <= binsize) {
    // D("ADDR: $%04x POS: %d VAL: %02X\n",aptr,pos,binary_[pos]);
    c64->mem_->write_byte_no_io(aptr++,binary_[pos++]);
  }

  /* basic-tokenized prg */
  if(load_addr == kBasicPrgStart)
  {
    /* make BASIC happy */
    c64->mem_->write_word_no_io(kBasicTxtTab,load_addr);
    c64->mem_->write_word_no_io(kBasicVarTab,aptr);
    c64->mem_->write_word_no_io(kBasicAryTab,aptr);
    c64->mem_->write_word_no_io(kBasicStrEnd,aptr);
    if (init_addr == 0x0) {
      init_addr = ((c64->mem_->read_byte_no_io(load_addr)|c64->mem_->read_byte_no_io(load_addr+0x1)<<8)+0x2);
      D("1. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
      c64->cpu_->pc(init_addr);
    } else {
      D("2. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
      c64->cpu_->pc(init_addr); /* Example: Cynthcart start/init address is 0x80D */
    }
  }
  else {
    init_addr = ((c64->mem_->read_byte_no_io(load_addr)|c64->mem_->read_byte_no_io(load_addr+0x1)<<8)+0x2);
    D("3. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
    c64->cpu_->pc(init_addr);
  }

  (void) heapafter();
  // C64::log_timings = true;
  if (run_continuously) {c64->start();};
  return;
}

extern "C" int reloc65(char** buf, int* fsize, int addr);

uint16_t find_free_page(void)
{
  /* Start and end pages. */
  int startp = (pl_loadaddr >> 8);
  int endp = (pl_lastloadaddr >> 8);

  /* Used memory ranges. */
  unsigned int used[] = {
      0x00,
      0x03,
      0xa0,
      0xbf,
      0xd0,
      0xff,
      0x00,
      0x00
  };        /* calculated below */

  unsigned int pages[256];
  unsigned int last_page = 0;
  unsigned int i, page, tmp;

  if (pl_start_page == 0x00) {
    fprintf(stdout, "No PSID freepages set, recalculating\n");
  } else {
    fprintf(stdout, "Calculating first free page\n");
  }

  /* finish initialization */
  used[6] = startp; used[7] = endp;

  /* Mark used pages in table. */
  memset(pages, 0, sizeof(pages));
  for (i = 0; i < sizeof(used) / sizeof(*used); i += 2) {
      for (page = used[i]; page <= used[i + 1]; page++) {
          pages[page] = 1;
      }
  }

  /* Find largest free range. */
  pl_max_pages = 0x00;
  for (page = 0; page < sizeof(pages) / sizeof(*pages); page++) {
      if (!pages[page]) {
          continue;
      }
      tmp = page - last_page;
      if (tmp > pl_max_pages) {
          pl_start_page = last_page;
          pl_max_pages = tmp;
      }
      last_page = page + 1;
  }

  if (pl_max_pages == 0x00) {
      pl_start_page = 0xff;
  }
  return (pl_start_page << 8);
}

inline int psid_set_cbm80(uint16_t vec, uint16_t addr)
{
    unsigned int i;
    uint8_t cbm80[] = { 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc2, 0xcd, 0x38, 0x30 };

    cbm80[0] = vec & 0xff;
    cbm80[1] = vec >> 8;

    for (i = 0; i < sizeof(cbm80); i++) {
        /* make backup of original content at 0x8000 */
        c64->mem_->write_byte_no_io((uint16_t)(addr + i), c64->mem_->read_byte_no_io((uint16_t)(0x8000 + i)));
        /* copy header */
        c64->mem_->write_byte_no_io((uint16_t)(0x8000 + i), cbm80[i]);
    }

    return i;
}

inline void psid_init_driver(void) /* RSID ONLY */
{
  c64->pla_->switch_banks(c64->pla_->m13);  /* TEST */

  // PSID_LOAD_FILE
  {
    if (pl_sidversion >= 2) {
      pl_sidflags = pl_sidflags;
      pl_start_page = sidfile_->GetStartPage();
      pl_max_pages = sidfile_->GetMaxPages();
    } else {
      pl_sidflags = 0;
      pl_start_page = 0;
      pl_max_pages = 0;
    }
    if ((subtune < 0) || (subtune > pl_songs)) {
      fprintf(stdout, "Default tune out of range (%d of %d ?), using 1 instead.\n", subtune, pl_songs);
      subtune = 1;
    } else printf("subtune: %d\n",subtune);

    /* Relocation setup. */
    if (pl_start_page == 0x00) {
      find_free_page();
    }

    if (pl_start_page == 0xff || pl_max_pages < 2) {
      fprintf(stdout, "No space for driver.\n");
      exit(1);
    }

  }

  // PSID_INIT_DRIVER
  {
    uint8_t psid_driver[] = {
  #include "psiddrv.h"
    };
    char *psid_reloc = (char *)psid_driver;
    int psid_size;

    uint16_t reloc_addr;
    uint16_t addr;
    int i;
    int sync; // FIXED TO PAL
    int sid2loc, sid3loc;

    for (addr = 0; addr < 0x0800; addr++) {
        c64->mem_->write_byte_no_io(addr, (uint8_t)0x00);
    }

    reloc_addr = pl_start_page << 8;
    psid_size = sizeof(psid_driver);
    fprintf(stdout, "PSID free pages: $%04x-$%04x\n",
            reloc_addr, (reloc_addr + (pl_max_pages << 8)) - 1U);

    if (!reloc65((char **)&psid_reloc, &psid_size, reloc_addr)) {
        fprintf(stderr, "Relocation.\n");
        // psid_set_tune(-1);
        return;
    }

    for (i = 0; i < psid_size; i++) {
        c64->mem_->write_byte_no_io((uint16_t)(reloc_addr + i), psid_reloc[i]);
    }

    /* Load SID data into memory */
    for (unsigned int i = 0; i < pl_datalength; i++) {
      c64->mem_->write_byte_no_io((pl_loadaddr+i),binary_p[pl_dataoffset+i]);
      if (i <= 8) printf("byte %d addr: $%04X data: $%02X\n", i, (pl_loadaddr+i), c64->mem_->read_byte_no_io(pl_loadaddr+i));
    }

    /* Skip JMP and CBM80 reset vector. */
    addr = reloc_addr + 3 + 9 + 9;

    /* Store parameters for PSID player. */
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(0));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_songs));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_loadaddr & 0xff));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_loadaddr >> 8));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_initaddr & 0xff));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_initaddr >> 8));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_playaddr & 0xff));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_playaddr >> 8));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_curr_sidspeed & 0xff));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)((pl_curr_sidspeed >> 8) & 0xff));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)((pl_curr_sidspeed >> 16) & 0xff));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_curr_sidspeed >> 24));
    // c64->mem_->write_byte_no_io(addr++, (uint8_t)((int)sync == MACHINE_SYNC_PAL ? 1 : 0));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)1);  // SYNC PAL
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_lastloadaddr & 0xff));
    c64->mem_->write_byte_no_io(addr++, (uint8_t)(pl_lastloadaddr >> 8));
  }

  // PSID_INIT_TUNE
  {
    int start_song = subtune;
    int sync, sid_model;
    int i;
    uint16_t reloc_addr;
    uint16_t addr;
    int speedbit;
    char* irq;
    char irq_str[20];
    const char csidflag[4][8] = { "UNKNOWN", "6581", "8580", "ANY"};
    reloc_addr = pl_start_page << 8;
    fprintf(stdout, "Driver=$%04X, Image=$%04X-$%04X, Init=$%04X, Play=$%04X\n",
            reloc_addr, pl_loadaddr,
            (unsigned int)(pl_loadaddr + pl_datalength - 1),
            pl_initaddr, pl_playaddr);

    if (start_song == 0) {
      start_song = subtune;
    } else if (start_song < (subtune - 1) || start_song > pl_songs) {
      fprintf(stdout, "Tune out of range.\n");
      start_song = subtune;
    }
    printf("start_song: %d\n",start_song);
    /* Check tune speed. */
    speedbit = 1;
    for (i = 1; i < start_song && i < 32; i++) {
        speedbit <<= 1;
    }

    /* Store parameters for PSID player. */
    if (1 /* install_driver_hook */) {
        /* Skip JMP. */
        addr = reloc_addr + 3 + 9;

        /* CBM80 reset vector. */
        addr += psid_set_cbm80((uint16_t)(reloc_addr + 9), addr);

        c64->mem_->write_byte_no_io(addr, (uint8_t)(start_song));
    }
    /* put song number into address 780/1/2 (A/X/Y) for use by BASIC tunes */
    c64->mem_->write_byte_no_io(780, (uint8_t)(start_song - 1));
    c64->mem_->write_byte_no_io(781, (uint8_t)(start_song - 1));
    c64->mem_->write_byte_no_io(782, (uint8_t)(start_song - 1));
    /* force flag in c64 memory, many sids reads it and must be set AFTER the sid flag is read */
    // c64->mem_->write_byte_no_io((uint16_t)(0x02a6), (uint8_t)(sync == MACHINE_SYNC_NTSC ? 0 : 1));
    c64->mem_->write_byte_no_io((uint16_t)(0x02a6), (uint8_t)1); // FORCE PAL
    // c64->cpu_->pc(psid->load_addr);
    // c64->cpu_->pc((unsigned int)(psid->load_addr + pl_datalength - 1));
    c64->cpu_->pc(pl_initaddr);
    // c64->cpu_->pc(pl_playaddr);
    // c64->cpu_->pc(reloc_addr);
    // c64->cpu_->reset();
    // c64->cpu_->irq();
  }
}

inline void load_Psidplayer(void)
{ /* Init SID player */
  D("Starting PSID player\n");
  uint16_t playerstart = playerstart =
    (pl_loadaddr <= 0x1000)
    ? (pl_initaddr+pl_datalength+1)
    : (pl_loadaddr-28);
  // uint16_t playerstart = (pl_initaddr+pl_datalength+1);
  uint8_t p_hi = ((playerstart >> 8) & 0xFF);
  uint8_t p_lo = (playerstart & 0xFF);
  printf("playerstart: $%04X p_lo: $%02X p_hi: $%02X\n", playerstart, p_lo, p_hi);

  /* install reset vector for microplayer */
  c64->mem_->write_byte(0xFFFC, p_lo); // lo
  c64->mem_->write_byte(0xFFFD, p_hi); // hi

  /* install IRQ vector for play routine launcher */
  c64->mem_->write_byte(0xFFFE, (p_lo+0x13)); // lo
  c64->mem_->write_byte(0xFFFF, p_hi); // hi

  /* clear everything from from ram except IO 'm13' */
  c64->pla_->switch_banks(c64->pla_->m13);

  /* install the micro player, 6502 assembly code */
  c64->mem_->write_byte(playerstart, 0xA9);                     // 0xA9 LDA imm load A with the song number
  c64->mem_->write_byte(playerstart+1, pl_song_number);         // 0xNN #NN song number

  /* jump to init address */
  c64->mem_->write_byte(playerstart+2, 0x20);                   // 0x20 JSR abs jump sub to INIT routine
  c64->mem_->write_byte(playerstart+3, (pl_initaddr & 0xFF));       // 0xxxNN address lo
  c64->mem_->write_byte(playerstart+4, (pl_initaddr >> 8) & 0xFF);  // 0xNNxx address hi

  c64->mem_->write_byte(playerstart+5, 0x58);                   // 0x58 CLI enable interrupt
  c64->mem_->write_byte(playerstart+6, 0xEA);                   // 0xEA NOP impl
  c64->mem_->write_byte(playerstart+7, 0x4C);                   // JMP jump to 0x0006
  c64->mem_->write_byte(playerstart+8, (p_lo+6));               // 0xxxNN address lo
  c64->mem_->write_byte(playerstart+9, p_hi);                   // 0xNNxx address hi

  /* play routine launcher */
  c64->mem_->write_byte(playerstart+0x13, 0xEA);                // 0xEA NOP
  c64->mem_->write_byte(playerstart+0x14, 0xEA);                // 0xEA NOP
  c64->mem_->write_byte(playerstart+0x15, 0xEA);                // 0xEA NOP
  c64->mem_->write_byte(playerstart+0x16, 0x20);                // 0x20 JSR jump sub to play routine
  c64->mem_->write_byte(playerstart+0x17, (pl_playaddr & 0xFF));       // playaddress lo
  c64->mem_->write_byte(playerstart+0x18, (pl_playaddr >> 8) & 0xFF);  // playaddress hi
  c64->mem_->write_byte(playerstart+0x19, 0xEA);                // 0xEA NOP
  c64->mem_->write_byte(playerstart+0x1A, 0x40);                // 0x40 RTI return from interruptrupt

  for (uint16_t i = playerstart ; i <= playerstart+0x1A ; i++) {
    printf("addr: $%04X data: $%02X\n",
      i, c64->mem_->read_byte_no_io(i));
  }
}

inline void preload_sid(void)
{
  pl_songs = sidfile_->GetNumOfSongs();
  pl_song_number = subtune;//sidfile_->GetFirstSong(); /* TODO: FIX N FINISH ;) */
  pl_sidflags = sidfile_->GetSidFlags();
  pl_sidspeed = sidfile_->GetSongSpeed(pl_song_number); // + 1);
  pl_curr_sidspeed = (pl_sidspeed & (1 << pl_song_number)); // ? 1 : 0;  // 1 ~ 60Hz, 2 ~ 50Hz
  pl_chiptype = sidfile_->GetChipType(1);
  pl_clockspeed = sidfile_->GetClockSpeed();
  pl_sidversion = sidfile_->GetSidVersion();
  pl_clock_speed = clockSpeed[pl_clockspeed];
  pl_raster_lines = scanLines[pl_clockspeed];
  pl_rasterrow_cycles = scanlinesCycles[pl_clockspeed];
  pl_frame_cycles = (pl_raster_lines * pl_rasterrow_cycles);
  pl_refresh_rate = refreshRate[pl_clockspeed];
  pl_loadaddr = sidfile_->GetLoadAddress();
  pl_lastloadaddr = ((sidfile_->GetLoadAddress() - 1) + sidfile_->GetDataLength());
  pl_datalength = sidfile_->GetDataLength();
  /* pl_databuffer = sidfile_->GetDataPtr(); */
  pl_dataoffset = (sidfile_->GetDataOffset() + 2); // skip first two load address bytes
  // pl_dataoffset = (sidfile_->GetDataOffset());
  // pl_databuffer = binary_+pl_dataoffset;
  pl_playaddr = sidfile_->GetPlayAddress();
  pl_initaddr = sidfile_->GetInitAddress();
  pl_start_page = sidfile_->GetStartPage();
  pl_max_pages = sidfile_->GetMaxPages();
  pl_isrsid = (sidfile_->GetSidType() == "RSID");
  C64::is_rsid = pl_isrsid;
}

inline bool do_sid(uint8_t * binary_, size_t binsize_)
{
  sidfile_ = new SidFile();
  if(sidfile_->ParsePtr(binary_,binsize_)) {
    DBG("[EMU] Error parsing sidfile!\n");
    delete sidfile_;
    return false;
  }
  binary_p = binary_;
  preload_sid();
  return true;
}

inline bool load_sid(void)
{
  print_sid_info();
  delete sidfile_;
  printf("load: $%04X play: $%04X init: $%04X\n", pl_loadaddr, pl_playaddr, pl_initaddr);

  if (!pl_isrsid) {
    /* Load SID data into memory */
    for (unsigned int i = 0; i < pl_datalength; i++) {
      c64->mem_->write_byte_no_io((pl_loadaddr+i),binary_p[pl_dataoffset+i]);
      if (i <= 8) printf("byte %d addr: $%04X data: $%02X\n", i, (pl_loadaddr+i), c64->mem_->read_byte_no_io(pl_loadaddr+i));
    }
    load_Psidplayer();
  } else {
    psid_init_driver();
  }
  c64->sid_->sid_flush();
  c64->sid_->set_playing(true);
  if (!pl_isrsid) {
    c64->cpu_->pc(c64->mem_->read_word_no_io(Memory::kAddrResetVector));
    c64->cpu_->irq();
  }
  return true;
}

bool callbackfn(void)
{
  if (load_sid())
    c64->callback(nullptr);
  return true;
}

extern "C" bool isrsid(void) { return (bool)pl_isrsid; }

/**
 * @brief start emudore SID tune player
 */
extern "C" void start_emudore_sidtuneplayer(
  // BUG: THE SID PLAYER STARTS TOO FAST FOR THE MACHINE TO FINISH BOOTING!!
  // TODO: THIS MUST COME FROM A CALLBACK! :)
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  size_t binsize_, int tuneno, bool run_continuously)
{

  (void) getFreeStack();
  (void) heapbefore();
  subtune = tuneno;

  do_sid(binary_, binsize_);

  if (c64 != nullptr) {
    D("Emudore already running!\n");
    return;
  }
  (void) heapbefore();
  midi_ = false; /* No need when playing tunes */
  c64 = new C64(
    nosdl_,isbinary_,havecart_,bankswlog,midi_,
    basic_,chargen_,kernal_,nullptr
  );

  if(!pl_isrsid) {
    c64->callback(callbackfn);
  } else {
    load_sid();
  }

  (void) heapafter();
  (void) getFreeStack();

  return;
}
#endif /* ONBOARD_SIDPLAYER */
#endif /* ONBOARD_EMULATOR */
