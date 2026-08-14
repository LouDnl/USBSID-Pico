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

/* Where a second write to each voice's control register goes. ASID reserves
 * these three positions for exactly that, and the receiver applies them after
 * the register itself, which is what lets a retrigger survive one snapshot. */
const GATE_SHADOW = { 0x04: 0x19, 0x0b: 0x1a, 0x12: 0x1b };

/* FM/OPL, for tunes written for the SFX Sound Expander (YM3526) or FM-YAM
 * (YM3812). ASID carries it in its own message, 0x60, as address and data pairs,
 * so a receiver with an OPL can play it whatever SID chips are attached.
 *
 * The emulation hands these over as registers $80 and $90, which are outside the
 * 0..$7f a SID register lives in: $80 is a write to $df40, the OPL's address
 * port, and $90 a write to $df50, its data port. See Mos6581_8580::io_write.
 *
 * Sixteen pairs per message, which is what the reference implementation uses.
 * The message is sent as soon as it is full and again at the frame boundary, so
 * a burst of OPL writes does not wait for a frame that may be a long way off. */
const FM_ADDR_REG = 0x80;
const FM_DATA_REG = 0x90;
const FM_CMD      = 0x60;
const FM_MAX_PAIRS = 16;
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
    /* FM/OPL: the address last selected, and the pairs waiting to go out. */
    this._fmAddr = 0;
    this._fmPairs = [];

    /* ASID carries FM/OPL in its **own** message, 0x60, not inside a SID
     * snapshot (0x4E, 0x50, 0x51, 0x52). So the emulation must leave $df40 and
     * $df50 unclaimed, reaching this transport as $80 and $90, rather than
     * routing them into some chip's register space where they would go out as
     * ordinary SID data and no receiver would decode them as FM.
     *
     * The player checks this flag and forces `fmopl_sid` to -1 for us. It used
     * to work only because this class has no readBoardConfig() and so the board
     * config was never applied over ASID, which is a thing that would break the
     * first time anyone added one. */
    this.fmAsOwnMessage = true;
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

    /* FM/OPL first: these are not SID registers at all and must not be folded
     * into a snapshot. $80 selects an OPL register, $90 writes it. Only a
     * complete pair is worth sending, so an address on its own is remembered. */
    if (reg === FM_ADDR_REG) { this._fmAddr = data; return; }
    if (reg === FM_DATA_REG) {
      this._fmPairs.push(this._fmAddr, data);
      if (this._fmPairs.length >= FM_MAX_PAIRS * 2) this._flushFm();
      return;
    }

    /* Only $00 to $18 exist to be written. $19 to $1f are read only on a real
     * SID, which ignores writes to them, and the emulation passes every write in
     * the range straight through because for a real chip that is harmless.
     *
     * Here it is not harmless, and this is a protocol collision rather than a
     * nicety. ASID reuses positions $19, $1a and $1b to carry a *second* write
     * to $04, $0b and $12 inside one frame, which is how a retrigger survives a
     * snapshot. So a stray write to $d419 arrives at the receiver as "set voice
     * one's control register again", and the gate is re-applied out of nowhere.
     *
     * Tunes do write there. A clear loop over the whole register range,
     * `lda #0 / sta $d400,x` for x = 0 to $1f, is a common way to silence a SID
     * in an init routine or between frames, and every one of those hits all
     * three shadow slots. That is heard as a small repeat of a previous gate
     * write in some parts of a tune and not others, which is exactly what it is.
     */
    if (r > 0x18) return;

    const c = this._chips[sid];
    if (c.modified[r] === 0) {
      c.reg[r] = data;
      c.modified[r] = 1;
      return;
    }
    /* Written more than once this frame, which a snapshot cannot express on its
     * own. Three ways out, one per kind of register.
     *
     * The gate registers get a shadow slot each, $19, $1a and $1b, which the
     * receiver applies after the register itself. That is what carries a
     * gate off followed by a gate on, so a retrigger inside one frame survives.
     *
     * A **third** write has nowhere left to go. This used to shuffle the second
     * value down into the primary slot and drop the first, and that loses notes:
     * gate on, gate off, gate off in one frame kept only the two offs, so a note
     * whose onset was in that frame never sounded at all. Heard as an occasional
     * skip, not often, because it needs three writes to one gate register in one
     * frame. Now the frame is simply cut short instead: flush what is there and
     * begin a new snapshot with the new value, which loses nothing.
     *
     * Filter and volume have no shadow and are flushed at once, because their
     * order within the frame is audible.
     */
    switch (r) {
      case 0x04: case 0x0b: case 0x12: {
        /* Not an arithmetic offset: the three control registers are 7 apart and
         * their shadows are 1 apart. Spelled out so it cannot be "simplified"
         * into a wrong sum. */
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

  /**
   * The FM/OPL message: F0 2D 60 <pairs-1> <msb bytes> <7 bit data> F7.
   *
   * One mask byte per seven data bytes, holding their high bits, since SysEx
   * data has to stay under $80. The layout is the reference implementation's.
   */
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

  /** Emit one snapshot per SID and start the next frame. */
  flush() {
    if (!this._open) return;
    /* Before the SID snapshots, so an OPL note and the SID writes around it stay
     * in the order the tune made them. */
    this._flushFm();
    /* Every chip that has something to say, not the first `nosids` of them.
     *
     * This used to stop at `nosids`, which the host set from its own reading of
     * the file header, and a chip the emulation wrote to but the header count
     * did not reach was **silently dropped**: measured on `Quad_Core_4SID.sid`,
     * 4667 register writes to chip four and not one ASID message carrying them,
     * while the same tune over WebUSB and over serial played correctly, because
     * those transports send by address and never consult a count.
     *
     * Two parsers disagreeing about how many chips a file has is the sort of
     * thing that stays wrong for a long time, so the stream now follows the
     * writes instead: a chip that nothing wrote to has an empty mask and is
     * skipped a line below, which is the same saving without the guess. */
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

  /** The player asking for silence: leaving ASID mode is what does it. */
  resetSID() { this.playbackStop(); }
  reset()    { this._clearAll(); }

  /* ---- internals -------------------------------------------------------- */

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
