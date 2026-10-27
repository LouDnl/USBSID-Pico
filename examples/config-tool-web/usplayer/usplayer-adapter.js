/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * usplayer-adapter.js
 * Wraps USBSIDPlayerWeb in the player interface config-tool-web expects
 * (load / play / pause / stop / setVolume / getSongInfo / paused / emulator)
 *
 * WebUSB mode uses the USBSIDDevice the app already connected
 * ASID mode opens Web MIDI and follows the app output picker by name
 *
 * Loaded as ES module, registers window.USPlayerAdapter
 * The WASM factory (window.USBSIDPlayer) comes from usbsid.js, a classic script
 *
 * Status goes to the app status line, load and stop go to its log panes
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

import { USBSIDPlayerWeb, NullTransport, isSidHeader, countSids, sidModel }
  from './usplayer-web.js';
import { USBSIDWebUSBTransport } from './usbsid-webusb.js';
import { USBSIDWebSerialTransport } from './usbsid-webserial.js';
import { ASIDMIDITransport } from './asid-midi.js';
import { UsPlayerAudio } from './usplayer-audio.js';
import { applySoftAudio } from './usplayer-softaudio.js';

/* Build artefacts live beside this file, resolved from import.meta.url
 * config-tool-web uses usplayer/, DeepSID js/handlers/usplayer/ */
const WASM_DIR = new URL('./', import.meta.url).href;

/* Cache buster from the host, empty when none was given
 * Added to the wasm and ES module URLs, a stale usbsid.wasm beside a fresh
 * usbsid.esm.js makes usp_audio_configure() return 0 */
const VERSION = new URL(import.meta.url).search;

/* Name with the host cache buster on it */
function versioned(name) { return WASM_DIR + name + VERSION; }

/* Milliseconds, monotonic when available */
function _now() {
  return (typeof performance !== 'undefined') ? performance.now() : Date.now();
}

/* Register grid push interval in ms, 20 a second
 * The app highlight lasts 180 ms */
const GRID_MS = 50;

/* Status line update, once a second */
const STATUS_EVERY = 20;

/* readMemory() page watch, worker mode
 * A page not read for MEM_WATCH_IDLE_MS drops out of the worker report
 * Re-checked each MEM_WATCH_CHECK_MS, MEM_WATCH_MAX pages at most */
const MEM_WATCH_IDLE_MS = 1000;
const MEM_WATCH_CHECK_MS = 250;
const MEM_WATCH_MAX = 16;

/* Above this many chips software audio drops from sinc to Fast (linear)
 * Sinc on more reSIDfp instances in one worker misses the 20 ms frame budget
 * (v5 8SID tune stuttered), the software path was tuned for 4 */
const SINC_CHIP_LIMIT = 4;

let _modulePromise = null;
/**
 * The wasm module, instantiated once for the page
 * Uses window.USBSIDPlayer when usbsid.js is loaded as classic script,
 * else imports usbsid.esm.js beside this file
 */
function getModule() {
  if (!_modulePromise) {
    _modulePromise = (async () => {
      let factory = (typeof window !== 'undefined') ? window.USBSIDPlayer : null;
      if (typeof factory !== 'function') {
        const mod = await import(versioned('usbsid.esm.js'));
        factory = mod.default;
      }
      return factory({ locateFile: (p) => versioned(p) });
    })();
  }
  return _modulePromise;
}

/* Chip count from the header, v3 has a second SID address at 0x7a, v4 a third at 0x7b */
export class USPlayerAdapter {
  /**
   * @param {string} emulator  'usplayer' (WebUSB) or 'usplayer-asid'
   * @param {object} device    the app's connected USBSIDDevice, WebUSB mode
   */
  constructor(emulator, device) {
    this.emulator = emulator;
    this._device = device || null;
    this._isAsid = (emulator === 'usplayer-asid');
    /* Web Serial uses the CDC interface of the board, the app driver only knows WebUSB
     * Opens a separate port, works in browsers with no WebUSB */
    this._isSerial = (emulator === 'usplayer-serial');
    /* No board, reSIDfp in the same wasm out through an AudioWorklet */
    this._isAudio = (emulator === 'usplayer-audio');
    /* File goes to the board, the board player plays it
     * No register writes or play position pass through the page */
    this._isSendsid = (emulator === 'usplayer-sendsid');
    /* Playback start (performance.now) and time played, see playtimeMs()
     * The board reports no position */
    this._boardStart = 0;
    this._boardPlayed = 0;
    /* Board position from the last successful poll, null until it answers, see _pollBoardTime() */
    this._boardMs = null;
    this._boardPoll = null;
    this._boardPollFails = 0;
    this._audio = null;
    this._player = null;
    this._transport = null;
    this._bytes = null;
    this._info = { maxSubsong: 0, songName: '', songAuthor: '', songReleased: '', numSids: 1 };
    this._ready = null;
    this._paused = false;
    this._midiWired = false;
    this._subtune = 0;
    this._reportTimer = 0;
    this._prefix = 'Playing';
    this._lastStatus = '';
    /* Set in load() when Fast replaced sinc, see SINC_CHIP_LIMIT */
    this._reducedQuality = false;
    this._volume = 1;
    /* Register grid shadow copy, see onWrite in _ensure() */
    this._shadow = new Uint8Array(128).fill(0xff);
    this._dirty = new Uint8Array(128);
    /* Slots a tune has written, the 0xff shadow fill is no value, see readRegister() */
    this._seen = new Uint8Array(128);
    this._anyDirty = false;
    this._tick = 0;
    /* Software audio runs the emulation in a worker, see _startWorker()
     * The page player only serves metadata and Songlengths lookups, it never steps
     * _snap is the last worker state */
    this._worker = null;
    this._workerReady = null;
    this._snap = null;
    /* Worker register reports waiting for their audible moment, see _queueRegisters()
     * Plus the CIA 1 timer A latch of the latest one */
    this._regQueue = [];
    this._workerCiaLatch = 0;
    /* RAM mirror of the worker machine for readMemory(), see _watchPage()
     * _memValid marks mirrored pages, _memPages maps page to last read time */
    this._mem = new Uint8Array(65536);
    this._memValid = new Uint8Array(256);
    this._memPages = new Map();
    this._memSent = '';
    /* Scope, panning and filter state, see usplayer-softaudio.js */
    this._softAudioInit();
    this._memCheck = 0;
    /* SID and FM mix in percent, undefined keeps the wasm defaults (100 and 50) */
    this._sidVolume = undefined;
    this._fmVolume = undefined;
    this._onResumeVisibility = null;
    this._msgId = 0;
    this._pending = new Map();
    /* Bumped on every load(), lets an older in-flight load stop, see load() */
    this._loadGen = 0;
    /* Host output hooks, see setHost(), all null = config-tool-web defaults */
    this._host = { log: null, status: null, registers: null, sidCount: null };
  }

  /**
   * Tell the adapter how this host wants to be talked to
   * Defaults are config-tool-web shaped: log lines go to #player-log and #debug-log,
   * status to #status-text, changed registers to window.updateSIDReg
   * A callback passed in host replaces the default
   *
   * @param {object} host
   * @param {(line: string) => void}                  [host.log]
   * @param {(line: string) => void}                  [host.status]
   * @param {(chip: number, reg: number, val: number) => void} [host.registers]
   * @param {(count: number) => void}                 [host.sidCount]
   */
  setHost(host) {
    if (!host) return;
    for (const k of ['log', 'status', 'registers', 'sidCount']) {
      if (typeof host[k] === 'function') this._host[k] = host[k];
    }
  }

  get paused() { return this._paused; }
  get stopped() { return !this._player || !this._player.isPlaying(); }

  /**
   * Put a line in the app log panes
   * Writes to the elements directly, usbsidLog is not reachable from a module
   * Occasional events only (load, stop), periodic output goes to the status line
   */
  _log(line) {
    if (this._host.log) { this._host.log(line); return; }
    for (const id of ['player-log', 'debug-log']) {
      const el = (typeof document !== 'undefined') ? document.getElementById(id) : null;
      if (!el) continue;
      el.textContent += '[USPLAYER] ' + line + '\n';
      el.scrollTop = el.scrollHeight;
    }
  }

  /**
   * Running status into the app status line
   * Appending to the log (textContent +=) gets slower while a tune plays
   * The app writes 'Playing: <name>', that stays as prefix and the numbers are appended
   *
   * drain = mean and worst gap between the writes of a frame (frame is about 20 ms)
   * queue = transfers waiting, dropped = write ring overflow, should stay zero
   */
  _report() {
    if (!this._player || !this._player.isPlaying()) return;
    /* No host callback and no element, skip building the line */
    if (!this._host.status &&
        (typeof document === 'undefined' ||
         !document.getElementById('status-text'))) return;

    const p = this._player;

    /* SendSID: board plays, only the time since sending is known */
    if (this._isSendsid) {
      const secs = (this.playtimeMs() / 1000).toFixed(1);
      this._status(`${this._prefix} | playing on the board` +
                   `${this._paused ? ', paused' : ''} | ${secs}s in`);
      return;
    }

    const s = (typeof p.stats === 'function') ? p.stats() : null;

    /* With the worker the numbers come from its latest snapshot */
    const w = (this._isAudio && this._worker) ? this._snap : null;
    let line = `${this._prefix} | ${(w ? (w.refreshHz || 0) : p.refreshHz()).toFixed(2)} fps` +
               ` | ${w ? (w.frames || 0) : p.frames()} frames`;

    /* Software audio has no transport, show ms/frame against the 20 ms budget
     * and starve (silence the worklet invented, heard as crackle) */
    if (this._isAudio && this._audio) {
      const a = this._audio.stats();
      if (w) {
        a.msPerFrame = w.msPerFrame || 0;
        a.fmWrites = w.fmWrites || 0;
        a.clipped = w.clipped || 0;
      }
      /* Silent lead-in, numbers mean nothing */
      if (a.skipping) {
        this._status(`${this._prefix} | running through the loader` +
                     ` | ${(p.playtimeMs() / 1000).toFixed(1)}s in`);
        return;
      }
      line += ` | ${a.msPerFrame.toFixed(1)}/20 ms per frame` +
              ` | buffer ${a.queuedMs} ms`;
      /* FM side only */
      if (a.fmWrites > 0) line += ` | FM ${a.fmWrites}`;
      if (this._reducedQuality) {
        line += ` | REDUCED QUALITY (>${SINC_CHIP_LIMIT} SIDs, too heavy for sinc)`;
      }
      if (a.starvedMs) line += ` | STARVED ${a.starvedMs} ms`;
      if (a.clipped > 0) line += ` | CLIPPED ${a.clipped}`;
      this._status(line);
      return;
    }

    if (s) {
      line += ` | drain ${s.meanDrainGap.toFixed(1)}/${s.maxDrainGap.toFixed(0)} ms` +
              ` | queue ${s.maxQueue}` +
              (s.usb ? ` | ${s.usb.transfersPerSecond.toFixed(0)} xfer/s` : '');
      if (s.dropped) line += ` | DROPPED ${s.dropped}`;
      if (s.starved || s.blocked) {
        line += ` | STALLS ${s.starved}/${s.blocked}`;
      }
    }

    this._status(line);
  }

  /** Set the status line when it changed */
  _status(line) {
    if (line === this._lastStatus) return;
    this._lastStatus = line;
    if (this._host.status) { this._host.status(line); return; }
    if (typeof document === 'undefined') return;
    const el = document.getElementById('status-text');
    if (!el) return;
    el.textContent = line;
    /* Orange while REDUCED QUALITY is shown, else CSS default (cyan) */
    el.style.color = this._reducedQuality ? 'var(--c64-orange)' : '';
  }

  /**
   * Fill the shadow from the register mirror of the emulation
   * Software audio only, usp_audio_configure hands the SID to reSIDfp and the
   * transport sees no writes
   * Board modes keep the write side shadow, worker mode reads worker reports,
   * see _queueRegisters()
   */
  _pollRegisters() {
    if (this._isAudio && this._worker) { this._drainRegisters(); return; }
    if (!this._isAudio) return;
    if (!this._player || typeof this._player.sidRegister !== 'function') return;
    const chips = Math.min(4, Math.max(1, this._info.numSids | 0));
    for (let c = 0; c < chips; c++) {
      for (let r = 0; r < 32; r++) {
        let v;
        try { v = this._player.sidRegister(c + 1, r) & 0xff; } catch (_) { return; }
        const i = (c << 5) | r;
        this._seen[i] = 1;
        if (this._shadow[i] === v) continue;
        this._shadow[i] = v;
        this._dirty[i] = 1;
        this._anyDirty = true;
      }
    }
  }

  /** Push the registers that changed */
  _flushRegisters() {
    if (!this._anyDirty) return;
    this._anyDirty = false;
    const fn = this._host.registers ||
               ((typeof window !== 'undefined') ? window.updateSIDReg : null);
    if (typeof fn !== 'function') { this._dirty.fill(0); return; }
    for (let i = 0; i < 128; i++) {
      if (!this._dirty[i]) continue;
      this._dirty[i] = 0;
      fn((i >> 5) & 0x03, i & 0x1f, this._shadow[i]);
    }
  }

  _startReporting() {
    this._stopReporting();
    if (typeof setInterval !== 'function') return;
    /* The status line holds the tune name set by the app right after load, pick it up once */
    if (this._host.status) {
      /* Host owns the status line, nothing to read back */
      this._prefix = 'Playing';
    } else {
      setTimeout(() => {
        const el = (typeof document !== 'undefined')
          ? document.getElementById('status-text') : null;
        const text = (el && el.textContent) ? el.textContent : 'Playing';
        /* _status() also writes 'prefix | fps | ...', keep the part before the first ' | ' */
        const bar = text.indexOf(' | ');
        this._prefix = bar === -1 ? text : text.slice(0, bar);
      }, 250);
    }
    /* One timer, grid 20 a second and status 1 a second */
    this._tick = 0;
    this._reportTimer = setInterval(() => {
      try {
        this._pollRegisters();
        this._flushRegisters();
        if ((++this._tick % STATUS_EVERY) === 0) this._report();
      } catch (_) { this._stopReporting(); }
    }, GRID_MS);
  }

  _stopReporting() {
    if (this._reportTimer) { clearInterval(this._reportTimer); this._reportTimer = 0; }
    this._anyDirty = false;
    this._dirty.fill(0);
  }

  async _ensure() {
    if (this._ready) return this._ready;
    this._ready = (async () => {
      const M = await getModule();
      if (this._isAudio) {
        /* No transport needed, usp_audio_configure() takes over the backend
         * AudioContext is opened in load(), it needs the tune clock and chip count
         * and a user gesture */
        this._transport = new NullTransport();
        /* Start the worker early, it fetches a wasm module */
        this._startWorker().catch((e) => this._log('worker init: ' + e.message));
      } else if (this._isAsid) {
        this._transport = new ASIDMIDITransport();
        try {
          await this._transport.connect(null);
          this._wireMidiPicker();
        } catch (e) { console.warn('ASID connect:', e); }
      } else if (this._isSendsid) {
        /* Both interfaces carry it, WebUSB first (no second dialog, only the vendor
         * interface can be probed for the firmware build), Web Serial as fallback */
        this._transport = new USBSIDWebUSBTransport({ device: this._device });
        try {
          if (this._device) await this._transport.connect();
          else await this._transport.connectGranted();
        } catch (e) { this._log('WebUSB: ' + e.message); }
        if (!this._transport.isOpen) {
          this._transport = new USBSIDWebSerialTransport();
          try {
            const ok = await this._transport.connectGranted();
            if (!ok) this._log('no board granted yet, press Connect');
          } catch (e) { this._log('Web Serial: ' + e.message); }
        }
        await this._checkSidPlayer();
      } else if (this._isSerial) {
        /* Both use the CDC interface, serial streams register writes, sendsid sends the file once
         * Only a granted port, connect() calls requestPort() which needs a user gesture
         * The picker belongs to the Connect button of the host */
        this._transport = new USBSIDWebSerialTransport();
        try {
          const ok = await this._transport.connectGranted();
          if (!ok) this._log('no serial port granted yet, press Connect');
        } catch (e) { this._log('Web Serial: ' + e.message); }
      } else {
        this._transport = new USBSIDWebUSBTransport({ device: this._device });
        try {
          /* With a device handed in connect() only confirms it
           * Without one use the granted device, requestDevice() needs a user
           * gesture, the Connect button of the host reaches it through connect() */
          if (this._device) await this._transport.connect();
          else await this._transport.connectGranted();
        } catch (_) {}
      }
      this._player = new USBSIDPlayerWeb(M, this._transport);

      /* Mirror the writes into the app register display through a shadow copy
       * updateSIDReg per write is a DOM update per write, a digi writes about 30000
       * registers a second (tab at 102% of a core, 2 GB), push changes 20 times a second */
      /* Read the board config through the connected device of the app
       * Without it $df40/$df50 reach nothing and FM/OPL tunes play no OPL */
      try {
        /* No board to ask */
        const board = this._isAudio ? null : await this._player.applyBoardConfig();
        if (board) {
          this._log(`board: socket one ${board.sidsSocketOne} SID(s), ` +
                    `socket two ${board.sidsSocketTwo}, FM/OPL on ` +
                    (board.fmoplSid > 0 ? `SID ${board.fmoplSid}` : 'nothing'));
        }
      } catch (e) { this._log('could not read the board config: ' + e); }

      this._transport.onWrite = (reg, val) => {
        const i = reg & 0x7f;
        this._seen[i] = 1;
        if (this._shadow[i] === val) return;
        this._shadow[i] = val;
        this._dirty[i] = 1;
        this._anyDirty = true;
      };
    })();
    return this._ready;
  }

  /**
   * Follow the shared MIDI output list of the app without rewriting it
   * Option values are indices into the MIDIAccess of the app, map the selected name
   * onto the transport
   */
  _wireMidiPicker() {
    const sel = document.getElementById('asid-midi-outputs');
    if (!sel || !this._transport.selectOutputByName || this._midiWired) return;
    const apply = () => {
      const opt = sel.options[sel.selectedIndex];
      if (opt) this._transport.selectOutputByName(opt.textContent);
    };
    apply();
    sel.addEventListener('change', apply);
    this._midiWired = true;
  }

  async _fetch(url) {
    const resp = await fetch(url);
    if (!resp.ok) throw new Error('HTTP ' + resp.status);
    return new Uint8Array(await resp.arrayBuffer());
  }

  /**
   * Entry point for the app: load(subtune, timeout, url, callback)
   * subtune counts from zero, zero is the default of the file
   */
  /**
   * Bring the transport up before the first tune loads
   * Web Serial requestPort() needs a user gesture, call this from the Connect button
   *
   * Return true when the transport is open
   */
  async connect() {
    await this._ensure();
    /* Still shut, _ensure() only takes granted devices and is memoized
     * A picker is allowed, this call comes from the Connect button */
    if (!this.isConnected() && this._transport &&
        typeof this._transport.connect === 'function') {
      try { await this._transport.connect(); }
      catch (e) { this._log('connect: ' + (e && e.message ? e.message : e)); }
      /* Chosen board must be able to do the job */
      await this._checkSidPlayer();
    }
    return this.isConnected();
  }

  /**
   * Let go of the board, the port or the MIDI output
   * Software audio has nothing to release and returns false
   * Transport and _ready stay, connect() shows a picker again
   *
   * Return true when the link is shut
   */
  async disconnect() {
    if (this._isAudio) return false;
    /* SendSID plays on the board, stop it before closing the link */
    if (this._isSendsid) { try { this.stop(); } catch (_) { /* going anyway */ } }
    this._stopBoardPoll();
    if (this._transport && typeof this._transport.disconnect === 'function') {
      try { await this._transport.disconnect(); }
      catch (e) { this._log('disconnect: ' + (e && e.message ? e.message : e)); }
    }
    this._log('disconnected');
    this._status('disconnected');
    return !this.isConnected();
  }

  /**
   * Refuse a board that cannot play what SendSID sends
   * Bit 7 of the firmware flags byte = embedded SID player, no flag = board plays nothing
   * Asked once at connect, a board lacking it is released
   * No answer = older firmware, left alone
   */
  /**
   * Poll the board position every 500 ms
   * Stops after 5 failed polls, playtimeMs() then falls back to the wall clock
   */
  _startBoardPoll() {
    if (this._boardPoll || !this._isSendsid) return;
    this._boardPollFails = 0;
    this._boardPoll = setInterval(() => this._pollBoardTime(), 500);
  }

  _stopBoardPoll() {
    if (this._boardPoll) { clearInterval(this._boardPoll); this._boardPoll = null; }
  }

  async _pollBoardTime() {
    const t = this._transport;
    if (!t || !t.isOpen || typeof t.playerTime !== 'function') return;
    if (this._paused) return;
    const ms = await t.playerTime();
    if (ms == null) {
      if (++this._boardPollFails >= 5) {
        this._stopBoardPoll();
        this._log('the board will not report its position, using the wall clock');
      }
      return;
    }
    this._boardPollFails = 0;
    this._boardMs = ms;
  }

  /** Send the length of the current subtune to the board */
  async _sendBoardPlaytime(subtune) {
    const t = this._transport;
    if (!t || typeof t.playerSetPlaytime !== 'function') return;
    let ms = 0;
    try {
      /* Songlengths database first, embedded table of a v5 tune as fallback */
      let lens = this.songLengths(this.md5());
      if ((!lens || !lens.length) && typeof this._player.embeddedSongLengths === 'function') {
        lens = this._player.embeddedSongLengths();
      }
      if (lens && lens.length) {
        ms = lens[Math.min(lens.length, Math.max(1, subtune)) - 1] || 0;
      }
    } catch (_) { /* no lengths, board default */ }
    if (!ms) return;
    try {
      await t.playerSetPlaytime(ms);
      this._log(`board max playtime set to ${(ms / 1000).toFixed(1)}s`);
    } catch (_) { /* board falls back to five minutes */ }
  }

  /* ---- the onboard player's mute controls ------------------------------- */

  /** Mute or unmute one voice of a chip on the board */
  async playerMute(chip, voice, mute) {
    const t = this._transport;
    if (!t || typeof t.playerMute !== 'function') return false;
    return await t.playerMute(chip, voice, mute);
  }

  /** Mute all voices of all chips on the board */
  async playerMuteAll(mute) {
    const t = this._transport;
    if (!t || typeof t.playerMuteAll !== 'function') return false;
    return await t.playerMuteAll(mute);
  }

  /** Mute one chip (three voices), firmware has no chip form */
  async playerMuteChip(chip, mute) {
    const t = this._transport;
    if (!t || typeof t.playerMuteChip !== 'function') return false;
    return await t.playerMuteChip(chip, mute);
  }

  /** Board mute state, one bitmask per chip, or null */
  async playerMuteState() {
    const t = this._transport;
    if (!t || typeof t.playerMuteState !== 'function') return null;
    return await t.playerMuteState();
  }

  async _checkSidPlayer() {
    if (!this._isSendsid || !this._transport || !this._transport.isOpen) return;
    if (typeof this._transport.hasSidPlayer !== 'function') return;
    let has = null;
    try { has = await this._transport.hasSidPlayer(); } catch (_) { return; }
    if (has === null) {
      this._log('this firmware does not say whether it has the onboard player');
      return;
    }
    if (has) return;
    this._log('this board has no onboard SID player: SendSID has nothing to ' +
              'play the file. Disconnected. Build the firmware with ' +
              'ONBOARD_EMULATOR=1, or use one of the other modes.');
    this._status('no onboard player on this board, disconnected');
    try { await this._transport.disconnect(); } catch (_) {}
  }

  /**
   * Bring up the worker that runs the emulation for software audio
   * A hidden page throttles the main thread (Android screen off barely runs it), the audio thread
   * is not throttled, the worker holds one end of a MessageChannel to the AudioWorkletProcessor
   */
  /**
   * Is the worker wanted?
   * On by default (provisional), cannot be tested headless (audioWorklet.addModule never completes)
   * ?worker=0 turns it off and is remembered, ?worker=1 turns it on
   * With the worker the register grid, piano and memory view of the host show a still picture,
   * the page player is not the one stepping
   */
  _workerWanted() {
    try {
      const q = new URLSearchParams(location.search).get('worker');
      if (q === '1' || q === '0') {
        localStorage.setItem('usbsid_audio_worker', q);
      }
      const stored = localStorage.getItem('usbsid_audio_worker');
      return stored === null ? true : stored === '1';
    } catch (_) { return true; }
  }

  _startWorker() {
    if (this._workerReady) return this._workerReady;
    if (!this._workerWanted()) {
      this._workerReady = Promise.resolve(false);
      return this._workerReady;
    }
    this._workerReady = (async () => {
      /* Not fatal, fall back to the main thread (stutters while hidden) */
      try {
        /* Versioned like the wasm, a cached older worker lacks messages */
        this._worker = new Worker(versioned('usplayer-worker.js'), { type: 'module' });
        this._worker.onmessage = (e) => this._onWorkerMessage(e.data);
        this._worker.onerror = (e) => this._log('worker error: ' + (e.message || e));
        /* Deadline of 5s, a silent worker would hang load() */
        await Promise.race([
          this._call('audioInit', {
            /* .esm.js and not .mjs, stock nginx mime.types has no .mjs entry
           * and module imports are MIME checked, see CMakeLists.txt */
          wasmUrl: versioned('usbsid.esm.js'),
          }),
          new Promise((_, reject) =>
            setTimeout(() => reject(new Error('worker did not start in 5s')), 5000)),
        ]);
        this._log('audio runs in a worker, off the main thread');
        return true;
      } catch (e) {
        this._log('no audio worker (' + (e && e.message ? e.message : e) +
                  '), playing from the main thread');
        if (this._worker) { try { this._worker.terminate(); } catch (_) {} }
        this._worker = null;
        return false;
      }
    })();
    return this._workerReady;
  }

  _onWorkerMessage(d) {
    if (!d) return;
    if (d.type === 'log') { this._log(d.payload.message); return; }
    if (d.type === 'state') { this._snap = d.payload; return; }
    if (d.type === 'regs') { this._queueRegisters(d.payload); return; }
    const p = this._pending.get(d.id);
    if (!p) return;
    this._pending.delete(d.id);
    if (d.type === 'error') p.reject(new Error(d.payload.message));
    else p.resolve(d.payload);
  }

  /** One request to the worker, as a promise. */
  _call(type, payload, transfer) {
    const id = ++this._msgId;
    return new Promise((resolve, reject) => {
      this._pending.set(id, { resolve, reject });
      this._worker.postMessage({ type, payload, id }, transfer || []);
    });
  }

  /** Is the transport open */
  isConnected() {
    /* Software audio is always ready */
    if (this._isAudio) return true;
    return !!(this._transport && this._transport.isOpen);
  }

  /** Why the last connect failed, when the transport can say */
  get lastError() {
    return (this._transport && this._transport.lastError) || null;
  }

  /**
   * Fetch a file and play it
   *
   * @param {number} subtune  0 based like load_sidtune(), 0 is the default song of the file
   * @param {number} timeout  unused
   * @param {string} url      file to fetch
   * @param {function} callback  called when the tune runs
   * @param {number} song     optional, counts from one, a song picked with prev or next
   *                          Song 1 cannot be said with subtune, 0 is the default there
   */
  async load(subtune, timeout, url, callback, song) {
    /* A newer load() supersedes a running one, gen is checked before each worker RPC
     * overlapping loads must not interleave on the worker */
    const gen = ++this._loadGen;
    const stale = () => gen !== this._loadGen;
    await this._ensure();
    if (stale()) return;
    /* Stop clocking the playing tune before the fetch
     * _fill() runs on the main thread and would delay the fetch and loadSID */
    if (this._isAudio && this._audio) {
      this._audio.unrun();
      if (typeof this._audio.flush === 'function') this._audio.flush();
    }
    try {
      const bytes = await this._fetch(url);
      if (stale()) return;
      this._bytes = bytes;

      const sid = isSidHeader(bytes) && !/\.(prg|p00)(\?|$)/i.test(url);
      const nsids = sid ? countSids(bytes) : 1;
      /* ASID emits and the register grid shows only the chips it is told about */
      if (this._transport && 'nosids' in this._transport) this._transport.nosids = nsids;
      if (this._host.sidCount) this._host.sidCount(nsids);
      else if (typeof window.updateRegGridSIDCount === 'function') {
        window.updateRegGridSIDCount(nsids);
      }

      /* The wasm module is shared by the page, an earlier software audio play left
       * the machine on reSIDfp, a board or ASID transport needs the web backend back
       * (older wasm has no release) */
      if (!this._isAudio && typeof this._player.M._usp_audio_release === 'function') {
        this._player.M._usp_audio_release();
      }
      const ok = sid ? this._player.loadSID(bytes, subtune || 0, song || 0)
                     : this._player.loadPRG(bytes);
      if (!ok) throw new Error('neither a SID nor a program');

      /* Take the metadata from the player */
      const i = this._player.info();
      this._info = {
        maxSubsong: Math.max(0, i.songs - 1),
        songName: i.name,
        songAuthor: i.author,
        songReleased: i.released,
        numSids: nsids,
        /* A program has no subtunes and no header */
        isPrg: !sid,
        /* Song actually playing, subtune 0 is the file default (Mechanicus starts at song 3 of 18)
         * Read it back, do not assume */
        song: i.song,
        /* v5 FM/OPL tune */
        hasFm: !!i.hasFmOpl,
        /* Chip addresses as placed by the emulation, v5 layouts included */
        sidAddresses: (i.sids || []).map((c) => c.addr),
      };

      this._subtune = song > 0 ? song - 1 : (subtune || 0);

      /* New tune starts from an unwritten chip */
      this._shadow.fill(0xff);
      this._seen.fill(0);
      this._dirty.fill(0);
      this._anyDirty = false;
      this._regQueue.length = 0;
      this._scopeClear();
      this._workerCiaLatch = 0;
      this._memValid.fill(0);

      if (this._isAudio) {
        /* Started in load(), synthesis needs clock, chip count and model */
        if (!this._audio) {
          this._audio = new UsPlayerAudio(this._player);
          /* Report a skipped silent lead-in, only when it was long */
          this._audio.onSkipEnd = (why, frames) => {
            const secs = frames / (this._player.refreshHz() || 50);
            if (secs >= 1) {
              this._log(`ran through ${secs.toFixed(1)}s of silent loader (${why})`);
            }
          };
        }
        /* Volume set by the host before there was a graph */
        if (this._volume !== undefined) this._audio.setVolume(this._volume);
        this._reducedQuality = nsids > SINC_CHIP_LIMIT;
        const quality = this._reducedQuality ? 0 : 1;
        const started = await this._audio.start({
          chips: nsids,
          quality,
          model: sid ? sidModel(bytes) : 0,
        });
        if (!started) throw new Error('reSIDfp would not take this rate');
        if (stale()) return;

        /* Emulation runs in the worker and gets the ring
         * The page player only gives metadata and Songlengths, never steps */
        const inWorker = await this._startWorker();
        if (stale()) return;
        if (inWorker) {
          if (!this._audio.handedOver) {
            const port = this._audio.handOver();
            if (port) await this._call('clock', { port }, [port]);
            /* Hidden and visible want other ring depths, the worker fills it */
            this._audio.onTargetChange = (target, steps) => {
              if (this._worker) this._call('audioTarget', { target, steps });
            };
            /* Deeper ring while hidden, UsPlayerAudio only does this from run() */
            if (typeof this._audio._watchVisibility === 'function') {
              this._audio._watchVisibility();
            }
            this._watchResume();
          }
          if (stale()) return;
          const copy = bytes.slice();
          await this._call('loadSID',
                           { bytes: copy.buffer, subtune: subtune || 0, song: song || 0 },
                           [copy.buffer]);
          if (stale()) return;
          await this._call('audioConfigure', {
            chips: nsids,
            rate: this._audio.ctx.sampleRate | 0,
            quality,
            model: sid ? sidModel(bytes) : 0,
            target: this._audio._target,
            steps: this._audio._maxSteps || 24,
            /* Always stereo, mono = every chip Center, see setPanning() */
            stereo: true,
          });
          if (stale()) return;
          await this._softAudioReplay();
          if (this._sidVolume !== undefined) {
            await this._call('sidVolume', { percent: this._sidVolume }).catch(() => {});
          }
          if (this._fmVolume !== undefined) {
            await this._call('fmVolume', { percent: this._fmVolume }).catch(() => {});
          }
          /* Speed set before the worker was up */
          if (this._player.speed !== 1) {
            await this._call('speed', { mult: this._player.speed }).catch(() => {});
          }
          await this._call('start', {});
          if (stale()) return;
        } else {
          /* Main thread fallback */
          await this._player.start({ externalClock: true });
          if (this._filter) this.setFilter(this._filter);
          if (this._sidVolume !== undefined) this._audio.setSidVolume(this._sidVolume);
          if (this._fmVolume !== undefined) this._audio.setFmVolume(this._fmVolume);
          this._audio.run(this._player);
        }
        this._log(`software audio${inWorker ? ' in a worker' : ''}: ` +
                  `${this._audio.ctx.sampleRate} Hz, ` +
                  `${nsids} chip${nsids === 1 ? '' : 's'}, ` +
                  `${sid && sidModel(bytes) ? '8580' : '6581'}, sinc`);
      } else if (this._isSendsid) {
        /* The board plays it, the local player only reads the file for title, song count
         * and Songlengths key
         * uploadSIDFile() = stop, START, file in 62 byte pieces, END, SIZE, subtune, start
         * Counts songs from one, subtune is 0 based */
        if (!this._transport || !this._transport.isOpen) {
          throw new Error('no board on the serial port to send it to');
        }
        const song = (subtune || 0) + 1;
        const sent = await this._transport.uploadSIDFile(bytes, song);
        if (!sent) {
          throw new Error(this._transport.lastError || 'the board would not take the file');
        }
        this._boardStart = _now();
        this._boardPlayed = 0;
        this._boardMs = null;
        /* Stop the board at the subtune length (default is five minutes) */
        await this._sendBoardPlaytime(song);
        this._log(`sent to the board: ${bytes.length} bytes, ` +
                  `song ${song} of ${i.songs}, playing on its own player`);
      } else {
        await this._player.start();
      }
      if (stale()) return;
      this._paused = false;

      this._log(`${sid ? 'SID' : 'program'} loaded: ${i.name || '(untitled)'}` +
                (i.author ? ` - ${i.author}` : '') +
                (i.released ? ` (${i.released})` : '') +
                `, ${i.songs} song${i.songs === 1 ? '' : 's'}, ${nsids} SID` +
                `${nsids === 1 ? '' : 's'}, ${bytes.length} bytes`);
      if (typeof this._player.resetStats === 'function') this._player.resetStats();
      this._startReporting();
      if (this._isSendsid) this._startBoardPoll();

      if (typeof callback === 'function') callback();
    } catch (e) {
      this._log('load failed: ' + (e && e.message ? e.message : e));
      console.error('USPlayerAdapter.load:', e);
    }
  }

  play() {
    if (this._isSendsid) {
      if (!this._paused) return;
      /* Resume the board, nothing plays locally */
      if (this._transport && this._transport.playerStart) this._transport.playerStart();
      this._boardStart = _now();
      this._paused = false;
      this._startBoardPoll();
      return;
    }
    if (!this._player) return;
    if (this._paused) {
      this._player.pause(false);
      this._paused = false;
      if (this._isAudio && this._worker) this._call('pause', { on: false });
    }
  }

  pause() {
    if (this._isSendsid) {
      if (this._paused) return;
      if (this._transport && this._transport.playerPause) this._transport.playerPause();
      /* Bank what has played, the clock only measures the running part */
      this._boardPlayed += _now() - this._boardStart;
      this._paused = true;
      this._log('paused on the board');
      return;
    }
    if (!this._player) return;
    this._player.pause(true);
    this._paused = true;
    if (this._isAudio && this._worker) this._call('pause', { on: true });
    this._log('paused');
  }

  stop() {
    this._stopReporting();
    if (this._isSendsid) {
      if (this._transport && this._transport.playerStop) this._transport.playerStop();
      this._stopBoardPoll();
      this._boardStart = 0;
      this._boardPlayed = 0;
      /* Reset to 0:00, the board figure freezes at the stop position */
      this._boardMs = null;
      this._paused = false;
      this._lastStatus = '';
      this._log('stopped on the board');
      return;
    }
    /* Unrun before stopping, flush the ring (discard leaves the worklet ring playing
     * the last fifth of a second) */
    if (this._audio) {
      this._audio.unrun();
      if (typeof this._audio.flush === 'function') this._audio.flush();
      else this._audio.discard();
    }
    if (this._isAudio && this._worker) {
      this._call('stop', {});
      this._call('audioDiscard', {});
      this._snap = null;
      this._regQueue.length = 0;
      this._scopeClear();
    }
    if (this._player) this._player.stop();
    this._paused = false;
    this._lastStatus = '';
    this._log('stopped');
  }

  /**
   * Volume, 0 to 1
   * Software audio only, a tune owns $d418 and the board has a fixed output level
   * Return whether it was applied, for greying out a slider
   *
   * @param {number} value 0 for silence, 1 for full
   * Return true when this mode has a volume
   */
  setVolume(value) {
    this._volume = value;
    if (!this._isAudio) return false;
    /* No graph before load(), kept and applied in start() */
    if (this._audio && typeof this._audio.setVolume === 'function') {
      this._audio.setVolume(value);
    }
    return true;
  }

  /** Can the loudness of this mode be changed */
  hasVolume() { return this._isAudio; }

  /**
   * Set the SID side of the software mix
   *
   * @param {number} percent 100 is unity, 0 silences the SID side
   * Return true when this mode has a mix
   */
  setSidVolume(percent) {
    this._sidVolume = Math.max(0, Number(percent) || 0);
    if (!this._isAudio) return false;
    if (this._worker) this._call('sidVolume', { percent: this._sidVolume }).catch(() => {});
    else if (this._audio) this._audio.setSidVolume(this._sidVolume);
    return true;
  }

  /**
   * Set the FM/OPL side of the software mix
   *
   * @param {number} percent 100 is unity, 0 silences the FM side
   * Return true when this mode has a mix
   */
  setFmVolume(percent) {
    this._fmVolume = Math.max(0, Number(percent) || 0);
    if (!this._isAudio) return false;
    if (this._worker) this._call('fmVolume', { percent: this._fmVolume }).catch(() => {});
    else if (this._audio) this._audio.setFmVolume(this._fmVolume);
    return true;
  }

  /**
   * Does the loaded tune have an FM/OPL side?
   * True for the v5 header flag or once an older SID+FM tune wrote to the FM/OPL
   */
  hasFm() {
    if (this._info.hasFm) return true;
    if (!this._isAudio) return false;
    if (this._worker) return !!this._snap && this._snap.fmWrites > 0;
    return !!this._audio && this._audio.stats().fmWrites > 0;
  }

  /** Trim the hidden ring back to the visible target and resync registers */
  _trimOnReturn() {
    if (!this._audio || typeof this._audio.trim !== 'function') return;
    this._audio.trim();
    /* Queued reports were timed against the untrimmed ring */
    while (this._regQueue.length) this._applyRegisters(this._regQueue.shift());
  }

  /**
   * Resume an audio context the OS suspended while the page was hidden (mobile tab switch)
   */
  _watchResume() {
    if (this._onResumeVisibility || typeof document === 'undefined') return;
    this._onResumeVisibility = () => {
      if (document.visibilityState !== 'visible') return;
      /* After the UsPlayerAudio handler set the visible target, drop the excess ring */
      setTimeout(() => this._trimOnReturn(), 0);
      if (this._paused) return;
      const ctx = this._audio && this._audio.ctx;
      if (!ctx || ctx.state === 'running' || ctx.state === 'closed') return;
      this._log(`audio context ${ctx.state} on return, resuming`);
      ctx.resume().catch((e) => this._log('resume failed: ' + e));
    };
    document.addEventListener('visibilitychange', this._onResumeVisibility);
  }

  /**
   * MIDI outputs ASID mode can play to, [{ id, name }]
   * Empty outside ASID mode and before Web MIDI is granted
   */
  midiOutputs() {
    const t = this._transport;
    if (!t || typeof t.outputs !== 'function') return [];
    try { return t.outputs(); } catch (_) { return []; }
  }

  /**
   * Play ASID to the output with this name
   * By name, ids change between visits in some browsers
   *
   * @param {string} name as in midiOutputs()
   * Return true when the output exists
   */
  selectMidiOutput(name) {
    const t = this._transport;
    if (!t || typeof t.selectOutputByName !== 'function') return false;
    try { return !!t.selectOutputByName(name); } catch (_) { return false; }
  }

  /**
   * Name of the output ASID mode plays to now, empty when there is none
   *
   * A host picker shows this rather than its first entry: the transport prefers a
   * port matching its device name hint
   */
  currentMidiOutput() {
    const t = this._transport;
    if (!t || typeof t.outputs !== 'function') return '';
    try { return t.productName || ''; } catch (_) { return ''; }
  }

  getSongInfo() { return this._info; }

  /**
   * What drives the tune and how it was started, null when the player cannot say
   */
  timing() {
    if (this._isAudio && this._worker && this._snap) return this._snap.timing;
    if (!this._player || typeof this._player.timing !== 'function') return null;
    try { return this._player.timing(); } catch (_) { return null; }
  }

  /**
   * Emulated play time in ms, or null when nothing is loaded
   * Not for SendSID, the board plays there and its transport shows a dash
   */
  playtimeMs() {
    /* Audio mode asks the worker, _snap is its report (three a second) */
    if (this._isAudio && this._worker && this._snap) return this._snap.playtimeMs || 0;
    /* SendSID: no emulation and no board position, wall clock time since sending minus pauses */
    if (this._isSendsid) {
      /* Board position (SID_PLAYER_TIME) from the poll, wall clock as fallback */
      if (this._boardMs != null) return this._boardMs;
      if (this._boardStart === 0) return 0;
      return this._paused ? this._boardPlayed
                          : this._boardPlayed + (_now() - this._boardStart);
    }
    if (!this._player || typeof this._player.playtimeMs !== 'function') return null;
    try { return this._player.playtimeMs(); } catch (_) { return null; }
  }

  /**
   * Songlengths key of the loaded tune, the MD5 of the whole file
   * Computed on demand, a few kilobytes of MD5
   * Return 32 hex characters, '' when nothing is loaded
   */
  md5() {
    if (!this._player || !this._bytes) return '';
    if (typeof this._player.md5 !== 'function') return '';
    try { return this._player.md5(this._bytes); } catch (_) { return ''; }
  }

  /**
   * Hand the Songlengths database to the player, once
   *
   * @param {string} text the file as fetched
   */
  loadSonglengths(text) {
    if (!this._player || typeof this._player.loadSonglengths !== 'function') return false;
    try { return this._player.loadSonglengths(text); } catch (_) { return false; }
  }

  get hasSonglengths() {
    return !!(this._player && this._player.hasSonglengths);
  }

  /**
   * Every song length in ms for a key, or null when absent
   *
   * @param {string} key from md5()
   */
  songLengths(key) {
    if (!this._player || typeof this._player.songLengths !== 'function') return null;
    try { return this._player.songLengths(key); } catch (_) { return null; }
  }

  /**
   * Last value written to a SID register, from the write shadow
   * Pull side of setHost({ registers })
   * A shadow because USB reads are too slow, ASID has no read and software audio lives in
   * the audio thread, $1b and $1c (read only, oscillator 3 and envelope) answer 0
   * @param {number} chip 1 to 4
   * @param {number} reg  0 to 31, $d400 relative
   * Return the byte, 0 for a register nothing has written
   */
  readRegister(chip, reg) {
    const i = (((chip | 0) - 1) << 5) | (reg & 0x1f);
    if (i < 0 || i > 127) return 0;
    /* Software audio asks the emulation, a piano redraw wants this frame */
    if (this._isAudio && !this._worker &&
        this._player && typeof this._player.sidRegister === 'function') {
      try { return this._player.sidRegister(chip | 0, reg & 0x1f) & 0xff; }
      catch (_) { /* fall through to the shadow */ }
    }
    if (this._isAudio && this._worker) this._drainRegisters();
    const v = this._shadow[i];
    /* 0xff fill = never written, a quiet chip looks quiet */
    return this._dirtyEver(i) ? v : 0;
  }

  /**
   * Hold a worker register report until its audio is heard
   * The worker emulates ahead of the playhead by leadMs
   *
   * @param {object} p { regs: Uint8Array, ciaLatch, leadMs, mem?, memPages? }
   */
  _queueRegisters(p) {
    if (!p || !p.regs) return;
    const due = _now() + Math.max(0, Number(p.leadMs) || 0);
    this._regQueue.push({ due, regs: p.regs, ciaLatch: p.ciaLatch | 0,
                          mem: p.mem, memPages: p.memPages,
                          scope: p.scope, scopeVoices: p.scopeVoices | 0 });
    /* Bound the queue */
    if (this._regQueue.length > 256) this._applyRegisters(this._regQueue.shift());
  }

  /** Apply the queued worker register reports that are due */
  _drainRegisters() {
    const now = _now();
    while (this._regQueue.length && this._regQueue[0].due <= now) {
      this._applyRegisters(this._regQueue.shift());
    }
  }

  /**
   * Copy one worker register report into the shadow
   *
   * @param {object} e { regs, ciaLatch, mem?, memPages? }
   */
  _applyRegisters(e) {
    const n = Math.min(128, e.regs.length);
    for (let i = 0; i < n; i++) {
      const v = e.regs[i];
      this._seen[i] = 1;
      if (this._shadow[i] === v) continue;
      this._shadow[i] = v;
      this._dirty[i] = 1;
      this._anyDirty = true;
    }
    this._workerCiaLatch = e.ciaLatch;
    if (e.scope && e.scopeVoices > 0) this._scopeAppend(e.scope, e.scopeVoices, e.due);
    if (e.mem && e.memPages) {
      const pages = e.memPages;
      for (let k = 0; k < pages.length; k++) {
        const pg = pages[k];
        this._mem.set(e.mem.subarray(k << 8, (k + 1) << 8), pg << 8);
        this._memValid[pg] = 1;
      }
    }
  }

  /** Has this shadow slot ever been written */
  _dirtyEver(i) {
    if (!this._seen) return false;
    return this._seen[i] === 1;
  }

  /**
   * One byte of emulated RAM, or 0 when nothing is loaded
   *
   * @param {number} address 0 to 65535
   */
  readMemory(address) {
    /* Worker mode answers from the mirror filled by worker reports */
    if (this._isAudio && this._worker) {
      const a = address & 0xffff;
      this._watchPage(a >> 8);
      this._drainRegisters();
      if (this._memValid[a >> 8]) return this._mem[a];
    }
    if (!this._player || typeof this._player.readMemory !== 'function') return 0;
    try { return this._player.readMemory(address); } catch (_) { return 0; }
  }

  /**
   * Mark a RAM page as read and tell the worker when the watched set changes
   * Pages idle for MEM_WATCH_IDLE_MS drop out of the set and the mirror
   *
   * @param {number} page 0 to 255
   */
  _watchPage(page) {
    const now = _now();
    const fresh = !this._memPages.has(page);
    this._memPages.set(page, now);
    if (!fresh && now - this._memCheck < MEM_WATCH_CHECK_MS) return;
    this._memCheck = now;
    for (const [pg, t] of this._memPages) {
      if (now - t <= MEM_WATCH_IDLE_MS) continue;
      this._memPages.delete(pg);
      this._memValid[pg] = 0;
    }
    /* Most recently read first, up to MEM_WATCH_MAX */
    const pages = [...this._memPages.entries()]
      .sort((x, y) => y[1] - x[1])
      .slice(0, MEM_WATCH_MAX)
      .map(([pg]) => pg)
      .sort((x, y) => x - y);
    const key = pages.join(',');
    if (key === this._memSent) return;
    this._memSent = key;
    this._call('memWatch', { pages }).catch(() => {});
  }

  /**
   * Latch of a CIA timer, the call rate of a CIA driven tune
   *
   * @param {number} cia   1 or 2
   * @param {number} timer 0 for A, 1 for B
   */
  ciaLatch(cia = 1, timer = 0) {
    if (this._isAudio && this._worker) {
      this._drainRegisters();
      return (cia === 1 && timer === 0) ? this._workerCiaLatch : 0;
    }
    if (!this._player || typeof this._player.ciaLatch !== 'function') return 0;
    try { return this._player.ciaLatch(cia, timer); } catch (_) { return 0; }
  }

  /**
   * Mute or unmute one voice
   *
   * @param {number} chip  1 to 4
   * @param {number} voice 1 to 3
   * @param {boolean} muted
   */
  setVoiceMute(chip, voice, muted) {
    /* SendSID: the board plays, mute it with SID_PLAYER_MUTE */
    if (this._isSendsid) {
      const t = this._transport;
      if (t && typeof t.playerMute === 'function') {
        /* Not awaited, no USB round trip in a click handler */
        t.playerMute(chip, voice, muted);
      }
      return;
    }
    if (!this._player || typeof this._player.setVoiceMute !== 'function') return;
    this._player.setVoiceMute(chip, voice, muted);
    /* Worker needs it too */
    if (this._isAudio && this._worker) {
      this._call('voiceMute', { chip, voice, muted: !!muted });
    }
  }

  /**
   * Mute or unmute a whole chip
   * Board command in SendSID mode, emulation (and worker) otherwise
   * A chip mute drops the writes, silences $18 sample playback too
   *
   * @param {number} chip 1 to 4
   * @param {boolean} muted
   */
  setChipMute(chip, muted) {
    if (this._isSendsid) {
      const t = this._transport;
      if (t && typeof t.playerMuteChip === 'function') {
        /* Not awaited, no USB round trip in a click handler */
        t.playerMuteChip(chip, muted);
      }
      return;
    }
    if (!this._player || typeof this._player.setChipMute !== 'function') return;
    this._player.setChipMute(chip, muted);
    if (this._isAudio && this._worker) {
      this._call('chipMute', { chip, muted: !!muted });
    }
  }

  /** Muted chips as bitmask (bit 0 = chip 1), 0 when unknown */
  chipMute() {
    if (!this._player || typeof this._player.chipMute !== 'function') return 0;
    try { return this._player.chipMute(); } catch (_) { return 0; }
  }

  /** Mute bits of one chip (bit 0 = voice 1), 0 when unknown */
  voiceMute(chip = 1) {
    if (!this._player || typeof this._player.voiceMute !== 'function') return 0;
    try { return this._player.voiceMute(chip); } catch (_) { return 0; }
  }

  /** Header of the loaded file, or null */
  bytes() { return this._bytes; }

  /**
   * Is the browser holding the audio context suspended?
   * Software audio only, other modes say no
   * Counts as suspended while the context does not exist yet, it opens on the first load
   */
  isSuspended() {
    const ctx = this._audio && this._audio.ctx;
    if (!ctx) return this._isAudio;
    return ctx.state === 'suspended';
  }

  /* Extras */
  /**
   * Run faster than the tune asks for
   *
   * @param {boolean} on
   * @param {number} [mult] how much faster, player default otherwise
   */
  fastForward(on, mult) {
    if (this._player) this._player.fastForward(on, mult);
    if (this._isAudio && this._worker) {
      this._call('fastForward', { on: !!on, mult }).catch(() => {});
    }
  }

  /**
   * Play at a multiple of the tune speed, 1 is normal
   * Unlike fastForward() (a seek, no writes to the board) the output is kept, board is audibly faster
   * Software audio changes the output rate, pitch follows
   *
   * @param {number} mult 0.1 to 8
   */
  setSpeed(mult) {
    if (this._player) this._player.setSpeed(mult);
    if (this._isAudio && this._worker) {
      this._call('speed', { mult }).catch(() => {});
    }
  }

  /**
   * Mono/stereo audio switch of the board, v1.3+ PCBs
   * Board transports only, return false when the transport cannot do it
   *
   * @param {boolean|undefined} stereo true or false to set it, undefined to flip
   */
  setAudioSwitch(stereo) {
    const t = this._transport;
    if (!t) return false;
    if (stereo === undefined) {
      if (typeof t.toggleAudioSwitch !== 'function') return false;
      t.toggleAudioSwitch();
      return true;
    }
    if (typeof t.setAudioSwitch !== 'function') return false;
    t.setAudioSwitch(!!stereo);
    return true;
  }

  /** Can the transport switch between mono and stereo */
  hasAudioSwitch() {
    return !!(this._transport &&
              typeof this._transport.setAudioSwitch === 'function');
  }
  nextSubtune() {
    if (!this._player) return;
    this._player.nextSubtune();
    this._subtune = Math.min(this._subtune + 1, this._info.maxSubsong);
  }
  prevSubtune() {
    if (!this._player) return;
    this._player.prevSubtune();
    this._subtune = Math.max(this._subtune - 1, 0);
  }
}

applySoftAudio(USPlayerAdapter);

if (typeof window !== 'undefined') {
  window.USPlayerAdapter = USPlayerAdapter;
}
