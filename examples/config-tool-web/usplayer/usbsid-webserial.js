/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usbsid-webserial.js
 * The same transport over the Web Serial API, which is what Firefox has.
 *
 * Firefox shipped Web Serial in 151 on macOS, Windows and Linux, enabled by
 * default, after Mozilla moved its standards position from "harmful" to
 * neutral. It has no WebUSB and no sign of it, so this is the whole of the
 * answer for that browser. Chromium has both and keeps using WebUSB; see
 * usbsid-transport.js for why, which is no longer about speed.
 *
 * Not on Firefox for Android, so mobile stays Chromium only.
 *
 *
 * WHY THIS IS NOT A REWRITE
 *
 * Web Serial hands the browser the board's CDC ACM interface, endpoints 0x02
 * and 0x82. That is exactly what the desktop driver uses: lib/driver/USBSID.h
 * defaults to those and only switches to the vendor interface's 0x04/0x84
 * under USE_VENDOR_ITF. So the command encoding here is the command line
 * player's, unchanged, and the firmware reaches it through the same
 * process_buffer() as the vendor port.
 *
 *
 * FRAMING, WHICH IS THE ONE THING THAT MATTERS
 *
 * The firmware's CDC receive path (repo/src/usbsid.c tud_cdc_rx_cb) reads the
 * FIFO, hands it to process_buffer(), and process_buffer() decodes **exactly
 * one** command out of sid_buffer[0] and returns. It never walks the buffer.
 * tud_cdc_n_read_flush() then throws away whatever was left. So two commands
 * in one read is one command executed and one silently dropped.
 *
 * What makes that safe rather than fatal is CFG_TUD_CDC_RX_BUFSIZE, which is
 * **64**, one endpoint packet (repo/src/tusb_config.h). The FIFO physically
 * cannot hold two packets, so the endpoint NAKs until the firmware has drained
 * the first, and the host retries. One packet in, one command out, enforced by
 * the buffer size rather than by timing.
 *
 * Therefore: **every command occupies exactly 64 bytes.** A serial port is a
 * continuous byte stream that the browser packs into full endpoint packets back
 * to back, so a stream made only of 64 byte units puts exactly one command in
 * every packet, for as long as the stream stays aligned. Many commands can ride
 * in one write() call, which is what keeps the call count down.
 *
 * This is easier than the WebUSB side, not harder. There, transfer boundaries
 * are visible to the device and a transfer that is a whole multiple of 64 may
 * be terminated by a zero length packet, which the vendor callback has no guard
 * for and which re-runs a stale buffer. A byte stream has no transfer
 * boundaries to get wrong, and the CDC callback guards the empty case anyway:
 * it tests tud_cdc_n_available() before reading.
 *
 * The one way to lose is to write anything that is not a multiple of 64, ever.
 * That shifts every later command off its packet boundary and the board reads
 * garbage from then on. _send() is the only place that writes and it asserts
 * the invariant.
 *
 *
 * THROUGHPUT, MEASURED 2026-08-10
 *
 * Not the risk it was expected to be. Twelve legs on a board, three tunes, main
 * thread and worker, WebUSB against this: see _project/20260810-bench_results.md.
 *
 * Both transports keep up on everything, nothing starved, nothing blocked,
 * nothing dropped, 50.1 frames a second throughout. On the worst drain gap, the
 * number that says how long the board was left without writes, this one wins
 * four of six pairings by the bench's own tenth of a frame margin and ties the
 * other two. It never loses. Its queue also sits at half the depth on the
 * demanding tunes, 7 to 9 against 16 to 17, which is the same fact from the
 * other end: the stream drains faster than the emulation fills it.
 *
 * Byte rate is the same, about 144 to 150 kB/s, at the same 298 to 301 writes a
 * second. **That is the useful finding**: two unrelated browser APIs landing on
 * the same ceiling means the ceiling is not the API. The command line player
 * reaches 275 kB/s on the same board, so what is left is the per call cost in
 * JavaScript and the 64 bytes every command occupies, both of which these two
 * share.
 *
 * The way past it is in the firmware, not here: a process_buffer() that looped,
 * advancing by the byte count in each header, would let one packet carry several
 * commands and remove the 64 bytes per command floor for both transports at
 * once. Worth doing only if a tune ever needs more than 150 kB/s, and none of
 * the three measured does.
 *
 * COALESCE of 8 was carried over from the WebUSB side as a guess and the
 * measurement gives no reason to change it.
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

const USBSID_VID = 0xcafe;
const USBSID_PID = 0x4011;

/* Meaningless over USB CDC, and required by the API. Do not tune it. */
const BAUD_RATE = 115200;

/**
 * Whether to narrow the port picker to USBSID-Pico's ids.
 *
 * **False**, and that is a deliberate choice against the obvious one.
 *
 * `requestPort({ filters })` matches on the ids Web Serial reports for a port,
 * and a port whose ids the browser does not fill in matches nothing. The
 * picker then says "No serial ports available", which is the same thing it says
 * when there genuinely is no port: a filter that is not working looks exactly
 * like a board that is not there, and there is no way to tell from inside the
 * page which one happened.
 *
 * That failure mode is worse than an unfiltered list. Firefox's implementation
 * is new, a desktop typically has one or two serial ports at most, and probe()
 * identifies the board properly anyway by asking it a question only it answers.
 * So the picker shows everything and the wrong choice is caught a moment later,
 * with a message that says what went wrong.
 *
 * Set true if a machine has enough serial ports for the list to be a nuisance,
 * having first confirmed the board's ids actually show up in getInfo().
 */
const FILTER_BY_IDS = false;

const CYCLED_WRITE = 2;
const CYCLED_CMD   = (CYCLED_WRITE << 6);  /* 0x80 */
const COMMAND      = 3;
const CMD          = (COMMAND << 6);       /* 0xC0 */
const MUTE      = 12;
const UNMUTE    = 13;
const RESET_SID = 14;
const CONFIG    = 18;
const CFG_CMD   = (CMD | CONFIG);          /* 0xD2 */
const SET_CLOCK = 0x50;
/* The board's audio switch, v1.3+ PCBs only. Same [CFG_CMD, command, item, ...]
 * shape as SET_CLOCK. Here rather than only in the app's config panels because
 * those are WebUSB only, and this has to work over Web Serial too. */
const SET_AUDIO    = 0x89;   /* item: 0 mono, 1 stereo */
const TOGGLE_AUDIO = 0x88;
const GET_AUDIO    = 0x91;
const READ_SOCKETCFG = 0x37;
const READ_FMOPLSID  = 0x3A;

/* The onboard SID player, the firmware's own. Same encoding as everything else
 * here: one CONFIG command per 64 byte slot. See repo/src/config.c. */
const UPLOAD_SID_START = 0xD0;
const UPLOAD_SID_DATA  = 0xD1;
const UPLOAD_SID_END   = 0xD2;
const UPLOAD_SID_SIZE  = 0xD3;
const SID_PLAYER_TUNE  = 0xE0;
const SID_PLAYER_START = 0xE1;
const SID_PLAYER_STOP  = 0xE2;
const SID_PLAYER_PAUSE = 0xE3;
const SID_PLAYER_NEXT  = 0xE4;
const SID_PLAYER_PREV  = 0xE5;
const SID_PLAYER_TWO   = 0xE6;

/** Clock ids, the same order the firmware's own table uses. */
export const CLOCK = { DEFAULT: 0, PAL: 1, NTSC: 2, DREAN: 3, NTSC2: 4 };

const WRITE_BYTES = 4;
/* The endpoint packet, the CDC RX FIFO and therefore the size of every command
 * on the wire. All three are 64 and they are the same 64. */
const SLOT       = 64;
const MAX_FRAMES = 15;    /* 1 + 15 * 4 = 61 bytes, the most that fits a slot */
const MAX_QUEUE  = 256;

/* Commands per write() call. Each still gets its own 64 byte slot and its own
 * endpoint packet; this only decides how many go in one call into the stream.
 * Carried over from the WebUSB side, and measured 2026-08-10 as giving nothing
 * away: same byte rate, shallower queue, equal or better drain gap. */
const COALESCE = 8;

/* How long to wait for a config reply before giving up. The board answers in
 * well under a millisecond when it is the right port; this is long enough to
 * be sure and short enough that probing three ports is not tedious. */
const REPLY_TIMEOUT_MS = 250;

/** Chips per socket, out of the board's 12 byte socket buffer. */
function parseSocketConfig(b) {
  const chips = (byte) =>
    (((byte & 0xf0) >> 4) === 1) ? (((byte & 0x0f) === 1) ? 2 : 1) : 0;
  return { one: b.length > 2 ? chips(b[2]) : 0,
           two: b.length > 5 ? chips(b[5]) : 0 };
}

/** Is this browser able to use this transport at all? */
export function webSerialAvailable() {
  return (typeof navigator !== 'undefined') && !!navigator.serial;
}

export class USBSIDWebSerialTransport {
  /**
   * @param {object} opts
   *   opts.batch     false to put one write in a command (default: batched)
   *   opts.coalesce  commands per write() call (default: COALESCE)
   *   opts.port      an already open SerialPort to adopt instead of asking
   *   opts.probe     false to accept whatever port was chosen without asking
   *                  the board to confirm itself (default: probe)
   */
  constructor(opts = {}) {
    this.batch = opts.batch !== false;
    this._port = opts.port || null;
    this._adopted = !!opts.port;
    this._probeOnConnect = opts.probe !== false;
    /** Why the last connect failed, for the page to show. */
    this.lastError = null;
    this._writer = null;
    this._reader = null;
    this._open = false;

    this._pkt = new Uint8Array(SLOT);
    this._pktFrames = 0;

    /* Optional tap: called with (reg, value) for every write, so a host can
     * mirror the register state in a display. */
    this.onWrite = null;

    this._coalesce = Math.max(1, opts.coalesce || COALESCE);
    this._batch = null;
    this._batchCount = 0;

    this._q = [];
    this._inflight = 0;
    this._draining = false;
    this._maxQ = MAX_QUEUE;

    /* Incoming bytes, accumulated. A serial port has no packet boundaries, so
     * a reply arrives in however many pieces it arrives in. */
    this._rx = new Uint8Array(0);
    this._rxWaiters = [];

    this.resetUsbStats();
  }

  get isOpen() { return this._open; }
  get productName() {
    if (!this._port || !this._port.getInfo) return '';
    const i = this._port.getInfo();
    /* Web Serial exposes the ids, never a product string. */
    return (i && i.usbVendorId)
      ? `USBSID-Pico (${i.usbVendorId.toString(16)}:${i.usbProductId.toString(16)})`
      : '';
  }

  /**
   * Ask for a port and open it. Must be called from a user gesture:
   * requestPort() shows a picker and the browser refuses to show one
   * otherwise, exactly as requestDevice() does.
   *
   * The filter narrows the picker to USBSID-Pico, but it filters by **device**
   * and not by interface, so a build with the telemetry and USB UART ports
   * enabled offers three entries for one board and two of them are wrong. The
   * probe below is what sorts that out, and it doubles as the check that this
   * is a USBSID-Pico at all.
   */
  async connect() {
    if (this._open) return true;
    if (!this._port) {
      /* Only asking needs the API. A port handed in is already a port. */
      if (!webSerialAvailable()) return false;
      /* No filter by default. See the note on FILTER_BY_IDS: a filter that
       * matches nothing is indistinguishable, in the picker, from having no
       * ports at all. probe() is what identifies the board. */
      const opts = FILTER_BY_IDS
        ? { filters: [{ usbVendorId: USBSID_VID, usbProductId: USBSID_PID }] }
        : {};
      this._port = await navigator.serial.requestPort(opts);
    }
    return this._openPort();
  }

  /**
   * Open a port the user has already granted, without a picker.
   *
   * requestPort() needs a user gesture and a document, and a worker has
   * neither. Permission is per origin rather than per thread, so once the page
   * has asked, getPorts() returns the port anywhere, including in the worker.
   * That is the same arrangement connectGranted() uses on the WebUSB side and
   * the reason the worker player needs no new structure for this.
   *
   * Returns false when nothing has been granted, which is the caller's cue to
   * ask on the main thread first.
   */
  async connectGranted() {
    if (this._open) return true;
    if (!this._port) {
      if (!webSerialAvailable()) return false;
      const ports = await navigator.serial.getPorts();
      this._port = ports.find((p) => {
        const i = p.getInfo ? p.getInfo() : null;
        return i && i.usbVendorId === USBSID_VID && i.usbProductId === USBSID_PID;
      }) || null;
    }
    if (!this._port) return false;
    return this._openPort();
  }

  async _openPort() {
    try {
      if (!this._adopted) {
        await this._port.open({ baudRate: BAUD_RATE, bufferSize: 4096 });
      }
      /* The line state the desktop driver sets by hand with a 0x21/0x22
       * control transfer of DTR|RTS. Web Serial has a method for it, and the
       * firmware will not talk without it. */
      if (this._port.setSignals) {
        await this._port.setSignals({ dataTerminalReady: true, requestToSend: true });
      }
      this._writer = this._port.writable.getWriter();
      this._startReader();
      this._open = true;
    } catch (e) {
      this.lastError = 'cannot open the port: ' + (e && e.message ? e.message : e);
      await this._teardown();
      return false;
    }

    /* With no filter on the picker the user can choose any serial port, and the
     * board has other CDC interfaces of its own when telemetry and the USB UART
     * are built in. Refusing a port that does not answer is the difference
     * between a clear message now and a tune that plays silently into a debug
     * probe. Nothing is playing at connect time, so the wait costs nothing. */
    if (this._probeOnConnect) {
      const ok = await this.probe();
      if (!ok) {
        this.lastError = 'that port did not answer as a USBSID-Pico. If the '
          + 'board offers several ports, try another.';
        await this._teardown();
        return false;
      }
    }
    this.lastError = null;
    return true;
  }

  /**
   * Is this actually the SID port?
   *
   * Ask the board a question it always answers. The telemetry and USB UART
   * CDC interfaces are on the same device and pass the picker's filter, so
   * this is the only way to tell them apart. Cheap, and worth doing before a
   * tune's init writes go somewhere that ignores them.
   */
  async probe() {
    const cfg = await this._configRead(READ_SOCKETCFG);
    return !!(cfg && cfg.length > 5);
  }

  async disconnect() { await this._teardown(); }

  async _teardown() {
    this._open = false;
    this._q.length = 0;
    this._batchCount = 0;
    this._inflight = 0;
    this._pktFrames = 0;
    for (const w of this._rxWaiters) w.reject(new Error('closed'));
    this._rxWaiters.length = 0;
    this._rx = new Uint8Array(0);

    if (this._reader) {
      try { await this._reader.cancel(); } catch (_) {}
      try { this._reader.releaseLock(); } catch (_) {}
      this._reader = null;
    }
    if (this._writer) {
      try { this._writer.releaseLock(); } catch (_) {}
      this._writer = null;
    }
    if (this._port && !this._adopted) {
      try { await this._port.close(); } catch (_) {}
      this._port = null;
    }
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
      this._queueCommand(
        new Uint8Array([CYCLED_CMD, reg & 0xFF, val & 0xFF, chi, clo]));
      return;
    }
    const o = 1 + this._pktFrames * WRITE_BYTES;   /* byte 0 is the header */
    this._pkt[o]     = reg & 0xFF;
    this._pkt[o + 1] = val & 0xFF;
    this._pkt[o + 2] = chi;
    this._pkt[o + 3] = clo;
    if (++this._pktFrames >= MAX_FRAMES) this._flushPacket();
  }

  /** Frame boundary: push a partly filled command rather than holding it. */
  flush() {
    if (this.batch) this._flushPacket();
    this._flushBatch();
  }

  reset() { this._pktFrames = 0; }

  /* ---- device commands -------------------------------------------------- */

  _command(sub, b1 = 0) {
    if (!this._open) return;
    this._queueCommand(new Uint8Array([CMD | (sub & 0x3F), b1, 0, 0, 0, 0]));
    this._flushBatch();   /* ordering: it must not sit behind queued writes */
  }

  /**
   * Silence. Drops the backlog first, so nothing plays after the stop and the
   * reset is not queued behind it. b1 = 1 zeroes every register including the
   * volume, which is what actually stops a sustained note.
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
    this._queueCommand(
      new Uint8Array([CFG_CMD, SET_CLOCK, rateId & 0xFF, 0, 0, 0]));
    this._flushBatch();
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
    /* _queueCommand and not _send: over CDC every command occupies a whole
     * 64 byte slot, and _send refuses anything that is not a multiple of one.
     * The first version of this copied the WebUSB transport, where _send takes
     * the six bytes raw, so the packet was rejected before it left the page and
     * the board never heard it while the app cheerfully logged success. */
    this._queueCommand(
      new Uint8Array([CFG_CMD, SET_AUDIO, stereo ? 1 : 0, 0, 0, 0]));
    this._flushBatch();
  }

  /** Flip whatever the audio switch currently is. */
  toggleAudioSwitch() {
    if (!this._open) return;
    this._queueCommand(new Uint8Array([CFG_CMD, TOGGLE_AUDIO, 0, 0, 0, 0]));
    this._flushBatch();
  }

  /**
   * Ask the board a config question and read the answer.
   *
   * This is the part with no WebUSB equivalent. transferIn() returns a packet;
   * a serial port returns bytes, in whatever pieces they arrive in, so the
   * reply has to be accumulated until it is whole. Reading "however much has
   * arrived" and parsing that is the bug that works on a fast machine and
   * fails intermittently elsewhere, reported as a board fault.
   */
  async _configRead(sub) {
    if (!this._open) return null;
    this._flushBatch();
    /* Discard anything stale so a late reply to a previous question cannot be
     * mistaken for the answer to this one. */
    this._rx = new Uint8Array(0);
    this._queueCommand(new Uint8Array([CFG_CMD, sub, 0, 0, 0, 0]));
    this._flushBatch();
    try {
      /* The socket buffer is 12 bytes, single value replies are 1. Wait for
       * the larger, settle for whatever arrived when the wait runs out: a one
       * byte answer is complete the moment it lands. */
      return await this._expect(12, REPLY_TIMEOUT_MS, 1);
    } catch (_) {
      return null;
    }
  }

  /**
   * What the board is carrying: chips per socket, and which one answers the
   * FM/OPL addresses. Without this an FM/OPL tune plays its SID voices and
   * none of its OPL.
   */
  async readBoardConfig() {
    const cfg = await this._configRead(READ_SOCKETCFG);
    if (!cfg) return null;
    const parsed = parseSocketConfig(cfg);
    const fm = await this._configRead(READ_FMOPLSID);
    return {
      sidsSocketOne: parsed.one,
      sidsSocketTwo: parsed.two,
      fmoplSid: (fm && fm.length) ? (fm[0] || -1) : -1,
    };
  }

  /* ---- the board's own onboard player ------------------------------------ *
   *
   * Nothing to do with the emulation: the file goes to the board and the RP2350
   * plays it. The host's part is an upload and six buttons.
   *
   * It maps onto this transport without any new framing, because the driver's
   * WebUSB version already sends each step as one 64 byte packet of
   * [0xD2, subcommand, payload...]. That is a command in a slot, which is what
   * everything here already is.
   *
   * These use _sendNow() rather than the write queue. The queue drops its oldest
   * entry past MAX_QUEUE, which is right for register writes, where a stale one
   * is worse than a missing one, and catastrophic for an upload, where a dropped
   * packet is a corrupted file with nothing to say so.
   */

  /** One 64 byte config packet, delivered rather than queued. */
  async _sendNow(sub, payload = null) {
    if (!this._open || !this._writer) return false;
    this._flushBatch();          /* keep ordering with anything queued */
    const pkt = new Uint8Array(SLOT);
    pkt[0] = CFG_CMD;
    pkt[1] = sub & 0xFF;
    if (payload) pkt.set(payload.subarray(0, SLOT - 2), 2);
    try {
      await this._writer.ready;
      await this._writer.write(pkt);
      return true;
    } catch (_) {
      this._open = false;
      return false;
    }
  }

  playerLoadTune(subtune) {
    /* The firmware counts subtunes from zero. */
    const t = new Uint8Array([0, subtune & 0xFF]);
    return this._sendNow(SID_PLAYER_TUNE, t);
  }
  playerStart()     { return this._sendNow(SID_PLAYER_START); }
  playerStop()      { return this._sendNow(SID_PLAYER_STOP); }
  playerPause()     { return this._sendNow(SID_PLAYER_PAUSE); }
  playerNext()      { return this._sendNow(SID_PLAYER_NEXT); }
  playerPrev()      { return this._sendNow(SID_PLAYER_PREV); }
  playerSocketTwo() { return this._sendNow(SID_PLAYER_TWO); }

  /**
   * Send a SID file to the board and start it.
   *
   * The same seven steps the WebUSB driver uses, in the same order: stop, START,
   * the data in 62 byte pieces, END, SIZE, load the subtune, start.
   *
   * 62 and not 64: two bytes of every packet are the command and the
   * subcommand, so a 64 byte slot carries 62 bytes of file. A 51 KB tune is
   * about 850 packets, and every one is awaited, so `writer.ready` paces the
   * upload against the board rather than filling a queue that would drop the
   * middle of the file.
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
      if (!await this._sendNow(UPLOAD_SID_DATA, bytes.subarray(sent, end))) {
        return false;
      }
      sent = end;
      if (onProgress) onProgress(sent, total);
    }

    if (!await this._sendNow(UPLOAD_SID_END)) return false;
    if (!await this._sendNow(UPLOAD_SID_SIZE,
          new Uint8Array([(total >> 8) & 0xFF, total & 0xFF]))) return false;
    if (!await this.playerLoadTune(subtune > 0 ? subtune - 1 : 0)) return false;
    return await this.playerStart();
  }

  /* ---- reading ---------------------------------------------------------- */

  _startReader() {
    const port = this._port;
    (async () => {
      while (this._open || this._reader === null) {
        if (!port.readable) break;
        let reader;
        try { reader = port.readable.getReader(); }
        catch (_) { break; }     /* already locked, which should not happen */
        this._reader = reader;
        try {
          for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            if (value && value.length) this._onBytes(value);
          }
        } catch (_) {
          /* a device unplugged mid read lands here */
        } finally {
          try { reader.releaseLock(); } catch (_) {}
        }
        if (this._reader !== reader) break;   /* torn down under us */
        this._reader = null;
        if (!this._open) break;
      }
    })();
  }

  _onBytes(chunk) {
    const merged = new Uint8Array(this._rx.length + chunk.length);
    merged.set(this._rx, 0);
    merged.set(chunk, this._rx.length);
    this._rx = merged;
    this._serveWaiters();
  }

  _serveWaiters() {
    while (this._rxWaiters.length > 0 && this._rx.length >= this._rxWaiters[0].n) {
      const w = this._rxWaiters.shift();
      if (w.timer) clearTimeout(w.timer);
      const out = this._rx.slice(0, w.n);
      this._rx = this._rx.slice(w.n);
      w.resolve(out);
    }
  }

  /**
   * Wait for n bytes.
   *
   * `atLeast` is what will do when the timeout runs out: the board's replies
   * are either 12 bytes or 1, and a one byte reply would otherwise always wait
   * out the full timeout before being handed over.
   */
  _expect(n, timeoutMs, atLeast = n) {
    return new Promise((resolve, reject) => {
      const w = { n, resolve, reject, timer: null };
      w.timer = setTimeout(() => {
        const i = this._rxWaiters.indexOf(w);
        if (i >= 0) this._rxWaiters.splice(i, 1);
        if (this._rx.length >= atLeast) {
          const out = this._rx.slice(0, this._rx.length);
          this._rx = new Uint8Array(0);
          resolve(out);
        } else {
          reject(new Error('no reply'));
        }
      }, timeoutMs);
      this._rxWaiters.push(w);
      this._serveWaiters();
    });
  }

  /* ---- writing ---------------------------------------------------------- */

  _flushPacket() {
    if (this._pktFrames === 0) return;
    const nbytes = this._pktFrames * WRITE_BYTES;
    this._pkt[0] = CYCLED_CMD | (nbytes & 0x3F);
    this._queueCommand(this._pkt.subarray(0, 1 + nbytes));
    this._pktFrames = 0;
  }

  /**
   * Add one command, in its own 64 byte slot.
   *
   * The slot is the whole point: see the note at the top of the file. The
   * firmware reads the byte count out of the header and ignores the padding,
   * and the padding is what keeps every later command on a packet boundary.
   */
  _queueCommand(cmd) {
    if (this._batch === null) {
      this._batch = new Uint8Array(this._coalesce * SLOT);
      this._batchCount = 0;
    }
    const at = this._batchCount * SLOT;
    this._batch.set(cmd, at);
    /* the tail of the slot is whatever was there before, so clear it */
    this._batch.fill(0, at + cmd.length, at + SLOT);
    if (++this._batchCount >= this._coalesce) this._flushBatch();
  }

  /**
   * Hand the batch to the stream, always a whole number of slots.
   *
   * Unlike the WebUSB side this must **not** end short. There, a short final
   * packet is what stops a zero length packet re-running a stale buffer. Here
   * there are no transfer boundaries to terminate, and a partial slot would
   * push every later command off its packet boundary for the life of the
   * connection.
   */
  _flushBatch() {
    if (this._batch === null || this._batchCount === 0) return;
    this._send(this._batch.slice(0, this._batchCount * SLOT));
    this._batchCount = 0;
  }

  /**
   * How many writes are waiting. The player reads this to decide whether to
   * emulate another frame: once the board is this far behind, more frames only
   * make the backlog deeper, and a backlog is a stretch of tune that plays
   * after the page thinks it stopped.
   */
  get queueDepth() { return this._q.length + this._inflight; }
  get queueLimit() { return this._maxQ; }

  _send(buf) {
    /* The invariant the whole framing rests on. A misaligned write corrupts
     * every command after it, so fail loudly here rather than quietly there. */
    if ((buf.length % SLOT) !== 0) {
      console.error('[usbsid-webserial] refusing a write of', buf.length,
                    'bytes, not a multiple of', SLOT);
      return;
    }
    this._q.push(buf);
    if (this._q.length > this._maxQ) this._q.shift();
    this._drain();
  }

  /**
   * Keep the stream fed without serialising on it.
   *
   * `writer.ready` is the backpressure and the only one on offer: it resolves
   * when the stream will take more. The write itself is deliberately not
   * awaited, so the next chunk can be queued while the last is still going
   * out, which is this API's equivalent of several transfers in flight.
   *
   * The real backpressure the player sees is still queueDepth.
   */
  async _drain() {
    if (this._draining) return;
    this._draining = true;
    try {
      while (this._open && this._q.length > 0 && this._writer) {
        const buf = this._q.shift();
        try {
          await this._writer.ready;
        } catch (_) {
          this._open = false;   /* the stream errored, the board is gone */
          break;
        }
        this._inflight++;
        const done = () => {
          if (this._inflight > 0) this._inflight--;
          this._noteCompletion();
        };
        let p;
        try { p = this._writer.write(buf); }
        catch (_) { done(); break; }
        if (p && p.then) p.then(done, done); else done();
      }
    } finally {
      this._draining = false;
      /* Anything queued while we were awaiting ready needs another pass. */
      if (this._open && this._q.length > 0 && this._writer) this._drain();
    }
  }

  /**
   * Whether the pipe to the board ever actually runs dry. `emptied` counts
   * writes completing with nothing queued behind them, which is the pipe
   * draining and the board being left to finish what it has. `maxGap` is the
   * longest a completion ever went unanswered, which is the same question for
   * a pipe that stutters rather than empties.
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
      completions: 0, emptied: 0, maxGap: 0, minInflight: 0x7fffffff,
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
      minInflight: (s.minInflight === 0x7fffffff) ? 0 : s.minInflight,
      /* This transport does not hold transfers in flight on purpose: it hands
       * chunks to a WritableStream and lets `writer.ready` be the backpressure.
       * So "fewest in flight" is always 0 and "pipe emptied" counts nearly every
       * write, and neither means what the same rows mean for WebUSB. Reading
       * them across transports says starvation where there is none: the queue
       * depth is *lower* and nothing starved. Flagged so the bench can say so
       * rather than inviting the misreading. */
      pipelined: false,
    };
  }
}
