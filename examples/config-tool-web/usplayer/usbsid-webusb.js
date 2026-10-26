/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * usbsid-webusb.js
 * The cycle exact transport: WebUSB straight to a USBSID-Pico.
 *
 * Connect sequence and packet format from 
 * USBSID-Pico repo: examples/config-tool-web/usbsid-driver.js
 *
 * Packet format (repo/src/usbsid.c handle_buffer_task, globals.h):
 *   byte 0    command, top two bits are the type, CYCLED_WRITE is 0b10 = 0x80
 *             lower six bits are the payload BYTE count, writes * 4
 *   byte 1..  that many bytes: [reg, value, cycles_hi, cycles_lo] each
 *   Max 15 writes: 1 + 15 * 4 = 61, under the 64 byte packet
 *
 * A command byte with the low six bits zero (bare 0x80) is a single cycled write,
 * the rest of the packet is ignored. A batch has to carry the byte count
 * { batch: false } sends one write per transfer
 *
 * Throughput depends on the number of transferOut calls
 * Several transfers are in flight (MAX_INFLIGHT), several commands ride in each
 * (COALESCE). Falling behind drops the oldest queued packet
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
/* Board audio switch, v1.3+ PCBs only. Same [CFG_CMD, command, item, ...] shape as SET_CLOCK */
const SET_AUDIO    = 0x89;   /* item: 0 mono, 1 stereo */
const TOGGLE_AUDIO = 0x88;
const GET_AUDIO    = 0x91;
/* Firmware feature flags, one byte. config.h: bit 0 Pico2, 1 wifi/bluetooth,
 * 2 RGB LED, 3 PIO UART, 6 Cynthcart, 7 embedded SID player */
const US_FEATURES  = 0x82;
export const FEATURE_SIDPLAYER = 1 << 7;

/* Onboard player: upload a file, then drive it. Same sub commands and 64 byte slots as the serial transport */
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
/* 0xE7 SID_PLAYER_FFWD is absent, marked non functional in the firmware */
const SID_PLAYER_MUTE     = 0xE9;
const SID_PLAYER_MUTED    = 0xEA;
const SID_PLAYER_TIME     = 0xEB;
const SLOT                = 64;
/* Config reads. Socket buffer is 12 bytes: byte 2 is socket one, byte 5 socket two,
 * high nibble 1 = present, low nibble 1 = two chips. See USBSID_GetSocketNumSIDS() */
const READ_SOCKETCFG = 0x37;
const READ_FMOPLSID  = 0x3A;
/* Clock ids, same order as the firmware table */
export const CLOCK = { DEFAULT: 0, PAL: 1, NTSC: 2, DREAN: 3, NTSC2: 4 };

const WRITE_BYTES = 4;
const MAX_PACKET  = 64;
/* Reads per reply before giving up on zero length packets, see _transferInReply() */
const ZLP_READ_ATTEMPTS = 4;
const MAX_FRAMES  = 15;   /* 1 + 15 * 4 = 61 bytes */
/* Max queued unsent packets, about 1 KB. Overflow drops the oldest */
const MAX_QUEUE   = 256;
/* Max transfers submitted but not completed, see _pump()
 * A digi needs about 2200 packets a second, 8 was not enough (bench.html) */
const MAX_INFLIGHT = 32;

/* Trace config reads, enable with window.USBSID_DEBUG_READS or localStorage 'usbsid-debug-reads' = '1' */
function debugReads() {
  if (typeof window === 'undefined') return false;
  if (window.USBSID_DEBUG_READS) return true;
  try { return window.localStorage.getItem('usbsid-debug-reads') === '1'; }
  catch (_) { return false; }
}

/* Wait for a config read answer, transferIn has no timeout */
const CONFIG_TIMEOUT_MS = 2000;
/* Report a read unanswered after this long, never gives up on it. See _configRead() */
const CONFIG_STUCK_MS = 10000;

/* Commands sent in one transferOut, each transferOut is a round trip through the browser USB stack
 * Firmware (CFG_TUD_VENDOR_RX_BUFSIZE 0) handles each 64 byte endpoint packet on its own in
 * tud_vendor_rx_cb (repo/src/usbsid.c). The host splits a bulk transfer into 64 byte packets,
 * each command is padded to exactly 64 for the split to land between commands
 * flush() empties a partial batch at every frame boundary
 * The batch ends on a short packet, see _flushBatch()
 * coalesce: 1 sends one command per transfer */
const COALESCE = 8;

/* Pad every command to a whole 64 byte packet, last one included
 * FIFO on (CFG_TUD_VENDOR_RX_BUFSIZE > 0, USE_VENDOR_BUFFER): true wanted. The FIFO is a byte
 * stream, process_buffer consumes only the header count and drops the rest, a short packet
 * desyncs all later reads. A ZLP is harmless
 * FIFO off: false. tud_vendor_rx_cb has no zero length guard, a transfer of a multiple of 64
 * can end in a ZLP that re-runs the previous command
 * False although the firmware has the FIFO on, short packets with FIFO is the combination
 * measured working. On garbled registers try true first */
const FULL_PACKETS = false;

const delay = (ms) => new Promise((r) => setTimeout(r, ms));

/* Chips per socket from the 12 byte socket buffer */
function parseSocketConfig(b) {
  const chips = (byte) =>
    (((byte & 0xf0) >> 4) === 1) ? (((byte & 0x0f) === 1) ? 2 : 1) : 0;
  return { one: b.length > 2 ? chips(b[2]) : 0,
           two: b.length > 5 ? chips(b[5]) : 0 };
}

export class USBSIDWebUSBTransport {
  /**
   * @param {object} opts
   *   opts.batch        false for one write per packet (default batched)
   *   opts.coalesce     commands per transferOut (default COALESCE, 1 disables)
   *   opts.fullPackets  pad every command to 64, default FULL_PACKETS
   *   opts.device       open USBSID device to share, needs writeArrayAwait, writeArray
   *                     or write taking a Uint8Array
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
    /* Optional tap, called with (reg, value) for every write */
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
    this.deviceOpened = false;
    this.openingDevice = false;
  }

  get isOpen() {
    if (this._extDev) return this._extDev.isOpen !== false;
    return this._open;
  }
  get productName() {
    if (this._extDev) return this._extDev.productName || 'USBSID-Pico';
    return this._dev ? (this._dev.productName || '') : '';
  }

  /* Ask for a USBSID-Pico and open it, needs a user gesture (device picker)
   * With an injected device this only confirms it is there */
  async connect() {
    if (this._extDev) { this._open = true; return this.isOpen; }
    if (this._open) return true;
    this._dev = await navigator.usb.requestDevice({
      filters: [{ vendorId: USBSID_VID, productId: USBSID_PID }],
    });
    return this._openDevice();
  }

  /* Open an already granted board without a picker, usable in a worker
   * Returns false when nothing is granted yet */
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

  /* Close and unlatch after a failed open, rethrows (NetworkError = in use) */
  async _openDevice() {
    if (this.deviceOpened || this.openingDevice)
      return;
    try {
      return await this._openDeviceInner();
    } catch (e) {
      try { await this._dev.close(); } catch (_) {}
      this.deviceOpened = false;
      this.openingDevice = false;
      this._open = false;
      this._ifaceNum = null; this._epOut = null; this._epIn = null;
      throw e;
    }
  }

  async _openDeviceInner() {
    this.openingDevice = true;
    try {
      await this._dev.open();
      this.deviceOpened = true;
    } finally {
      this.openingDevice = false;
    }

    if (this._dev.configuration === null) await this._dev.selectConfiguration(1);

    /* Walk the configuration for the vendor interface and its bulk endpoints, numbers
     * move between firmware builds. First 0xFF interface is the WebUSB command interface */
    this._ifaceNum = null;
    for (const iface of this._dev.configuration.interfaces) {
      for (const alt of iface.alternates) {
        if (alt.interfaceClass === DEVICE_CLASS && this._ifaceNum === null) {
          this._ifaceNum = iface.interfaceNumber;
          for (const ep of alt.endpoints) {
            if (ep.direction === 'out') this._epOut = ep.endpointNumber;
            if (ep.direction === 'in')  this._epIn  = ep.endpointNumber;
          }
        }
      }
    }
    if (this._ifaceNum === null) {
      await this._dev.close();
      this.deviceOpened = false;
      return false;
    }

    await this._dev.claimInterface(this._ifaceNum);
    /* No selectAlternateInterface(), only alt 0 exists and SET_INTERFACE resets the host data
     * toggles but not the firmware's, the first packet is then dropped as a duplicate */
    /* clearHalt crashes the browser process on Windows (Chromium CHECK(),
     * issues.chromium.org/issues/557605844) */
    //try { await this._dev.clearHalt('out', this._epOut); } catch (_) {}
    //try { await this._dev.clearHalt('in',  this._epIn);  } catch (_) {}
    await this._dev.controlTransferOut({
      requestType: 'class',
      recipient:   'interface',
      request:     CTRL_TRANSFER,
      value:       CTRL_ENABLE,
      index:       this._ifaceNum,
    });
    /* Settle, the first OUT after the reset can be swallowed */
    await delay(100);
    this._open = true;
    return true;
  }

  async disconnect() {
    this._open = false;
    this.deviceOpened = false;
    this.openingDevice = false;
    /* New chain for the following connection, the old one may hold a read that never gets answered */
    this._cfgReadChain = null;
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
    /* $80 and above are FM/OPL addresses for the ASID transport. Drop them */
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

  /* Frame boundary, push a partly filled packet */
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
    this._flushBatch();   /* Must not overtake queued writes */
    this._send(new Uint8Array([CMD | (sub & 0x3F), b1, 0, 0, 0, 0]));
  }

  /* Silence. Drops the backlog first, b1 = 1 zeroes every register incl. the volume
   * (a RES pulse alone does not reliably stop a sustained note) */
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
    this._flushBatch();
    this._send(new Uint8Array([CFG_CMD, SET_CLOCK, rateId & 0xFF, 0, 0, 0]));
  }

  /**
   * Mono or stereo audio switch, v1.3+ PCBs only. Older boards ignore it
   * @param {boolean} stereo true for stereo, false for mono
   */
  setAudioSwitch(stereo) {
    if (!this._open) return;
    this._flushBatch();
    this._send(new Uint8Array([CFG_CMD, SET_AUDIO, stereo ? 1 : 0, 0, 0, 0]));
  }

  /* Flip the audio switch */
  toggleAudioSwitch() {
    if (!this._open) return;
    this._flushBatch();
    this._send(new Uint8Array([CFG_CMD, TOGGLE_AUDIO, 0, 0, 0, 0]));
  }

  /**
   * Ask the board a config question and read the answer
   * @param sub  config command
   * @param len  bytes to ask for, match the firmware reply: SID_PLAYER_TIME and
   *             SID_PLAYER_MUTED answer 4, config and socket reads a full packet
   *
   * transferIn is asked for a full packet, Chrome on Linux does not reliably complete
   * a bulk IN shorter than wMaxPacketSize. Returns null when there is nothing to ask,
   * incl. a shared device (the owner does its own reads)
   * transferIn cannot be cancelled, a read given up on still gets its reply and the following
   * read receives it. Reads are never raced against a timeout, a timeout only reports
   * All reads go through one chain, two readers on one endpoint take each other's replies
   * A caller that must not block waits on the returned promise with its own timeout,
   * see readBoardConfig()
   */
  async _configRead(sub, len = MAX_PACKET) {
    if (!this._open || this._extDev || !this._dev) return null;
    const mine = this._cfgReadChain = (this._cfgReadChain || Promise.resolve())
      .then(() => this._configReadOnce(sub, len),
            () => this._configReadOnce(sub, len));
    return await mine;
  }

  /* Read one reply from the IN endpoint, skipping zero length packets
   * A 64 byte reply is followed by a ZLP, seen as an empty result on the following read
   * Retry only after an empty result. Returns null when every attempt was empty */
  async _transferInReply() {
    for (let i = 0; i < ZLP_READ_ATTEMPTS; i++) {
      const r = await this._dev.transferIn(this._epIn, MAX_PACKET); /* Vendor is fixed at 64 bytes */
      if (r && r.data && r.data.byteLength > 0) {
        return new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
      }
    }
    return null;
  }

  async _configReadOnce(sub, len) {
    if (!this._open || this._extDev || !this._dev) return null;
    this._flushBatch();
    /* Report a read that never comes back, without giving up on it */
    if (debugReads()) {
      console.debug('[usbsid-webusb] read 0x' + (sub & 0xff).toString(16) +
                    ' issued, open=' + this._open + ' ext=' + !!this._extDev);
    }
    const stuck = setTimeout(() => {
      console.warn('[usbsid-webusb] config read 0x' + (sub & 0xff).toString(16) +
                   ' has not answered after ' + (CONFIG_STUCK_MS / 1000) +
                   's, still waiting (reads are serialised, nothing else' +
                   ' will run until it does)');
    }, CONFIG_STUCK_MS);
    try {
      await this._dev.transferOut(this._epOut,
        new Uint8Array([CFG_CMD, sub, 0, 0, 0, 0]));
      const out = await this._transferInReply();
      if (!out) return null;
      if (debugReads()) {
        console.debug('[usbsid-webusb] read 0x' + (sub & 0xff).toString(16) +
                      ' answered ' + out.length + ' bytes');
      }
      return out;
    } catch (e) {
      /* Caller sees null for a failed and a timed out read, the log tells them apart */
      console.warn('[usbsid-webusb] config read 0x' + (sub & 0xff).toString(16) +
                   ' failed: ' + (e && e.message ? e.message : e));
      return null;
    } finally {
      clearTimeout(stuck);
    }
  }

  /* Wait on a config read with a timeout, the read stays on the chain and its reply
   * is consumed there. For the connect path, a silent board must not block the page */
  async _configReadBounded(sub, len = MAX_PACKET, ms = CONFIG_TIMEOUT_MS) {
    const name = '0x' + (sub & 0xff).toString(16);
    const t0 = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    const waiting = this._cfgReadsQueued | 0;
    this._cfgReadsQueued = waiting + 1;
    const read = this._configRead(sub, len)
      .finally(() => { this._cfgReadsQueued = (this._cfgReadsQueued | 0) - 1; });
    let timer = null;
    let gaveUp = false;
    const bound = new Promise((res) => {
      timer = setTimeout(() => { gaveUp = true; res(null); }, ms);
    });
    try {
      return await Promise.race([read, bound]);
    } finally {
      clearTimeout(timer);
      if (gaveUp) {
        /* Say what happened, the read stays on the chain and still gets answered
         * waiting = reads already queued ahead of this one */
        console.warn('[usbsid-webusb] config read ' + name + ' gave up after ' +
                     ms + 'ms, ' + waiting + ' read(s) were queued ahead of it');
        read.then((late) => {
          const dt = ((typeof performance !== 'undefined')
                        ? performance.now() : Date.now()) - t0;
          console.warn('[usbsid-webusb] config read ' + name + ' did answer, ' +
                       dt.toFixed(0) + 'ms after it was asked for (' +
                       (late ? late.length + ' bytes: ' +
                          Array.from(late).slice(0, 8)
                            .map((x) => x.toString(16)).join(' ')
                        : 'null') + ')');
        }, () => {});
      }
    }
  }

  /* Firmware feature flags or null, see FEATURE_SIDPLAYER. A board without the embedded player takes an upload and plays nothing */
  async features() {
    const r = await this._configRead(US_FEATURES);
    return (r && r.length >= 1) ? r[0] : null;
  }

  /* Board carries the onboard SID player? null when unknown */
  async hasSidPlayer() {
    const f = await this.features();
    return (f === null) ? null : (f & FEATURE_SIDPLAYER) !== 0;
  }

  /* ---- the onboard player ---------------------------------------------- */

  /* Send one command packet immediately, bypassing the write queue
   * The queue drops its oldest entry when full, fine for sound but not for a file
   * (61566 byte tune = 993 packets). Waits for each transfer, slower than the queue */
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
    /* Two bytes: file id (0 = last uploaded), then the subtune counted from zero
     * config.c reads tuneno = buffer[2] after the config init byte, this packet's byte 3 */
    const t = new Uint8Array([0, subtune & 0xFF]);
    return this._sendNow(SID_PLAYER_TUNE, t);
  }
  playerStart()     { return this._sendNow(SID_PLAYER_START); }
  playerStop()      { return this._sendNow(SID_PLAYER_STOP); }
  playerPause()     { return this._sendNow(SID_PLAYER_PAUSE); }
  playerNext()      { return this._sendNow(SID_PLAYER_NEXT); }
  playerPrev()      { return this._sendNow(SID_PLAYER_PREV); }
  playerSocketTwo() { return this._sendNow(SID_PLAYER_TWO); }

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


  /* Send a whole file to the board player and start it
   * Order: stop, START, data in 62 byte pieces (2 of the 64 bytes are command and
   * sub command), END, SIZE, load subtune, start
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

  /* Chips per socket and which one answers the FM/OPL addresses
   * Emulation needs this to route $df40/$df50 to a chip, without it FM/OPL tunes
   * play their SID voices only. A shared device is asked through the host app driver */
  async readBoardConfig() {
    if (this._extDev) {
      /* Ask the host driver */
      const d = this._extDev;
      try {
        /* Host driver helpers, readSocketConfig() answers {one, two} already counted
         * Both use the driver serialised config read, which recovers from a lost reply */
        let parsed = null;
        if (typeof d.readSocketConfig === 'function') {
          parsed = await d.readSocketConfig();
        }
        const fm = (typeof d.readFMOplSID === 'function') ? await d.readFMOplSID() : 0;
        return {
          sidsSocketOne: parsed ? parsed.one : 0,
          sidsSocketTwo: parsed ? parsed.two : 0,
          fmoplSid: fm || -1,
        };
      } catch (e) {
        console.warn('[usbsid-webusb] board config read through the app driver ' +
                     'failed: ' + (e && e.message ? e.message : e));
        return null;
      }
    }

    /* Bounded, runs on the connect path and a silent board must not block the page
     * Reads stay on the chain, a slow answer costs one unknown value */
    const cfg = await this._configReadBounded(READ_SOCKETCFG);
    if (!cfg) return null;
    const parsed = parseSocketConfig(cfg);
    /* If this goes unanswered only FM/OPL is lost, fmoplSid -1 = no FM chip */
    const fm = await this._configReadBounded(READ_FMOPLSID);
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

  /* Add one command to the batch, send it when full
   * Each command takes a 64 byte slot for the host split to land between commands,
   * the firmware ignores the padding */
  _queueCommand(cmd) {
    if (this._coalesce <= 1) { this._send(cmd.slice()); return; }

    if (this._batch === null) {
      this._batch = new Uint8Array(this._coalesce * MAX_PACKET);
      this._batchCount = 0;
    }
    this._batch.set(cmd, this._batchCount * MAX_PACKET);
    /* Clear the tail of the slot */
    this._batch.fill(0, this._batchCount * MAX_PACKET + cmd.length,
                     (this._batchCount + 1) * MAX_PACKET);
    this._batchLast = cmd.length;
    if (++this._batchCount >= this._coalesce) this._flushBatch();
  }

  /* Send the batch, ending on a short packet
   * Length is (n - 1) * 64 + last command length, never a multiple of 64
   * A transfer of an exact multiple may end with a ZLP, the firmware rx callback has no guard:
   *     webread = bufsize;
   *     memcpy(sid_buffer, buffer, bufsize);   // bufsize 0 copies nothing
   *     process_buffer(wusb_itf, &webread);    // still reads sid_buffer[0]
   * A ZLP re-runs the command left in the buffer, a duplicated burst with stale cycle counts
   * Every command still starts on a 64 byte boundary
   * fullPackets must be set when the vendor FIFO is enabled (CFG_TUD_VENDOR_RX_BUFSIZE and
   * USE_VENDOR_BUFFER). The FIFO is a byte stream, vendor_task() reads 64 bytes and
   * process_buffer consumes only the header count, a short packet desyncs later reads
   * With the FIFO a ZLP is harmless. Both or neither */
  _flushBatch() {
    if (this._batch === null || this._batchCount === 0) return;
    const bytes = this._fullPackets
      ? this._batchCount * MAX_PACKET
      : (this._batchCount - 1) * MAX_PACKET + this._batchLast;
    this._send(this._batch.slice(0, bytes));
    this._batchCount = 0;
  }

  /* Packets waiting to go out, the player stops emulating frames when the board is behind */
  get queueDepth() { return this._q.length + this._inflight; }
  get queueLimit() { return this._maxQ; }

  _send(buf) {
    this._q.push(buf);
    if (this._q.length > this._maxQ) this._q.shift();
    this._pump();
  }

  /* Keep several transfers in flight
   * A transferOut round trip is 0.5 to 1 ms, serialised that caps near 1000 packets a second
   * A digi writing 650 registers a frame needs 44 packets a frame, 2170 a second
   * The host controller completes submissions on an endpoint in order, writes stay in sequence
   * Backpressure is the queue depth */
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

  /* Track whether the pipe to the board runs dry
   * emptied = transfers completing with nothing submitted behind them
   * maxGap = longest time between two completions */
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
      /* Transfers stay outstanding up to MAX_INFLIGHT, emptied and minInflight are meaningful */
      pipelined: true,
    };
  }

  /* One transfer, resolves when the device took the packet (backpressure)
   * A shared device may only have the fire and forget writeArray, resolves early */
  _transfer(buf) {
    if (this._extDev) {
      const fn = this._extDev.writeArrayAwait
        || this._extDev.writeArray || this._extDev.write;
      return fn.call(this._extDev, buf);
    }
    return this._dev.transferOut(this._epOut, buf);
  }
}
