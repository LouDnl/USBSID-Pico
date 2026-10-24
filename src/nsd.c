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
#include <bus.h>
#include <pio.h>
#include <sid.h>
#include <logging.h>
#include <stdio.h>
#include <string.h>

/* Largest single NSD packet this device accepts. The reference client caps
 * itself at 1020 B of payload; 1024 gives slack without a heap allocation. */
#define NSD_MAX_PAYLOAD 1024

/* TRY_SET_SID_COUNT maximum: TRY_WRITE_EX/TRY_READ_EX address 16 SIDs */
#define NSD_SID_MAX     16
/* SID chip selects on this board, chip 5-16 accesses keep only their delay */
#define NSD_BOARD_SIDS  4

/* Largest delay one bus write holds, signed for carry arithmetic */
#define NSD_DELAY_MAX   0xFFFF
/* Least delay a filler split leaves for the write after the filler */
#define NSD_DELAY_REST  32
/* Floor for a negative carry, bounds an endless run of short FM pairs */
#define NSD_DEBT_MIN    (-16 * NSD_DELAY_MAX)
/* FM OPL index port to data port gap in cycles, as opl_write() */
#define NSD_FMOPL_GAP   10
/* Ring register byte flag: write as is, no mute check (fillers, FM OPL) */
#define NSD_RING_RAW    0x80

/* Longest wait for queued bus writes to finish before TRY_RESET or
 * TRY_SET_CLOCK go ahead anyway: 4 PIO FIFO entries plus one in progress
 * of NSD_DELAY_MAX cycles each take ~333 ms PAL */
#define NSD_SETTLE_US   500000u

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
  /* payload_len (from the header) exceeded NSD_MAX_PAYLOAD: ERROR was
   * already sent at header time (see nsd_feed()), and the payload bytes are
   * counted but not stored, since there is nowhere to put them. */
  bool     oversized;
} nsd_reassembly_t;

static nsd_reassembly_t reasm;

/* Cycle accounting for the write ring - the authoritative BUSY signal, see
 * nsd_pending_cycles(). One writer per counter, no shared read-modify-write;
 * all three free-run and compare wrap-safe, like the net_ring.c indices.
 * nsd_cycles_in:   core 0 only, raised by each entry before its push.
 * nsd_cycles_out:  core 1 only, raised by the cycles of each entry once
 *                  written, dropped, or spent by a later write (muted
 *                  entry carry). Never leads nsd_cycles_in.
 * nsd_cycles_mark: core 0 only, nsd_cycles_in at the last ring reset; the
 *                  cycles below it belong to dropped entries. */
static uint32_t          nsd_cycles_in = 0;
static volatile uint32_t nsd_cycles_out = 0;
static uint32_t          nsd_cycles_mark = 0;
static volatile bool     nsd_ring_busy_latched = false;

/* Delay not handed to the bus yet, core 0 only, see nsd_lead(). Positive:
 * cycles of delay-only accesses, at most NSD_DELAY_MAX, added to the
 * following bus write. Negative: cycles an FM OPL pair ran past its delay,
 * taken off the following delays. Pending audio time like the queued entries. */
static int32_t nsd_carry = 0;

/* Core 1 only: cycles of entries muted at play time, added to the
 * following entry. Counted into nsd_cycles_out when spent or dropped. */
static uint32_t nsd_drain_carry = 0;

/* nsd_bus_settled() wait window, core 0 only */
static bool     nsd_settle_waiting = false;
static uint64_t nsd_settle_since = 0;
static uint64_t nsd_settle_last = 0;

/* Per-chip voice/digi mute mask, set by the MUTE command. Bits 0-2 are
 * voices 1-3, bit 3 is the volume/filter register (0x18). Indexed by the
 * chip number encoded in the top bits of the register byte (reg >> 5).
 * Written by core 0 only, read by core 1 per entry. nsd_mute_gen counts
 * mask changes, raised after the mask store. */
static volatile uint8_t nsd_mute_mask[NSD_BOARD_SIDS];
static volatile uint8_t nsd_mute_gen = 0;

/* Number of SID chips the connected client believes it is driving, set by
 * TRY_SET_SID_COUNT. Never reprograms socket layout - purely bookkeeping
 * for TRY_RESET's volume-register sweep. */
static uint8_t nsd_sid_count = 1;

/* Telemetry-only copy of the last SET_SID_HEADER payload; nothing reads it
 * back today, it exists so the info is available if that changes later. */
static uint8_t nsd_psid_header[124];

/* TRY_SET_FM_OPL's own on/off state (spec: "enable or disable FM OPL for
 * the current connection... disabled by default on a new connection").
 * Deliberately separate from cfg.fmopl_enabled/cfg.fmopl_sid, which record
 * whether a physical socket is wired as FM OPL at all - a board-owner
 * config decision (BOARD_FMOPL) this connection-scoped command must never
 * change. TRY_WRITE_EX/TRY_READ_EX's FMOpl handling below gates on both:
 * cfg.fmopl_enabled says the hardware exists, this says the current NSD
 * connection has turned it on. */
static bool nsd_fmopl_active = false;

/* Session state - single session at a time, enforced by the transport
 * layer (net_wifi.c / net_bluetooth.c) via `nsd_session_open()`. */
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
  reasm.oversized = false;
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
 * @brief PHI1 delay cycles of a ring entry {cyc_hi, cyc_lo, reg, data}
 *
 * @param quad ring entry
 * @return uint16_t delay cycles
 */
static inline uint16_t nsd_quad_cycles(const uint8_t quad[4])
{
  return (uint16_t)(((uint16_t)quad[0] << 8) | quad[1]);
}

/**
 * @brief Cycles queued in the write ring, core 0 only
 *
 * Counts the entry core 1 is writing until it returns, and the carry not
 * pushed yet (nsd_carry). Excludes entries dropped by nsd_ring_reset(),
 * also before core 1 has popped them.
 *
 * @return uint32_t queued PHI1 cycles
 */
static uint32_t nsd_pending_cycles(void)
{
  uint32_t out = nsd_cycles_out;
  /* Dropped entries not popped yet hold nsd_cycles_out below the mark */
  if ((int32_t)(out - nsd_cycles_mark) < 0) out = nsd_cycles_mark;
  int32_t pending = (int32_t)(nsd_cycles_in - out) + nsd_carry;
  return (pending > 0) ? (uint32_t)pending : 0;
}

/**
 * @brief Drop all queued entries, their cycles and the carry, core 0 only
 *
 * Core 1 keeps running: it pops the dropped entries without writing them
 * and counts their cycles into nsd_cycles_out, up to nsd_cycles_mark.
 */
static void nsd_ring_reset(void)
{
  net_ring_clear();
  nsd_cycles_mark = nsd_cycles_in;
  nsd_carry = 0;
  nsd_ring_busy_latched = false;
}

/**
 * @brief Count an entry's cycles, then push it to the write ring
 *
 * Raise nsd_cycles_in first: core 1 may pop and count the entry as soon as
 * it is pushed. Caller checks net_ring_free() first.
 *
 * @param quad ring entry {cyc_hi, cyc_lo, reg, data}
 */
static void nsd_queue(const uint8_t quad[4])
{
  uint16_t cycles = nsd_quad_cycles(quad);
  nsd_cycles_in += cycles;
  if (!net_ring_push(quad)) {
    nsd_cycles_in -= cycles; /* Not pushed, never visible to core 1 */
  }
}

/**
 * @brief Update and read the ring backpressure latch
 *
 * Hysteresis: once nsd_pending_cycles() crosses NSD_CYCLES_HIGH the latch
 * sticks until it drains back below NSD_CYCLES_LOW, so BUSY doesn't
 * chatter around the high threshold on every packet. An empty ring always
 * clears the latch.
 */
static bool nsd_ring_busy(void)
{
  uint32_t pending = nsd_pending_cycles();
  if (!nsd_ring_busy_latched && pending >= NSD_CYCLES_HIGH) {
    nsd_ring_busy_latched = true;
  }
  if (nsd_ring_busy_latched && pending <= NSD_CYCLES_LOW) {
    nsd_ring_busy_latched = false;
  }
  if (nsd_ring_busy_latched && net_ring_count() == 0) {
    /* Nothing queued: drop the latch, count no cycles as pending */
    nsd_ring_busy_latched = false;
    nsd_cycles_mark = nsd_cycles_in;
  }
  if (nsd_ring_busy_latched) return true;
  if (net_ring_free() < NSD_ENTRIES_MIN) return true;

  /* Hold off while USB, MIDI or the onboard player actively own the bus.
   * Same rule as the bus_try_claim(BUS_OWNER_NET) in nsd_drain_task().
   * Lock free first: a free or NET owned bus needs no idle time check. */
  bus_owner_t owner = bus_owner_peek();
  if ((owner == BUS_OWNER_NONE) || (owner == BUS_OWNER_NET)) return false;
  return bus_held_by_other(BUS_OWNER_NET);
}

/**
 * @brief Whether a write to `reg` is currently muted (MUTE command)
 *
 * @param reg SID address of an active chip, 0x20 per chip
 * @return true when the MUTE mask drops the write
 */
static bool nsd_write_is_muted(uint16_t reg)
{
  if (reg >= (NSD_BOARD_SIDS * 0x20)) return false;
  uint8_t local = (uint8_t)(reg & 0x1F);
  uint8_t mask = nsd_mute_mask[reg >> 5];
  if (mask == 0) return false;
  if (local == 0x18) return (mask & 0x08) != 0;         /* volume/filter */
  if (local < 21) return (mask & (1u << (local / 7))) != 0; /* voice 1-3 */
  return false;
}

/**
 * @brief Whether a SID address maps to an enabled chip select
 *
 * Same rule as set_bus_bits() (bus.c): CS 0b110/0b111 drops the write
 * and its delay. Chips at or above cfg.numsids, and SID 5-16
 * (addr >= 0x80), have no socket on this board.
 *
 * @param addr SID address, 0x20 per chip
 * @return true when a bus write to `addr` reaches a socket
 */
static bool nsd_socket_active(uint16_t addr)
{
  uint16_t chip = (uint16_t)(addr >> 5);
  if (chip >= NSD_BOARD_SIDS || chip >= cfg.numsids) return false;
  uint8_t cs;
  switch (chip) {
    case 0: cs = cfg.one; break;
    case 1: cs = cfg.two; break;
    case 2: cs = cfg.three; break;
    default: cs = cfg.four; break;
  }
  return (cs != 0b110 && cs != 0b111);
}

/**
 * @brief Whether the board owner wired chip `chip` as FM OPL
 *
 * Same convention as midi_fmopl.c's fmopl_base_address(): cfg.fmopl_sid
 * is 1-based, 0 = disabled.
 *
 * @param chip chip number, addr >> 5
 * @return true for the FM OPL socket
 */
static bool nsd_fmopl_chip(uint16_t chip)
{
  return (cfg.fmopl_enabled && cfg.fmopl_sid > 0
          && chip == (uint16_t)(cfg.fmopl_sid - 1));
}

/**
 * @brief Base bus address of the configured FMOpl socket, or 0 if none
 *
 * Duplicated from midi_fmopl.c's fmopl_base_address() to avoid pulling in
 * midi_fmopl.h.
 */
static inline uint8_t nsd_fmopl_base(void)
{
  return (uint8_t)((cfg.fmopl_sid > 0 ? (cfg.fmopl_sid - 1) : 0) * 0x20);
}

/**
 * @brief Whether the FM OPL socket exists and its chip select is enabled
 */
static bool nsd_fmopl_socket_active(void)
{
  return (cfg.fmopl_enabled && cfg.fmopl_sid > 0
          && nsd_socket_active(nsd_fmopl_base()));
}

/**
 * @brief Whether a SID address reaches a SID chip
 *
 * An active socket that is not the FM OPL socket: SID register writes
 * there would hit the OPL index and data ports.
 *
 * @param addr SID address, 0x20 per chip
 * @return true when a SID access to `addr` reaches the bus
 */
static bool nsd_sid_active(uint16_t addr)
{
  return nsd_socket_active(addr) && !nsd_fmopl_chip((uint16_t)(addr >> 5));
}

/**
 * @brief Hand one bus write to the write ring or to the bus
 *
 * @param reg register byte, NSD_RING_RAW allowed
 * @param data data byte
 * @param cycles delay cycles before the write
 * @param direct true: write at once, caller owns the bus; false: queue
 */
static void nsd_emit(uint8_t reg, uint8_t data, uint16_t cycles, bool direct)
{
  if (direct) {
    cycled_write_operation((uint8_t)(reg & 0x7F), data, cycles);
    return;
  }
  uint8_t quad[4] = {
    (uint8_t)(cycles >> 8), (uint8_t)(cycles & 0xFF), reg, data
  };
  nsd_queue(quad);
}

/**
 * @brief Register byte for a filler write, a write that only paces a delay
 *
 * Register 0x1E of an active SID, the chip of `hint` first. Without an
 * active SID the FM OPL index port: an index write sets no OPL register,
 * every OPL write sets the index before its data. 0x1E of SID 1 when
 * nothing is active, set_bus_bits() drops it.
 *
 * @param hint SID address the delay belongs to
 * @return uint8_t register byte
 */
static uint8_t nsd_filler_reg(uint16_t hint)
{
  if (nsd_sid_active(hint)) return (uint8_t)((hint & 0x60) | 0x1E);
  for (uint8_t base = 0x00; base < (NSD_BOARD_SIDS * 0x20); base += 0x20) {
    if (nsd_sid_active(base)) return (uint8_t)(base | 0x1E);
  }
  if (nsd_fmopl_socket_active()) return nsd_fmopl_base();
  return 0x1E;
}

/**
 * @brief Pace `cycles` with a filler write of 0x00, see nsd_filler_reg()
 *
 * @param hint SID address the delay belongs to
 * @param cycles delay cycles, 0 writes nothing
 * @param direct see nsd_emit()
 */
static void nsd_filler(uint16_t hint, uint16_t cycles, bool direct)
{
  if (cycles == 0) return;
  nsd_emit((uint8_t)(nsd_filler_reg(hint) | NSD_RING_RAW), 0x00, cycles, direct);
}

/**
 * @brief Filler size for a delay above NSD_DELAY_MAX
 *
 * Leave at least NSD_DELAY_REST for the write after the filler: an FM OPL
 * index write right after a filler on the index port could be lost.
 *
 * @param t delay cycles, above NSD_DELAY_MAX
 * @return uint16_t filler delay cycles
 */
static inline uint16_t nsd_split(uint32_t t)
{
  uint32_t f = t - NSD_DELAY_REST;
  return (uint16_t)((f > NSD_DELAY_MAX) ? NSD_DELAY_MAX : f);
}

/**
 * @brief Add a delay-only access to the carry, core 0 only
 *
 * No bus write: the following write takes the delay. Emit a filler only when
 * the carry passes NSD_DELAY_MAX.
 *
 * @param hint SID address the delay belongs to
 * @param cycles delay cycles
 * @param direct see nsd_emit()
 */
static void nsd_carry_delay(uint16_t hint, uint16_t cycles, bool direct)
{
  nsd_carry += cycles;
  if (nsd_carry > NSD_DELAY_MAX) {
    uint16_t f = nsd_split((uint32_t)nsd_carry);
    nsd_filler(hint, f, direct);
    nsd_carry -= f;
  }
}

/**
 * @brief Delay of a bus write with the carry added, core 0 only
 *
 * Above NSD_DELAY_MAX emit a filler first. Below zero the write goes with
 * no delay and the rest stays as negative carry.
 *
 * @param hint SID address of the write
 * @param cycles delay the client asked for, may be negative
 * @param direct see nsd_emit()
 * @return uint16_t delay for the write
 */
static uint16_t nsd_lead(uint16_t hint, int32_t cycles, bool direct)
{
  int32_t t = nsd_carry + cycles;
  nsd_carry = 0;
  if (t > NSD_DELAY_MAX) {
    uint16_t f = nsd_split((uint32_t)t);
    nsd_filler(hint, f, direct);
    t -= f;
  }
  if (t < 0) {
    nsd_carry = (t < NSD_DEBT_MIN) ? NSD_DEBT_MIN : t;
    t = 0;
  }
  return (uint16_t)t;
}

/**
 * @brief SID write, a muted or unconfigured write keeps only its delay
 *
 * "accesses to unconfigured SIDs are ignored, but their delays still
 * apply" (spec).
 *
 * @param addr SID address, 0x20 per chip
 * @param value data byte
 * @param cycles delay cycles before the write
 * @param direct see nsd_emit()
 */
static void nsd_sid_write(uint16_t addr, uint8_t value, uint16_t cycles, bool direct)
{
  if (!nsd_sid_active(addr) || nsd_write_is_muted(addr)) {
    nsd_carry_delay(addr, cycles, direct);
    return;
  }
  uint16_t lead = nsd_lead(addr, cycles, direct);
  nsd_emit((uint8_t)addr, value, lead, direct);
}

/**
 * @brief FM OPL write as an index port, data port pair
 *
 * The data write lands at the caller's delay: the index write takes
 * delay - NSD_FMOPL_GAP. A delay below the gap runs long, the overrun
 * stays as negative carry and shortens the following delays.
 *
 * @param reg OPL register
 * @param value data byte
 * @param cycles delay cycles before the data write
 * @param direct see nsd_emit()
 */
static void nsd_fmopl_write(uint8_t reg, uint8_t value, uint16_t cycles, bool direct)
{
  uint8_t base = nsd_fmopl_base();
  uint16_t lead = nsd_lead(base, (int32_t)cycles - NSD_FMOPL_GAP, direct);
  nsd_emit((uint8_t)(base | NSD_RING_RAW), reg, lead, direct);
  nsd_emit((uint8_t)((base + 0x10) | NSD_RING_RAW), value, NSD_FMOPL_GAP, direct);
}

/**
 * @brief Whether all queued audio has played, core 0 only
 *
 * Queue a positive carry as a filler first, it is audio time not played
 * yet. A negative carry ends at an idle ring. Idle means core 1 also
 * returned from its last write (net_ring_idle()).
 *
 * @return true when the ring is idle and no delay is carried
 */
static bool nsd_quiescent(void)
{
  if (nsd_carry > 0 && net_ring_free() > 0) {
    nsd_filler(0x00, (uint16_t)nsd_carry, false);
    nsd_carry = 0;
  }
  if (nsd_carry > 0 || !net_ring_idle()) return false;
  nsd_carry = 0;
  return true;
}

/**
 * @brief Whether the bus PIO pipeline finished every queued write
 *
 * Lock free: TX FIFOs empty and the control, data and delay state machines
 * parked on their first `pull`. A pipeline that does not settle within
 * NSD_SETTLE_US of retries counts as settled.
 *
 * @return true when no queued write can land later
 */
static bool nsd_bus_settled(void)
{
  bool idle = pio_sm_is_tx_fifo_empty(bus_pio, sm_control)
    && pio_sm_is_tx_fifo_empty(bus_pio, sm_data)
    && pio_sm_is_tx_fifo_empty(bus_pio, sm_delay)
    && ((uint)pio_sm_get_pc(bus_pio, sm_control) == (uint)(offset_control + bus_control_wrap_target))
    && ((uint)pio_sm_get_pc(bus_pio, sm_data) == (uint)(offset_data + data_bus_wrap_target))
    && ((uint)pio_sm_get_pc(bus_pio, sm_delay) == (uint)(offset_delay + delay_timer_wrap_target));
  uint64_t now = time_us_64();
  if (!idle && (!nsd_settle_waiting || (now - nsd_settle_last) > NSD_SETTLE_US)) {
    nsd_settle_waiting = true; /* First try, or a stale window: start over */
    nsd_settle_since = now;
  }
  nsd_settle_last = now;
  if (idle || (now - nsd_settle_since) >= NSD_SETTLE_US) {
    nsd_settle_waiting = false;
    return true;
  }
  return false;
}

/**
 * @brief Silence the voice of a muted register, caller owns the bus
 *
 * Zero control and sustain/release. No-op for registers outside voice 1-3.
 *
 * @param reg SID address in the voice, 0x20 per chip
 */
static void nsd_mute_voice(uint8_t reg)
{
  uint8_t local = (uint8_t)(reg & 0x1F);
  if (local >= 21) return;
  uint8_t vbase = (uint8_t)((reg & 0x60) + (local / 7) * 7);
  cycled_write_operation((uint8_t)(vbase + 0x04), 0x00, 0); /* control */
  cycled_write_operation((uint8_t)(vbase + 0x06), 0x00, 0); /* sustain/release */
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
  nsd_ring_reset();
  if (bus_try_claim(BUS_OWNER_NET)) { /* USB still wins, see bus.c */
    mute_sid();
    bus_release(BUS_OWNER_NET);
  }
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

/**
 * @brief TRY_SET_SID_COUNT: accept 1-16 SIDs, chips this board lacks are
 *        ignored with their delays kept (nsd_sid_active())
 */
static uint16_t handle_try_set_sid_count(uint8_t sid_number, uint8_t *resp_buf)
{
  if (sid_number == 0 || sid_number > NSD_SID_MAX) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  if (!nsd_quiescent()) { /* "returns BUSY until audio quiescent, otherwise OK" (spec) */
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  uint8_t max_sids = cfg.numsids ? cfg.numsids : 1;
  nsd_sid_count = (sid_number < max_sids) ? sid_number : max_sids;
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

/**
 * @brief MUTE: "bypasses the buffer and takes immediate effect" (spec)
 *
 * Silence the voice at once, nsd_drain_task() drops the queued writes of
 * a muted voice when they play and silences it again after a write it
 * had started before the mask changed. SID 5-16 have no socket: OK, no-op.
 */
static uint16_t handle_mute(uint8_t sid_number, const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len < 2) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  uint8_t voice = payload[0];
  uint8_t enable = payload[1];

  if (voice <= 3 && sid_number < NSD_BOARD_SIDS) {
    uint8_t mask = nsd_mute_mask[sid_number];
    if (enable) mask |= (uint8_t)(1u << voice);
    else        mask &= (uint8_t)~(1u << voice);
    nsd_mute_mask[sid_number] = mask;
    __dmb(); /* Mask visible before the generation that announces it */
    nsd_mute_gen = (uint8_t)(nsd_mute_gen + 1);

    uint8_t base = (uint8_t)(sid_number * 0x20);
    if (enable && voice <= 2 && nsd_sid_active(base) && bus_try_claim(BUS_OWNER_NET)) {
      nsd_mute_voice((uint8_t)(base + voice * 7));
      bus_release(BUS_OWNER_NET);
    }
  }
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_try_reset(uint8_t volume, uint8_t *resp_buf)
{
  /* Ring idle, then every queued PIO write landed: none after the reset */
  if (!nsd_quiescent() || !nsd_bus_settled()) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  if (!bus_try_claim(BUS_OWNER_NET)) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  reset_sid();
  for (uint8_t sid = 0; sid < nsd_sid_count; sid++) {
    uint8_t base = (uint8_t)(sid * 0x20);
    /* "The volume value does not apply to FM OPL" (spec), base+0x18 is
     * its data port */
    if (nsd_sid_active(base)) cycled_write_operation((uint8_t)(base + 0x18), volume, 0);
  }
  bus_release(BUS_OWNER_NET);
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

static uint16_t handle_try_delay(uint16_t cycles, uint8_t *resp_buf)
{
  if (cycles == 0) { /* "0 is not allowed" (spec) */
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  if (nsd_ring_busy() || net_ring_free() < 2) { /* Filler worst case */
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  nsd_carry_delay(0x00, cycles, false);
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
  /* One slot per quad, plus one filler: fillers never outnumber the
   * delay-only quads plus one */
  if (nsd_ring_busy() || net_ring_free() < (uint32_t)(n_quads + 1)) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  for (uint16_t i = 0; i < n_quads; i++) {
    const uint8_t *quad = &payload[i * 4];
    /* Muted, SID 5-8 or disabled: delay only */
    nsd_sid_write(quad[2], quad[3], nsd_quad_cycles(quad), false);
  }
  resp_buf[0] = NSD_RESP_OK;
  return 1;
}

/**
 * @brief TRY_READ: n leading write quads, then a final {cyc_hi,cyc_lo,reg}
 *
 * Waits for an idle ring (core 1 returned from its last write), then
 * applies the leading writes and the read directly: the read queues behind
 * every prior write in the PIO FIFO. cycled_*_operation() (bus.c) is safe
 * from either core. The carry goes into the first write or the read.
 */
static uint16_t handle_try_read(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len < 3 || ((payload_len - 3) % 4) != 0) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  if (!net_ring_idle()) {
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
    nsd_sid_write(quad[2], quad[3], nsd_quad_cycles(quad), true);
  }

  const uint8_t *tail = &payload[n_leading * 4];
  uint16_t read_cycles = ((uint16_t)tail[0] << 8) | tail[1];
  uint8_t reg = tail[2];
  /* SID 5-8 (reg >= 0x80) or a disabled socket: "accesses to unconfigured
   * SIDs are ignored, but their delays still apply... an ignored read
   * returns READ followed by 0x00" (spec). */
  uint8_t value = 0x00;
  if (nsd_sid_active(reg)) {
    value = cycled_read_operation(reg, nsd_lead(reg, read_cycles, true));
  } else {
    nsd_carry_delay(reg, read_cycles, true);
  }
  bus_release(BUS_OWNER_NET);

  resp_buf[0] = NSD_RESP_READ;
  resp_buf[1] = value;
  return 2;
}

/**
 * @brief Whether a v5 address is an FM OPL write that reaches the bus
 *
 * In the 0xdf00-0xdfff range, socket configured as FM OPL, turned on for
 * this connection and its chip select enabled.
 *
 * @param addr v5 register address
 * @return true to route as an OPL index/data write pair
 */
static bool nsd_fmopl_routed(uint16_t addr)
{
  return (addr >= NSD_EX_FMOPL_LO && addr <= NSD_EX_FMOPL_HI
          && nsd_fmopl_active && nsd_fmopl_socket_active());
}

/**
 * @brief TRY_WRITE_EX (19): v5 extended write, up to 16 SIDs or FM OPL
 *
 * Payload is 5*N bytes, each {delay_hi, delay_lo, addr_hi, addr_lo, value}.
 * addr 0x0000-0x01ff addresses SID 1-16 (0x20 per chip); this board only
 * has 4 real sockets: chip 5-16 entries, and entries for a disabled
 * socket, are accepted and keep only their delay. addr 0xdf00-0xdfff
 * becomes the two-port index/data write a real OPL2 wants (opl_write(),
 * midi_fmopl.c) when a socket is configured for it AND TRY_SET_FM_OPL has
 * turned it on for this connection (nsd_fmopl_routed()), delay only
 * otherwise. A pair below NSD_FMOPL_GAP cycles carries its overrun.
 */
static uint16_t handle_try_write_ex(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len == 0 || (payload_len % 5) != 0) {
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  uint16_t n_entries = payload_len / 5;

  /* Pre-count net_ring slots needed: an FMOpl entry is two writes (index
   * port, data port), any other entry at most one, plus one filler */
  uint32_t n_slots = 1;
  for (uint16_t i = 0; i < n_entries; i++) {
    const uint8_t *e = &payload[i * 5];
    n_slots += nsd_fmopl_routed((uint16_t)((e[2] << 8) | e[3])) ? 2 : 1;
  }
  if (nsd_ring_busy() || net_ring_free() < n_slots) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  for (uint16_t i = 0; i < n_entries; i++) {
    const uint8_t *e = &payload[i * 5];
    uint16_t delay = (uint16_t)((e[0] << 8) | e[1]);
    uint16_t addr  = (uint16_t)((e[2] << 8) | e[3]);
    uint8_t  value = e[4];

    if (addr < NSD_EX_SID_MAX) { /* SID 5-16, disabled socket: delay only */
      nsd_sid_write(addr, value, delay, false);
    } else if (nsd_fmopl_routed(addr)) {
      nsd_fmopl_write((uint8_t)(addr & 0xFF), value, delay, false);
    } else {
      /* FM OPL off, or a register this protocol version does not define:
       * accepted, delay kept */
      nsd_carry_delay(addr, delay, false);
    }
  }
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
  if (!net_ring_idle()) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }
  if (!bus_try_claim(BUS_OWNER_NET)) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  uint16_t n_leading = (payload_len - 4) / 5;
  for (uint16_t i = 0; i < n_leading; i++) {
    const uint8_t *e = &payload[i * 5];
    uint16_t cycles = (uint16_t)((e[0] << 8) | e[1]);
    uint16_t addr   = (uint16_t)((e[2] << 8) | e[3]);
    uint8_t  value  = e[4];
    if (addr < NSD_EX_SID_MAX) {
      nsd_sid_write(addr, value, cycles, true); /* SID 5-16, disabled: delay only */
    } else if (nsd_fmopl_routed(addr)) {
      nsd_fmopl_write((uint8_t)(addr & 0xFF), value, cycles, true);
    } else {
      nsd_carry_delay(addr, cycles, true); /* FM OPL off or undefined: delay kept */
    }
  }

  const uint8_t *tail = &payload[n_leading * 5];
  uint16_t read_cycles = (uint16_t)((tail[0] << 8) | tail[1]);
  uint16_t read_addr   = (uint16_t)((tail[2] << 8) | tail[3]);
  uint8_t value = 0x00;
  if (nsd_sid_active(read_addr)) {
    value = cycled_read_operation((uint8_t)read_addr, nsd_lead(read_addr, read_cycles, true));
  } else {
    /* The read target's own address range dropped FM OPL in the spec
     * (network_sid_device_V5.adoc, TRY_READ_EX no longer lists
     * 0xdf00-0xdfff - only TRY_WRITE_EX still does, see the leading-entries
     * loop above): an unconfigured SID, or any address this protocol
     * version no longer defines for a read, is an ignored read - its delay
     * still applies, returns READ followed by 0x00 (spec). */
    nsd_carry_delay(read_addr, read_cycles, true);
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
  /* Ring idle, then every queued PIO write landed: none at the new clock */
  if (!nsd_quiescent() || !nsd_bus_settled()) {
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
  if (payload_len >= 1 && sid_number < NSD_BOARD_SIDS) { /* SID 5-16: no socket */
    uint8_t base_address = (uint8_t)(sid_number * 0x20);
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
 * @brief TRY_SET_FM_OPL (21): enable/disable FM OPL for this connection
 *
 * Toggles nsd_fmopl_active, which the FMOpl-range handling in
 * handle_try_write_ex()/handle_try_read_ex() checks alongside
 * cfg.fmopl_enabled - see nsd_fmopl_active's own comment for why this never
 * touches cfg.fmopl_sid/set_fmopl_sidno() (BOARD_FMOPL/config.c's job, a
 * persistent board setting, not a per-connection one).
 *
 * Requires the write ring drained first, same reasoning as TRY_RESET/
 * TRY_SET_CLOCK: it changes how TRY_WRITE_EX routes FMOpl-range entries
 * from here on, and a queued entry from before the switch should not be
 * reinterpreted after it.
 *
 * Known gap: spec says "enabling a disabled device resets it" - this does
 * not reset the OPL chip's own registers on enable, only starts routing to
 * it again. The chip keeps whatever state MIDI/ASID left it in.
 */
static uint16_t handle_try_set_fm_opl(const uint8_t *payload, uint16_t payload_len, uint8_t *resp_buf)
{
  if (payload_len != 1) { /* spec: "the data packet is one byte long" */
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }
  if (!nsd_quiescent()) {
    resp_buf[0] = NSD_RESP_BUSY;
    return 1;
  }

  bool enable = (payload[0] != 0);
  if (enable == nsd_fmopl_active) {
    /* "enabling an already enabled device or disabling an already disabled
     * device returns OK without resetting it or interrupting playback.
     * Disabling FM OPL on an unsupported device also returns OK." (spec) */
    resp_buf[0] = NSD_RESP_OK;
    return 1;
  }
  if (enable && !cfg.fmopl_enabled) {
    /* No socket on this board is configured as FM OPL at all: "returns
     * ERROR when enabling FM OPL on a device that does not support it."
     * (spec) */
    resp_buf[0] = NSD_RESP_ERROR;
    return 1;
  }

  nsd_fmopl_active = enable;
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
      if (payload_len < 1) { resp_buf[0] = NSD_RESP_ERROR; resp_len = 1; break; }
      resp_len = handle_try_reset(payload[0], resp_buf);
      break;
    case NSD_CMD_TRY_DELAY:
      if (payload_len < 2) { resp_buf[0] = NSD_RESP_ERROR; resp_len = 1; break; }
      resp_len = handle_try_delay((uint16_t)((payload[0] << 8) | payload[1]), resp_buf);
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
    case NSD_CMD_SET_SID_POSITION:      /* reSID software-emulation concepts,
                                          * this board has no equivalent: */
    case NSD_CMD_SET_SID_LEVEL:
    case NSD_CMD_SET_DELAY:
      resp_buf[0] = NSD_RESP_OK;         /* "it always returns OK" (spec) */
      resp_len = 1;
      break;
    case NSD_CMD_TRY_SET_SAMPLING:
    case NSD_CMD_SET_FADE_IN:
    case NSD_CMD_SET_FADE_OUT:
      /* Also no-ops here, but "returns BUSY until audio quiescent, otherwise
       * OK" (spec) rather than always OK - distinct from the group above. */
      if (!nsd_quiescent()) { resp_buf[0] = NSD_RESP_BUSY; resp_len = 1; break; }
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
        /* Too big to buffer (spec allows up to 65,535 B of payload; this
         * board only has room for NSD_MAX_PAYLOAD). Missing/malformed data
         * gets ERROR elsewhere (see the Error Handling section), and a
         * silent drop here is worse: a client waiting on a response for a
         * legally-sized packet would otherwise hang forever. Still consume
         * and discard exactly payload_len bytes rather than resetting
         * outright, so framing stays in sync with a client that already
         * committed to sending them - see the `oversized` branch below. */
        uint8_t resp = NSD_RESP_ERROR;
        nsd_send(&resp, 1);
        reasm.oversized = true;
        reasm.payload_have = 0;
        reasm.in_payload = true;
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

    if (!reasm.oversized) reasm.payload[reasm.payload_have] = byte;
    reasm.payload_have++;
    if (reasm.payload_have == reasm.payload_len) {
      if (!reasm.oversized) process_nsd_command(reasm.header, reasm.payload, reasm.payload_len);
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
  nsd_cycles_mark = nsd_cycles_in; /* nsd_cycles_out stays core 1 owned */
  nsd_carry = 0;                   /* nsd_drain_carry stays core 1 owned */
  nsd_ring_busy_latched = false;
  nsd_sid_count = 1;
  nsd_fmopl_active = false;
  for (uint8_t i = 0; i < NSD_BOARD_SIDS; i++) nsd_mute_mask[i] = 0;
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
  nsd_fmopl_active = false; /* "disabled by default on a new connection" (spec) */
  return true;
}

/**
 * @brief Close the current NSD session
 */
void nsd_session_close(void)
{
  session_active = false;
  g_transport = NULL;
  nsd_ring_reset();
  nsd_fmopl_active = false;
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
 * latency obligations while nothing else is queued. Each entry stays in
 * the ring until its bus write returned (net_ring_peek(), then
 * net_ring_advance()): core 0 sees the ring idle only once the last write
 * sits in the PIO FIFO. Sole writer of nsd_cycles_out: a written entry
 * adds its cycles after the write, a dropped entry (nsd_ring_reset())
 * adds them without one.
 *
 * A write muted after it was queued (MUTE) keeps only its delay: the
 * following entry takes it (nsd_drain_carry), a last entry spends it on
 * a filler.
 */
void nsd_drain_task(void)
{
  uint8_t quad[4];
  bool dropped;

  /* Retire dropped entries first, no bus needed */
  while (net_ring_dropped() > 0 && net_ring_pop(quad, &dropped)) {
    nsd_cycles_out += nsd_drain_carry + nsd_quad_cycles(quad);
    nsd_drain_carry = 0; /* Delay of a muted entry before the drop */
  }

  if (net_ring_count() == 0) return;
  if (!bus_try_claim(BUS_OWNER_NET)) return; /* USB (or MIDI/player) is active, try again next loop */

  while (net_ring_peek(quad, &dropped)) {
    uint8_t reg = quad[2];
    bool raw = ((reg & NSD_RING_RAW) != 0);
    uint32_t t = nsd_drain_carry + nsd_quad_cycles(quad);
    uint8_t gen = nsd_mute_gen;
    __dmb(); /* Mask read after the generation, see handle_mute() */

    if (dropped) {
      nsd_cycles_out += t;
      nsd_drain_carry = 0;
    } else if (!raw && nsd_write_is_muted(reg)) {
      if (t > NSD_DELAY_MAX) {
        uint16_t f = nsd_split(t);
        nsd_filler(reg, f, true);
        nsd_cycles_out += f;
        t -= f;
      }
      if (net_ring_count() > 1) {
        nsd_drain_carry = t; /* Following entry takes the delay */
      } else {
        nsd_filler(reg, (uint16_t)t, true); /* Last entry: spend it at once */
        nsd_cycles_out += t;
        nsd_drain_carry = 0;
      }
    } else {
      if (t > NSD_DELAY_MAX) {
        uint16_t f = nsd_split(t);
        nsd_filler((uint16_t)(reg & 0x7F), f, true);
        nsd_cycles_out += f;
        t -= f;
      }
      cycled_write_operation((uint8_t)(reg & 0x7F), quad[3], (uint16_t)t);
      nsd_cycles_out += t;
      nsd_drain_carry = 0;
      /* MUTE came in while this write was under way: silence again */
      if (!raw && gen != nsd_mute_gen && nsd_write_is_muted(reg)) nsd_mute_voice(reg);
    }
    net_ring_advance();
  }

  bus_release(BUS_OWNER_NET);
}
