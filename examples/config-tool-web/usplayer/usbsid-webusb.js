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
/* The board's audio switch, v1.3+ PCBs only. Same [CFG_CMD, command, item, ...]
 * shape as SET_CLOCK. Here rather than only in the app's config panels because
 * those are WebUSB only, and this has to work over Web Serial too. */
const SET_AUDIO    = 0x89;   /* item: 0 mono, 1 stereo */
const TOGGLE_AUDIO = 0x88;
const GET_AUDIO    = 0x91;
/* What the firmware was compiled with, one byte of flags. config.h: bit 0
 * Pico2, 1 wifi/bluetooth, 2 RGB LED, 3 PIO UART, 6 Cynthcart, **bit 7 the
 * embedded SID player**. */
const US_FEATURES  = 0x82;
export const FEATURE_SIDPLAYER = 1 << 7;

/* The onboard player: upload a file, then drive it. Same sub commands and the
 * same 64 byte slots as the serial transport uses, because it is the same
 * firmware answering; only the way a packet reaches it differs. */
const UPLOAD_SID_START    = 0xD0;
const UPLOAD_SID_DATA     = 0xD1;
const UPLOAD_SID_END      = 0xD2;
const UPLOAD_SID_SIZE     = 0xD3;
const UPLOAD_SID_PLAYTIME = 0xD4;
const SID_PLAYER_TUNE     = 0xE0;
const SID_PLAYER_START    = 0xE1;
const SID_PLAYER_STOP     = 0xE2;
const SID_PLAYER_PAUSE    = 0xE3;
const SID_PLAYER_NEXT     = 0xE4;
const SID_PLAYER_PREV     = 0xE5;
const SID_PLAYER_TWO      = 0xE6;
/* 0xE7 SID_PLAYER_FFWD is deliberately absent: the firmware marks it non
   functional and its emu_ffwd() call is commented out, because on the device the
   SID writes are the pacing and there is nothing to skip until the embedded build
   has a pacer. Nothing should offer a control for it. */
const SID_PLAYER_MUTE     = 0xE9;
const SID_PLAYER_MUTED    = 0xEA;
const SID_PLAYER_TIME     = 0xEB;
const SLOT                = 64;
/* Config reads. The 12 byte socket buffer's layout is the driver's: byte 2 is
 * socket one, byte 5 socket two, high nibble 1 for present and low nibble 1
 * for a socket carrying two chips. See USBSID_GetSocketNumSIDS(). */
const READ_SOCKETCFG = 0x37;
const READ_FMOPLSID  = 0x3A;
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
 * How long to wait for an answer to a config read.
 *
 * `transferIn` does not time out. If the board never answers, and a config
 * command the firmware does not implement is exactly that, the promise never
 * settles and whoever awaited it waits for ever. That is not hypothetical: it
 * hung the worker player's `init` indefinitely, which looked like the worker
 * failing to start, because `readBoardConfig()` asks two questions and the
 * second went unanswered.
 *
 * The board replies in well under a millisecond when it is going to reply at
 * all, so this is generous rather than tight.
 */
const CONFIG_TIMEOUT_MS = 250;

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

/** Chips per socket, out of the board's 12 byte socket buffer. */
function parseSocketConfig(b) {
  const chips = (byte) =>
    (((byte & 0xf0) >> 4) === 1) ? (((byte & 0x0f) === 1) ? 2 : 1) : 0;
  return { one: b.length > 2 ? chips(b[2]) : 0,
           two: b.length > 5 ? chips(b[5]) : 0 };
}

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
    /* $80 and above are the FM/OPL addresses that no SID claimed, which the
     * emulation forwards so the ASID transport can carry them in its own
     * message. They are not SID registers and this transport talks to a board,
     * so it drops them: sending $80 as a register would be junk on the bus. */
    if (reg >= 0x80) return;
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

  /**
   * Mono or stereo on the board's audio switch. v1.3+ PCBs only; older boards
   * ignore it, which is why nothing here checks the PCB version: a board that
   * cannot do it is not harmed by being asked.
   *
   * @param {boolean} stereo true for stereo, false for mono
   */
  setAudioSwitch(stereo) {
    if (!this._open) return;
    this._flushBatch();
    this._send(new Uint8Array([CFG_CMD, SET_AUDIO, stereo ? 1 : 0, 0, 0, 0]));
  }

  /** Flip whatever the audio switch currently is. */
  toggleAudioSwitch() {
    if (!this._open) return;
    this._flushBatch();
    this._send(new Uint8Array([CFG_CMD, TOGGLE_AUDIO, 0, 0, 0, 0]));
  }

  /**
   * Ask the board a config question and read the answer.
   *
   * transferIn asks for a whole packet, not the byte or twelve actually
   * wanted: Chrome's WebUSB on Linux does not reliably complete a bulk IN
   * shorter than wMaxPacketSize, which the config tool's own driver ran into
   * and documents. Returns null when there is nothing to ask, which includes
   * a shared device, whose owner does its own reads.
   */
  /**
   * @param sub  the config command
   * @param len  how many bytes to ask for. Match what the firmware sends for that
   *             command: SID_PLAYER_TIME and SID_PLAYER_MUTED answer with 4, the
   *             config and socket reads with a full packet. Asking for more than
   *             the board sends is not free, so the callers say.
   */
  async _configRead(sub, len = MAX_PACKET) {
    if (!this._open || this._extDev || !this._dev) return null;
    this._flushBatch();
    try {
      await this._dev.transferOut(this._epOut,
        new Uint8Array([CFG_CMD, sub, 0, 0, 0, 0]));
      /* Bounded: see CONFIG_TIMEOUT_MS. A null here means "the board did not
       * answer", which every caller already treats as "do not know" rather
       * than as an error.
       *
       * The losing transferIn is left pending, which is the one wart: if a late
       * reply does turn up it will satisfy the next read instead. That is why
       * readBoardConfig() stops asking after the first unanswered question
       * rather than carrying on and misreading the answers. */
      const r = await Promise.race([
        this._dev.transferIn(this._epIn, len),
        new Promise((res) => setTimeout(() => res(null), CONFIG_TIMEOUT_MS)),
      ]);
      if (!r || !r.data || r.data.byteLength === 0) return null;
      return new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
    } catch (e) {
      return null;
    }
  }

  /**
   * What the firmware was built with, or null when it will not say.
   *
   * See FEATURE_SIDPLAYER: a board without the embedded player takes an upload
   * and plays nothing, which looks exactly like a bad file.
   */
  async features() {
    const r = await this._configRead(US_FEATURES);
    return (r && r.length >= 1) ? r[0] : null;
  }

  /** Does this board carry the onboard SID player? null when it will not say. */
  async hasSidPlayer() {
    const f = await this.features();
    return (f === null) ? null : (f & FEATURE_SIDPLAYER) !== 0;
  }

  /* ---- the onboard player ---------------------------------------------- */

  /**
   * One command packet, now, past the write queue entirely.
   *
   * **Not `_send()`.** That queue is for register writes, and when it is full it
   * drops its oldest entry rather than lag the tune, which is right for sound
   * and ruinous for a file: a 61566 byte tune is 993 packets posted back to
   * back, and the board received 17422 bytes of it. `uploadSIDFile()` over Web
   * Serial had always been correct because that transport writes each packet
   * straight out and waits for it.
   *
   * So this waits for each transfer. It is slower than the queue by design; an
   * upload is a second of one-off work, not something in the audio path.
   */
  async _sendNow(sub, payload = null) {
    if (!this._open) return false;
    this._flushBatch();
    const pkt = new Uint8Array(SLOT);
    pkt[0] = CFG_CMD;
    pkt[1] = sub & 0xFF;
    if (payload) pkt.set(payload.subarray(0, SLOT - 2), 2);
    try { await this._transfer(pkt); return true; }
    catch (_) { return false; }
  }

  playerLoadTune(subtune) {
    /* The firmware counts subtunes from zero. */
    const t = new Uint8Array([subtune & 0xFF]);
    return this._sendNow(SID_PLAYER_TUNE, t);
  }
  playerStart()     { return this._sendNow(SID_PLAYER_START); }
  playerStop()      { return this._sendNow(SID_PLAYER_STOP); }
  playerPause()     { return this._sendNow(SID_PLAYER_PAUSE); }
  playerNext()      { return this._sendNow(SID_PLAYER_NEXT); }
  playerPrev()      { return this._sendNow(SID_PLAYER_PREV); }
  playerSocketTwo() { return this._sendNow(SID_PLAYER_TWO); }

  /**
   * How long the tune just uploaded should run, in milliseconds.
   *
   * Send after the upload. Without it the board stops after five minutes, which
   * is its own default and not the tune's length. UPLOAD_SID_START zeroes both the
   * position and the maximum, and SID_PLAYER_STOP puts the maximum back to five
   * minutes, so this belongs with every upload rather than once per session.
   */
  playerSetPlaytime(ms) {
    const v = Math.max(0, Math.round(ms)) >>> 0;
    return this._sendNow(UPLOAD_SID_PLAYTIME, new Uint8Array([
      (v >>> 24) & 0xFF, (v >>> 16) & 0xFF, (v >>> 8) & 0xFF, v & 0xFF,
    ]));
  }

  /**
   * The board's play position in milliseconds, or null when it will not say.
   *
   * The firmware only samples the player while it is playing, so the value holds
   * at the last live reading once playback stops rather than dropping to zero.
   * UPLOAD_SID_START resets it, so it cannot carry a stale position from the
   * previous tune into a new one.
   *
   * One read at a time. `_configRead()` leaves a timed out read pending, and a
   * late reply then satisfies the *next* one, so a caller polling this must not
   * let two overlap: hence the guard rather than trusting the caller.
   */
  async playerTime() {
    if (this._timeBusy) return null;
    this._timeBusy = true;
    try {
      const r = await this._configRead(SID_PLAYER_TIME, 4);
      if (!r || r.length < 4) return null;
      return ((r[0] << 24) | (r[1] << 16) | (r[2] << 8) | r[3]) >>> 0;
    } finally {
      this._timeBusy = false;
    }
  }

  /**
   * Mute or unmute one voice, or everything.
   *
   *   chip 1..4, voice 1..3   that voice, masked on the way out
   *   chip 1..4, voice 0      that whole chip, by dropping its writes
   *   chip 0,    voice 0      every chip and every voice
   *
   * Two mechanisms, not degrees of one. A voice mute masks the gate and the
   * sustain and lets every other write through. A chip mute drops the chip's
   * writes, which is the only one of the two that reaches $18, so it is what
   * silences a tune playing samples through the volume register.
   */
  playerMute(chip, voice, mute) {
    return this._sendNow(SID_PLAYER_MUTE,
      new Uint8Array([chip & 0xFF, voice & 0xFF, mute ? 1 : 0]));
  }

  /** Every voice of every chip. */
  playerMuteAll(mute) { return this.playerMute(0, 0, mute); }

  /**
   * One whole chip, in one command.
   *
   * Sent as voice 0, which set_mutestate() routes to usplayer_set_chip_mute().
   * This used to send the three voices, because the firmware had no chip form.
   */
  playerMuteChip(chip, mute) { return this.playerMute(chip, 0, mute); }

  /**
   * The mute state as one bitmask per chip, bits 0 to 2 for voices 1 to 3, or
   * null. Reads zeros for every chip while nothing is playing: the firmware only
   * asks the player when it is running.
   */
  async playerMuteState() {
    if (this._timeBusy) return null;
    this._timeBusy = true;
    try {
      const r = await this._configRead(SID_PLAYER_MUTED, 4);
      if (!r || r.length < 4) return null;
      return [r[0], r[1], r[2], r[3]];
    } finally {
      this._timeBusy = false;
    }
  }


  /**
   * Send a whole file to the board's own player and start it.
   *
   * Seven steps in this order: stop, START, the data in 62 byte pieces, END,
   * SIZE, load the subtune, start. 62 and not 64 because two bytes of every
   * packet are the command and the sub command.
   *
   * @param {Uint8Array} bytes     the file
   * @param {number} subtune       counted from one, as a host counts songs
   * @param {number} fileType      1 for a SID file
   * @param {function} [onProgress] called with (sent, total)
   */
  async uploadSIDFile(bytes, subtune = 1, fileType = 0x01, onProgress = null) {
    if (!this._open) return false;
    const CHUNK = SLOT - 2;
    const total = bytes.length;

    if (!await this.playerStop()) return false;
    if (!await this._sendNow(UPLOAD_SID_START, new Uint8Array([fileType]))) return false;

    let sent = 0;
    while (sent < total) {
      const end = Math.min(sent + CHUNK, total);
      if (!await this._sendNow(UPLOAD_SID_DATA, bytes.subarray(sent, end))) return false;
      sent = end;
      if (onProgress) onProgress(sent, total);
    }

    if (!await this._sendNow(UPLOAD_SID_END)) return false;
    if (!await this._sendNow(UPLOAD_SID_SIZE,
          new Uint8Array([(total >> 8) & 0xFF, total & 0xFF]))) return false;
    if (!await this.playerLoadTune(subtune > 0 ? subtune - 1 : 0)) return false;
    return await this.playerStart();
  }

  /**
   * What the board is carrying: chips per socket, and which one answers the
   * FM/OPL addresses.
   *
   * The command line player reads exactly this at connect and hands it to the
   * emulation, which is how `$df40`/`$df50` reach a chip at all. Without it an
   * FM/OPL tune plays its SID voices and none of its OPL.
   *
   * A shared device is asked through its own driver rather than behind its
   * back, since that is the object the host app is already talking to.
   */
  async readBoardConfig() {
    if (this._extDev) {
      /* Through the host's own driver. It has readFMOplSID() but no socket
       * read of its own, so the socket buffer goes through its generic
       * configCmdRead(), which answers with an array of packets. */
      const d = this._extDev;
      try {
        let parsed = null;
        if (typeof d.configCmdRead === 'function') {
          const packets = await d.configCmdRead(READ_SOCKETCFG, 0, 0, 0, 0, MAX_PACKET);
          if (packets && packets.length) parsed = parseSocketConfig(packets[0]);
        }
        const fm = (typeof d.readFMOplSID === 'function') ? await d.readFMOplSID() : 0;
        return {
          sidsSocketOne: parsed ? parsed.one : 0,
          sidsSocketTwo: parsed ? parsed.two : 0,
          fmoplSid: fm || -1,
        };
      } catch (_) { return null; }
    }

    const cfg = await this._configRead(READ_SOCKETCFG);
    if (!cfg) return null;
    const parsed = parseSocketConfig(cfg);
    /* If this one goes unanswered the sockets are still known and worth having:
     * only FM/OPL is lost, and an fmoplSid of -1 is the same as a board with no
     * FM chip. Reporting the sockets beats reporting nothing. */
    const fm = await this._configRead(READ_FMOPLSID);
    if (!fm) {
      console.warn('[usbsid-webusb] the board did not answer the FM/OPL read; ' +
                   'FM/OPL tunes will play their SID voices only');
    }
    return {
      sidsSocketOne: parsed.one,
      sidsSocketTwo: parsed.two,
      fmoplSid: (fm && fm.length) ? (fm[0] || -1) : -1,
    };
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
      /* This one really does keep transfers outstanding, up to MAX_INFLIGHT, so
       * `emptied` and `minInflight` mean what they say here. */
      pipelined: true,
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
