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
static bool bankswlog  = false; /* log bank switching */
/* Static IO/Memory variables */
static uint16_t load_addr, init_addr; /* Zero as signal it hasn't been changed */
static uint16_t aptr; /* pointer for counting addresses */
#endif

#ifdef ONBOARD_SIDPLAYER
static uint16_t pl_loadaddr;
static uint16_t pl_initaddr;
static uint16_t pl_playaddr;
static uint16_t pl_datalength;
static uint8_t *pl_databuffer;
static uint16_t pl_dataoffset;
static uint32_t pl_sidspeed;
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
    case 0: logmemrw  = true; break;
    case 1: logcia1rw = true; break;
    case 2: logcia2rw = true; break;
    case 3: logiorw   = true; break;
    case 4: logplarw  = true; break;
    case 5: logcrtrw  = true; break;
    case 6: logsidrw  = true; break;
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
    case 0: logmemrw  = false; break;
    case 1: logcia1rw = false; break;
    case 2: logcia2rw = false; break;
    case 3: logiorw   = false; break;
    case 4: logplarw  = false; break;
    case 5: logcrtrw  = false; break;
    case 6: logsidrw  = false; break;
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
 bankswlog  = false;
 return;
}

/**
 * @brief set c64 logging
 */
extern "C" void _set_logging(void)
{
  if(logmemrw) {c64->mem_->setlogrw(0);} else {c64->mem_->unsetlogrw(0);};
  if(logcia1rw){c64->mem_->setlogrw(1);} else {c64->mem_->unsetlogrw(1);};
  if(logcia2rw){c64->mem_->setlogrw(2);} else {c64->mem_->unsetlogrw(2);};
  if(logiorw)  {c64->mem_->setlogrw(3);} else {c64->mem_->unsetlogrw(3);};
  if(logplarw) {c64->mem_->setlogrw(4);} else {c64->mem_->unsetlogrw(4);};
  if(logcrtrw) {c64->mem_->setlogrw(5);} else {c64->mem_->unsetlogrw(5);};
  if(logsidrw) {c64->mem_->setlogrw(6);} else {c64->mem_->unsetlogrw(6);};
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
    delete c64;
    disable_all_logging();
    c64 = nullptr;
    (void) heapafter();
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
static void print_sid_info()
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
  cout << "Image length       : $" << hex << sidfile_->GetInitAddress() << " - $" << hex << (sidfile_->GetInitAddress() - 1) + sidfile_->GetDataLength() << endl;
  cout << "Load Address       : $" << hex << sidfile_->GetLoadAddress() << endl;
  cout << "Init Address       : $" << hex << sidfile_->GetInitAddress() << endl;
  cout << "Play Address       : $" << hex << sidfile_->GetPlayAddress() << endl;
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
  midi_ = true;
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
  if (run_continuously) {c64->start();};
  return;
}

/**
 * @brief start emudore SID tune player
 */
extern "C" void start_emudore_sidtuneplayer(
  uint8_t * basic_, uint8_t * chargen_,
  uint8_t * kernal_, uint8_t * binary_,
  size_t binsize_, bool run_continuously)
{
  (void) getFreeStack();
  (void) heapbefore();

  sidfile_ = new SidFile();
  if(sidfile_->ParsePtr(binary_,binsize_)) {
    DBG("[EMU] Error parsing sidfile!\n");
    delete sidfile_;
    return;
  }
  pl_songs = sidfile_->GetNumOfSongs();
  pl_song_number = sidfile_->GetFirstSong(); /* TODO: FIX N FINISH ;) */
  pl_sidflags = sidfile_->GetSidFlags();
  pl_sidspeed = sidfile_->GetSongSpeed(pl_song_number); // + 1);
  pl_curr_sidspeed = pl_sidspeed & (1 << pl_song_number); // ? 1 : 0;  // 1 ~ 60Hz, 2 ~ 50Hz
  pl_chiptype = sidfile_->GetChipType(1);
  pl_clockspeed = sidfile_->GetClockSpeed();
  pl_sidversion = sidfile_->GetSidVersion();
  pl_clock_speed = clockSpeed[pl_clockspeed];
  pl_raster_lines = scanLines[pl_clockspeed];
  pl_rasterrow_cycles = scanlinesCycles[pl_clockspeed];
  pl_frame_cycles = pl_raster_lines * pl_rasterrow_cycles;
  pl_refresh_rate = refreshRate[pl_clockspeed];
  pl_loadaddr = sidfile_->GetLoadAddress();
  pl_datalength = sidfile_->GetDataLength();
  /* pl_databuffer = sidfile_->GetDataPtr(); */
  pl_dataoffset = (sidfile_->GetDataOffset() + 2);
  // pl_databuffer = binary_+pl_dataoffset;
  pl_playaddr = sidfile_->GetPlayAddress();
  pl_initaddr = sidfile_->GetInitAddress();

  (void) heapafter();
  (void) getFreeStack();
  print_sid_info();
  delete sidfile_;
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
  (void) heapafter();

  // logging_enable(0); /* Memory RW */
  _set_logging(); /* Set c64 logging */
  // c64->mem_->setlogrw(6); /* Enable SID logging */

  // unsigned short binsize = 0x109f;

  // load_addr = aptr = read_short_le(binary_,0);
  // load_addr = aptr = pl_loadaddr; // $109f
  // 0x007c
  // size_t pos = (0x7C+2); /* pos starts at 2 after reading the load address at 0 and 1 */
  (void) heapbefore();
  // while(pos <= binsize) {
    // D("ADDR: $%04x POS: %d VAL: %02X\n",aptr,pos,binary_[pos]);
    // c64->mem_->write_byte(aptr++,binary_[pos++]);
  // }
  /* Load SID data into memory */
  // for (unsigned int i = 0; i < binsize; i++) {
    // c64->mem_->write_byte((load_addr+i),binary_[(0x7C+2)+i]);
  for (unsigned int i = 0; i < pl_datalength; i++) {
    // c64->mem_->write_byte((pl_loadaddr+i),pl_databuffer[i]);
    c64->mem_->write_byte_no_io((pl_loadaddr+i),binary_[pl_dataoffset+i]);
    // printf("MEM @ $%04x:$%02X FILE @ $%04x:$%02X\n",
      // pl_loadaddr+i, c64->mem_->read_byte_no_io((pl_loadaddr+i)),
      // pl_dataoffset+i, binary_[pl_dataoffset+i]);
  }
  printf("load: $%04X play: $%04X init: $%04X\n",
    pl_loadaddr, pl_playaddr, pl_initaddr);
  { /* Init SID player */
    // install reset vector for microplayer (0x0202)
    // c64->mem_->write_byte_no_io(0x0007, 0x78);               // 0x78 SEI disable interrupts
    c64->mem_->write_byte_no_io(0xFFFC, 0x02); /* addr lo */
    c64->mem_->write_byte_no_io(0xFFFD, 0x02); /* addr hi */

    // install IRQ vector for play routine launcher (0x020C)
    c64->mem_->write_byte_no_io(0xFFFE, 0x0C); /* addr lo */
    c64->mem_->write_byte_no_io(0xFFFF, 0x02); /* addr hi */

    // clear kernel and basic rom from ram
    // c64->mem_->write_byte_no_io(0x0001, 0x35);
    c64->pla_->runtime_bank_switching(0x1C); /* Mode 28 */

    // install the micro player, 6502 assembly code
    c64->mem_->write_byte_no_io(0x0202, 0xA9);               // 0xA9 LDA imm load A with the song number
    c64->mem_->write_byte_no_io(0x0203, 0);                  // 0xNN #NN song number

    c64->mem_->write_byte_no_io(0x0204, 0x20);               // 0x20 JSR abs jump sub to INIT routine
    c64->mem_->write_byte_no_io(0x0205, pl_initaddr & 0xFF);        // $xxNN init address lo
    c64->mem_->write_byte_no_io(0x0206, (pl_initaddr >> 8) & 0xFF); // $NNxx init address hi

    c64->mem_->write_byte_no_io(0x0207, 0x58);               // 0x58 CLI enable interrupts
    c64->mem_->write_byte_no_io(0x0208, 0xEA);               // 0xEA NOP impl
    c64->mem_->write_byte_no_io(0x0209, 0x4C);               // JMP jump to 0x0208
    c64->mem_->write_byte_no_io(0x020A, 0x08);               // 0xxxNN address lo
    c64->mem_->write_byte_no_io(0x020B, 0x02);               // 0xNNxx address hi

    c64->mem_->write_byte_no_io(0x020C, 0xEA);               // 0xEA NOP // 0xA9 LDA imm // A = 1
    c64->mem_->write_byte_no_io(0x020D, 0xEA);               // 0xEA NOP // 0x01 #NN
    c64->mem_->write_byte_no_io(0x020E, 0xEA);               // 0xEA NOP // 0x78 SEI disable interrupt
    c64->mem_->write_byte_no_io(0x020F, 0x20);               // 0x20 JSR jump sub to play routine
    c64->mem_->write_byte_no_io(0x0210, pl_playaddr & 0xFF);        // $xxNN play address lo
    c64->mem_->write_byte_no_io(0x0211, (pl_playaddr >> 8) & 0xFF); // $NNxx play address hi
    c64->mem_->write_byte_no_io(0x0212, 0xEA);               // 0xEA NOP // 0x58 CLI enable interrupt
    c64->mem_->write_byte_no_io(0x0213, 0x40);               // 0x40 RTI return from interrupt
  }
  // c64->emulate();
  // delete sidfile_;
  c64->sid_->sid_flush();
  c64->sid_->set_playing(true);
  c64->cpu_->pc(c64->mem_->read_word(Memory::kAddrResetVector));
  (void) heapafter();
  if (run_continuously) {c64->start();};
  return;
}
#endif /* ONBOARD_SIDPLAYER */
#endif /* ONBOARD_EMULATOR */
