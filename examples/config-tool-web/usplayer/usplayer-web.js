/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * usplayer-web.js
 * The glue: the WASM module on one side, a transport on the other.
 *
 * Once per tick it steps the emulation by the C64 frames that wall clock time
 * has passed, drains the SID write ring from the WASM heap and hands the
 * (register, value, cycles) triples to a transport. The gaps are the same
 * cycle exact gaps the command line player sends over libusb, WebUSB is cycle
 * exact too. ASID is not and ignores them.
 *
 * Pacing is against performance.now(), speed is right on a 144 Hz monitor as
 * well as on a 60 and when the audio thread is the tick source. Pacing per
 * animation frame plays a PAL tune at 2.9x on 144 Hz.
 *
 * DOM free: runs on the main thread, in a worker or under node with a
 * capture transport.
 *
 * A transport implements:
 *   writeCycled(reg, val, cycles)  one cycle exact register write
 *   flush()                        frame boundary: push what is queued
 * and may implement:
 *   resetSID() / reset()           silence the chips
 *   mute() / unmute()              pause and resume
 *   setClock(id)                   0 default, 1 PAL, 2 NTSC, 3 DREAN
 *   playbackStart()                enter the transport's play mode (ASID)
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

/**
 * Max SID chips the wasm emulation core wires up (SidConfig::base in
 * mos6581_8580.h), matches kMaxSids there and in sidfile.h. Real board
 * backends stay at 4 chips, the reSIDfp software audio backend synthesises all.
 */
export const MAX_SIDS = 15;

/**
 * Check if bytes are a .sid file (PSID or RSID)
 *
 * @param {Uint8Array} bytes
 */
export function isSidHeader(bytes) {
  if (!bytes || bytes.length < 4) return false;
  const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
  return magic === 'PSID' || magic === 'RSID';
}

/**
 * Count the SID chips a .sid header asks for, 1 to MAX_SIDS
 *
 * Chips two and three live at $7a and $7b, only from header version 3 and 4
 * (a v2 file has something else there). Version 5 redefines both: the low
 * nibble of $7a is the total chip count (multiSidConfig), up to 15 = MAX_SIDS.
 * Which addresses are valid is sidfile.cpp's job after loading, this is a
 * quick pre-parse for UI and transport setup.
 *
 * Lives in this file because more than one frontend needs it. ASID only emits
 * the chips it is told about, a transport left at one chip plays a three SID
 * tune as one.
 *
 * @param {Uint8Array} bytes
 */
export function countSids(bytes) {
  if (!bytes || bytes.length < 0x7c) return 1;
  const version = (bytes[0x04] << 8) | bytes[0x05];
  if (version >= 5) {
    const total = bytes[0x7a] & 0x0f;
    return Math.min(MAX_SIDS, Math.max(1, total || 1));
  }
  let n = 1;
  if (version >= 3 && bytes[0x7a] !== 0) n++;
  if (version >= 4 && bytes[0x7b] !== 0) n++;
  return n;
}

/**
 * Get the chip a tune was written for (0 for a 6581, 1 for an 8580), from the header flags word
 *
 * Bits 4 and 5 of the big endian word at $76: 0 unknown, 1 is a 6581, 2 is an
 * 8580, 3 is either. Unknown and either give 6581, same as the command line
 * player.
 *
 * Only meaningful for software audio, a board plays whatever chip is fitted.
 *
 * @param {Uint8Array} bytes
 */
export function sidModel(bytes) {
  if (!bytes || bytes.length < 0x78) return 0;
  const version = (bytes[0x04] << 8) | bytes[0x05];
  if (version < 2) return 0;   /* v1 has no flags word */
  const flags = (bytes[0x76] << 8) | bytes[0x77];
  return (((flags >> 4) & 0x03) === 2) ? 1 : 0;
}

/**
 * Whether the tune header asks for a 6581, for the ASID SID type message (0x32)
 *
 * Bits 4 and 5 of the big endian word at $76 equal to 1. Unknown, either and a
 * header older than v2 (no flags word) are not a 6581, as the command line player
 * (info.sid_model == SidModel::Mos6581). Differs from sidModel(), which falls back to a 6581.
 *
 * @param {Uint8Array} bytes
 */
export function headerIs6581(bytes) {
  if (!bytes || bytes.length < 0x78) return false;
  const version = (bytes[0x04] << 8) | bytes[0x05];
  if (version < 2) return false;
  const flags = (bytes[0x76] << 8) | bytes[0x77];
  return ((flags >> 4) & 0x03) === 1;
}

/** Discards everything, the default for a player without a device */
export class NullTransport {
  writeCycled(_reg, _val, _cycles) {}
  flush() {}
  reset() {}
}

/** Records what it is given */
export class CaptureTransport {
  constructor() { this.writes = []; this.flushes = 0; this.resets = 0; }
  writeCycled(reg, val, cycles) { this.writes.push([reg, val, cycles]); }
  flush() { this.flushes++; }
  resetSID() { this.resets++; }
  reset() { this.writes.length = 0; this.flushes = 0; }
}

export class USBSIDPlayerWeb {
  /**
   * @param {object} module    an instantiated Emscripten module (USBSIDPlayer())
   * @param {object} transport something implementing writeCycled/flush
   */
  constructor(module, transport = new NullTransport()) {
    this.M = module;
    this.transport = transport;
    this._running = false;
    this._rafId = 0;
    this._audio = null;      // { ctx, node } while the AudioWorklet clock runs
    this._tunePtr = 0;
    this._asidTune = null;   // { is6581 } of the loaded SID tune, null for a program
    this._lastFlush = 0;
    this._lastReset = 0;
    this._hz = 50.125;       // frames a second, replaced on load
    this._acc = 0;           // wall clock accumulator, ms
    this._lastT = 0;
    this._speed = 1;         // fast forward multiplier
    this._seeking = false;   // true while fast forwarding: step, send nothing
    this._paused = false;
    this.resetStats();
    this._boardConfig = null;
    /* The Songlengths database, see loadSonglengths(). Stays in the heap,
     * every lookup walks it */
    this._slPtr = 0;
    this._slLen = 0;

    const M = module;
    /* control */
    this._alloc          = M.cwrap('usp_alloc', 'number', ['number']);
    this._freeBuf        = M.cwrap('usp_free', null, ['number']);
    this._loadSidtune    = M.cwrap('usp_load_sidtune', 'number', ['number', 'number', 'number']);
    this._loadPrg        = M.cwrap('usp_load_prg', 'number', ['number', 'number']);
    this._initSidplayer  = M.cwrap('usp_init_sidplayer', null, []);
    this._start          = M.cwrap('usp_start', null, []);
    this._step           = M.cwrap('usp_step', null, []);
    this._stop           = M.cwrap('usp_stop', null, []);
    this._nextSubtune    = M.cwrap('usp_next_subtune', null, []);
    this._prevSubtune    = M.cwrap('usp_prev_subtune', null, []);
    this._pause          = M.cwrap('usp_pause', null, ['number']);
    this._runStop        = M.cwrap('usp_key_runstop', 'number', []);
    this._forceSocketTwo = M.cwrap('usp_force_socket_two', null, []);
    this._setSidConfig   = M.cwrap('usp_set_sid_config', null,
                                   ['number', 'number', 'number', 'number']);
    /* state */
    this._isPlaying   = M.cwrap('usp_is_playing', 'number', []);
    this._isPrg       = M.cwrap('usp_is_prg', 'number', []);
    this._clockId     = M.cwrap('usp_clock_id', 'number', []);
    this._refreshHz   = M.cwrap('usp_refresh_hz', 'number', []);
    /* Older wasm may lack it, the ASID environment message is then skipped */
    this._clockHz     = (typeof M._usp_clock_hz === 'function')
                      ? M.cwrap('usp_clock_hz', 'number', []) : null;
    this._song        = M.cwrap('usp_song', 'number', []);
    this._songs       = M.cwrap('usp_songs', 'number', []);
    this._frames      = M.cwrap('usp_frames', 'number', []);
    this._playtimeMs  = M.cwrap('usp_playtime_ms', 'number', []);
    this._irqSources  = M.cwrap('usp_irq_sources', 'number', []);
    this._startMode   = M.cwrap('usp_start_mode', 'number', []);
    this._driverAddr  = M.cwrap('usp_driver_address', 'number', []);
    this._songMd5     = M.cwrap('usp_song_md5', null, ['number', 'number', 'number']);
    this._songlenMs   = M.cwrap('usp_songlength_ms', 'number',
                                ['number', 'number', 'number', 'number']);
    this._songlenCount = M.cwrap('usp_songlength_count', 'number',
                                 ['number', 'number', 'number']);
    this._sidWrites   = M.cwrap('usp_sid_writes', 'number', []);
    this._tuneName    = M.cwrap('usp_tune_name', 'number', []);
    this._tuneAuthor  = M.cwrap('usp_tune_author', 'number', []);
    this._tuneReleased = M.cwrap('usp_tune_released', 'number', []);
    /* v5: multi-SID, panning, FM/OPL, embedded song lengths. Informational
     * only, see web_api.cpp */
    this._sidCount    = M.cwrap('usp_sid_count', 'number', []);
    this._sidAddr     = M.cwrap('usp_sid_addr', 'number', ['number']);
    this._sidPan      = M.cwrap('usp_sid_pan', 'number', ['number']);
    this._panLayout   = M.cwrap('usp_pan_layout', 'number', []);
    this._panMode     = M.cwrap('usp_pan_mode', 'number', []);
    this._hasFmOpl    = M.cwrap('usp_has_fm_opl', 'number', []);
    this._hasEmbeddedSonglengths = M.cwrap('usp_has_embedded_songlengths', 'number', []);
    this._embeddedSonglenMs = M.cwrap('usp_embedded_songlength_ms', 'number', ['number']);
    this._readMemory  = M.cwrap('usp_read_memory', 'number', ['number']);
    this._ciaLatch    = M.cwrap('usp_cia_latch', 'number', ['number', 'number']);
    this._sidRegister = M.cwrap('usp_sid_register', 'number', ['number', 'number']);
    this._setVoiceMute = M.cwrap('usp_set_voice_mute', null,
                                 ['number', 'number', 'number']);
    this._voiceMuteBits = M.cwrap('usp_voice_mute', 'number', ['number']);
    this._setChipMute = M.cwrap('usp_set_chip_mute', null, ['number', 'number']);
    this._chipMuteBits = M.cwrap('usp_chip_mute', 'number', []);
    /* the ring */
    this._ringPtr     = M.cwrap('usbsid_web_ring_ptr', 'number', []);
    this._ringEntries = M.cwrap('usbsid_web_ring_entries', 'number', []);
    this._ringHead    = M.cwrap('usbsid_web_ring_head', 'number', []);
    this._ringTail    = M.cwrap('usbsid_web_ring_tail', 'number', []);
    this._ringSetTail = M.cwrap('usbsid_web_ring_set_tail', null, ['number']);
    this._flushCount  = M.cwrap('usbsid_web_flush_count', 'number', []);
    this._dropCount   = M.cwrap('usbsid_web_drop_count', 'number', []);
    this._resetCount  = M.cwrap('usbsid_web_reset_count', 'number', []);

    /* Fixed for the module's lifetime, the ring is a static array. HEAPU8 is
     * re-created on memory growth, read the view every drain */
    this._ringBase = this._ringPtr();
    this._ringCap  = this._ringEntries();
  }

  /** Copy a file into the WASM heap, freeing whatever was there before */
  _stage(bytes) {
    if (this._tunePtr) { this._freeBuf(this._tunePtr); this._tunePtr = 0; }
    const ptr = this._alloc(bytes.length);
    this.M.HEAPU8.set(bytes, ptr);
    this._tunePtr = ptr;
    return ptr;
  }

  /**
   * Load a PSID or RSID (Uint8Array), subtune 0 is the file's default
   * Return falsy if the bytes are not a SID file, try loadPRG then
   */
  loadSID(bytes, subtune = 0) {
    if (this._running) this.stop();   // never leave a tune half playing
    const ptr = this._stage(bytes);
    const ok = this._loadSidtune(ptr, bytes.length, subtune);
    this._asidTune = ok ? { is6581: headerIs6581(bytes) } : null;
    if (ok) {
      this._initSidplayer();
      this._start();
      this._afterStart();
    }
    return ok;
  }

  /**
   * Load a PRG or P00 (Uint8Array)
   *
   * Slow: boots a machine to the BASIC prompt and types RUN, about two
   * seconds of emulated time on the calling thread. A tune starts in about
   * fourteen milliseconds.
   */
  loadPRG(bytes) {
    if (this._running) this.stop();
    const ptr = this._stage(bytes);
    const ok = this._loadPrg(ptr, bytes.length);
    this._asidTune = null;    // the command line sends no environment for a program
    if (ok) {
      this._start();
      this._afterStart();
    }
    return ok;
  }

  _afterStart() {
    this._lastFlush = this._flushCount();
    this._lastReset = this._resetCount();
    const hz = this._refreshHz();
    if (hz > 1) this._hz = hz;    // PAL 50.125, NTSC 59.83
    /* Match the device SID clock to the tune, else a PAL tune on a board left
     * on NTSC plays sharp and fast. The id is the firmware index */
    if (this.transport.setClock) this.transport.setClock(this._clockId());
  }

  /** One frame of emulation, then hand what it produced to the transport */
  stepAndDrain() {
    this._step();
    this._drain();
  }

  _drain() {
    const head = this._ringHead();
    let tail = this._ringTail();

    /* Seeking: step the emulation but send nothing. Needed for fast forward on
     * hardware, the writes carry cycle deltas and the board plays them in real
     * time: the queue grows, audio arrives seconds late and distorted and a
     * stop or mono/stereo command waits behind the backlog.
     * The CLI swaps in a NullSidBackend for this (main_cli.cpp, ff_null).
     * The chip keeps its registers while seeking, most tunes rewrite their
     * voices each frame. No export for the register mirror in this API yet */
    if (this._seeking) {
      if (tail !== head) this._ringSetTail(head >>> 0);
      return;
    }

    if (tail !== head) {
      const heap = this.M.HEAPU8;
      const base = this._ringBase;
      const cap = this._ringCap;
      for (; tail !== head; tail = (tail + 1) >>> 0) {
        const o = base + (tail % cap) * 4;
        this.transport.writeCycled(heap[o], heap[o + 1],
                                   (heap[o + 2] << 8) | heap[o + 3]);
      }
      this._ringSetTail(head >>> 0);
    }
    /* Player asked for silence (a stop), only the page can reach the device */
    const rc = this._resetCount();
    if (rc !== this._lastReset) {
      this._lastReset = rc;
      if (this.transport.resetSID) this.transport.resetSID();
      else if (this.transport.reset) this.transport.reset();
    }
    /* One device flush per frame boundary crossed */
    const fc = this._flushCount();
    if (fc !== this._lastFlush) {
      this._lastFlush = fc;
      this.transport.flush();
    }
  }

  /**
   * One tick of the driving clock: advance as many emulated frames as real
   * time has passed, scaled by the fast forward multiplier. Measured against
   * performance.now() to keep the tempo right at any tick rate.
   */
  _tick() {
    if (!this._running) return;
    const now = () => ((typeof performance !== 'undefined') ? performance.now() : Date.now());
    if (this._paused) {
      this._lastT = now();   // stops resuming from bursting through a backlog
      return;
    }
    const t = now();
    let dt = t - this._lastT;
    this._lastT = t;
    if (dt < 0) dt = 0;
    if (dt > 250) dt = 250;              // drop a long stall
    this._acc += dt;
    const period = (1000 / this._hz) / this._speed;
    /* Cap catching up at two frames. A write heavy frame is several hundred
     * cycled writes, a burst sits in the transport queue and board buffer and
     * cannot be taken back. The depth check below needs room to work */
    const cap = Math.max(2, Math.ceil(2 * this._speed));
    /* Past this the board is behind, another frame is backlog. Transports
     * without a queue (ASID, capture, null) say zero */
    const limit = (this.transport.queueLimit || 256) >> 1;
    let steps = 0;
    let blocked = false;
    while (this._acc >= period && steps < cap) {
      /* Stop stepping, do not zero _acc. That loses time and playback runs
       * slow while the queue is deep. The clamp below keeps one frame */
      if ((this.transport.queueDepth || 0) > limit) { blocked = true; break; }
      this.stepAndDrain();
      this._acc -= period;
      steps++;
    }
    if (this._acc > period) this._acc = period;

    this._record(t, dt, steps, blocked, period, now());
  }

  /**
   * Record what playback looked like, for comparing arrangements (bench.html)
   *
   * maxDrainGap matters most, the longest a frame's writes went unsent. The
   * board survives a late tick and starves on a long one, an average hides it.
   */
  _record(t, dt, steps, blocked, period, done) {
    const s = this._stats;
    s.ticks++;
    s.workMs += (done - t);
    if (blocked) s.blocked++;

    const depth = this.transport.queueDepth || 0;
    if (depth > s.maxQueue) s.maxQueue = depth;

    if (steps === 0) return;

    s.steps += steps;
    if (s.lastDrain !== 0) {
      const gap = t - s.lastDrain;
      if (gap > s.maxDrainGap) s.maxDrainGap = gap;
      s.gapSum += gap;
      s.gaps++;
      /* Late enough that the board ran out of what it was given */
      if (gap > period * 2) s.starved++;
    }
    s.lastDrain = t;
    if (dt > s.maxTickGap) s.maxTickGap = dt;
  }

  resetStats() {
    if (this.transport.resetUsbStats) this.transport.resetUsbStats();
    this._stats = {
      ticks: 0, steps: 0, gaps: 0, gapSum: 0, starved: 0, blocked: 0,
      maxDrainGap: 0, maxTickGap: 0, maxQueue: 0, workMs: 0, lastDrain: 0,
      since: (typeof performance !== 'undefined') ? performance.now() : Date.now(),
    };
  }

  stats() {
    const s = this._stats;
    const now = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    const secs = Math.max(0.001, (now - s.since) / 1000);
    return {
      seconds: secs,
      ticks: s.ticks,
      framesPerSecond: s.steps / secs,
      meanDrainGap: s.gaps ? (s.gapSum / s.gaps) : 0,
      maxDrainGap: s.maxDrainGap,
      maxTickGap: s.maxTickGap,
      starved: s.starved,
      blocked: s.blocked,
      maxQueue: s.maxQueue,
      workPercent: 100 * (s.workMs / (secs * 1000)),
      dropped: this.droppedWrites(),
      usb: this.transport.usbStats ? this.transport.usbStats() : null,
    };
  }

  /**
   * Start the pump on an AudioWorklet clock, it runs while the tab is in the
   * background (requestAnimationFrame is frozen there). Async because
   * resuming an AudioContext needs the user gesture.
   */
  /** One tick of the clock, whatever is providing it */
  tick() { this._tick(); }

  /**
   * @param {object} opts
   *   opts.externalClock  true when something else calls tick(), as the worker
   *                       does (no AudioContext or requestAnimationFrame there)
   */
  async start(opts = {}) {
    if (this._running) return;
    this._running = true;
    this._paused = false;
    this._acc = 0;
    this._lastT = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    if (this.transport.playbackStart) this.transport.playbackStart();
    this._sendAsidTune();

    if (opts.externalClock) return;

    if (typeof AudioContext !== 'undefined' || typeof webkitAudioContext !== 'undefined') {
      try { await this._startAudioClock(); return; }
      catch (e) { /* no Web Audio, or no gesture: fall through */ }
    }
    this._startRafClock();
  }

  /**
   * ASID environment (0x31) and SID type (0x32) of the loaded tune, after the play start
   *
   * Same order and bytes as main_cli.cpp: PAL is a SID clock under 1 MHz, speed 1x.
   * Only a SID tune, only a transport with the methods (ASID) and a wasm with the clock.
   */
  _sendAsidTune() {
    const t = this.transport;
    if (!this._asidTune || !this._clockHz) return;
    if (typeof t.sendEnvironment !== 'function' || typeof t.sendSidType !== 'function') return;
    t.sendEnvironment(this._clockHz() < 1000000, 1);
    t.sendSidType(this._asidTune.is6581);
  }

  _startRafClock() {
    const pump = () => {
      if (!this._running) return;
      this._tick();
      this._rafId = requestAnimationFrame(pump);
    };
    this._rafId = requestAnimationFrame(pump);
  }

  async _startAudioClock() {
    const AC = globalThis.AudioContext || globalThis.webkitAudioContext;
    const ctx = new AC();
    /* Inline processor, posts a message every render quantum (about 344 a second at 48 kHz) */
    const src = `
      class UspClock extends AudioWorkletProcessor {
        process() { this.port.postMessage(0); return true; }
      }
      registerProcessor('usp-clock', UspClock);`;
    const url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
    await ctx.audioWorklet.addModule(url);
    URL.revokeObjectURL(url);
    const node = new AudioWorkletNode(ctx, 'usp-clock');
    node.port.onmessage = () => this._tick();
    node.connect(ctx.destination);   // something has to pull it or it never runs
    /* resume() stays pending on a context the browser will not start, race a
     * timeout */
    await Promise.race([ctx.resume(), new Promise((r) => setTimeout(r, 2000))]);
    if (ctx.state !== 'running') {
      console.warn('[usplayer] the AudioContext is', ctx.state +
        '. The browser needs a click on the page before audio may start.');
    }
    this._audio = { ctx, node };
  }

  _stopAudioClock() {
    if (!this._audio) return;
    const { ctx, node } = this._audio;
    try { node.port.onmessage = null; node.disconnect(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
    this._audio = null;
  }

  /** Stop the pump, silence the device, tear the tune down */
  stop() {
    this._running = false;
    this._paused = false;
    if (this._rafId) { cancelAnimationFrame(this._rafId); this._rafId = 0; }
    this._stopAudioClock();
    /* Silence first, the device holds its last register state */
    if (this.transport.resetSID) this.transport.resetSID();
    else if (this.transport.reset) this.transport.reset();
    this._stop();
    this._lastReset = this._resetCount();
  }

  /** Pause, and mute the device to silence a sustained note */
  pause(on) {
    this._paused = !!on;
    this._pause(on ? 1 : 0);
    if (on && this.transport.mute) this.transport.mute();
    else if (!on && this.transport.unmute) this.transport.unmute();
  }
  get paused() { return this._paused; }

  /** The fast forward multiplier in force, read by whatever clocks the player */
  get speed() { return this._speed; }

  /** Playback speed, 1 is normal, 4 is four times as fast */
  setSpeed(mult) { this._speed = Math.max(0.1, Math.min(8, mult || 1)); }

  /**
   * Seek: run the emulation fast with nothing reaching the device, see _drain()
   * On release the accumulator is dropped, else the frames seeked past are
   * chased at 1x and played into the queue a second time.
   */
  fastForward(on, mult = 4) {
    this.setSpeed(on ? mult : 1);
    this._seeking = !!on;
    if (!on) {
      this._acc = 0;
      this._lastT = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    }
  }

  setClock(rateId) { if (this.transport.setClock) this.transport.setClock(rateId); }

  /**
   * Tell the emulation what the board carries
   *
   * fmopl is the chip answering $df40/$df50, one based, -1 for none. Without it
   * an FM/OPL tune plays its SID voices and none of its OPL. numsids is
   * accepted and ignored, the tune decides how many chips are decoded.
   */
  setSidConfig(numsids, socketOne, socketTwo, fmopl) {
    let fm = (fmopl === undefined || fmopl === null) ? -1 : (fmopl | 0);
    /* A transport that carries FM in its own message needs $df40/$df50
     * unclaimed (they arrive as $80/$90). ASID decodes FM from its 0x60 command,
     * FM sent as SID data in a 0x4E snapshot is not FM to any receiver. Forced
     * in this method, the board value describes its hardware only */
    if (this.transport && this.transport.fmAsOwnMessage) fm = -1;
    this._setSidConfig(numsids | 0, socketOne | 0, socketTwo | 0, fm);
    /* fmoplSid = board report, fmoplApplied = given to the emulation (differs
     * over ASID) */
    this._boardConfig = { sidsSocketOne: socketOne | 0,
                          sidsSocketTwo: socketTwo | 0,
                          fmoplSid: (fmopl === undefined) ? -1 : (fmopl | 0),
                          fmoplApplied: fm };
  }

  /**
   * Read the board configuration and apply it, like the command line player
   * does at connect
   *
   * Call once after the transport is open and before loading a tune, its init
   * writes go out under the config set at that moment.
   *
   * Return what was applied, or null when the transport cannot say (defaults
   * stay, set them by hand).
   */
  async applyBoardConfig() {
    if (!this.transport.readBoardConfig) return null;
    const cfg = await this.transport.readBoardConfig();
    if (!cfg) return null;
    this.setSidConfig(0, cfg.sidsSocketOne, cfg.sidsSocketTwo, cfg.fmoplSid);
    return cfg;
  }

  /** The last configuration applied, for a page that wants to show it */
  boardConfig() { return this._boardConfig || null; }
  nextSubtune() { this._nextSubtune(); }
  prevSubtune() { this._prevSubtune(); }
  /** RUN/STOP on the keyboard matrix, how a program is interrupted */
  runStop() { return !!this._runStop(); }
  forceSocketTwo() { this._forceSocketTwo(); }

  isPlaying() { return !!this._isPlaying(); }
  isPrg() { return !!this._isPrg(); }
  droppedWrites() { return this._dropCount(); }
  sidWrites() { return this._sidWrites(); }
  frames() { return this._frames(); }
  refreshHz() { return this._hz; }

  /**
   * Time into the tune in milliseconds, in emulated time
   *
   * Does not advance while paused, jumps on a seek and is the same whatever
   * the transport does.
   */
  playtimeMs() { return this._playtimeMs(); }

  /**
   * Read one byte of the emulated machine RAM
   *
   * Plain RAM, no banking and no side effects. An address under I/O gives what
   * is beneath the chip, reading a CIA interrupt register would acknowledge its
   * interrupts. See usp_read_memory() in src/host/web_api.cpp.
   *
   * @param {number} address 0 to 65535
   */
  readMemory(address) { return this._readMemory(address & 0xffff); }

  /**
   * Get the last value written to a SID register, from the emulation mirror
   *
   * Works in every mode, in software audio the writes never reach the page.
   * See usp_sid_register() in src/host/web_api.cpp.
   *
   * @param {number} chip 1 to 4
   * @param {number} reg  0 to 31
   */
  sidRegister(chip, reg) { return this._sidRegister(chip, reg); }

  /**
   * Get a CIA timer latch, how often a CIA driven tune is called
   *
   * A frame's cycles divided by this is the calls a frame: about 19654 is once,
   * half of it twice. timing() says which timer drives.
   *
   * @param {number} cia   1 or 2
   * @param {number} timer 0 for A, 1 for B
   */
  ciaLatch(cia = 1, timer = 0) { return this._ciaLatch(cia, timer); }

  /**
   * Hold one voice silent while the tune plays on
   *
   * Masked inside the emulation, same in software audio and on every transport.
   * The writes still happen, only the gate is held down.
   *
   * @param {number} chip  1 to 4
   * @param {number} voice 1 to 3
   * @param {boolean} muted
   */
  setVoiceMute(chip, voice, muted) {
    this._setVoiceMute(chip, voice, muted ? 1 : 0);
  }

  /** Mute bits of one chip, bit 0 is voice 1, chip counts from 1 */
  voiceMute(chip = 1) { return this._voiceMuteBits(chip); }

  /**
   * Hold a whole chip silent, dropping its writes
   *
   * A voice mute masks gate and sustain, other writes pass. A chip mute drops
   * the writes and is the only one reaching $18: a tune playing samples through
   * the volume register keeps sounding with voices muted and stops with its chip.
   *
   * @param {number} chip 1 to 4
   * @param {boolean} muted
   */
  setChipMute(chip, muted) {
    this._setChipMute(chip, muted ? 1 : 0);
  }

  /** The muted chips as a bitmask, bit 0 is chip one */
  chipMute() { return this._chipMuteBits(); }

  /**
   * Get what drives the tune and how it was started
   *
   * Read from the chips as they stand, true for the playing subtune. The init
   * routine decides if a CIA timer, raster compare or TOD alarm calls play,
   * and a later subtune can differ. Return {irq: string[], start, driver}.
   */
  timing() {
    const bits = this._irqSources();
    const irq = [];
    /* Same as the USP_IRQ_* defines in src/api/usplayer.h */
    if (bits & 0x01) irq.push('CIA1 TA');
    if (bits & 0x02) irq.push('CIA1 TB');
    if (bits & 0x04) irq.push('CIA1 TOD');
    if (bits & 0x08) irq.push('CIA2 TA');
    if (bits & 0x10) irq.push('CIA2 TB');
    if (bits & 0x20) irq.push('CIA2 TOD');
    if (bits & 0x40) irq.push('VIC raster');
    const START = ['PSIDdrv', 'BASIC', 'PRG'];
    return {
      irq,
      start: START[this._startMode()] || '?',
      driver: this._driverAddr(),
    };
  }

  /**
   * Get the Songlengths key for a tune: MD5 of the whole file, 32 hex characters
   *
   * The plain MD5 of every byte of the .sid (the PSID MD5 of older players
   * misses every entry in the database).
   *
   * Done in the wasm, a browser has no MD5 (WebCrypto leaves it out).
   *
   * Return empty string when it cannot be computed
   *
   * @param {Uint8Array} bytes the file, as loaded
   */
  md5(bytes) {
    if (!bytes || !bytes.length) return '';
    const inp = this._alloc(bytes.length);
    const out = this._alloc(33);
    if (!inp || !out) {
      if (inp) this._freeBuf(inp);
      if (out) this._freeBuf(out);
      return '';
    }
    try {
      this.M.HEAPU8.set(bytes, inp);
      this._songMd5(inp, bytes.length, out);
      return this.M.UTF8ToString(out);
    } finally {
      this._freeBuf(inp);
      this._freeBuf(out);
    }
  }

  /**
   * Hand over the Songlengths database once, kept in the heap
   *
   * Four to five megabytes of text walked by every lookup, re-uploading per
   * tune would cost more than emulation. releaseSonglengths() frees it.
   *
   * Return false when it cannot be allocated
   *
   * @param {string} text the file as fetched
   */
  loadSonglengths(text) {
    this.releaseSonglengths();
    if (!text) return false;
    /* Latin-1, the file is ASCII apart from the tune name comments and the
     * parser only looks at hex, '=' and digits. One byte per character gives
     * the length up front */
    const n = text.length;
    const p = this._alloc(n + 1);
    if (!p) return false;
    const heap = this.M.HEAPU8;
    for (let i = 0; i < n; i++) heap[p + i] = text.charCodeAt(i) & 0xff;
    heap[p + n] = 0;
    this._slPtr = p;
    this._slLen = n;
    return true;
  }

  releaseSonglengths() {
    if (this._slPtr) { this._freeBuf(this._slPtr); this._slPtr = 0; this._slLen = 0; }
  }

  get hasSonglengths() { return !!this._slPtr; }

  /**
   * Get every song length for one key, in milliseconds
   *
   * Return one entry per song, or null when the key is absent
   *
   * @param {string} key 32 hex characters from md5()
   */
  songLengths(key) {
    if (!this._slPtr || !key || key.length !== 32) return null;
    const kp = this._alloc(33);
    if (!kp) return null;
    try {
      for (let i = 0; i < 32; i++) this.M.HEAPU8[kp + i] = key.charCodeAt(i) & 0xff;
      this.M.HEAPU8[kp + 32] = 0;
      const count = this._songlenCount(this._slPtr, this._slLen, kp);
      if (count <= 0) return null;
      const out = [];
      /* Database counts songs from one, the array is zero based: out[0] is song 1 */
      for (let s = 1; s <= count; s++) {
        out.push(this._songlenMs(this._slPtr, this._slLen, kp, s));
      }
      return out;
    } finally {
      this._freeBuf(kp);
    }
  }

  /**
   * Get every song length from the v5 embedded table, or null without one
   * Same shape as songLengths(), a fallback when the database has nothing
   */
  embeddedSongLengths() {
    if (!this._hasEmbeddedSonglengths()) return null;
    const count = this._songs();
    if (count <= 0) return null;
    const out = [];
    for (let s = 1; s <= count; s++) out.push(this._embeddedSonglenMs(s));
    return out;
  }

  /** Title, author and release from the header, plus the v5 multi-SID/panning/FM-OPL
   * summary. Chip fields cover what this player wires up (MAX_SIDS at most).
   * numSidsRequested is the raw count the header asked for */
  info() {
    const str = (fn) => {
      const p = fn();
      return p ? this.M.UTF8ToString(p) : '';
    };
    const requested = this._sidCount();
    const wired = Math.min(MAX_SIDS, Math.max(1, requested));
    const sids = [];
    for (let i = 0; i < wired; i++) {
      sids.push({ addr: this._sidAddr(i), pan: ['L', 'C', 'R'][this._sidPan(i)] || 'C' });
    }
    return {
      name: str(this._tuneName),
      author: str(this._tuneAuthor),
      released: str(this._tuneReleased),
      song: this._song(),
      songs: this._songs(),
      isPrg: this.isPrg(),
      numSidsRequested: requested,
      sids,
      panLayout: ['standard', 'lcr', 'centerFirst', 'fullyCentered'][this._panLayout()] || 'standard',
      panMode: ['direct', 'reverse', 'group', 'spread'][this._panMode()] || 'direct',
      hasFmOpl: !!this._hasFmOpl(),
      hasEmbeddedSongLengths: !!this._hasEmbeddedSonglengths(),
    };
  }
}
