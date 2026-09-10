/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * nsd.c
 * Network SID Device protocol state machine implementation
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
#include <nsd.h>
#include <net_ring.h>
#include <config.h>
#include <usbsid_constants.h>
#include <sid_cloneconfig.h>
#include <config_socket.h>
#include <bus.h>
#include <sid.h>
#include <logging.h>
#include <stdio.h>
#include <string.h>

/* Largest single NSD packet this device accepts. The reference client caps
 * itself at 1020 B of payload; 1024 gives slack without a heap allocation. */
#define NSD_MAX_PAYLOAD 1024

/* v5 TRY_WRITE_EX/TRY_READ_EX register address ranges (nsd.h). 0x0000-0x01ff
 * is SID 1-16, 0x20 per chip, the same convention TRY_WRITE's 8-bit register
 * byte already used for SID 1-8 (top bits = chip); 0xdf00-0xdfff is a real
 * OPL2's 256-register space at the C64 I/O address an FM OPL cartridge/clone
 * answers on. */
#define NSD_EX_SID_MAX  0x0200
#define NSD_EX_FMOPL_LO 0xDF00
#define NSD_EX_FMOPL_HI 0xDFFF

/* Reassembly state machine. One instance: `nsd_session_open()` refuses a
 * second concurrent session, so there is never more than one packet in
 * flight at a time. */
typedef struct {
  uint8_t  header[4];
  uint8_t  hdr_have;
  uint8_t  payload[NSD_MAX_PAYLOAD];
  uint16_t payload_len;
  uint16_t payload_have;
  bool     in_payload;
} nsd_reassembly_t;

static nsd_reassembly_t reasm;

/* Pending cycles queued in the write ring - the authoritative BUSY signal.
 * Incremented by the producer (core 0, in the command handlers below),
 * decremented by the consumer (core 1, `nsd_drain_task`). */
static volatile uint32_t nsd_pending_cycles = 0;
static volatile bool     nsd_ring_busy_latched = false;

/* Per-chip voice/digi mute mask, set by the MUTE command. Bits 0-2 are
 * voices 1-3, bit 3 is the volume/filter register (0x18). Indexed by the
 * chip number encoded in the top bits of the register byte (reg >> 5). */
static uint8_t nsd_mute_mask[8];

/* Number of SID chips the connected client believes it is driving, set by
 * TRY_SET_SID_COUNT. Never reprograms socket layout - purely bookkeeping
 * for TRY_RESET's volume-register sweep. */
static uint8_t nsd_sid_count = 1;

/* Telemetry-only copy of the last SET_SID_HEADER payload; nothing reads it
 * back today, it exists so the info is available if that changes later. */
static uint8_t nsd_psid_header[124];

/* Session state - single session at a time, enforced by the transport
 * layer (net_wifi.c / bluetooth.c) via `nsd_session_open()`. */
static const nsd_transport_t *g_transport = NULL;
static bool session_active = false;

/**
 * @brief Reset the byte-reassembly state machine
 */
static void nsd_reasm_reset(void)
{
  reasm.hdr_have = 0;
  reasm.payload_len = 0;
  reasm.payload_have = 0;
  reasm.in_payload = false;
}

/**
 * @brief Send an encoded response through the active transport
 */
static void nsd_send(const uint8_t *data, uint16_t len)
{
  if (g_transport && g_transport->send) {
    g_transport->send(g_transport->ctx, data, len);
  }
}

/**
 * @brief Update and read the ring backpressure latch
 *
 * Hysteresis: once `nsd_pending_cycles` crosses NSD_CYCLES_HIGH the latch
 * sticks until it drains back below NSD_CYCLES_LOW, so BUSY doesn't
 * chatter around the high threshold on every packet.
 */
static bool nsd_ring_busy(void)
{
  if (!nsd_ring_busy_latched && nsd_pending_cycles >= NSD_CYCLES_HIGH) {
    nsd_ring_busy_latched = true;
  }
  if (nsd_ring_busy_latched && nsd_pending_cycles <= NSD_CYCLES_LOW) {
    nsd_ring_busy_latched = false;
  }
  if (nsd_ring_busy_latched) return true;
  if (net_ring_free() < NSD_ENTRIES_MIN) return true;

  /* USB, MIDI or the onboard player currently own the bus - see bus.c.
   * NET is our own drain task, NONE is idle: neither blocks us. */
  bus_owner_t owner = bus_current_owner();
  return (owner != BUS_OWNER_NONE && owner != BUS_OWNER_NET);
}

/**
 * @brief Whether a write to `reg` is currently muted (MUTE command)
 */
static bool nsd_write_is_muted(uint8_t reg)
{
  uint8_t sid = (reg >> 5) & 0x07;
  uint8_t local = reg & 0x1F;
  uint8_t mask = nsd_mute_mask[sid];
  if (mask == 0) return false;
  if (local == 0x18) return (mask & 0x08) != 0;         /* volume/filter */
  if (local < 21) return (mask & (1u << (local / 7))) != 0; /* voice 1-3 */
  return false;
}

/**
 * @brief Find the socket a given SID base address (0x00/0x20/0x40/0x60)
 *        belongs to, for TRY_SET_SID_MODEL's clone check
 */
static bool nsd_chiptype_for_base(uint8_t base_address, uint8_t *chiptype_out)
{
  const Socket *sockets[2] = { &usbsid_config.socketOne, &usbsid_config.socketTwo };
  for (int i = 0; i < 2; i++) {
    if (sockets[i]->sid1.addr == base_address || sockets[i]->sid2.addr == base_address) {
      *chiptype_out = sockets[i]->chiptype;
      return true;
    }
  }
  return false;
}

/* ==================== Command handlers ====================
 * Every handler writes into `resp_buf` and returns the total response
 * length (including the leading response-code byte). */

static uint16_t handle_flush(uint8_t *resp_buf)
{
  net_ring_clear();
  nsd_pending_cycles = 0;
  nsd_ring_busy_latched = false;
  if (bus_try_claim(BUS_OWNER_NET)) { /* USB still wins, see bus.c */
    mute_sid();
    bus_release(BUS_OWNER_NET);
  }
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_try_set_sid_count(uint8_t sid_number, uint8_t *resp_buf)
{
  if (sid_number == 0 || sid_number > 8) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  uint8_t max_sids = cfg.numsids ? cfg.numsids : 1;
  nsd_sid_count = (sid_number < max_sids) ? sid_number : max_sids;
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_mute(uint8_t sid_number, const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len < 2) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  uint8_t voice = payload[0];
  uint8_t enable = payload[1];
  uint8_t sid = sid_number & 0x07;

  if (voice <= 3) {
    if (enable) nsd_mute_mask[sid] |= (uint8_t)(1u << voice);
    else        nsd_mute_mask[sid] &= (uint8_t)~(1u << voice);

    if (enable && voice <= 2 && bus_try_claim(BUS_OWNER_NET)) {
      uint8_t base = (uint8_t)(sid * 0x20);
      uint8_t vbase = (uint8_t)(voice * 7);
      cycled_write_operation((uint8_t)(base + 0x04 + vbase), 0x00, 0); /* control */
      cycled_write_operation((uint8_t)(base + 0x06 + vbase), 0x00, 0); /* sustain/release */
      bus_release(BUS_OWNER_NET);
    }
  }
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_try_reset(uint8_t volume, uint8_t *resp_buf)
{
  if (net_ring_count() > 0) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  if (!bus_try_claim(BUS_OWNER_NET)) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  reset_sid();
  for (uint8_t sid = 0; sid < nsd_sid_count; sid++) {
    cycled_write_operation((uint8_t)(sid * 0x20 + 0x18), volume, 0);
  }
  bus_release(BUS_OWNER_NET);
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_try_delay(uint8_t sid_number, uint16_t cycles, uint8_t *resp_buf)
{
  if (nsd_ring_busy() || net_ring_free() < 1) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  uint8_t sid = sid_number & 0x07;
  uint8_t quad[4] = {
    (uint8_t)(cycles >> 8), (uint8_t)(cycles & 0xFF),
    (uint8_t)(sid * 0x20 + 0x1E), 0x00
  };
  net_ring_push(quad);
  nsd_pending_cycles += cycles;
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_try_write(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len == 0 || (payload_len % 4) != 0) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  uint16_t n_quads = payload_len / 4;
  if (nsd_ring_busy() || net_ring_free() < n_quads) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  uint32_t added_cycles = 0;
  for (uint16_t i = 0; i < n_quads; i++) {
    const uint8_t *quad = &payload[i * 4];
    added_cycles += ((uint16_t)quad[0] << 8) | quad[1];
    if (nsd_write_is_muted(quad[2])) continue; /* dropped, but still "accepted" */
    net_ring_push(quad);
  }
  nsd_pending_cycles += added_cycles;
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

/**
 * @brief TRY_READ: n leading write quads, then a final {cyc_hi,cyc_lo,reg}
 *
 * Drains the ring first, then applies the leading writes and the read
 * directly, so the read observes every prior write. cycled_*_operation()
 * (bus.c) is spinlock-safe from either core.
 */
static uint16_t handle_try_read(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len < 3 || ((payload_len - 3) % 4) != 0) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  if (net_ring_count() > 0) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  if (!bus_try_claim(BUS_OWNER_NET)) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  uint16_t n_leading = (payload_len - 3) / 4;
  for (uint16_t i = 0; i < n_leading; i++) {
    const uint8_t *quad = &payload[i * 4];
    uint16_t cycles = ((uint16_t)quad[0] << 8) | quad[1];
    if (!nsd_write_is_muted(quad[2])) {
      cycled_write_operation(quad[2], quad[3], cycles);
    }
  }

  const uint8_t *tail = &payload[n_leading * 4];
  uint16_t read_cycles = ((uint16_t)tail[0] << 8) | tail[1];
  uint8_t reg = tail[2];
  uint8_t value = cycled_read_operation(reg, read_cycles);
  bus_release(BUS_OWNER_NET);

  resp_buf[0] = NSD_RESP_READ;
  resp_buf[1] = value;
  return 2;
}

/**
 * @brief Base bus address of the configured FMOpl socket, or 0 if none
 *
 * Same convention as midi_fmopl.c's fmopl_base_address() (cfg.fmopl_sid is
 * 1-based, 0 = disabled), duplicated to avoid pulling in midi_fmopl.h.
 */
static inline uint8_t nsd_fmopl_base(void)
{
  return (uint8_t)((cfg.fmopl_sid > 0 ? (cfg.fmopl_sid - 1) : 0) * 0x20);
}

/**
 * @brief TRY_WRITE_EX (19): v5 extended write, up to 16 SIDs or FM OPL
 *
 * Payload is 5*N bytes, each {delay_hi, delay_lo, addr_hi, addr_lo, value}.
 * addr 0x0000-0x01ff addresses SID 1-16 (0x20 per chip); this board only
 * has 4 real sockets, so chip 5-16 entries are accepted (bookkeeping only)
 * but never touch the bus. addr 0xdf00-0xdfff becomes the two-port
 * index/data write a real OPL2 wants (opl_write(), midi_fmopl.c) when
 * cfg.fmopl_enabled, dropped (bookkeeping only) otherwise.
 */
static uint16_t handle_try_write_ex(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len == 0 || (payload_len % 5) != 0) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  uint16_t n_entries = payload_len / 5;

  /* Pre-count net_ring slots needed: a SID entry is one write, an FMOpl
   * entry is two (index port, data port), unsupported entries need none. */
  uint16_t n_slots = 0;
  for (uint16_t i = 0; i < n_entries; i++) {
    const uint8_t *e = &payload[i * 5];
    uint16_t addr = (uint16_t)((e[2] << 8) | e[3]);
    if (addr < NSD_EX_SID_MAX) {
      if (addr < 0x80) n_slots += 1; /* chip 0-3, real hardware */
    } else if (addr >= NSD_EX_FMOPL_LO && addr <= NSD_EX_FMOPL_HI) {
      if (cfg.fmopl_enabled) n_slots += 2;
    }
  }
  if (nsd_ring_busy() || net_ring_free() < n_slots) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  uint32_t added_cycles = 0;
  uint8_t fmopl_base = nsd_fmopl_base();
  for (uint16_t i = 0; i < n_entries; i++) {
    const uint8_t *e = &payload[i * 5];
    uint16_t delay = (uint16_t)((e[0] << 8) | e[1]);
    uint16_t addr  = (uint16_t)((e[2] << 8) | e[3]);
    uint8_t  value = e[4];
    added_cycles += delay;

    if (addr < NSD_EX_SID_MAX) {
      if (addr >= 0x80) continue; /* SID 5-16: no such socket on this board */
      uint8_t reg = (uint8_t)addr;
      if (nsd_write_is_muted(reg)) continue;
      uint8_t quad[4] = { (uint8_t)(delay >> 8), (uint8_t)(delay & 0xFF), reg, value };
      net_ring_push(quad);
    } else if (addr >= NSD_EX_FMOPL_LO && addr <= NSD_EX_FMOPL_HI) {
      if (!cfg.fmopl_enabled) continue;
      uint8_t reg = (uint8_t)(addr & 0xFF);
      /* Index port write honours the caller's own delay; the data port
       * write follows it after a short settle, the same 10 cycles
       * opl_write() (midi_fmopl.c) uses for both halves of this dance. */
      uint8_t quad1[4] = { (uint8_t)(delay >> 8), (uint8_t)(delay & 0xFF), fmopl_base, reg };
      uint8_t quad2[4] = { 0x00, 0x0A, (uint8_t)(fmopl_base + 0x10), value };
      net_ring_push(quad1);
      net_ring_push(quad2);
    }
    /* else: neither range - not a register this protocol version defines;
     * accepted (bookkeeping only), same as an unsupported SID/OPL entry. */
  }
  nsd_pending_cycles += added_cycles;
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

/**
 * @brief TRY_READ_EX (20): v5 extended read, mirrors handle_try_read()'s
 * drain-then-apply approach over the wider addr_ex register space.
 */
static uint16_t handle_try_read_ex(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len < 4 || ((payload_len - 4) % 5) != 0) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  if (net_ring_count() > 0) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  if (!bus_try_claim(BUS_OWNER_NET)) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  uint8_t fmopl_base = nsd_fmopl_base();
  uint16_t n_leading = (payload_len - 4) / 5;
  for (uint16_t i = 0; i < n_leading; i++) {
    const uint8_t *e = &payload[i * 5];
    uint16_t cycles = (uint16_t)((e[0] << 8) | e[1]);
    uint16_t addr   = (uint16_t)((e[2] << 8) | e[3]);
    uint8_t  value  = e[4];
    if (addr < NSD_EX_SID_MAX) {
      if (addr >= 0x80) continue;
      uint8_t reg = (uint8_t)addr;
      if (!nsd_write_is_muted(reg)) cycled_write_operation(reg, value, cycles);
    } else if (addr >= NSD_EX_FMOPL_LO && addr <= NSD_EX_FMOPL_HI && cfg.fmopl_enabled) {
      uint8_t reg = (uint8_t)(addr & 0xFF);
      cycled_write_operation(fmopl_base, reg, cycles);
      cycled_write_operation((uint8_t)(fmopl_base + 0x10), value, 10);
    }
  }

  const uint8_t *tail = &payload[n_leading * 5];
  uint16_t read_cycles = (uint16_t)((tail[0] << 8) | tail[1]);
  uint16_t read_addr   = (uint16_t)((tail[2] << 8) | tail[3]);
  uint8_t value;
  if (read_addr < NSD_EX_SID_MAX && read_addr < 0x80) {
    value = cycled_read_operation((uint8_t)read_addr, read_cycles);
  } else if (read_addr >= NSD_EX_FMOPL_LO && read_addr <= NSD_EX_FMOPL_HI && cfg.fmopl_enabled) {
    /* A real OPL2 only ever answers a status byte on the index port,
     * regardless of which register number was nominally addressed - there
     * is no per-register readback on the real chip. */
    value = cycled_read_operation(fmopl_base, read_cycles);
  } else {
    value = 0xFF; /* nothing on this board answers this address - open bus */
  }
  bus_release(BUS_OWNER_NET);

  resp_buf[0] = NSD_RESP_READ;
  resp_buf[1] = value;
  return 2;
}

static uint16_t handle_get_version(uint8_t *resp_buf)
{
  resp_buf[0] = NSD_RESP_VERSION;
  resp_buf[1] = NSD_PROTOCOL_VERSION;
  return 2;
}

static uint16_t handle_try_set_clock(uint8_t which, uint8_t *resp_buf)
{
  if (net_ring_count() > 0) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  if (!bus_try_claim(BUS_OWNER_NET)) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  /* apply_clockrate() takes an index into sid_defs.h's clockrates[]
   * { DEFAULT, PAL, NTSC, DREAN, NTSC2 }, not a raw Hz value. */
  apply_clockrate(which == 1 ? 2 /* NTSC */ : 1 /* PAL */, true);
  bus_release(BUS_OWNER_NET);
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_get_config_count(uint8_t *resp_buf)
{
  resp_buf[0] = NSD_RESP_COUNT;
  resp_buf[1] = 2; /* 6581, 8580 */
  return 2;
}

static uint16_t handle_get_config_info(uint8_t sid_number, uint8_t *resp_buf)
{
  resp_buf[0] = NSD_RESP_INFO;
  const char *name;
  if ((sid_number & 1) == 0) {
    resp_buf[1] = 0; /* 6581 */
    name = "USBSID-Pico (6581)";
  } else {
    resp_buf[1] = 1; /* 8580 */
    name = "USBSID-Pico (8580)";
  }
  size_t name_len = strlen(name) + 1; /* include NUL */
  memcpy(&resp_buf[2], name, name_len);
  return (uint16_t)(2 + name_len);
}

static uint16_t handle_try_set_sid_model(uint8_t sid_number, const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len >= 1) {
    uint8_t base_address = (uint8_t)((sid_number & 0x03) * 0x20);
    uint8_t chiptype;
    if (nsd_chiptype_for_base(base_address, &chiptype)
      && chiptype != CHIP_REAL && chiptype != CHIP_UNKNOWN) {
      /* set_sidemu_sidtype() (sid_cloneconfig.c) is itself unfinished
       * upstream; the NSD model index (0=6581, 1=8580) is passed through. */
      if (bus_try_claim(BUS_OWNER_NET)) {
        set_sidemu_sidtype(base_address, payload[0]);
        bus_release(BUS_OWNER_NET);
      }
    }
  }
  resp_buf[0] = NSD_RESP_OK; /* Always OK, even when no-op */
  return 1;
}

static uint16_t handle_set_sid_header(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  uint16_t to_copy = (payload_len < sizeof(nsd_psid_header)) ? payload_len : sizeof(nsd_psid_header);
  if (to_copy > 0) memcpy(nsd_psid_header, payload, to_copy);
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

/**
 * @brief TRY_SET_FM_OPL (21): enable/disable the board's FM OPL socket
 *
 * Mirrors config.c's BOARD_FMOPL command: payload[0] is the enable flag,
 * payload[1] is the 1-4 SID number to assign, required when enabling.
 * Requires the write ring drained first (like TRY_RESET/TRY_SET_CLOCK),
 * since it changes how TRY_WRITE_EX routes FMOpl-range entries.
 */
static uint16_t handle_try_set_fm_opl(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len < 1) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  if (net_ring_count() > 0) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  bool ok;
  if (payload[0] == 0) {
    ok = set_fmopl_sidno(0);  /* Clear any assigned FMOpl SID */
  } else if (payload_len >= 2) {
    ok = set_fmopl_sidno((int)payload[1]);
  } else {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }

  if (!ok) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  apply_fmopl_config(&cfg);
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

/**
 * @brief Process one fully-reassembled NSD command packet
 */
static void process_nsd_command(const uint8_t *header, const uint8_t *payload, uint16_t payload_len)
{
  uint8_t cmd = header[0];
  uint8_t sid_number = header[1];
  uint8_t resp_buf[128];
  uint16_t resp_len;

  switch (cmd) {
    case NSD_CMD_FLUSH:
      resp_len = handle_flush(resp_buf);
      break;
    case NSD_CMD_TRY_SET_SID_COUNT:
      resp_len = handle_try_set_sid_count(sid_number, resp_buf);
      break;
    case NSD_CMD_MUTE:
      resp_len = handle_mute(sid_number, payload, payload_len, resp_buf);
      break;
    case NSD_CMD_TRY_RESET:
      resp_len = handle_try_reset(payload_len >= 1 ? payload[0] : 0, resp_buf);
      break;
    case NSD_CMD_TRY_DELAY:
      if (payload_len < 2) { resp_buf[0] = NSD_RESP_ERROR; resp_len = 1; break; }
      resp_len = handle_try_delay(sid_number, (uint16_t)((payload[0] << 8) | payload[1]), resp_buf);
      break;
    case NSD_CMD_TRY_WRITE:
      resp_len = handle_try_write(payload, payload_len, resp_buf);
      break;
    case NSD_CMD_TRY_READ:
      resp_len = handle_try_read(payload, payload_len, resp_buf);
      break;
    case NSD_CMD_TRY_WRITE_EX:
      resp_len = handle_try_write_ex(payload, payload_len, resp_buf);
      break;
    case NSD_CMD_TRY_READ_EX:
      resp_len = handle_try_read_ex(payload, payload_len, resp_buf);
      break;
    case NSD_CMD_GET_VERSION:
      resp_len = handle_get_version(resp_buf);
      break;
    case NSD_CMD_TRY_SET_SAMPLING:      /* reSID software-emulation concept, accept and no-op */
    case NSD_CMD_SET_SID_POSITION:
    case NSD_CMD_SET_SID_LEVEL:
    case NSD_CMD_SET_DELAY:
    case NSD_CMD_SET_FADE_IN:
    case NSD_CMD_SET_FADE_OUT:
      resp_buf[0] = NSD_RESP_OK;
      resp_len = 1;
      break;
    case NSD_CMD_TRY_SET_CLOCK:
      if (payload_len < 1) { resp_buf[0] = NSD_RESP_ERROR; resp_len = 1; break; }
      resp_len = handle_try_set_clock(payload[0], resp_buf);
      break;
    case NSD_CMD_GET_CONFIG_COUNT:
      resp_len = handle_get_config_count(resp_buf);
      break;
    case NSD_CMD_GET_CONFIG_INFO:
      resp_len = handle_get_config_info(sid_number, resp_buf);
      break;
    case NSD_CMD_TRY_SET_SID_MODEL:
      resp_len = handle_try_set_sid_model(sid_number, payload, payload_len, resp_buf);
      break;
    case NSD_CMD_SET_SID_HEADER:
      resp_len = handle_set_sid_header(payload, payload_len, resp_buf);
      break;
    case NSD_CMD_TRY_SET_FM_OPL:
      resp_len = handle_try_set_fm_opl(payload, payload_len, resp_buf);
      break;
    default:
      resp_buf[0] = NSD_RESP_ERROR;
      resp_len = 1;
      break;
  }

  nsd_send(resp_buf, resp_len);
}

/**
 * @brief Feed incoming transport bytes to the NSD reassembly state machine
 *
 * Byte at a time, so a TCP/RFCOMM split mid-packet doesn't matter. Always
 * consumes everything given.
 *
 * @param data incoming bytes
 * @param len number of bytes
 * @return number of bytes consumed (always equal to `len`)
 */
uint16_t nsd_feed(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++) {
    uint8_t byte = data[i];

    if (!reasm.in_payload) {
      reasm.header[reasm.hdr_have++] = byte;
      if (reasm.hdr_have < 4) continue;

      reasm.payload_len = (uint16_t)((reasm.header[2] << 8) | reasm.header[3]);
      if (reasm.payload_len > NSD_MAX_PAYLOAD) {
        /* Client protocol violation - nothing sane to resync on but the
         * next header, so just drop this framing attempt. */
        nsd_reasm_reset();
        continue;
      }
      if (reasm.payload_len == 0) {
        process_nsd_command(reasm.header, NULL, 0);
        nsd_reasm_reset();
      } else {
        reasm.payload_have = 0;
        reasm.in_payload = true;
      }
      continue;
    }

    reasm.payload[reasm.payload_have++] = byte;
    if (reasm.payload_have == reasm.payload_len) {
      process_nsd_command(reasm.header, reasm.payload, reasm.payload_len);
      nsd_reasm_reset();
    }
  }

  return len;
}

/**
 * @brief Initialize the NSD module
 */
void nsd_init(void)
{
  net_ring_init();
  nsd_reasm_reset();
  nsd_pending_cycles = 0;
  nsd_ring_busy_latched = false;
  nsd_sid_count = 1;
  memset(nsd_mute_mask, 0, sizeof(nsd_mute_mask));
  memset(nsd_psid_header, 0, sizeof(nsd_psid_header));
  g_transport = NULL;
  session_active = false;
}

/**
 * @brief Open an NSD session via a transport
 */
bool nsd_session_open(const nsd_transport_t *t)
{
  if (session_active) {
    return false; /* Already have a session - caller must ERROR + close */
  }
  g_transport = t;
  session_active = true;
  nsd_reasm_reset();
  return true;
}

/**
 * @brief Close the current NSD session
 */
void nsd_session_close(void)
{
  session_active = false;
  g_transport = NULL;
  net_ring_clear();
  nsd_pending_cycles = 0;
  nsd_ring_busy_latched = false;
  if (bus_current_owner() == BUS_OWNER_NET) {
    bus_release(BUS_OWNER_NET);
  }
}

/**
 * @brief Check if an NSD session is active
 */
bool nsd_session_is_active(void)
{
  return session_active;
}

/**
 * @brief Core 1 task to drain the NSD write ring
 *
 * Called every core 1 loop iteration. Blocking here is fine: core 1 has no
 * latency obligations while nothing else is queued.
 */
void nsd_drain_task(void)
{
  if (net_ring_count() == 0) return;
  if (!bus_try_claim(BUS_OWNER_NET)) return; /* USB (or MIDI/player) is active, try again next loop */

  uint8_t quad[4];
  while (net_ring_pop(quad)) {
    uint16_t cycles = ((uint16_t)quad[0] << 8) | quad[1];
    cycled_write_operation(quad[2], quad[3], cycles);
    if (nsd_pending_cycles > cycles) nsd_pending_cycles -= cycles;
    else nsd_pending_cycles = 0;
  }

  bus_release(BUS_OWNER_NET);
}
