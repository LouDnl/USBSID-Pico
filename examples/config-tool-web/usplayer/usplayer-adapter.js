/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usplayer-adapter.js
 * The player wearing the interface another app expects.
 *
 * Carried over from player-repo/web/usplayer-adapter.js. It wraps
 * USBSIDPlayerWeb in the shape repo/examples/config-tool-web asks of a player
 * backend (load / play / pause / stop / setVolume / getSongInfo / paused /
 * emulator), so the app can select it the way it selects any other.
 *
 * In WebUSB mode it reuses the USBSIDDevice the app has already connected,
 * injected by whatever constructs this, so there is never a second connection
 * to the same board. In ASID mode it opens Web MIDI itself and follows the
 * app's own output picker by name.
 *
 * Loaded as an ES module, and registers window.USPlayerAdapter so a classic
 * script can construct it. The WASM factory (window.USBSIDPlayer) comes from
 * usbsid.js, which the page loads as a classic script.
 *
 * It also reports the player's status into the app's own status line, and logs
 * a load and a stop into its log panes, which is why nothing in
 * config-tool-web needs changing to see any of it. Periodic output goes to the
 * status line rather than the log on purpose: see _report().
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

import { USBSIDPlayerWeb, NullTransport, isSidHeader, countSids, sidModel }
  from './usplayer-web.js';
import { USBSIDWebUSBTransport } from './usbsid-webusb.js';
import { USBSIDWebSerialTransport } from './usbsid-webserial.js';
import { ASIDMIDITransport } from './asid-midi.js';
import { UsPlayerAudio } from './usplayer-audio.js';

/* Where the build artefacts are: beside this file, whatever the page's own URL
 * is.
 *
 * Resolved from `import.meta.url` and not from the page, because the two are not
 * the same directory in every host. config-tool-web keeps the whole build in
 * `usplayer/` next to its index.html, so a page relative 'usplayer/' worked
 * there; DeepSID keeps it in `js/handlers/usplayer/` under a page at the site
 * root, where a page relative path would look for the wasm at `/usplayer/` and
 * find nothing. The wasm, the ES module and this file ship together and always
 * have, so "next to me" is the one answer that is right in every host. */
const WASM_DIR = new URL('./', import.meta.url).href;

/* A cache buster to pass on, when the host gave this file one.
 *
 * A host that imports this as `usplayer-adapter.js?v=<something>` wants that
 * `<something>` on the wasm and the ES module too, and it matters more there
 * than here: those two are fetched by script rather than by the document, so a
 * reload does not necessarily refetch them, and even a hard reload was seen to
 * hand back a stale `usbsid.wasm` beside a fresh `usbsid.esm.js`. That pairing
 * does not fail loudly. It fails as `usp_audio_configure()` returning 0 and a
 * tune that will not play, which is a long way from the cause.
 *
 * Empty when the host did not ask for one, which is config-tool-web and the demo
 * pages, and then every URL below is exactly what it always was. */
const VERSION = new URL(import.meta.url).search;

/** `name` with the host's cache buster on it, if there is one. */
function versioned(name) { return WASM_DIR + name + VERSION; }

/** Milliseconds, monotonic where the browser offers it. */
function _now() {
  return (typeof performance !== 'undefined') ? performance.now() : Date.now();
}

/* How often the register grid is pushed, in milliseconds.
 *
 * Twenty a second. The eye reads a hex byte in well under that, and the app's
 * highlight lasts 180 ms, so a change is always seen; going slower starts to
 * look like a recording rather than a live chip. Going faster shows changes
 * nobody can distinguish and puts the work back on the thread the emulation is
 * on. A tune writing $d418 six hundred times a frame is not something a grid
 * can show and not something anyone could read: what is displayed is the value
 * as it stands each time this fires. */
const GRID_MS = 50;

/* The status line, once a second: it is text to read, not an animation. */
const STATUS_EVERY = 20;

let _modulePromise = null;
/**
 * The wasm module, instantiated once for the page.
 *
 * The factory comes from `usbsid.js` when the host has loaded it as a classic
 * script and put `USBSIDPlayer` on the window, which is what config-tool-web
 * does. A host that has not is not required to: the same build ships as
 * `usbsid.esm.js` beside this file and is imported on demand. That way adding
 * this player to a page is one module import and nothing else, with no global
 * to arrange and no script tag whose order matters.
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

/* How many chips a tune wants, from its header. Version 3 puts a second SID
 * address at 0x7a and version 4 a third at 0x7b; a non zero byte means that
 * chip is there. Anything older, or too short to say, is a single SID. */
export class USPlayerAdapter {
  /**
   * @param {string} emulator  'usplayer' (WebUSB) or 'usplayer-asid'
   * @param {object} device    the app's connected USBSIDDevice, WebUSB mode
   */
  constructor(emulator, device) {
    this.emulator = emulator;
    this._device = device || null;
    this._isAsid = (emulator === 'usplayer-asid');
    /* Web Serial talks to the board's CDC interface, which the host app's own
     * driver knows nothing about: the app owns a WebUSB device on the vendor
     * interface. So this mode opens its own port rather than sharing, and it is
     * the only mode here that works in a browser with no WebUSB at all. */
    this._isSerial = (emulator === 'usplayer-serial');
    /* No board at all: reSIDfp inside the same wasm, out through an
     * AudioWorklet. The one mode here that needs nothing plugged in, which is
     * what it replaced Hermit jsSID for. */
    this._isAudio = (emulator === 'usplayer-audio');
    /* The whole file goes to the board and the board's own player plays it.
     * Nothing is emulated here, so this is the one mode where the page cannot
     * say what the chip is doing: no register writes pass through it and there
     * is no play position to read back. Same transport as `usplayer-serial`,
     * a very different division of labour. */
    this._isSendsid = (emulator === 'usplayer-sendsid');
    /* Where the board's playback started, in `performance.now()` terms, and
     * how much of it has been played. The board reports no position, so this
     * is the only clock there is: see playtimeMs(). */
    this._boardStart = 0;
    this._boardPlayed = 0;
    /* The board's own position, from the last successful poll, or null while it
     * has not answered. See _pollBoardTime(). */
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
    this._volume = 1;
    /* The register grid's shadow copy: see the onWrite comment in _ensure(). */
    this._shadow = new Uint8Array(128).fill(0xff);
    this._dirty = new Uint8Array(128);
    /* Which slots a tune has ever written. The shadow starts filled with 0xff so
     * that the first write of any value counts as a change and gets pushed, which
     * means the fill cannot itself be read back as a value: see
     * readRegister(). */
    this._seen = new Uint8Array(128);
    this._anyDirty = false;
    this._tick = 0;
    /* Software audio runs the emulation in a worker, see _startWorker(). The
     * page keeps a player of its own for the file's metadata and for the
     * Songlengths lookup, both of which are synchronous calls the host makes,
     * but that player is never started and never steps. What is playing is the
     * worker's, and `_snap` is the last thing it said about itself. */
    this._worker = null;
    this._workerReady = null;
    this._snap = null;
    this._msgId = 0;
    this._pending = new Map();
    /* What the host wants doing with the running commentary, see setHost(). All
     * null means "config-tool-web", which is what this was written for and what
     * every default below reproduces. */
    this._host = { log: null, status: null, registers: null, sidCount: null };
  }

  /**
   * Tell the adapter how this host wants to be talked to.
   *
   * Everything here has a config-tool-web shaped default: log lines go into
   * `#player-log` and `#debug-log`, the status line into `#status-text`, and
   * changed registers to `window.updateSIDReg`. That suits a page written around
   * this player and suits nothing else, so a host with its own ideas passes them
   * in and none of the defaults run.
   *
   * Callbacks rather than element ids on purpose: a host that wants a log line
   * in a console, a status in a title bar or registers in a canvas should not
   * have to own an element with a particular id to get them.
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
   * Put a line in the app's log panes.
   *
   * Straight into the elements rather than through the app's own `usbsidLog`,
   * which is a plain function in a classic script and so is not reachable from
   * a module. Doing it this way is what keeps this a drop in: nothing in
   * config-tool-web has to change.
   *
   * Only for the occasional event, a load or a stop. Anything periodic goes to
   * the status line instead: see _report() for why appending here while a tune
   * plays is a bad idea.
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
   * The running status, into the app's status line rather than its log.
   *
   * Not the log: appending there is `textContent +=`, which rereads and
   * rewrites the whole buffer every time and so gets slower the longer it
   * runs, followed by a scroll that forces a synchronous layout. Twice a
   * second on the thread the emulation is on, that is enough to show up as a
   * gap in the writes. This is one small element, set only when the text has
   * actually changed.
   *
   * The app writes `Playing: <name>` here itself, so that is kept as the
   * prefix and the numbers are appended to it.
   *
   * `drain` is the mean and the worst gap between a frame's writes going out,
   * against a frame of about 20 ms; `queue` is transfers waiting; `dropped` is
   * the write ring overflowing and should never be anything but zero.
   */
  _report() {
    if (!this._player || !this._player.isPlaying()) return;
    /* Nowhere to put it: no host callback and no element to write into. Worth
     * leaving early, because building the line asks the transport for its
     * statistics. */
    if (!this._host.status &&
        (typeof document === 'undefined' ||
         !document.getElementById('status-text'))) return;

    const p = this._player;

    /* Nothing here is running in SendSID mode: the board has the file and is
     * playing it on its own. Frames, fps and the transport counters would all
     * read zero and say nothing, so this reports the one thing the page does
     * know, which is how long ago it sent it. */
    if (this._isSendsid) {
      const secs = (this.playtimeMs() / 1000).toFixed(1);
      this._status(`${this._prefix} | playing on the board` +
                   `${this._paused ? ', paused' : ''} | ${secs}s in`);
      return;
    }

    const s = (typeof p.stats === 'function') ? p.stats() : null;

    let line = `${this._prefix} | ${p.refreshHz().toFixed(2)} fps` +
               ` | ${p.frames()} frames`;

    /* Software audio has no transport, so the board numbers below are all zero
     * and meaningless. These are the ones that matter instead: ms/frame against
     * a 20 ms budget says whether the machine can keep up at all, and starve is
     * silence the worklet had to invent, heard as crackle rather than as a gap.
     * Both stay flat when it is working. */
    if (this._isAudio && this._audio) {
      const a = this._audio.stats();
      /* Running through a silent lead-in is the one time the numbers below mean
       * nothing: the ring is empty on purpose and the tune is deliberately not
       * playing at one times speed. Say what is happening instead. */
      if (a.skipping) {
        this._status(`${this._prefix} | running through the loader` +
                     ` | ${(p.playtimeMs() / 1000).toFixed(1)}s in`);
        return;
      }
      line += ` | ${a.msPerFrame.toFixed(1)}/20 ms per frame` +
              ` | buffer ${a.queuedMs} ms`;
      /* Only when a tune has an FM side at all, which most do not. */
      if (a.fmWrites > 0) line += ` | FM ${a.fmWrites}`;
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

  /** The status line, wherever this host keeps it, and only when it changed. */
  _status(line) {
    if (line === this._lastStatus) return;
    this._lastStatus = line;
    if (this._host.status) { this._host.status(line); return; }
    if (typeof document === 'undefined') return;
    const el = document.getElementById('status-text');
    if (el) el.textContent = line;
  }

  /**
   * Fill the shadow from the emulation's own register mirror.
   *
   * Only software audio needs this, and it needs it badly: `usp_audio_configure`
   * hands the emulation's SID over to reSIDfp, which consumes every write inside
   * `advance()`, so the transport this adapter listens to sees nothing at all.
   * Without this a register grid, a piano and an oscilloscope are dead in the one
   * mode that needs no hardware, while the tune plays perfectly.
   *
   * The board modes are left on the write side, where the shadow is a record of
   * what was actually sent to the board, which is the more useful truth there.
   *
   * Not in worker mode: the page's own player is loaded but never stepped, so its
   * mirror is whatever the tune wrote during the load. See TODO 29.
   */
  _pollRegisters() {
    if (!this._isAudio || this._worker) return;
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

  /** Push the registers that changed since last time, and nothing else. */
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
    /* Whatever the app last put in the status line is the name of the tune,
     * which it sets right after load. Picked up once rather than fought over. */
    if (this._host.status) {
      /* A host that takes the status line owns what is in it, so there is
       * nothing of its to read back and nothing to preserve. */
      this._prefix = 'Playing';
    } else {
      setTimeout(() => {
        const el = (typeof document !== 'undefined')
          ? document.getElementById('status-text') : null;
        this._prefix = (el && el.textContent) ? el.textContent : 'Playing';
      }, 250);
    }
    /* One timer for both: the grid at twenty a second, which is faster than
     * the eye, and the status line at one. */
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
        /* Nothing to connect to, and nothing for a transport to carry:
         * usp_audio_configure() takes the emulation's backend over, so no write
         * ever reaches the write ring. The AudioContext is not opened here but
         * in load(), because it needs the tune's clock and chip count and
         * because a context created outside a user gesture stays suspended. */
        this._transport = new NullTransport();
        /* Bring the worker up now rather than at the first load. It has a wasm
         * module of its own to fetch and instantiate, and doing that while the
         * user is still choosing a tune costs nothing; doing it inside load()
         * puts it between the click and the first note. */
        this._startWorker().catch((e) => this._log('worker init: ' + e.message));
      } else if (this._isAsid) {
        this._transport = new ASIDMIDITransport();
        try {
          await this._transport.connect(null);
          this._wireMidiPicker();
        } catch (e) { console.warn('ASID connect:', e); }
      } else if (this._isSendsid) {
        /* Either interface can carry it: the firmware takes the same packets
         * over the vendor interface and over CDC. WebUSB first, because a
         * browser that has already granted the board for another mode needs no
         * second dialog and no second picker, and because only the vendor
         * interface can be probed for what the firmware was built with. Web
         * Serial is the fallback, and the only route in a browser with no
         * WebUSB at all: see TODO 35 and the note in config-tool-web. */
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
        /* Both talk to the board's CDC interface. The difference is what goes
         * down it: a stream of register writes for `serial`, the whole file
         * once for `sendsid`.
         *
         * Only what the origin has already been granted, exactly as the WebUSB
         * branch below does. `connect()` calls `requestPort()`, which shows a
         * picker and needs a user gesture; calling it from here threw a chooser
         * at anyone who merely selected the mode, and when there was no gesture
         * it threw an exception instead and left the port shut with a line in
         * the log. The picker belongs to the host's Connect button, which
         * reaches it through connect(). */
        this._transport = new USBSIDWebSerialTransport();
        try {
          const ok = await this._transport.connectGranted();
          if (!ok) this._log('no serial port granted yet, press Connect');
        } catch (e) { this._log('Web Serial: ' + e.message); }
      } else {
        this._transport = new USBSIDWebUSBTransport({ device: this._device });
        try {
          /* With a device handed to us there is nothing to ask for: the host has
           * already opened the board and `connect()` only confirms it.
           *
           * Without one, take what the origin has already been granted and do
           * not go further. `connect()` calls `requestDevice()`, which shows a
           * picker, and a picker belongs to a button the user pressed, not to
           * whatever happened to bring the player up: a host with no board
           * plugged in would otherwise get a chooser thrown at it for choosing
           * the handler. The host's own connect button reaches the picker
           * through connect() below. */
          if (this._device) await this._transport.connect();
          else await this._transport.connectGranted();
        } catch (_) {}
      }
      this._player = new USBSIDPlayerWeb(M, this._transport);

      /* Mirror the writes into the app's register display, which is otherwise
       * only fed by its own player.
       *
       * Into a shadow copy, not straight through. Calling the app's
       * updateSIDReg per write means a DOM update per write, and a digi writes
       * about thirty thousand registers a second: measured in the config tool,
       * that alone held the tab at 102% of a core and grew it to two gigabytes,
       * because the garbage came faster than it could be collected. The grid
       * cannot show thirty thousand changes a second and nobody could read them
       * if it did. So the values are kept here and only the ones that actually
       * changed are pushed, twenty times a second, which is faster than the eye
       * and a thousandth of the work. */
      /* What the board is carrying. The app hands us its own connected
       * device, so the read goes through that rather than opening a second
       * conversation with the same board. Without this $df40/$df50 reach
       * nothing and an FM/OPL tune plays no OPL. */
      try {
        /* No board to ask, and asking would time out. */
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
   * Follow the app's shared MIDI output list without rewriting it.
   *
   * The list belongs to the app: its option values are indices into the app's
   * own MIDIAccess, so replacing them with our port ids breaks the app's other
   * player. Read the selected option's name and map that onto our transport.
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
   * The app's entry point: load(subtune, timeout, url, callback), subtune
   * counted from zero with zero meaning the file's own default.
   */
  /**
   * Bring the transport up now, rather than when the first tune loads.
   *
   * Web Serial's `requestPort()` needs a user gesture, and `_ensure()` used to
   * run on the first `load()`, which meant the port picker appeared long after
   * the host's connect button had already gone green. Whoever owns that button
   * calls this from the click instead.
   *
   * @returns true when the transport is open and ready to be written to.
   */
  async connect() {
    await this._ensure();
    /* Still shut. `_ensure()` only ever takes what was already granted, so this
     * is where a picker is allowed: this call came from a host's connect button,
     * which is the user gesture `requestDevice()` and `requestPort()` need.
     * `_ensure()` is memoized and will not retry, so the retry lives here. */
    if (!this.isConnected() && this._transport &&
        typeof this._transport.connect === 'function') {
      try { await this._transport.connect(); }
      catch (e) { this._log('connect: ' + (e && e.message ? e.message : e)); }
      /* This is where a board is really chosen, so this is where it has to be
       * asked whether it can do the job at all. */
      await this._checkSidPlayer();
    }
    return this.isConnected();
  }

  /**
   * Refuse a board that cannot play what SendSID sends it.
   *
   * The firmware says what it was compiled with, one byte of flags, and bit 7
   * is the embedded SID player. Without it the board takes the whole upload,
   * answers every packet and plays **nothing**, which from the outside is
   * indistinguishable from a broken file or a wrong subtune. So it is asked
   * once, at connect, and a board that says no is let go of again rather than
   * left looking connected.
   *
   * A board that will not answer at all is left alone: older firmware predates
   * the question, and refusing to talk to it would be worse than not knowing.
   */
  /**
   * Ask the board where it has got to, on a timer of its own.
   *
   * Half a second, not the host's display rate: each poll is a write followed by
   * a read with a timeout, and `_configRead()` leaves a timed out read pending so
   * a late reply would satisfy the next one. The transport guards against
   * overlapping reads, and this gives up after a few failures rather than asking a
   * board that clearly will not answer twice a second for the rest of the session.
   * Giving up leaves `_boardMs` null, which puts playtimeMs() back on the wall
   * clock, so the display carries on either way.
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

  /** The current subtune's length, to the board, when it is known. */
  async _sendBoardPlaytime(subtune) {
    const t = this._transport;
    if (!t || typeof t.playerSetPlaytime !== 'function') return;
    let ms = 0;
    try {
      const lens = this.songLengths(this.md5());
      if (lens && lens.length) {
        ms = lens[Math.min(lens.length, Math.max(1, subtune)) - 1] || 0;
      }
    } catch (_) { /* no lengths, the board keeps its own default */ }
    if (!ms) return;
    try {
      await t.playerSetPlaytime(ms);
      this._log(`board max playtime set to ${(ms / 1000).toFixed(1)}s`);
    } catch (_) { /* not fatal: the board falls back to five minutes */ }
  }

  /* ---- the onboard player's mute controls ------------------------------- */

  /** Mute or unmute one voice of one chip on the board. */
  async playerMute(chip, voice, mute) {
    const t = this._transport;
    if (!t || typeof t.playerMute !== 'function') return false;
    return await t.playerMute(chip, voice, mute);
  }

  /** Every voice of every chip on the board. */
  async playerMuteAll(mute) {
    const t = this._transport;
    if (!t || typeof t.playerMuteAll !== 'function') return false;
    return await t.playerMuteAll(mute);
  }

  /** One chip, as its three voices: the firmware has no chip form. */
  async playerMuteChip(chip, mute) {
    const t = this._transport;
    if (!t || typeof t.playerMuteChip !== 'function') return false;
    return await t.playerMuteChip(chip, mute);
  }

  /** The board's mute state, one bitmask per chip, or null. */
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
              'ONBOARD_SIDPLAYER=1, or use one of the other modes.');
    this._status('no onboard player on this board, disconnected');
    try { await this._transport.disconnect(); } catch (_) {}
  }

  /**
   * Bring up the worker that will run the emulation for software audio.
   *
   * The main thread is throttled hard as soon as the page is hidden, and on
   * Android with the screen off it barely runs; the audio thread is never
   * throttled. Feeding the ring from the main thread therefore stutters however
   * deep the buffer is, which is what a bigger buffer and a keep-alive timer
   * failed to fix. A worker is throttled far less, and once it holds one end of
   * a MessageChannel whose other end is inside the AudioWorkletProcessor, the
   * main thread is not in the audio path at all.
   */
  /**
   * Is the worker wanted?
   *
   * **On by default, provisionally, so that it gets used and judged.** Moving
   * the emulation off the main thread is the right fix for a hidden page
   * stuttering, and it cannot be exercised in a headless browser at all:
   * `audioWorklet.addModule` never completes there. It was off behind a switch
   * for exactly that reason, which meant nobody ever ran it, which meant it
   * stayed unproven. See TODO 29.
   *
   * `?worker=0` turns it off and is remembered, `?worker=1` turns it back on.
   * What to weigh while it is on: the audio should survive a backgrounded tab,
   * and a host's register grid, piano and memory view will show a still picture,
   * because the player they read is the page's and the one that is stepping is
   * the worker's.
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
      /* Never fatal. A browser with no module workers, a page served in a way
       * that will not let one start, a wasm the worker cannot fetch: any of
       * those must leave a working player rather than a dead one, because the
       * main thread can still do this. It only does it badly while hidden,
       * which is the whole reason for the worker and is not a reason to lose
       * playback when there cannot be one. */
      try {
        this._worker = new Worker(new URL('./usplayer-worker.js', import.meta.url),
                                  { type: 'module' });
        this._worker.onmessage = (e) => this._onWorkerMessage(e.data);
        this._worker.onerror = (e) => this._log('worker error: ' + (e.message || e));
        /* With a deadline. A worker that comes up but never answers, because
         * its wasm will not fetch or instantiate, would otherwise leave load()
         * awaiting for ever and nothing would play at all: a worse failure than
         * the stutter this is here to fix. Five seconds is far longer than
         * instantiating a 150 kB module takes on a phone. */
        await Promise.race([
          this._call('audioInit', {
            /* `.esm.js` and not `.mjs`: nginx's stock mime.types has no entry
           * for .mjs, so it arrives as application/octet-stream, and a module
           * import is strictly MIME checked and refuses it. Same bytes, a name
           * every server already knows. See CMakeLists.txt. */
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

  /** Is the transport actually open? Not "could it be". */
  isConnected() {
    /* Software audio has nothing to open, so it is always ready. Reporting
     * otherwise leaves the host's connect button and transport controls greyed
     * out for ever, since nothing will ever make it true. */
    if (this._isAudio) return true;
    return !!(this._transport && this._transport.isOpen);
  }

  /** Why the last connect failed, when the transport can say. */
  get lastError() {
    return (this._transport && this._transport.lastError) || null;
  }

  async load(subtune, timeout, url, callback) {
    await this._ensure();
    /* Stop clocking the tune that is playing, before the fetch rather than
     * after it.
     *
     * `_fill()` runs on the main thread and emulates up to eight frames every
     * time the worklet asks, which is twice every ten milliseconds or so. A
     * fetch, a JSON-free parse and a `loadSID` all have to get in between those,
     * so choosing a tune while one is playing took visibly longer than pressing
     * stop and then choosing it: stop calls `unrun()`, and the main thread is
     * free. Doing it here makes the two paths the same one.
     *
     * It also means the sound stops when the tune is clicked rather than when
     * the file arrives, which is what clicking a different tune is asking for,
     * and it removes the window in which the fill loop could step a player that
     * had already been handed different bytes. */
    if (this._isAudio && this._audio) {
      this._audio.unrun();
      if (typeof this._audio.flush === 'function') this._audio.flush();
    }
    try {
      const bytes = await this._fetch(url);
      this._bytes = bytes;

      const sid = isSidHeader(bytes) && !/\.(prg|p00)(\?|$)/i.test(url);
      const nsids = sid ? countSids(bytes) : 1;
      /* ASID only emits the chips it is told about, and the app's register
       * grid only shows those it is told about. */
      if (this._transport && 'nosids' in this._transport) this._transport.nosids = nsids;
      if (this._host.sidCount) this._host.sidCount(nsids);
      else if (typeof window.updateRegGridSIDCount === 'function') {
        window.updateRegGridSIDCount(nsids);
      }

      const ok = sid ? this._player.loadSID(bytes, subtune || 0)
                     : this._player.loadPRG(bytes);
      if (!ok) throw new Error('neither a SID nor a program');

      /* The player has read the header itself, so take the metadata from it
       * rather than parsing the same bytes twice. */
      const i = this._player.info();
      this._info = {
        maxSubsong: Math.max(0, i.songs - 1),
        songName: i.name,
        songAuthor: i.author,
        songReleased: i.released,
        numSids: nsids,
        /* A page showing what is loaded wants to say so: a program has no
         * subtunes and no header of its own, and looks broken labelled as a
         * tune with one song. */
        isPrg: !sid,
        /* Which song is actually playing.
         *
         * Subtune 0 means "the file's own default", and for plenty of tunes
         * that is not song 1: Mechanicus starts at song 3 of 18. The player
         * honours it, so the number has to be read back rather than assumed,
         * or the page says "Tune 1/18" while song 3 plays and picks the wrong
         * song length to go with it. */
        song: i.song,
      };

      this._subtune = subtune || 0;

      /* A new tune starts from an unwritten chip. Without this a host reading
       * the shadow shows the previous tune's registers for every one this tune
       * has not got round to setting yet, which reads as a chip half programmed
       * by two different tunes. */
      this._shadow.fill(0xff);
      this._seen.fill(0);
      this._dirty.fill(0);
      this._anyDirty = false;

      if (this._isAudio) {
        /* Started here and not in _ensure(): the synthesis has to be
         * configured for this tune's clock, chip count and model, all of which
         * are only known once it is loaded. */
        if (!this._audio) {
          this._audio = new UsPlayerAudio(this._player);
          /* Say when a silent lead-in was run through, and only when there was
           * one: every tune reports the end of a skip, most of them having
           * skipped a single frame. */
          this._audio.onSkipEnd = (why, frames) => {
            const secs = frames / (this._player.refreshHz() || 50);
            if (secs >= 1) {
              this._log(`ran through ${secs.toFixed(1)}s of silent loader (${why})`);
            }
          };
        }
        /* Whatever the host asked for before there was a graph to ask. */
        if (this._volume !== undefined) this._audio.setVolume(this._volume);
        const started = await this._audio.start({
          chips: nsids,
          quality: 1,
          model: sid ? sidModel(bytes) : 0,
        });
        if (!started) throw new Error('reSIDfp would not take this rate');

        /* The emulation goes in a worker and the ring is handed to it, so the
         * main thread is out of the audio path entirely. The player on this
         * thread stays loaded for its metadata and for the Songlengths lookup,
         * both of which the host calls synchronously, but it is never started
         * and never steps. */
        const inWorker = await this._startWorker();
        if (inWorker) {
          if (!this._audio.handedOver) {
            const port = this._audio.handOver();
            if (port) await this._call('clock', { port }, [port]);
            /* Hidden and visible want different ring depths, and the worker is
             * the side that fills it, so the change has to reach it. */
            this._audio.onTargetChange = (target, steps) => {
              if (this._worker) this._call('audioTarget', { target, steps });
            };
          }
          const copy = bytes.slice();
          await this._call('loadSID',
                           { bytes: copy.buffer, subtune: subtune || 0 },
                           [copy.buffer]);
          await this._call('audioConfigure', {
            chips: nsids,
            rate: this._audio.ctx.sampleRate | 0,
            quality: 1,
            model: sid ? sidModel(bytes) : 0,
            target: this._audio._target,
            steps: this._audio._maxSteps || 24,
          });
          await this._call('start', {});
        } else {
          /* The main thread does it, as it did before there was a worker. */
          await this._player.start({ externalClock: true });
          this._audio.run(this._player);
        }
        this._log(`software audio${inWorker ? ' in a worker' : ''}: ` +
                  `${this._audio.ctx.sampleRate} Hz, ` +
                  `${nsids} chip${nsids === 1 ? '' : 's'}, ` +
                  `${sid && sidModel(bytes) ? '8580' : '6581'}, sinc`);
      } else if (this._isSendsid) {
        /* The board plays it, not us. The local player above has still read
         * the file, because the host asks for the title, the song count and
         * the Songlengths key synchronously, but it is never started and never
         * steps: `loadSID()` only primes the emulation, and what clocks it in
         * the other modes is the `start()` in the branch below.
         *
         * `uploadSIDFile()` is the whole sequence: stop, START, the file in 62
         * byte pieces, END, SIZE, pick the subtune, start. It counts songs from
         * one, the same as this method's argument. */
        if (!this._transport || !this._transport.isOpen) {
          throw new Error('no board on the serial port to send it to');
        }
        const sent = await this._transport.uploadSIDFile(bytes, subtune || 1);
        if (!sent) {
          throw new Error(this._transport.lastError || 'the board would not take the file');
        }
        this._boardStart = _now();
        this._boardPlayed = 0;
        this._boardMs = null;
        /* Tell the board how long this subtune runs, so it stops there rather
         * than at its own five minute default. The lengths come from the same
         * database the host uses, and the local player has already read the file,
         * so this is available without asking anyone. */
        await this._sendBoardPlaytime(subtune || 1);
        this._log(`sent to the board: ${bytes.length} bytes, ` +
                  `song ${subtune || 1} of ${i.songs}, playing on its own player`);
      } else {
        await this._player.start();
      }
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
      /* The board's own transport. Nothing here is playing, so there is
       * nothing here to resume. */
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
      /* Bank what has played: the clock below only measures the running part. */
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
      /* Back to zero rather than holding the last reading: a stop means the
       * transport should read 0:00, and the board's own figure freezes instead of
       * clearing, so leaving it set would show the position it stopped at. */
      this._boardMs = null;
      this._paused = false;
      this._lastStatus = '';
      this._log('stopped on the board');
      return;
    }
    /* Let go of the player before stopping it, so nothing is stepped after the
     * tune has been torn down, and drop what was rendered but not yet played:
     * otherwise the last fifth of a second is heard after the stop. */
    /* flush, not discard: discard only drops what the page has rendered and
     * not yet posted, so the worklet's own ring played on and the last fifth
     * of a second was still heard after the stop. */
    if (this._audio) {
      this._audio.unrun();
      if (typeof this._audio.flush === 'function') this._audio.flush();
      else this._audio.discard();
    }
    if (this._isAudio && this._worker) {
      this._call('stop', {});
      this._call('audioDiscard', {});
      this._snap = null;
    }
    if (this._player) this._player.stop();
    this._paused = false;
    this._lastStatus = '';
    this._log('stopped');
  }

  /**
   * How loud, 0 to 1.
   *
   * Software audio only, where there is a gain stage after the synthesis. A
   * board plays at whatever its own output is set to and a tune owns $d418, so
   * there is nothing here that could turn it down without changing the tune.
   * Returns whether it was applied, so a host can grey a slider that would do
   * nothing rather than offer one that lies.
   *
   * @param {number} value 0 for silence, 1 for full
   * @returns {boolean} true when this mode has a volume to set
   */
  setVolume(value) {
    this._volume = value;
    if (!this._isAudio) return false;
    /* Before load() there is no graph yet, so it is kept and applied in
     * start(): a host restoring a stored volume does it at startup. */
    if (this._audio && typeof this._audio.setVolume === 'function') {
      this._audio.setVolume(value);
    }
    return true;
  }

  /** Can this mode's loudness be changed at all? */
  hasVolume() { return this._isAudio; }

  /**
   * The MIDI outputs ASID mode could play to, `[{ id, name }]`.
   *
   * For a host that wants to offer its own picker. `_wireMidiPicker()` is the
   * other arrangement: it follows a list the host already owns, by the name
   * shown in it. Empty in every mode but ASID, and before Web MIDI has been
   * granted.
   */
  midiOutputs() {
    const t = this._transport;
    if (!t || typeof t.outputs !== 'function') return [];
    try { return t.outputs(); } catch (_) { return []; }
  }

  /**
   * Play ASID to the output with this name.
   *
   * By name and not by id, because a host's own list is usually built from
   * names and because an id changes between visits in some browsers.
   *
   * @param {string} name as it appears in `midiOutputs()`
   * @returns {boolean} true when there is an output with that name
   */
  selectMidiOutput(name) {
    const t = this._transport;
    if (!t || typeof t.selectOutputByName !== 'function') return false;
    try { return !!t.selectOutputByName(name); } catch (_) { return false; }
  }

  getSongInfo() { return this._info; }

  /**
   * What is driving the tune and how it was started, for a page that wants to
   * show it. Null when the player cannot say, which is any older copy.
   */
  timing() {
    if (this._isAudio && this._worker && this._snap) return this._snap.timing;
    if (!this._player || typeof this._player.timing !== 'function') return null;
    try { return this._player.timing(); } catch (_) { return null; }
  }

  /**
   * Emulated play time in milliseconds, or null when there is nothing loaded.
   *
   * Every mode this adapter drives runs the same emulation in the page, so all
   * of them can answer. The one mode that cannot is SendSID, which is not this
   * adapter: there the tune is playing on the board and the page is not
   * emulating anything, so its transport shows a dash until the firmware can
   * report the figure back.
   */
  playtimeMs() {
    /* The worker is what is playing in audio mode, so it is the only thing that
     * knows where the tune has got to. `_snap` is its last report, three times
     * a second, which is more than a clock showing seconds needs. */
    if (this._isAudio && this._worker && this._snap) return this._snap.playtimeMs || 0;
    /* Nothing is emulated here in SendSID mode and the board reports no
     * position, so the wall clock is the only answer available. It is honest
     * about what it is: time since the file was sent, less any pause. The board
     * plays from its own crystal, so this drifts against the tune by whatever
     * the two clocks disagree by, which is far below what a display showing
     * seconds can show. What it cannot know is the board finishing early. */
    if (this._isSendsid) {
      /* The board reports its own position now (SID_PLAYER_TIME), which is the
       * player's real clock rather than an estimate, and unlike the wall clock it
       * knows when the board finished early. `_boardMs` is what the poll below
       * last read; the wall clock stays as the fallback for a board or a firmware
       * that will not answer. */
      if (this._boardMs != null) return this._boardMs;
      if (this._boardStart === 0) return 0;
      return this._paused ? this._boardPlayed
                          : this._boardPlayed + (_now() - this._boardStart);
    }
    if (!this._player || typeof this._player.playtimeMs !== 'function') return null;
    try { return this._player.playtimeMs(); } catch (_) { return null; }
  }

  /**
   * The Songlengths key of the tune that is loaded: the MD5 of the whole file.
   *
   * For a tune that came from the served library the lengths are in
   * `sidfilelist.json` already and this is not needed. It is for everything
   * else: a local folder, an upload, a URL. Cheap, a few kilobytes of MD5, so
   * it is computed on demand rather than kept.
   *
   * @returns {string} 32 hex characters, or '' when nothing is loaded
   */
  md5() {
    if (!this._player || !this._bytes) return '';
    if (typeof this._player.md5 !== 'function') return '';
    try { return this._player.md5(this._bytes); } catch (_) { return ''; }
  }

  /**
   * Hand the Songlengths database to the player, once.
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
   * Every song's length in milliseconds for a key, or null when it is absent.
   *
   * @param {string} key from `md5()`
   */
  songLengths(key) {
    if (!this._player || typeof this._player.songLengths !== 'function') return null;
    try { return this._player.songLengths(key); } catch (_) { return null; }
  }

  /**
   * The last value written to a SID register, from the write shadow.
   *
   * A read and not a push, for a host that asks rather than one that is told:
   * `setHost({ registers })` is the push side and this is the pull side, and
   * both read the same shadow.
   *
   * It is a shadow of the writes because there is nothing else it could be. A
   * real chip over USB cannot be read back at the rate a display wants, ASID has
   * no read at all, and in software audio the synthesis lives behind the audio
   * thread. Every register a tune sets is write only on real hardware anyway, so
   * the last thing written *is* the state, with the two read only registers at
   * $1b and $1c (oscillator 3 and its envelope) the exception: nothing writes
   * them, so they answer 0.
   *
   * @param {number} chip 1 to 4
   * @param {number} reg  0 to 31, so $d400 relative
   * @returns {number} the byte, or 0 for a register nothing has written
   */
  readRegister(chip, reg) {
    const i = (((chip | 0) - 1) << 5) | (reg & 0x1f);
    if (i < 0 || i > 127) return 0;
    /* Software audio has no writes to watch, so ask the emulation instead, and
     * ask it now rather than reading what the poll last saw: a host redrawing a
     * piano every frame wants this frame's notes. See _pollRegisters(). */
    if (this._isAudio && !this._worker &&
        this._player && typeof this._player.sidRegister === 'function') {
      try { return this._player.sidRegister(chip | 0, reg & 0x1f) & 0xff; }
      catch (_) { /* fall through to the shadow */ }
    }
    const v = this._shadow[i];
    /* 0xff is the fill this starts as, meaning "never written". A page wants a
     * quiet chip to look quiet rather than to look like every bit is set. */
    return this._dirtyEver(i) ? v : 0;
  }

  /** Has anything ever been written to this shadow slot? */
  _dirtyEver(i) {
    if (!this._seen) return false;
    return this._seen[i] === 1;
  }

  /**
   * One byte of the emulated machine's RAM, or 0 when nothing is loaded.
   *
   * @param {number} address 0 to 65535
   */
  readMemory(address) {
    if (!this._player || typeof this._player.readMemory !== 'function') return 0;
    try { return this._player.readMemory(address); } catch (_) { return 0; }
  }

  /**
   * A CIA timer's latch, which is how often a CIA driven tune is called.
   *
   * @param {number} cia   1 or 2
   * @param {number} timer 0 for A, 1 for B
   */
  ciaLatch(cia = 1, timer = 0) {
    if (!this._player || typeof this._player.ciaLatch !== 'function') return 0;
    try { return this._player.ciaLatch(cia, timer); } catch (_) { return 0; }
  }

  /**
   * Hold one voice silent, or let it play again.
   *
   * @param {number} chip  1 to 4
   * @param {number} voice 1 to 3
   * @param {boolean} muted
   */
  setVoiceMute(chip, voice, muted) {
    /* In SendSID mode the board is playing, not the local emulation. The local
     * player still exists here because it read the file for the title and the
     * song count, so muting it would succeed and change nothing audible. The
     * board takes SID_PLAYER_MUTE, so send it there and the host's existing voice
     * controls work on the board without the host knowing anything about it. */
    if (this._isSendsid) {
      const t = this._transport;
      if (t && typeof t.playerMute === 'function') {
        /* Not awaited: the host calls this from a click handler and does not want
         * a USB round trip in the way. A failure is the transport's to report. */
        t.playerMute(chip, voice, muted);
      }
      return;
    }
    if (!this._player || typeof this._player.setVoiceMute !== 'function') return;
    this._player.setVoiceMute(chip, voice, muted);
    /* The worker is what is playing in audio mode, so it needs telling too. */
    if (this._isAudio && this._worker) {
      this._call('voiceMute', { chip, voice, muted: !!muted });
    }
  }

  /**
   * Hold a whole chip silent.
   *
   * The counterpart of setVoiceMute() and the same shape: the board's own command
   * in SendSID mode, where the board is playing and the local player is not, and
   * the emulation everywhere else. The worker is told as well when it is the thing
   * sounding, exactly as for a voice.
   *
   * Not three voice mutes. A voice mute masks the gate and the sustain and lets
   * every other write through; a chip mute drops the writes, which is the only one
   * of the two that reaches $18, so it is the only one that silences a tune playing
   * samples through the volume register.
   *
   * @param {number} chip 1 to 4
   * @param {boolean} muted
   */
  setChipMute(chip, muted) {
    if (this._isSendsid) {
      const t = this._transport;
      if (t && typeof t.playerMuteChip === 'function') {
        /* Not awaited: the host calls this from a click and does not want a USB
         * round trip in the way. */
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

  /** The muted chips as a bitmask, bit 0 being chip one, or 0 when unknown. */
  chipMute() {
    if (!this._player || typeof this._player.chipMute !== 'function') return 0;
    try { return this._player.chipMute(); } catch (_) { return 0; }
  }

  /** The mute bits of one chip, bit 0 being voice 1, or 0 when unknown. */
  voiceMute(chip = 1) {
    if (!this._player || typeof this._player.voiceMute !== 'function') return 0;
    try { return this._player.voiceMute(chip); } catch (_) { return 0; }
  }

  /** The header of what is loaded, or null. For a host that wants to parse it. */
  bytes() { return this._bytes; }

  /**
   * Is the browser holding the audio context suspended?
   *
   * Only software audio has a context to hold, so every other mode says no. A
   * host that dims its display while the sound is stopped needs to tell the
   * difference between a tune that is paused and a page the browser has not let
   * make a sound yet.
   *
   * In audio mode a context that does not exist yet counts as suspended. The
   * context is opened by the first load, on purpose, because it needs the tune's
   * rate and because one opened outside a user gesture stays suspended for ever.
   * So a host asking before any tune has played is asking whether it may start
   * one without a click, and the answer then is no.
   */
  isSuspended() {
    const ctx = this._audio && this._audio.ctx;
    if (!ctx) return this._isAudio;
    return ctx.state === 'suspended';
  }

  /* extras the app may not call, but which cost nothing to offer */
  /**
   * Run faster than the tune asks for.
   *
   * @param {boolean} on
   * @param {number} [mult] how much faster, the player's own default otherwise
   */
  fastForward(on, mult) {
    if (this._player) this._player.fastForward(on, mult);
  }

  /**
   * Play at a multiple of the tune's own speed. 1 is normal.
   *
   * Not the same as `fastForward()`, which is a seek: that one also stops the
   * writes reaching the board, because the point of it is to arrive somewhere
   * rather than to hear the way there. This one leaves the output alone, so on a
   * board it is audibly faster. Software audio cannot render ahead of a ring
   * that plays at one times speed, so there the extra frames are emulated and
   * their audio dropped, which comes out as a fast silent seek.
   *
   * @param {number} mult 0.1 to 8
   */
  setSpeed(mult) { if (this._player) this._player.setSpeed(mult); }

  /**
   * The board's mono/stereo audio switch, v1.3+ PCBs.
   *
   * Only the board transports carry it: ASID has no such concept and a software
   * receiver has its own mixing. Returns false when the active transport cannot
   * do it, so a caller can grey the control rather than pretend.
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

  /** Can the active transport switch the board between mono and stereo? */
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

if (typeof window !== 'undefined') {
  window.USPlayerAdapter = USPlayerAdapter;
}
