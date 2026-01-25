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
#include <reloc65.h>
#include <util.h>


extern "C" {
  #include "globals.h"
  #include "logging.h"
}


#ifdef ONBOARD_EMULATOR
/* Shared memory */
extern "C" uint8_t c64memory[0x10000];

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
static bool logvicrw   = false; /* log vic io writes */
static bool loginstr   = false; /* log cpu instructions */
static bool logtimings = false; /* log cpu instructions */
static bool bankswlog  = false; /* log bank switching */
/* Static IO/Memory variables */
static uint16_t load_addr, init_addr; /* Zero as signal it hasn't been changed */
static uint16_t aptr; /* pointer for counting addresses */
#endif


/* Machine */
#ifdef ONBOARD_EMULATOR
C64 *c64_ = nullptr;
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
    case 8: logvicrw    = true; break;
    case 9: loginstr    = true; break;
    case 10:logtimings  = true; break;
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
    case 8: logvicrw    = false; break;
    case 9: loginstr    = false; break;
    case 10:logtimings  = false; break;
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
 logvicrw   = false;
 loginstr   = false;
 logtimings = false;
 return;
}

/**
 * @brief set c64 logging
 */
extern "C" void _set_logging(void)
{
  if(logmemrw)   {c64_->mem_->setlogrw(0);} else {c64_->mem_->unsetlogrw(0);};
  if(logcia1rw)  {c64_->mem_->setlogrw(1);} else {c64_->mem_->unsetlogrw(1);};
  if(logcia2rw)  {c64_->mem_->setlogrw(2);} else {c64_->mem_->unsetlogrw(2);};
  if(logiorw)    {c64_->mem_->setlogrw(3);} else {c64_->mem_->unsetlogrw(3);};
  if(logplarw)   {c64_->mem_->setlogrw(4);} else {c64_->mem_->unsetlogrw(4);};
  if(logcrtrw)   {c64_->mem_->setlogrw(5);} else {c64_->mem_->unsetlogrw(5);};
  if(logsidrw)   {c64_->mem_->setlogrw(6);} else {c64_->mem_->unsetlogrw(6);};
  if(logsidiorw) {c64_->mem_->setlogrw(7);} else {c64_->mem_->unsetlogrw(7);};
  if(logvicrw)   {c64_->mem_->setlogrw(8);} else {c64_->mem_->unsetlogrw(8);};
  if(loginstr)   {c64_->cpu_->loginstructions = true;} else {c64_->cpu_->loginstructions = false;};
  if(logtimings) {C64::log_timings = true;} else {C64::log_timings = false;};
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
  if (c64_ != nullptr) {
    D("Emudore already running!\n");
    return;
  }
  (void) heapbefore();
  midi_ = true;
  c64_ = new C64(
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
    c64_->mem_->write_byte_no_io(aptr++,binary_[pos++]);
  }

  /* basic-tokenized prg */
  if(load_addr == kBasicPrgStart)
  {
    /* make BASIC happy */
    c64_->mem_->write_word_no_io(kBasicTxtTab,load_addr);
    c64_->mem_->write_word_no_io(kBasicVarTab,aptr);
    c64_->mem_->write_word_no_io(kBasicAryTab,aptr);
    c64_->mem_->write_word_no_io(kBasicStrEnd,aptr);
    if (init_addr == 0x0) {
      init_addr = ((c64_->mem_->read_byte_no_io(load_addr)|c64_->mem_->read_byte_no_io(load_addr+0x1)<<8)+0x2);
      D("1. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
      c64_->cpu_->pc(init_addr);
    } else {
      D("2. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
      c64_->cpu_->pc(init_addr); /* Example: Cynthcart start/init address is 0x80D */
    }
  }
  else {
    init_addr = ((c64_->mem_->read_byte_no_io(load_addr)|c64_->mem_->read_byte_no_io(load_addr+0x1)<<8)+0x2);
    D("3. load_addr %04X calculated init_addr %04X\n", load_addr, init_addr);
    c64_->cpu_->pc(init_addr);
  }

  (void) heapafter();
  if (run_continuously) {c64_->start();};
  return;
}

extern "C" unsigned int run_emulation_cycle(void)
{
  return c64_->emulate();
}

extern "C" unsigned int run_specified_cycle(
  bool callback,
  bool cpu, bool cia1, bool cia2,
  bool vic, bool io,   bool cart)
{
  return c64_->emulate_specified(callback,cpu,cia1,cia2,vic,io,cart);
}

/**
 * @brief stop Emudore and free up memory
 */
extern "C" bool stop_emudore(void)
{
  D("Stopping emudore\n");
  (void) heapbefore();
  if (c64_ != nullptr) {
    if (c64_->is_looping()) {
      D("Disabling emudore loop\n");
      c64_->disable_looping();
    }
    D("Stopping c64\n");
    disable_all_logging();
    C64::log_timings = false;
    C64::is_cynthcart = false;
    C64::is_rsid = false;
    _set_logging();
    delete c64_;
    (void) heapafter();
    memset(c64memory, 0, 0x10000);
    return true;
  }
  (void) heapafter();
  return false;
}

extern "C" bool is_looping(void)
{
  if (c64_ != nullptr)
    return c64_->is_looping();
  else return false;
}

extern "C" bool disable_looping(void)
{
  if (c64_ != nullptr)
    return c64_->disable_looping();
  else return false;
}

#endif /* ONBOARD_EMULATOR */
