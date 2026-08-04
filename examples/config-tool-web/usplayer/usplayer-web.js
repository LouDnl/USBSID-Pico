/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usplayer-web.js
 * The glue: the WASM module on one side, a transport on the other.
 *
 * Once per tick it steps the emulation by however many C64 frames of wall
 * clock time have passed, drains the SID write ring out of the WASM heap, and
 * hands the (register, value, cycles) triples to a transport. The gaps are the
 * same cycle exact gaps the command line player sends over libusb, so the
 * WebUSB path is cycle exact too; ASID is not, by nature, and ignores them.
 *
 * Pacing is against `performance.now()` rather than against the tick source,
 * so playback runs at the right speed on a 144 Hz monitor as well as on a 60,
 * and keeps its speed when the tick source is the audio thread instead of the
 * display. That distinction is not academic: pacing per animation frame plays
 * a PAL tune at 2.9x on a 144 Hz display.
 *
 * DOM free, so it runs on the main thread, in a worker, or under node with a
 * capture transport, which is what the smoke test uses.
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

/** Discards everything. The default, so a player without a device is inert. */
export class NullTransport {
  writeCycled(_reg, _val, _cycles) {}
  flush() {}
  reset() {}
}

/** Records what it is given. What the smoke test measures. */
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
    this._lastFlush = 0;
    this._lastReset = 0;
    this._hz = 50.125;       // frames a second, replaced on load
    this._acc = 0;           // wall clock accumulator, ms
    this._lastT = 0;
    this._speed = 1;         // fast forward multiplier
    this._paused = false;
    this.resetStats();

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
    /* state */
    this._isPlaying   = M.cwrap('usp_is_playing', 'number', []);
    this._isPrg       = M.cwrap('usp_is_prg', 'number', []);
    this._clockId     = M.cwrap('usp_clock_id', 'number', []);
    this._refreshHz   = M.cwrap('usp_refresh_hz', 'number', []);
    this._song        = M.cwrap('usp_song', 'number', []);
    this._songs       = M.cwrap('usp_songs', 'number', []);
    this._frames      = M.cwrap('usp_frames', 'number', []);
    this._sidWrites   = M.cwrap('usp_sid_writes', 'number', []);
    this._tuneName    = M.cwrap('usp_tune_name', 'number', []);
    this._tuneAuthor  = M.cwrap('usp_tune_author', 'number', []);
    this._tuneReleased = M.cwrap('usp_tune_released', 'number', []);
    /* the ring */
    this._ringPtr     = M.cwrap('usbsid_web_ring_ptr', 'number', []);
    this._ringEntries = M.cwrap('usbsid_web_ring_entries', 'number', []);
    this._ringHead    = M.cwrap('usbsid_web_ring_head', 'number', []);
    this._ringTail    = M.cwrap('usbsid_web_ring_tail', 'number', []);
    this._ringSetTail = M.cwrap('usbsid_web_ring_set_tail', null, ['number']);
    this._flushCount  = M.cwrap('usbsid_web_flush_count', 'number', []);
    this._dropCount   = M.cwrap('usbsid_web_drop_count', 'number', []);
    this._resetCount  = M.cwrap('usbsid_web_reset_count', 'number', []);

    /* Fixed for the module's lifetime: the ring is a static array. Its address
     * survives memory growth because growth moves the heap's end, not its
     * start, but HEAPU8 itself is re-created, so read the view every drain. */
    this._ringBase = this._ringPtr();
    this._ringCap  = this._ringEntries();
  }

  /** Copy a file into the WASM heap, freeing whatever was there before. */
  _stage(bytes) {
    if (this._tunePtr) { this._freeBuf(this._tunePtr); this._tunePtr = 0; }
    const ptr = this._alloc(bytes.length);
    this.M.HEAPU8.set(bytes, ptr);
    this._tunePtr = ptr;
    return ptr;
  }

  /**
   * Load a PSID or RSID (Uint8Array). subtune 0 is the file's own default.
   * Returns falsy if the bytes are not a SID file, which is the page's cue to
   * try loadPRG instead.
   */
  loadSID(bytes, subtune = 0) {
    if (this._running) this.stop();   // never leave a tune half playing
    const ptr = this._stage(bytes);
    const ok = this._loadSidtune(ptr, bytes.length, subtune);
    if (ok) {
      this._initSidplayer();
      this._start();
      this._afterStart();
    }
    return ok;
  }

  /**
   * Load a PRG or P00 (Uint8Array).
   *
   * Slow, and unavoidably so: a program is started the way a person starts
   * one, by booting a machine to the BASIC prompt and typing RUN, which is
   * about two seconds of emulated time and runs on the calling thread. A tune
   * skips all of it and starts in about fourteen milliseconds.
   */
  loadPRG(bytes) {
    if (this._running) this.stop();
    const ptr = this._stage(bytes);
    const ok = this._loadPrg(ptr, bytes.length);
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
    /* Match the device's SID clock to the tune, or a PAL tune on a board left
     * on NTSC plays sharp and fast. The id is the firmware's own index, which
     * the player picked when it read the header. */
    if (this.transport.setClock) this.transport.setClock(this._clockId());
  }

  /** One frame of emulation, then hand everything it produced to the transport. */
  stepAndDrain() {
    this._step();
    this._drain();
  }

  _drain() {
    const head = this._ringHead();
    let tail = this._ringTail();
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
    /* The player asking for silence (a stop, from anywhere) reaches the device
     * through here, because only the page can talk to it. */
    const rc = this._resetCount();
    if (rc !== this._lastReset) {
      this._lastReset = rc;
      if (this.transport.resetSID) this.transport.resetSID();
      else if (this.transport.reset) this.transport.reset();
    }
    /* One device flush per frame boundary crossed. */
    const fc = this._flushCount();
    if (fc !== this._lastFlush) {
      this._lastFlush = fc;
      this.transport.flush();
    }
  }

  /**
   * One tick of whatever clock is driving us: advance as many emulated frames
   * as real time has passed, scaled by the fast forward multiplier. Measuring
   * against performance.now() rather than counting ticks is what keeps the
   * tempo right whatever the tick rate turns out to be.
   */
  _tick() {
    if (!this._running) return;
    const now = () => ((typeof performance !== 'undefined') ? performance.now() : Date.now());
    if (this._paused) {
      this._lastT = now();   // so resuming does not burst through a backlog
      return;
    }
    const t = now();
    let dt = t - this._lastT;
    this._lastT = t;
    if (dt < 0) dt = 0;
    if (dt > 250) dt = 250;              // a long stall is dropped, not chased
    this._acc += dt;
    const period = (1000 / this._hz) / this._speed;
    /* Two frames of catching up, not six. A frame of a write heavy tune is
     * several hundred cycled writes, and the only place a burst of them can go
     * is the transport's queue and then the board's own buffer. Neither can be
     * taken back, so an overshoot here is heard long after the tick that caused
     * it, and after a stop it is still playing. Two is enough to ride out a
     * missed tick and small enough that the depth check below has something
     * left to protect. */
    const cap = Math.max(2, Math.ceil(2 * this._speed));
    /* Past this the board is already behind, so another frame is pure backlog.
     * Transports that queue nothing (ASID, capture, null) say zero and never
     * trip it. */
    const limit = (this.transport.queueLimit || 256) >> 1;
    let steps = 0;
    let blocked = false;
    while (this._acc >= period && steps < cap) {
      /* Not `_acc = 0`: throwing the accumulator away here loses the time
       * outright and playback runs slow for as long as the queue is deep. Just
       * stop stepping. The clamp below keeps at most one frame of it. */
      if ((this.transport.queueDepth || 0) > limit) { blocked = true; break; }
      this.stepAndDrain();
      this._acc -= period;
      steps++;
    }
    if (this._acc > period) this._acc = period;

    this._record(t, dt, steps, blocked, period, now());
  }

  /**
   * What playback actually looked like, for comparing one arrangement against
   * another. See web/bench.html.
   *
   * The number that matters is `maxDrainGap`, the longest a frame's writes ever
   * went unsent, not the average anything: the board plays what it was given at
   * the pre delays it was given, so it survives a late tick and starves on a
   * long one. An average hides exactly the event that is audible.
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
      /* Late enough that the board has run out of what it was last given. */
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
   * Start the pump. Prefers an AudioWorklet, whose callback runs on the audio
   * thread and keeps firing while the tab is in the background, where
   * requestAnimationFrame is frozen and playback would otherwise stop dead.
   * Async because resuming an AudioContext needs the user gesture that got us
   * here.
   */
  /** One tick of the clock, whatever is providing it. */
  tick() { this._tick(); }

  /**
   * @param {object} opts
   *   opts.externalClock  true when something else is calling tick(), which is
   *                       what the worker does: the audio thread posts straight
   *                       to it and neither AudioContext nor
   *                       requestAnimationFrame exists there anyway.
   */
  async start(opts = {}) {
    if (this._running) return;
    this._running = true;
    this._paused = false;
    this._acc = 0;
    this._lastT = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    if (this.transport.playbackStart) this.transport.playbackStart();

    if (opts.externalClock) return;

    if (typeof AudioContext !== 'undefined' || typeof webkitAudioContext !== 'undefined') {
      try { await this._startAudioClock(); return; }
      catch (e) { /* no Web Audio, or no gesture: fall through */ }
    }
    this._startRafClock();
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
    /* An inline processor that produces no audio and posts a message every
     * render quantum, about 344 times a second at 48 kHz. */
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
    await ctx.resume();
    this._audio = { ctx, node };
  }

  _stopAudioClock() {
    if (!this._audio) return;
    const { ctx, node } = this._audio;
    try { node.port.onmessage = null; node.disconnect(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
    this._audio = null;
  }

  /** Stop the pump, silence the device, tear the tune down. */
  stop() {
    this._running = false;
    this._paused = false;
    if (this._rafId) { cancelAnimationFrame(this._rafId); this._rafId = 0; }
    this._stopAudioClock();
    /* Silence first. The device holds the last register state it was given,
     * so a stop that only tears down the emulation leaves the voices ringing. */
    if (this.transport.resetSID) this.transport.resetSID();
    else if (this.transport.reset) this.transport.reset();
    this._stop();
    this._lastReset = this._resetCount();
  }

  /** Pause, and mute the device so a sustained note actually goes quiet. */
  pause(on) {
    this._paused = !!on;
    this._pause(on ? 1 : 0);
    if (on && this.transport.mute) this.transport.mute();
    else if (!on && this.transport.unmute) this.transport.unmute();
  }
  get paused() { return this._paused; }

  /** Playback speed. 1 is normal, 4 is four times as fast. */
  setSpeed(mult) { this._speed = Math.max(0.1, Math.min(8, mult || 1)); }
  fastForward(on, mult = 4) { this.setSpeed(on ? mult : 1); }

  setClock(rateId) { if (this.transport.setClock) this.transport.setClock(rateId); }
  nextSubtune() { this._nextSubtune(); }
  prevSubtune() { this._prevSubtune(); }
  /** RUN/STOP on the keyboard matrix, which is how a program is interrupted. */
  runStop() { return !!this._runStop(); }
  forceSocketTwo() { this._forceSocketTwo(); }

  isPlaying() { return !!this._isPlaying(); }
  isPrg() { return !!this._isPrg(); }
  droppedWrites() { return this._dropCount(); }
  sidWrites() { return this._sidWrites(); }
  frames() { return this._frames(); }
  refreshHz() { return this._hz; }

  /** Title, author and release, as the file's own header spells them. */
  info() {
    const str = (fn) => {
      const p = fn();
      return p ? this.M.UTF8ToString(p) : '';
    };
    return {
      name: str(this._tuneName),
      author: str(this._tuneAuthor),
      released: str(this._tuneReleased),
      song: this._song(),
      songs: this._songs(),
      isPrg: this.isPrg(),
    };
  }
}
