/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * usbsid-webserial.js
 * The same transport over the Web Serial API (Firefox).
 *
 * Firefox 151 has Web Serial on macOS, Windows and Linux and no WebUSB
 * Not on Firefox for Android. See usbsid-transport.js for the transport choice
 *
 * Web Serial hands the browser the CDC ACM interface, endpoints 0x02 and 0x82
 * lib/driver/USBSID.h uses those too, the vendor interface 0x04/0x84 only under USE_VENDOR_ITF
 * Command encoding is the same as the command line player, reaching process_buffer() as on the vendor port
 *
 * FRAMING
 *
 * CDC rx path (repo/src/usbsid.c tud_cdc_rx_cb) reads the FIFO and calls process_buffer(),
 * which decodes exactly one command from sid_buffer[0]. tud_cdc_n_read_flush() drops the rest,
 * two commands in one read = one executed, one dropped
 * CFG_TUD_CDC_RX_BUFSIZE is 64 (repo/src/tusb_config.h), one endpoint packet. The FIFO cannot
 * hold two packets, the endpoint NAKs until the firmware drained the first
 *
 * Every command occupies exactly 64 bytes. The browser packs the stream into full endpoint
 * packets, a stream of 64 byte units puts one command in each packet
 * Many commands can ride in one write() call
 * No transfer boundaries, no ZLP issue as on WebUSB (the CDC callback checks
 * tud_cdc_n_available() before reading)
 * Never write anything that is not a multiple of 64, later commands shift off the packet
 * boundary. _send() asserts this
 *
 * THROUGHPUT
 *
 * WebUSB and Web Serial both measure about 144 to 150 kB/s at 298 to 301 writes a second
 * (50.1 frames a second). The command line player reaches 275 kB/s on the same board
 * A process_buffer() looping over the header byte counts would remove the 64 bytes per command floor
 * COALESCE 8 is the WebUSB value
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

const USBSID_VID = 0xcafe;
const USBSID_PID = 0x4011;

/* Required by the API, ignored over USB CDC */
const BAUD_RATE = 115200;

/* Narrow the port picker to USBSID-Pico ids, set true only if getInfo() reports them
 * False since a port without reported ids matches nothing and the picker then says
 * "No serial ports available", same as with no port. probe() identifies the board anyway */
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
/* Board audio switch, v1.3+ PCBs only. Same [CFG_CMD, command, item, ...] shape as SET_CLOCK */
const SET_AUDIO    = 0x89;   /* item: 0 mono, 1 stereo */
const TOGGLE_AUDIO = 0x88;
const GET_AUDIO    = 0x91;
const READ_SOCKETCFG = 0x37;
/* Firmware feature flags, one byte. config.h: bit 0 Pico2, 1 wifi/bluetooth,
 * 2 RGB LED, 3 PIO UART, 6 Cynthcart, 7 embedded SID player (needed by SendSID) */
const US_FEATURES    = 0x82;
export const FEATURE_SIDPLAYER = 1 << 7;
const READ_FMOPLSID  = 0x3A;

/* Onboard SID player, one CONFIG command per 64 byte slot. See repo/src/config.c */
const UPLOAD_SID_START = 0xD0;
const UPLOAD_SID_DATA  = 0xD1;
const UPLOAD_SID_END   = 0xD2;
const UPLOAD_SID_SIZE  = 0xD3;
const UPLOAD_SID_PLAYTIME = 0xD4;
const SID_PLAYER_TUNE  = 0xE0;
const SID_PLAYER_START = 0xE1;
const SID_PLAYER_STOP  = 0xE2;
const SID_PLAYER_PAUSE = 0xE3;
const SID_PLAYER_NEXT  = 0xE4;
const SID_PLAYER_PREV  = 0xE5;
const SID_PLAYER_TWO   = 0xE6;
const SID_PLAYER_FFWD  = 0xE7;
const SID_PLAYER_MUTE  = 0xE9;
const SID_PLAYER_MUTED = 0xEA;
const SID_PLAYER_TIME  = 0xEB;

/* Clock ids, same order as the firmware table */
export const CLOCK = { DEFAULT: 0, PAL: 1, NTSC: 2, DREAN: 3, NTSC2: 4 };

const WRITE_BYTES = 4;
/* Endpoint packet, CDC RX FIFO and command size on the wire, all 64 */
const SLOT       = 64;
const MAX_FRAMES = 15;    /* 1 + 15 * 4 = 61 bytes, the most that fits a slot */
const MAX_QUEUE  = 256;

/* Commands per write() call, each keeps its own 64 byte slot
 * Same value as WebUSB, same byte rate and a shallower queue */
const COALESCE = 8;

/* Config reply timeout, the board answers in well under a millisecond */
const REPLY_TIMEOUT_MS = 250;

/* Chips per socket from the 12 byte socket buffer */
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
   *   opts.batch     false for one write per command (default batched)
   *   opts.coalesce  commands per write() call (default COALESCE)
   *   opts.port      open SerialPort to adopt, no picker
   *   opts.probe     false to skip the board check on connect (default probe)
   */
  constructor(opts = {}) {
    this.batch = opts.batch !== false;
    this._port = opts.port || null;
    this._adopted = !!opts.port;
    this._probeOnConnect = opts.probe !== false;
    /* Why the last connect failed */
    this.lastError = null;
    this._writer = null;
    this._reader = null;
    this._open = false;

    this._pkt = new Uint8Array(SLOT);
    this._pktFrames = 0;

    /* Optional tap, called with (reg, value) for every write */
    this.onWrite = null;

    this._coalesce = Math.max(1, opts.coalesce || COALESCE);
    this._batch = null;
    this._batchCount = 0;

    this._q = [];
    this._inflight = 0;
    this._draining = false;
    this._maxQ = MAX_QUEUE;

    /* Incoming bytes, accumulated. A serial port has no packet boundaries, a reply arrives in pieces */
    this._rx = new Uint8Array(0);
    this._rxWaiters = [];

    this.resetUsbStats();
  }

  get isOpen() { return this._open; }
  get productName() {
    if (!this._port || !this._port.getInfo) return '';
    const i = this._port.getInfo();
    /* Web Serial exposes ids only, no product string */
    return (i && i.usbVendorId)
      ? `USBSID-Pico (${i.usbVendorId.toString(16)}:${i.usbProductId.toString(16)})`
      : '';
  }

  /* Ask for a port and open it, needs a user gesture (picker)
   * The picker lists several entries for one board when extra CDC ports are built in,
   * probe() finds the right one and checks it is a USBSID-Pico */
  async connect() {
    if (this._open) return true;
    if (!this._port) {
      /* Only asking needs the API */
      if (!webSerialAvailable()) return false;
      /* No filter by default, see FILTER_BY_IDS
       * probe() identifies the board */
      const opts = FILTER_BY_IDS
        ? { filters: [{ usbVendorId: USBSID_VID, usbProductId: USBSID_PID }] }
        : {};
      this._port = await navigator.serial.requestPort(opts);
    }
    return this._openPort();
  }

  /* Open an already granted port without a picker, usable in a worker
   * Same arrangement as connectGranted() on the WebUSB side
   * Returns false when nothing is granted yet */
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
      /* DTR|RTS, the desktop driver sets them with a 0x21/0x22 control transfer
       * The firmware will not talk without */
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

    /* Without a picker filter any serial port can be chosen and the board has other CDC
     * interfaces (USB UART). Refusing a port that does not answer gives a clear message
     * at connect, nothing is playing yet and the wait costs nothing */
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

  /* Is this the SID port? Ask a question only the SID port answers, other CDC interfaces
   * of the board pass the picker filter. Do this before a tune's init writes go to a port
   * that ignores them */
  async probe() {
    const cfg = await this._configRead(READ_SOCKETCFG);
    return !!(cfg && cfg.length > 5);
  }

  /* Firmware feature flags (us_features in config.h) or null. Bit 7 is the embedded SID player, without it a board takes an upload and plays nothing */
  async features() {
    const r = await this._configRead(US_FEATURES);
    return (r && r.length >= 1) ? r[0] : null;
  }

  /* Board carries the onboard SID player? null when unknown */
  async hasSidPlayer() {
    const f = await this.features();
    return (f === null) ? null : (f & FEATURE_SIDPLAYER) !== 0;
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
    /* $80 and above are FM/OPL addresses for the ASID transport. Drop them */
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

  /* Frame boundary, push a partly filled command */
  flush() {
    if (this.batch) this._flushPacket();
    this._flushBatch();
  }

  reset() { this._pktFrames = 0; }

  /* ---- device commands -------------------------------------------------- */

  _command(sub, b1 = 0) {
    if (!this._open) return;
    this._queueCommand(new Uint8Array([CMD | (sub & 0x3F), b1, 0, 0, 0, 0]));
    this._flushBatch();   /* Must not sit behind queued writes */
  }

  /* Silence. Drops the backlog first, b1 = 1 zeroes every register incl. the volume */
  resetSID() {
    this._pktFrames = 0;
    this._batchCount = 0;
    this._q.length = 0;
    this._command(RESET_SID, 1);
  }

  mute()   { if (this.batch) this._flushPacket(); this._command(MUTE); }
  unmute() { this._command(UNMUTE); }

  /* Set the board SID clock, ids in CLOCK */
  setClock(rateId) {
    if (!this._open) return;
    this._queueCommand(
      new Uint8Array([CFG_CMD, SET_CLOCK, rateId & 0xFF, 0, 0, 0]));
    this._flushBatch();
  }

  /**
   * Mono or stereo audio switch, v1.3+ PCBs only. Older boards ignore it
   * @param {boolean} stereo true for stereo, false for mono
   */
  setAudioSwitch(stereo) {
    if (!this._open) return;
    /* Over CDC every command takes a 64 byte slot, _send refuses anything else */
    this._queueCommand(
      new Uint8Array([CFG_CMD, SET_AUDIO, stereo ? 1 : 0, 0, 0, 0]));
    this._flushBatch();
  }

  /* Flip the audio switch */
  toggleAudioSwitch() {
    if (!this._open) return;
    this._queueCommand(new Uint8Array([CFG_CMD, TOGGLE_AUDIO, 0, 0, 0, 0]));
    this._flushBatch();
  }

  /**
   * Ask the board a config question and read the answer
   * @param sub  config command
   * @param len  bytes of the answer, 12 covers the socket buffer (largest). The player
   *             reads answer with 4, giving len avoids waiting out the timeout
   *
   * A serial port returns bytes in whatever pieces they arrive, the reply is accumulated until whole
   */
  async _configRead(sub, len = 12) {
    if (!this._open) return null;
    this._flushBatch();
    /* Discard stale bytes, a late reply to a previous question must not pass as this answer */
    this._rx = new Uint8Array(0);
    this._queueCommand(new Uint8Array([CFG_CMD, sub, 0, 0, 0, 0]));
    this._flushBatch();
    try {
      /* Socket buffer is 12 bytes, single value replies are 1. Wait for the larger
       * and settle for what arrived when the wait runs out */
      return await this._expect(len, REPLY_TIMEOUT_MS, 1);
    } catch (_) {
      return null;
    }
  }

  /* Chips per socket and which one answers the FM/OPL addresses, needed to play the OPL part of FM/OPL tunes */
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

  /* ---- the board's onboard player ---------------------------------------- *
   *
   * The file goes to the board, the RP2350 plays it. Each step is one 64 byte packet
   * [0xD2, subcommand, payload...], a command in a slot like everything in this file
   * _sendNow() skips the write queue, which drops its oldest entry past MAX_QUEUE
   * (fine for register writes, a corrupted file for an upload)
   */

  /* One 64 byte config packet, delivered directly */
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
    /* Firmware counts subtunes from zero */
    const t = new Uint8Array([0, subtune & 0xFF]);
    return this._sendNow(SID_PLAYER_TUNE, t);
  }
  playerStart()     { return this._sendNow(SID_PLAYER_START); }
  playerStop()      { return this._sendNow(SID_PLAYER_STOP); }
  playerPause()     { return this._sendNow(SID_PLAYER_PAUSE); }
  playerNext()      { return this._sendNow(SID_PLAYER_NEXT); }
  playerPrev()      { return this._sendNow(SID_PLAYER_PREV); }
  playerSocketTwo() { return this._sendNow(SID_PLAYER_TWO); }

  /* Fast forward on or off, the board applies it at a frame boundary */
  playerFfwd(on) {
    return this._sendNow(SID_PLAYER_FFWD, new Uint8Array([on ? 1 : 0]));
  }

  /* Set the play time of the last uploaded tune in milliseconds, send after the upload
   * Board default is five minutes. UPLOAD_SID_START zeroes position and maximum,
   * SID_PLAYER_STOP restores five minutes, send with every upload */
  playerSetPlaytime(ms) {
    const v = Math.max(0, Math.round(ms)) >>> 0;
    return this._sendNow(UPLOAD_SID_PLAYTIME, new Uint8Array([
      (v >>> 24) & 0xFF, (v >>> 16) & 0xFF, (v >>> 8) & 0xFF, v & 0xFF,
    ]));
  }

  /* Board play position in milliseconds, null when unknown
   * Firmware samples the player only while playing, the value holds at the last
   * reading after stop. UPLOAD_SID_START resets it
   * One read at a time, a timed out read leaves a late reply for the following one */
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

  /* Mute or unmute one voice or everything
   *   chip 1..4, voice 1..3   that voice, masked on the way out
   *   chip 1..4, voice 0      whole chip, its writes are dropped
   *   chip 0,    voice 0      every chip and voice
   * Voice mute masks gate and sustain. Chip mute is the only one reaching $18,
   * it silences tunes playing samples through the volume register */
  playerMute(chip, voice, mute) {
    return this._sendNow(SID_PLAYER_MUTE,
      new Uint8Array([chip & 0xFF, voice & 0xFF, mute ? 1 : 0]));
  }

  /* Every voice of every chip */
  playerMuteAll(mute) { return this.playerMute(0, 0, mute); }

  /* Whole chip in one command, voice 0 goes to usplayer_set_chip_mute() via set_mutestate() */
  playerMuteChip(chip, mute) { return this.playerMute(chip, 0, mute); }

  /* Mute state as a bitmask per chip, bits 0 to 2 = voices 1 to 3, or null
   * All zero while nothing plays */
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


  /* Send a SID file to the board and start it
   * Order: stop, START, data in 62 byte pieces (command and sub command take 2 of the
   * 64 bytes), END, SIZE, PLAYTIME (only when playtimeMs > 0), load subtune, start
   * A 51 KB tune is about 850 packets, each awaited (writer.ready), the board paces the upload
   * Same signature and packet order as uploadSIDFile() in usbsid-driver.js
   * bytes: file data, subtune: 1 based, fileType: 0x01 SID or 0x02 PRG,
   * onProgress: optional callback(sent, total), playtimeMs: play time sent before the
   * tune starts. The board resets it to five minutes on every START, send it with each upload
   * Returns true when every packet was sent, false on the first failure (no throw) */
  async uploadSIDFile(bytes, subtune = 1, fileType = 0x01, onProgress = null,
                      playtimeMs = 0) {
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
    /* Before the load, a length sent after the start leaves the tune running against
     * the board's five minute default for a while */
    if (playtimeMs > 0 && !await this.playerSetPlaytime(playtimeMs)) return false;
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
          /* a device unplugged mid read lands in this handler */
        } finally {
          try { reader.releaseLock(); } catch (_) {}
        }
        if (this._reader !== reader) break;   /* torn down concurrently */
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

  /* Wait for n bytes. atLeast is accepted when the timeout runs out, replies are
   * 12 bytes or 1 and a one byte reply would otherwise wait the full timeout */
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

  /* Add one command in its own 64 byte slot. The firmware reads the byte count from the
   * header and ignores the padding, which keeps later commands on a packet boundary */
  _queueCommand(cmd) {
    if (this._batch === null) {
      this._batch = new Uint8Array(this._coalesce * SLOT);
      this._batchCount = 0;
    }
    const at = this._batchCount * SLOT;
    this._batch.set(cmd, at);
    /* Clear the tail of the slot */
    this._batch.fill(0, at + cmd.length, at + SLOT);
    if (++this._batchCount >= this._coalesce) this._flushBatch();
  }

  /* Hand the batch to the stream, always a whole number of slots
   * Must not end short, a partial slot shifts every later command off its packet boundary */
  _flushBatch() {
    if (this._batch === null || this._batchCount === 0) return;
    this._send(this._batch.slice(0, this._batchCount * SLOT));
    this._batchCount = 0;
  }

  /* Writes waiting, the player stops emulating frames when the board is behind */
  get queueDepth() { return this._q.length + this._inflight; }
  get queueLimit() { return this._maxQ; }

  _send(buf) {
    /* A misaligned write corrupts every command after it, refuse it */
    if ((buf.length % SLOT) !== 0) {
      console.error('[usbsid-webserial] refusing a write of', buf.length,
                    'bytes, not a multiple of', SLOT);
      return;
    }
    this._q.push(buf);
    if (this._q.length > this._maxQ) this._q.shift();
    this._drain();
  }

  /* Keep the stream fed without serialising on it
   * writer.ready is the backpressure, it resolves when the stream takes more
   * The write is not awaited, the following chunk can be queued while the last goes out
   * Player backpressure is queueDepth */
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
      /* Anything queued while awaiting ready needs another pass */
      if (this._open && this._q.length > 0 && this._writer) this._drain();
    }
  }

  /* Track whether the pipe runs dry. emptied = writes completing with nothing queued
   * behind them, maxGap = longest time between two completions */
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
      /* Transfers are not held in flight, writer.ready is the backpressure
       * minInflight is always 0 and emptied counts nearly every write, differs from the WebUSB rows */
      pipelined: false,
    };
  }
}
