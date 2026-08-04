/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usbsid-webusb.js
 * The cycle exact transport: WebUSB straight to a USBSID-Pico.
 *
 * Carried over from player-repo/web/usbsid-webusb.js, whose connect sequence
 * and packet format come in turn from repo/examples/config-tool-web's
 * usbsid-driver.js, the driver that is known to work on a board.
 *
 * Packet format (repo/src/usbsid.c handle_buffer_task, globals.h):
 *   byte 0    command. Top two bits are the type, CYCLED_WRITE is 0b10 = 0x80.
 *             The lower six bits are the payload BYTE count, writes * 4.
 *   byte 1..  that many bytes: [reg, value, cycles_hi, cycles_lo] each.
 *   Fifteen writes is the most that fits: 1 + 15 * 4 = 61, under the 64 byte
 *   packet.
 *
 * The firmware treats a command byte whose low six bits are zero, a bare 0x80,
 * as a *single* cycled write and ignores the rest of the packet. So a batch has
 * to carry the byte count; `{ batch: false }` uses the single write form and
 * one transfer per write.
 *
 * Throughput is the whole difficulty here, and it is the number of transferOut
 * calls rather than the bytes: each one is a round trip through the browser's
 * USB plumbing. So several are in flight at once (MAX_INFLIGHT) and several
 * commands ride in each (COALESCE). Both numbers were set from measurements
 * taken with web/bench.html on a real board; see their comments. Falling behind
 * drops the oldest queued packet rather than lagging by seconds.
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

const USBSID_VID   = 0xcafe;
const USBSID_PID   = 0x4011;
const DEVICE_CLASS = 0xFF;   /* the vendor interface carrying the SID bus */
const CTRL_TRANSFER = 0x22;  /* WebUSB enable request */
const CTRL_ENABLE   = 0x01;

const CYCLED_WRITE = 2;                    /* top two bits: type */
const CYCLED_CMD   = (CYCLED_WRITE << 6);  /* 0x80 */
const COMMAND      = 3;
const CMD          = (COMMAND << 6);       /* 0xC0 */
/* COMMAND sub commands, globals.h */
const MUTE      = 12;
const UNMUTE    = 13;
const RESET_SID = 14;
const CONFIG    = 18;                 /* the sub type carrying config commands */
const CFG_CMD   = (CMD | CONFIG);     /* 0xD2 */
const SET_CLOCK = 0x50;
/** Clock ids, the same order the firmware's own table uses. */
export const CLOCK = { DEFAULT: 0, PAL: 1, NTSC: 2, DREAN: 3, NTSC2: 4 };

const WRITE_BYTES = 4;
const MAX_PACKET  = 64;
const MAX_FRAMES  = 15;   /* 1 + 15 * 4 = 61 bytes */
/* Queued but unsent packets, about a kilobyte. At rest this holds one or two;
 * a sustained overflow means we are behind the device, and dropping the oldest
 * caps the latency instead of letting it grow without bound. */
const MAX_QUEUE   = 256;
/* Transfers submitted but not yet completed. See _pump() for why this is not
 * one: a digi needs about 2200 packets a second and a serialised round trip
 * cannot do half of it.
 *
 * Eight was not enough either. Measured with web/bench.html on a digi: the
 * queue reached 172, the player's backpressure stopped it stepping 449 times in
 * twenty seconds, and the board went up to 85 ms without a write, which is four
 * frames. Emulation load was 5%, so nothing was short of CPU; the transfers
 * simply were not completing fast enough. At eight in flight that needs a round
 * trip under about 3.9 ms and Chrome's WebUSB does not manage it. */
const MAX_INFLIGHT = 32;

/**
 * Commands sent in one transferOut.
 *
 * The bottleneck is the number of calls, not the bytes: each transferOut is a
 * round trip through Chrome's USB plumbing. The firmware reads with
 * `CFG_TUD_VENDOR_RX_BUFSIZE 0`, so `tud_vendor_rx_cb` fires once per 64 byte
 * endpoint packet and handles each on its own (repo/src/usbsid.c). A bulk
 * transfer larger than the endpoint is split into full 64 byte packets by the
 * host, so several commands travel together and arrive as separate commands, as
 * long as each is padded to exactly 64 so the split lands between them.
 *
 * A partly filled batch never waits longer than a frame: flush() empties it at
 * every frame boundary. The batch always ends on a short packet, never on a
 * whole multiple of 64; see _flushBatch() for why that matters.
 *
 * Set `coalesce: 1` on the transport to send one command per transfer, which is
 * what this did before, if a board ever disagrees about the packet split.
 */
const COALESCE = 8;

/**
 * Whether every command fills a whole 64 byte packet, last one included.
 *
 * This tracks the board's firmware and the two cannot disagree:
 *
 *   FIFO on  (CFG_TUD_VENDOR_RX_BUFSIZE > 0, USE_VENDOR_BUFFER): true.
 *     The FIFO is a byte stream with no packet framing. The firmware reads a
 *     chunk and `process_buffer` consumes only the count in the header,
 *     dropping the remainder, so one short packet desynchronises every read
 *     after it. A zero length packet is harmless here, because nothing
 *     reprocesses a stale buffer.
 *
 *   FIFO off: false.
 *     `tud_vendor_rx_cb` gets the packet directly and has no zero length
 *     guard, so a transfer that is a whole multiple of 64 risks a terminating
 *     zero length packet re-running the previous command. Ending short avoids
 *     it.
 *
 * **False**, and deliberately so even though the firmware now has the FIFO on.
 * The run that finally came out clean, eight commands a transfer with the
 * vendor port buffered, used short final packets, because that was the default
 * at the time. So short packets and a FIFO is the combination actually
 * measured working, and the desync above is a hazard on paper that did not
 * appear in practice: a batch's short packet lands where the reader has caught
 * up, not mid stream. Switching this to true would trade a verified setting for
 * an unverified one.
 *
 * If a desync ever does show, garbled registers rather than timing, this is the
 * first thing to try.
 *
 * The FIFO itself is what removed the crackle. Without it the firmware played
 * out each packet inside `tud_vendor_rx_cb` while the endpoint NAKed, so the
 * board could not accept the next packet until it had finished the last, and
 * that gap landed on top of the next write's pre delay. The CDC port the libusb
 * driver uses has always been buffered, which is why the command line player
 * was clean on the identical write stream. No amount of host side work reached
 * it: coalescing one command a transfer against eight changed the transfer rate
 * from 2173 a second to 296 and sounded exactly the same.
 */
const FULL_PACKETS = false;

const delay = (ms) => new Promise((r) => setTimeout(r, ms));

export class USBSIDWebUSBTransport {
  /**
   * @param {object} opts
   *   opts.batch     false to put one write in a packet (default: batched)
   *   opts.coalesce  commands per transferOut (default: COALESCE, 1 disables)
   *   opts.fullPackets  pad every command to 64, including the last. Defaults
   *                  to FULL_PACKETS, which tracks the firmware: true with the
   *                  vendor FIFO enabled, false without. Set it explicitly for
   *                  a board running older firmware.
   *   opts.device  an already open USBSID device to share instead of opening
   *                one of our own. Must have writeArrayAwait, writeArray or
   *                write taking a Uint8Array. This is how the player slots
   *                into an app that has already connected to the board.
   */
  constructor(opts = {}) {
    this.batch = opts.batch !== false;
    this._extDev = opts.device || null;
    this._dev = null;
    this._ifaceNum = null;
    this._epOut = null;
    this._epIn = null;
    this._open = false;
    this._pkt = new Uint8Array(MAX_PACKET);
    this._pktFrames = 0;
    /* Optional tap: called with (reg, value) for every write, so a host can
     * mirror the register state in a display. */
    this.onWrite = null;
    this._coalesce = Math.max(1, opts.coalesce || COALESCE);
    this._fullPackets = (opts.fullPackets === undefined)
      ? FULL_PACKETS : (opts.fullPackets === true);
    this._batch = null;
    this._batchCount = 0;
    this._batchLast = 0;
    this._q = [];
    this._inflight = 0;
    this._maxInflight = MAX_INFLIGHT;
    this._maxQ = MAX_QUEUE;
    this.resetUsbStats();
  }

  get isOpen() {
    if (this._extDev) return this._extDev.isOpen !== false;
    return this._open;
  }
  get productName() {
    if (this._extDev) return this._extDev.productName || 'USBSID-Pico';
    return this._dev ? (this._dev.productName || '') : '';
  }

  /**
   * Ask for a USBSID-Pico and open it. Must be called from a user gesture:
   * navigator.usb.requestDevice shows a picker and the browser refuses to
   * show one otherwise. With a device injected this only confirms it is there.
   */
  async connect() {
    if (this._extDev) { this._open = true; return this.isOpen; }
    if (this._open) return true;
    this._dev = await navigator.usb.requestDevice({
      filters: [{ vendorId: USBSID_VID, productId: USBSID_PID }],
    });
    return this._openDevice();
  }

  /**
   * Open a board the user has already granted, without a picker.
   *
   * `requestDevice()` shows a picker, so it needs a user gesture and a
   * document, which a worker has neither of. Permission is per origin though,
   * not per thread, so once the page has asked once `getDevices()` returns the
   * board anywhere. That is what lets the emulation and the USB writes live in
   * a worker together, which is the whole point of the worker: a backgrounded
   * main thread stops feeding the board, and switching tabs is audible.
   *
   * Returns false when nothing has been granted yet, which is the caller's cue
   * to ask on the main thread first.
   */
  async connectGranted() {
    if (this._extDev) { this._open = true; return this.isOpen; }
    if (this._open) return true;
    if (typeof navigator === 'undefined' || !navigator.usb) return false;

    const devices = await navigator.usb.getDevices();
    this._dev = devices.find((d) => d.vendorId === USBSID_VID &&
                                    d.productId === USBSID_PID) || null;
    if (!this._dev) return false;
    return this._openDevice();
  }

  async _openDevice() {
    await this._dev.open();
    if (this._dev.configuration === null) await this._dev.selectConfiguration(1);

    /* Walk the configuration for the vendor interface and its bulk endpoints
     * rather than assuming numbers: they move between firmware builds. */
    for (const iface of this._dev.configuration.interfaces) {
      for (const alt of iface.alternates) {
        if (alt.interfaceClass === DEVICE_CLASS) {
          this._ifaceNum = iface.interfaceNumber;
          for (const ep of alt.endpoints) {
            if (ep.direction === 'out') this._epOut = ep.endpointNumber;
            if (ep.direction === 'in')  this._epIn  = ep.endpointNumber;
          }
        }
      }
    }
    if (this._ifaceNum === null) { await this._dev.close(); return false; }

    await this._dev.claimInterface(this._ifaceNum);
    await this._dev.selectAlternateInterface(this._ifaceNum, 0);
    try { await this._dev.clearHalt('out', this._epOut); } catch (_) {}
    try { await this._dev.clearHalt('in',  this._epIn);  } catch (_) {}
    await this._dev.controlTransferOut({
      requestType: 'class',
      recipient:   'interface',
      request:     CTRL_TRANSFER,
      value:       CTRL_ENABLE,
      index:       this._ifaceNum,
    });
    /* SET_INTERFACE resets the bulk endpoints, and the first OUT after it can
     * be swallowed. Settle before the tune's first writes go out. */
    await delay(100);
    this._open = true;
    return true;
  }

  async disconnect() {
    this._open = false;
    this._q.length = 0;
    this._batchCount = 0;
    this._inflight = 0;
    if (this._extDev) { this._pktFrames = 0; return; }  // not ours to close
    if (this._dev) {
      try {
        if (this._ifaceNum !== null) await this._dev.releaseInterface(this._ifaceNum);
        await this._dev.close();
      } catch (_) {}
    }
    this._dev = null; this._epOut = null; this._epIn = null; this._ifaceNum = null;
    this._pktFrames = 0;
  }

  /* ---- the transport interface ----------------------------------------- */

  writeCycled(reg, val, cycles) {
    if (!this._open) return;
    if (this.onWrite) this.onWrite(reg, val);
    const chi = (cycles >> 8) & 0xFF;
    const clo = cycles & 0xFF;
    if (!this.batch) {
      this._send(new Uint8Array([CYCLED_CMD, reg & 0xFF, val & 0xFF, chi, clo]));
      return;
    }
    const o = 1 + this._pktFrames * WRITE_BYTES;   /* byte 0 is the header */
    this._pkt[o]     = reg & 0xFF;
    this._pkt[o + 1] = val & 0xFF;
    this._pkt[o + 2] = chi;
    this._pkt[o + 3] = clo;
    if (++this._pktFrames >= MAX_FRAMES) this._flushPacket();
  }

  /** Frame boundary: push a partly filled packet rather than holding it. */
  flush() {
    if (this.batch) this._flushPacket();
    this._flushBatch();
  }

  reset() {
    this._pktFrames = 0;
  }

  /* ---- device commands, six byte packets -------------------------------- */

  _command(sub, b1 = 0) {
    if (!this._open) return;
    this._flushBatch();   /* ordering: it must not overtake queued writes */
    this._send(new Uint8Array([CMD | (sub & 0x3F), b1, 0, 0, 0, 0]));
  }

  /**
   * Silence. Drops the backlog first, so nothing plays after the stop and the
   * reset is not queued behind it. b1 = 1 zeroes every register including the
   * volume, which is what actually stops a sustained note: pulsing the RES
   * line alone does not reliably do it.
   */
  resetSID() {
    this._pktFrames = 0;
    this._batchCount = 0;
    this._q.length = 0;
    this._command(RESET_SID, 1);
  }

  mute()   { if (this.batch) this._flushPacket(); this._command(MUTE); }
  unmute() { this._command(UNMUTE); }

  /** Set the board's SID clock. See the CLOCK map for the ids. */
  setClock(rateId) {
    if (!this._open) return;
    this._flushBatch();
    this._send(new Uint8Array([CFG_CMD, SET_CLOCK, rateId & 0xFF, 0, 0, 0]));
  }

  _flushPacket() {
    if (this._pktFrames === 0) return;
    const nbytes = this._pktFrames * WRITE_BYTES;
    this._pkt[0] = CYCLED_CMD | (nbytes & 0x3F);
    this._queueCommand(this._pkt.subarray(0, 1 + nbytes));
    this._pktFrames = 0;
  }

  /**
   * Add one command to the batch, and send the batch when it is full.
   *
   * Each command occupies a whole 64 byte slot so the host's split into
   * endpoint packets lands exactly between commands. The firmware reads the
   * byte count out of the header and ignores the padding.
   */
  _queueCommand(cmd) {
    if (this._coalesce <= 1) { this._send(cmd.slice()); return; }

    if (this._batch === null) {
      this._batch = new Uint8Array(this._coalesce * MAX_PACKET);
      this._batchCount = 0;
    }
    this._batch.set(cmd, this._batchCount * MAX_PACKET);
    /* the tail of the slot is whatever was there before, so clear it */
    this._batch.fill(0, this._batchCount * MAX_PACKET + cmd.length,
                     (this._batchCount + 1) * MAX_PACKET);
    this._batchLast = cmd.length;
    if (++this._batchCount >= this._coalesce) this._flushBatch();
  }

  /**
   * Send the batch, ending on a short packet.
   *
   * The length is `(n - 1) * 64 + the last command's own length`, never a whole
   * multiple of the endpoint size. That is deliberate. A transfer that is an
   * exact multiple may be terminated with a zero length packet, and the
   * firmware's receive callback has no guard for one:
   *
   *     webread = bufsize;
   *     memcpy(sid_buffer, buffer, bufsize);   // bufsize 0 copies nothing
   *     process_buffer(wusb_itf, &webread);    // still reads sid_buffer[0]
   *
   * so a zero length packet re-runs whatever command was left in the buffer:
   * a duplicated burst of writes carrying stale cycle counts, which on a cycle
   * exact digi is precisely the artefact this transport exists to avoid. A
   * final short packet terminates the transfer on its own and the question
   * never arises. Every command still starts on a 64 byte boundary, so the
   * host's split still lands between them.
   *
   * `fullPackets` inverts this, and must be set if the board's vendor FIFO is
   * ever enabled (CFG_TUD_VENDOR_RX_BUFSIZE and USE_VENDOR_BUFFER). A FIFO is a
   * byte stream with no packet framing: `vendor_task()` reads 64 bytes and
   * `process_buffer` consumes only the count in the header, dropping the rest,
   * so one short packet in the middle desynchronises every read after it. With
   * the FIFO on, a zero length packet is harmless instead, because nothing
   * reprocesses a stale buffer. The two settings go together, both or neither.
   */
  _flushBatch() {
    if (this._batch === null || this._batchCount === 0) return;
    const bytes = this._fullPackets
      ? this._batchCount * MAX_PACKET
      : (this._batchCount - 1) * MAX_PACKET + this._batchLast;
    this._send(this._batch.slice(0, bytes));
    this._batchCount = 0;
  }

  /**
   * How many packets are waiting to go out. The player reads this to decide
   * whether to emulate another frame: once the board is this far behind, more
   * frames only make the backlog deeper, and a backlog is a stretch of tune
   * that plays after the page thinks it stopped.
   */
  get queueDepth() { return this._q.length + this._inflight; }
  get queueLimit() { return this._maxQ; }

  _send(buf) {
    this._q.push(buf);
    if (this._q.length > this._maxQ) this._q.shift();
    this._pump();
  }

  /**
   * Keep several transfers in flight.
   *
   * One at a time is a throughput ceiling, not backpressure. A round trip to
   * `transferOut` is half a millisecond to a millisecond, so serialised it caps
   * out somewhere near a thousand packets a second. A digi writing 650
   * registers a frame needs 44 packets a frame, 2170 a second, so the queue
   * grew without bound and playback was throttled to whatever USB managed. The
   * host controller queues submissions on an endpoint in order and completes
   * them in order, which is the same thing the libusb driver relies on for its
   * async transfers, so the writes stay in sequence. The real backpressure is
   * the queue depth, which the player reads.
   */
  _pump() {
    while (this._inflight < this._maxInflight && this._q.length > 0) {
      const buf = this._q.shift();
      this._inflight++;
      const done = () => {
        if (this._inflight > 0) this._inflight--;
        this._noteCompletion();
        this._pump();
      };
      let p;
      try { p = this._transfer(buf); }
      catch (_) { done(); continue; }
      if (p && p.then) p.then(done, done);
      else done();
    }
  }

  /**
   * Whether the pipe to the board ever actually runs dry.
   *
   * The player's own numbers stop at the queue: they say whether writes were
   * produced and handed over on time, not whether they kept flowing out. If
   * JavaScript cannot keep a board fed the way a dedicated libusb thread can,
   * this is where it shows, and it shows as `emptied`: transfers completing
   * with nothing submitted behind them, which is the pipe draining and the
   * board being left to finish what it has.
   *
   * `maxGap` is the longest a completion ever went unanswered by another,
   * which is the same quantity for a pipe that is merely stuttering rather
   * than emptying.
   */
  _noteCompletion() {
    const s = this._usb;
    const now = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    s.completions++;
    if (s.last !== 0) {
      const gap = now - s.last;
      if (gap > s.maxGap) s.maxGap = gap;
    }
    s.last = now;
    if (this._inflight === 0) s.emptied++;
    if (this._inflight < s.minInflight) s.minInflight = this._inflight;
  }

  resetUsbStats() {
    this._usb = {
      completions: 0, emptied: 0, maxGap: 0, minInflight: this._maxInflight,
      last: 0,
      since: (typeof performance !== 'undefined') ? performance.now() : Date.now(),
    };
  }

  usbStats() {
    const s = this._usb;
    const now = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    const secs = Math.max(0.001, (now - s.since) / 1000);
    return {
      transfersPerSecond: s.completions / secs,
      emptied: s.emptied,
      maxCompletionGap: s.maxGap,
      minInflight: s.minInflight,
    };
  }

  /* One transfer, resolving when the device has taken the packet, which is
   * the backpressure. A shared device may only offer the fire and forget
   * writeArray, which resolves early; that still works, it just queues less. */
  _transfer(buf) {
    if (this._extDev) {
      const fn = this._extDev.writeArrayAwait
        || this._extDev.writeArray || this._extDev.write;
      return fn.call(this._extDev, buf);
    }
    return this._dev.transferOut(this._epOut, buf);
  }
}
