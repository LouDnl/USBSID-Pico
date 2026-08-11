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

/* Where the host page put the two build artefacts. */
const WASM_DIR = 'usplayer/';

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
function getModule() {
  if (!_modulePromise) {
    _modulePromise = window.USBSIDPlayer({ locateFile: (p) => WASM_DIR + p });
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
    /* The register grid's shadow copy: see the onWrite comment in _ensure(). */
    this._shadow = new Uint8Array(128).fill(0xff);
    this._dirty = new Uint8Array(128);
    this._anyDirty = false;
    this._tick = 0;
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
    if (typeof document === 'undefined') return;
    const el = document.getElementById('status-text');
    if (!el) return;

    const p = this._player;
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
      line += ` | ${a.msPerFrame.toFixed(1)}/20 ms per frame` +
              ` | buffer ${a.queuedMs} ms`;
      if (a.starvedMs) line += ` | STARVED ${a.starvedMs} ms`;
      if (a.clipped > 0) line += ` | CLIPPED ${a.clipped}`;
      if (line !== this._lastStatus) { this._lastStatus = line; el.textContent = line; }
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

    if (line !== this._lastStatus) {
      this._lastStatus = line;
      el.textContent = line;
    }
  }

  /** Push the registers that changed since last time, and nothing else. */
  _flushRegisters() {
    if (!this._anyDirty) return;
    this._anyDirty = false;
    const fn = (typeof window !== 'undefined') ? window.updateSIDReg : null;
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
    setTimeout(() => {
      const el = (typeof document !== 'undefined')
        ? document.getElementById('status-text') : null;
      this._prefix = (el && el.textContent) ? el.textContent : 'Playing';
    }, 250);
    /* One timer for both: the grid at twenty a second, which is faster than
     * the eye, and the status line at one. */
    this._tick = 0;
    this._reportTimer = setInterval(() => {
      try {
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
      } else if (this._isAsid) {
        this._transport = new ASIDMIDITransport();
        try {
          await this._transport.connect(null);
          this._wireMidiPicker();
        } catch (e) { console.warn('ASID connect:', e); }
      } else if (this._isSerial) {
        this._transport = new USBSIDWebSerialTransport();
        try {
          const ok = await this._transport.connect();
          if (!ok) this._log(this._transport.lastError || 'could not open a port');
        } catch (e) { this._log('Web Serial connect: ' + e.message); }
      } else {
        this._transport = new USBSIDWebUSBTransport({ device: this._device });
        try { await this._transport.connect(); } catch (_) {}
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
    return this.isConnected();
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
    try {
      const bytes = await this._fetch(url);
      this._bytes = bytes;

      const sid = isSidHeader(bytes) && !/\.(prg|p00)(\?|$)/i.test(url);
      const nsids = sid ? countSids(bytes) : 1;
      /* ASID only emits the chips it is told about, and the app's register
       * grid only shows those it is told about. */
      if (this._transport && 'nosids' in this._transport) this._transport.nosids = nsids;
      if (typeof window.updateRegGridSIDCount === 'function') {
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
      };

      this._subtune = subtune || 0;

      if (this._isAudio) {
        /* Started here and not in _ensure(): the synthesis has to be
         * configured for this tune's clock, chip count and model, all of which
         * are only known once it is loaded. */
        if (!this._audio) this._audio = new UsPlayerAudio(this._player);
        const started = await this._audio.start({
          chips: nsids,
          quality: 1,
          model: sid ? sidModel(bytes) : 0,
        });
        if (!started) throw new Error('reSIDfp would not take this rate');
        /* The ring is the clock in this mode, so the player's own wall clock
         * pump is not started: two clocks would fight, and the wall clock one
         * drops frames when it falls behind, which here is a hole in the audio
         * rather than a frame played late. See UsPlayerAudio.run(). */
        await this._player.start({ externalClock: true });
        this._audio.run(this._player);
        this._log(`software audio: ${this._audio.ctx.sampleRate} Hz, ` +
                  `${nsids} chip${nsids === 1 ? '' : 's'}, ` +
                  `${sid && sidModel(bytes) ? '8580' : '6581'}, sinc`);
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

      if (typeof callback === 'function') callback();
    } catch (e) {
      this._log('load failed: ' + (e && e.message ? e.message : e));
      console.error('USPlayerAdapter.load:', e);
    }
  }

  play() {
    if (!this._player) return;
    if (this._paused) { this._player.pause(false); this._paused = false; }
  }

  pause() {
    if (!this._player) return;
    this._player.pause(true);
    this._paused = true;
    this._log('paused');
  }

  stop() {
    this._stopReporting();
    /* Let go of the player before stopping it, so nothing is stepped after the
     * tune has been torn down, and drop what was rendered but not yet played:
     * otherwise the last fifth of a second is heard after the stop. */
    if (this._audio) { this._audio.unrun(); this._audio.discard(); }
    if (this._player) this._player.stop();
    this._paused = false;
    this._lastStatus = '';
    this._log('stopped');
  }

  setVolume(_v) { /* pause() and stop() do the silencing; nothing to set */ }

  getSongInfo() { return this._info; }

  /* extras the app may not call, but which cost nothing to offer */
  fastForward(on) { if (this._player) this._player.fastForward(on); }

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
