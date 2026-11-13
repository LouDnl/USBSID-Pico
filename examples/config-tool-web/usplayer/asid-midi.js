/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * asid-midi.js
 * ASID over Web MIDI transport
 *
 * Ports the register packing of player-repo/lib/midi/asid.cpp
 * Protocol: https://github.com/thomasj/asid-protocol
 *
 * Not cycle exact, one snapshot per frame (about 50 per second)
 *
 *   writeCycled(reg, val, cycles)  accumulate the change, ignore the gap
 *   flush()                        emit one 0x4E SysEx for the frame
 *
 * The 0x30 timing extension is not implemented, use WebUSB for digi tunes
 *
 * Gate register shadowing (0x19, 0x1a, 0x1b) keeps a voice retriggered twice
 * in one frame from arriving as one note
 *
 * SysEx: F0 2D <cmd> <payload> F7, manufacturer 0x2D
 *   0x4C start, 0x4D stop, 0x4E SID one, 0x50/0x51/0x52 SIDs two to four
 *
 * This file is part of USBSID-Player (https://github.com/LouDnl/USBSID-Player)
 * File author: LouD
 *
 * Copyright (c) 2026 LouD
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ASID register order, index = bit position, value = SID register */
const REGMAP = [0,1,2,3,5,6,7,8,9,10,12,13,14,15,16,17,19,20,21,22,23,24,4,11,18,25,26,27];
const SID_CMD = [0x4E, 0x50, 0x51, 0x52];

/* Shadow slots for a second control register write in one frame, applied by the receiver after the register */
const GATE_SHADOW = { 0x04: 0x19, 0x0b: 0x1a, 0x12: 0x1b };

/* FM/OPL for SFX Sound Expander (YM3526) and FM-YAM (YM3812) tunes
 * Sent as message 0x60 with address and data pairs
 * Registers $80 and $90 are writes to $df40 (OPL address) and $df50 (OPL data), see Mos6581_8580::io_write
 * 16 pairs per message, sent when full and at the frame boundary */
const FM_ADDR_REG = 0x80;
const FM_DATA_REG = 0x90;
const FM_CMD      = 0x60;
const FM_MAX_PAIRS = 16;
const ASID_MFR = 0x2D;
const ASID_START = 0x4C;
const ASID_STOP  = 0x4D;
const ASID_ENV   = 0x31;   /* SID environment: PAL/NTSC, speed, frame delta */
const ASID_TYPE  = 0x32;   /* SID chip type */
/* Cycles a second of the PAL and NTSC machine, as EMULATION_CYCLES_PER_SECOND_* in sid_asid.h */
const CYCLES_PAL  = 985248;
const CYCLES_NTSC = 1022727;

function makeChip() {
  return { reg: new Uint8Array(32), modified: new Uint8Array(32) };
}

export class ASIDMIDITransport {
  /**
   * @param {object} opts { nosids?: number, deviceNameHint?: string }
   */
  constructor(opts = {}) {
    this.nosids = opts.nosids || 1;
    this._hint = (opts.deviceNameHint || 'USBSID').toLowerCase();
    this._access = null;
    this._out = null;
    this._open = false;
    this._chips = [makeChip(), makeChip(), makeChip(), makeChip()];
    /* FM/OPL address last selected and pairs waiting to go out */
    this._fmAddr = 0;
    this._fmPairs = [];

    /* FM/OPL goes out as message 0x60, outside the SID snapshots
     * $df40 and $df50 must reach this transport as $80 and $90
     * The player forces fmopl_sid to -1 when this flag is set */
    this.fmAsOwnMessage = true;
    this.onWrite = null;
  }

  get isOpen() { return this._open; }
  get productName() { return this._out ? (this._out.name || 'MIDI') : ''; }

  /* MIDI outputs as [{ id, name }], needs connect() first */
  outputs() {
    if (!this._access) return [];
    return [...this._access.outputs.values()].map((o) => ({ id: o.id, name: o.name }));
  }

  /* Request Web MIDI with SysEx, pick the given output, else the first USBSID match, else the first output */
  async connect(outputId = null) {
    this._access = await navigator.requestMIDIAccess({ sysex: true });
    const outs = [...this._access.outputs.values()];
    if (outs.length === 0) throw new Error('no MIDI outputs');
    const byHint = outs.find((o) => (o.name || '').toLowerCase().includes(this._hint));
    this._out = (outputId && outs.find((o) => o.id === outputId)) || byHint || outs[0];
    this._open = true;
    return true;
  }

  selectOutput(outputId) {
    if (!this._access) return false;
    const o = [...this._access.outputs.values()].find((x) => x.id === outputId);
    if (o) { this._out = o; return true; }
    return false;
  }

  /* Pick an output by display name */
  selectOutputByName(name) {
    if (!this._access || !name) return false;
    const o = [...this._access.outputs.values()].find((x) => (x.name || '') === name);
    if (o) { this._out = o; return true; }
    return false;
  }

  async disconnect() {
    this.playbackStop();
    this._open = false;
    this._out = null;
    this._access = null;
  }

  /* Transport interface */

  playbackStart() { this._basic(ASID_START); }
  playbackStop() { this._basic(ASID_STOP); this._clearAll(); }

  /**
   * Tell the receiver the video standard and frame time (ASID 0x31)
   *
   * Port of ASIDBackend::sendSIDEnvironment() in sid_asid.cpp: F0 2D 31 <settings>
   * <framedelta us in 7+7+2 bits, LSB first> F7. Speed 1x, no buffering requested.
   *
   * @param {boolean} pal         true for a PAL tune, false for NTSC
   * @param {number}  multiplier  speed 1 to 16, 1 is normal
   */
  sendEnvironment(pal, multiplier = 1) {
    if (!this._out) return;
    const mult = Math.min(16, Math.max(1, multiplier | 0));
    const frameDeltaUs = pal
      ? Math.floor(1000000 * 63 * 312 / CYCLES_PAL)
      : Math.floor(1000000 * 65 * 263 / CYCLES_NTSC);
    this._send([
      0xF0, ASID_MFR, ASID_ENV,
      (((mult - 1) & 0x0f) << 1) | (pal ? 0 : 1),
      frameDeltaUs & 0x7f,
      (frameDeltaUs >> 7) & 0x7f,
      (frameDeltaUs >> 14) & 0x03,
      0xF7,
    ]);
  }

  /**
   * Tell the receiver which SID chip the tune expects (ASID 0x32)
   *
   * Port of ASIDBackend::sendSIDType(): F0 2D 32 00 <00 for 6581, 01 otherwise> F7.
   * Chip index is always 0, as the command line player.
   *
   * @param {boolean} is6581  true when the tune header asks for a 6581
   */
  sendSidType(is6581) {
    if (!this._out) return;
    this._send([0xF0, ASID_MFR, ASID_TYPE, 0x00, is6581 ? 0x00 : 0x01, 0xF7]);
  }

  /* Add one register change to the frame snapshot */
  writeCycled(reg, val, _cycles) {
    if (!this._open) return;
    if (this.onWrite) this.onWrite(reg, val);
    const sid = (reg >> 5) & 0x03;    // $00-$1f chip one, $20-$3f chip two
    const r = reg & 0x1f;
    const data = val & 0xFF;

    /* FM/OPL, $80 selects an OPL register, $90 writes it, only complete pairs are sent */
    if (reg === FM_ADDR_REG) { this._fmAddr = data; return; }
    if (reg === FM_DATA_REG) {
      this._fmPairs.push(this._fmAddr, data);
      if (this._fmPairs.length >= FM_MAX_PAIRS * 2) this._flushFm();
      return;
    }

    /* Drop writes to $19-$1f, ASID uses $19, $1a and $1b as shadow slots for $04, $0b and $12
     * Tunes clear the whole range with sta $d400,x for x = 0 to $1f, this would repeat a gate write */
    if (r > 0x18) return;

    const c = this._chips[sid];
    if (c.modified[r] === 0) {
      c.reg[r] = data;
      c.modified[r] = 1;
      return;
    }
    /* Written more than once this frame
     * Gate registers use the shadow slot for the second write
     * A third write flushes the frame and starts a new snapshot
     * Filter and volume have no shadow and are flushed at once, order is audible */
    switch (r) {
      case 0x04: case 0x0b: case 0x12: {
        /* Control registers are 7 apart, shadows are 1 apart, no arithmetic offset */
        const shadow = GATE_SHADOW[r];
        if (c.modified[shadow] !== 0) {
          this.flush();
          c.reg[r] = data; c.modified[r] = 1;
        } else {
          c.reg[shadow] = data; c.modified[shadow] = 1;
        }
        break;
      }
      case 0x16: case 0x17: case 0x18:
        this.flush();
        c.reg[r] = data; c.modified[r] = 1; break;
      default:
        c.reg[r] = data;   // for the rest the last value of the frame wins
    }
  }

  /* FM/OPL message: F0 2D 60 <pairs-1> <msb bytes> <7 bit data> F7
   * One mask byte per 7 data bytes holds their high bits (SysEx data stays under $80) */
  _flushFm() {
    const n = this._fmPairs.length;
    if (n === 0) return;
    const maskBytes = Math.floor((n - 1) / 7) + 1;
    const msg = [0xF0, ASID_MFR, FM_CMD, (n / 2) - 1];
    for (let i = 0; i < maskBytes; i++) msg.push(0);
    for (let i = 0; i < n; i++) {
      if (this._fmPairs[i] & 0x80) msg[4 + Math.floor(i / 7)] |= (1 << (i % 7));
      msg.push(this._fmPairs[i] & 0x7f);
    }
    msg.push(0xF7);
    this._send(msg);
    this._fmPairs.length = 0;
  }

  /* Emit one snapshot per SID and start the following frame */
  flush() {
    if (!this._open) return;
    /* FM first, keeps the order of the tune */
    this._flushFm();
    /* All chips, a chip without writes has an empty mask and is skipped */
    for (let sid = 0; sid < this._chips.length; sid++) {
      const c = this._chips[sid];
      let mask = 0, msb = 0;
      for (let i = 0; i < 28; i++) {
        const j = REGMAP[i];
        if (c.modified[j] !== 0) mask |= (1 << i);
        if (c.reg[j] > 0x7f)     msb  |= (1 << i);
      }
      if (mask === 0) continue;   // nothing changed for this chip
      const msg = [0xF0, ASID_MFR, SID_CMD[sid],
        mask & 0x7f, (mask >> 7) & 0x7f, (mask >> 14) & 0x7f, (mask >> 21) & 0x7f,
        msb & 0x7f, (msb >> 7) & 0x7f, (msb >> 14) & 0x7f, (msb >> 21) & 0x7f];
      for (let i = 0; i < 28; i++) {
        const j = REGMAP[i];
        if (c.modified[j] !== 0) msg.push(c.reg[j] & 0x7f);
      }
      msg.push(0xF7);
      this._send(msg);
      c.modified.fill(0);
    }
  }

  /* Silence by leaving ASID mode */
  resetSID() { this.playbackStop(); }
  reset()    { this._clearAll(); }

  /* Internals */

  _clearAll() {
    for (const c of this._chips) c.modified.fill(0);
    this._fmPairs.length = 0;
  }

  _basic(cmd) {
    if (!this._out) return;
    this._send([0xF0, ASID_MFR, cmd, 0xF7]);
  }

  _send(bytes) {
    try { this._out.send(bytes); } catch (_) {}
  }
}
