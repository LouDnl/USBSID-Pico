/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/asid-midi.js
 * The other transport: ASID over Web MIDI.
 *
 * Carried over from player-repo/web/asid-midi.js, which ports the register
 * packing in player-repo/lib/midi/asid.cpp. Drives a USBSID-Pico, or any other
 * ASID host, over the protocol at https://github.com/thomasj/asid-protocol.
 *
 * ASID is not cycle exact and cannot be: it sends one snapshot per frame of
 * the registers that changed, so the timing it conveys is the flush cadence,
 * about fifty per second. On the transport interface that is:
 *
 *   writeCycled(reg, val, cycles)  accumulate the change, ignore the gap
 *   flush()                        emit one 0x4E SysEx for the frame
 *
 * The gap is genuinely dropped rather than approximated. ASID's 0x30 timing
 * extension would carry it and is not implemented here; a digi tune wants the
 * WebUSB transport instead, where the cycles survive.
 *
 * What is kept from the C implementation is the gate register shadowing: a
 * voice retriggered twice inside one frame would otherwise arrive as one note,
 * so the second write goes to a shadow register (0x19, 0x1a, 0x1b) that the
 * protocol carries alongside the first.
 *
 * SysEx: F0 2D <cmd> <payload> F7, manufacturer 0x2D.
 *   0x4C start, 0x4D stop, 0x4E SID one, 0x50/0x51/0x52 SIDs two to four.
 *
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Player)
 * File author: LouD
 *
 * Copyright (c) 2026 LouD
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
 */

/* ASID register order: the index is the bit position, the value is the SID
 * register it stands for. */
const REGMAP = [0,1,2,3,5,6,7,8,9,10,12,13,14,15,16,17,19,20,21,22,23,24,4,11,18,25,26,27];
const SID_CMD = [0x4E, 0x50, 0x51, 0x52];
const ASID_MFR = 0x2D;
const ASID_START = 0x4C;
const ASID_STOP  = 0x4D;

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
    this.onWrite = null;
  }

  get isOpen() { return this._open; }
  get productName() { return this._out ? (this._out.name || 'MIDI') : ''; }

  /** The MIDI outputs available, [{ id, name }]. Needs connect() first. */
  outputs() {
    if (!this._access) return [];
    return [...this._access.outputs.values()].map((o) => ({ id: o.id, name: o.name }));
  }

  /**
   * Ask for Web MIDI with SysEx and pick an output: the one requested, else
   * the first whose name looks like a USBSID-Pico, else whatever is first.
   * Never throws over a stale id.
   */
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

  /** Pick an output by display name, for following a list a host owns. */
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

  /* ---- the transport interface ----------------------------------------- */

  playbackStart() { this._basic(ASID_START); }
  playbackStop() { this._basic(ASID_STOP); this._clearAll(); }

  /** Fold one register change into this frame's snapshot. */
  writeCycled(reg, val, _cycles) {
    if (!this._open) return;
    if (this.onWrite) this.onWrite(reg, val);
    const sid = (reg >> 5) & 0x03;    // $00-$1f chip one, $20-$3f chip two...
    const r = reg & 0x1f;
    const data = val & 0xFF;
    const c = this._chips[sid];
    if (c.modified[r] === 0) {
      c.reg[r] = data;
      c.modified[r] = 1;
      return;
    }
    /* Written twice this frame. The gate registers go to their shadows so a
     * retrigger is not lost; filter and volume are flushed at once, because
     * their order within the frame is audible. */
    switch (r) {
      case 0x04:
        if (c.modified[0x19] !== 0) c.reg[0x04] = c.reg[0x19];
        c.reg[0x19] = data; c.modified[0x19] = 1; break;
      case 0x0b:
        if (c.modified[0x1a] !== 0) c.reg[0x0b] = c.reg[0x1a];
        c.reg[0x1a] = data; c.modified[0x1a] = 1; break;
      case 0x12:
        if (c.modified[0x1b] !== 0) c.reg[0x12] = c.reg[0x1b];
        c.reg[0x1b] = data; c.modified[0x1b] = 1; break;
      case 0x16: case 0x17: case 0x18:
        this.flush();
        c.reg[r] = data; c.modified[r] = 1; break;
      default:
        c.reg[r] = data;   // for the rest the last value of the frame wins
    }
  }

  /** Emit one snapshot per SID and start the next frame. */
  flush() {
    if (!this._open) return;
    for (let sid = 0; sid < this.nosids; sid++) {
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

  /** The player asking for silence: leaving ASID mode is what does it. */
  resetSID() { this.playbackStop(); }
  reset()    { this._clearAll(); }

  /* ---- internals -------------------------------------------------------- */

  _clearAll() { for (const c of this._chips) c.modified.fill(0); }

  _basic(cmd) {
    if (!this._out) return;
    this._send([0xF0, ASID_MFR, cmd, 0xF7]);
  }

  _send(bytes) {
    try { this._out.send(bytes); } catch (_) {}
  }
}
