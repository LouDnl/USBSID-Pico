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

/**
 * Is this a .sid file rather than a program?
 *
 * @param {Uint8Array} bytes
 * @returns {boolean}
 */
export function isSidHeader(bytes) {
  if (!bytes || bytes.length < 4) return false;
  const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
  return magic === 'PSID' || magic === 'RSID';
}

/**
 * How many SID chips a .sid header asks for, 1 to 3.
 *
 * The second and third chips live at $7a and $7b and only exist from header
 * version 3 and 4, so the version has to be checked before the bytes are
 * believed: a v2 file has something else there.
 *
 * Lives here rather than in a frontend because more than one of them needs it
 * and they must agree. ASID in particular only emits the chips it is told
 * about, so a transport left at one chip plays a three SID tune as one.
 *
 * @param {Uint8Array} bytes
 * @returns {number} 1, 2 or 3
 */
export function countSids(bytes) {
  if (!bytes || bytes.length < 0x7c) return 1;
  const version = (bytes[0x04] << 8) | bytes[0x05];
  let n = 1;
  if (version >= 3 && bytes[0x7a] !== 0) n++;
  if (version >= 4 && bytes[0x7b] !== 0) n++;
  return n;
}

/**
 * Which chip a tune was written for, from the header's flags word.
 *
 * Bits 4 and 5 of the big endian word at $76: 0 unknown, 1 is a 6581, 2 is an
 * 8580, 3 is either. Unknown and either both fall to 6581, which is what a
 * player with no opinion should do and what the command line player does.
 *
 * Only meaningful for software audio, where the model decides which filter is
 * emulated and is plainly audible. A board plays whatever chip is fitted and
 * does not care what the file says.
 *
 * @param {Uint8Array} bytes
 * @returns {number} 0 for a 6581, 1 for an 8580
 */
export function sidModel(bytes) {
  if (!bytes || bytes.length < 0x78) return 0;
  const version = (bytes[0x04] << 8) | bytes[0x05];
  if (version < 2) return 0;   /* v1 has no flags word */
  const flags = (bytes[0x76] << 8) | bytes[0x77];
  return (((flags >> 4) & 0x03) === 2) ? 1 : 0;
}

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
    this._seeking = false;   // true while fast forwarding: step, send nothing
    this._paused = false;
    this.resetStats();
    this._boardConfig = null;
    /* The Songlengths database, once a page hands it over. See
     * loadSonglengths(): it stays in the heap because every lookup walks it. */
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

    /* Seeking: step the emulation but send nothing.
     *
     * This is not an optimisation, it is the only way fast forward can work
     * against hardware. The writes carry cycle deltas and the board honours
     * them, so four frames of emulation per real frame means four frames of
     * writes the board will take four frames to play. The queue grows without
     * bound, the audio arrives seconds late and distorted, and anything queued
     * behind it (a mono/stereo command, a stop) waits for the backlog. All three
     * were reported and all three are this.
     *
     * The command line player has always done it this way: `f` swaps in a
     * NullSidBackend for the duration (main_cli.cpp, `ff_null`). Same idea, one
     * level up, because the page is where the ring is drained.
     *
     * The chip keeps whatever registers it had while this runs. Almost every
     * tune rewrites its voices each frame, so it catches up immediately on
     * release. The CLI can do better, pushing the register file back on arrival,
     * because it can read the emulation's mirror; there is no export for that
     * here yet. */
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
    /* Not a bare await: resume() on a context the browser will not let start
     * stays pending rather than rejecting, which shows up as playback that
     * silently never begins. */
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

  /** The fast forward multiplier in force. Read by whatever is clocking us. */
  get speed() { return this._speed; }

  /** Playback speed. 1 is normal, 4 is four times as fast. */
  setSpeed(mult) { this._speed = Math.max(0.1, Math.min(8, mult || 1)); }

  /**
   * Seek: run the emulation fast with nothing reaching the device.
   *
   * Silent by design, see _drain(). On release the accumulator is dropped so the
   * frames that went by while seeking are not then chased at 1x, which would
   * play the seek a second time into the queue this exists to protect.
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
   * Tell the emulation what the board is carrying.
   *
   * `fmopl` is which chip answers $df40/$df50, one based, -1 for none. Without
   * it those writes reach nothing and an FM/OPL tune plays its SID voices and
   * none of its OPL. `numsids` is accepted and ignored, as everywhere else:
   * how many chips the emulation decodes is the tune's business.
   */
  setSidConfig(numsids, socketOne, socketTwo, fmopl) {
    let fm = (fmopl === undefined || fmopl === null) ? -1 : (fmopl | 0);
    /* A transport that carries FM in a message of its own needs $df40/$df50 to
     * stay unclaimed, so they arrive as $80/$90 instead of being folded into a
     * chip's registers. ASID is the case: it decodes FM from its 0x60 command,
     * and FM sent as SID data inside a 0x4E snapshot is not FM to any receiver.
     * Forced here rather than trusted to the caller, because the value comes
     * from a board read and the board is right about its own hardware and wrong
     * about this transport. */
    if (this.transport && this.transport.fmAsOwnMessage) fm = -1;
    this._setSidConfig(numsids | 0, socketOne | 0, socketTwo | 0, fm);
    /* `fmoplSid` is what the board says it has; `fmoplApplied` is what the
     * emulation was actually given, which differs over ASID. Reporting only the
     * first would have a page saying "FM/OPL on SID 2" while the player has it
     * unclaimed, which is true of the hardware and false of the playback. */
    this._boardConfig = { sidsSocketOne: socketOne | 0,
                          sidsSocketTwo: socketTwo | 0,
                          fmoplSid: (fmopl === undefined) ? -1 : (fmopl | 0),
                          fmoplApplied: fm };
  }

  /**
   * Read the board's own configuration and apply it.
   *
   * This is what the command line player does at connect, and the browser had
   * no equivalent, which is why FM/OPL tunes did not work in it. Call it once
   * after the transport is open and before loading anything: the tune's init
   * writes go out under whatever is set at that moment.
   *
   * Returns what was applied, or null when the transport cannot say, in which
   * case the emulation keeps its defaults and the caller can set them by hand.
   */
  async applyBoardConfig() {
    if (!this.transport.readBoardConfig) return null;
    const cfg = await this.transport.readBoardConfig();
    if (!cfg) return null;
    this.setSidConfig(0, cfg.sidsSocketOne, cfg.sidsSocketTwo, cfg.fmoplSid);
    return cfg;
  }

  /** The last configuration applied, for a page that wants to show it. */
  boardConfig() { return this._boardConfig || null; }
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

  /**
   * How far into the tune the emulation is, in milliseconds.
   *
   * Emulated time and not wall clock time, which is the point: it does not
   * advance while paused, it jumps when a seek does, and it is the same figure
   * whatever the transport is doing. A page showing wall clock time would drift
   * away from the tune the first time the board made the player wait.
   */
  playtimeMs() { return this._playtimeMs(); }

  /**
   * One byte of the emulated machine's RAM.
   *
   * The RAM itself, with no banking and no side effects, so an address under
   * I/O answers with what is beneath the chip rather than asking the chip. That
   * is deliberate: reading a CIA's interrupt register acknowledges its pending
   * interrupts, so a page redrawing a memory view every frame would break the
   * tune it is showing. See usp_read_memory() in src/host/web_api.cpp.
   *
   * @param {number} address 0 to 65535
   */
  readMemory(address) { return this._readMemory(address & 0xffff); }

  /**
   * The last value written to a SID register, from the emulation's own mirror.
   *
   * Works in every mode, which watching the writes go past does not: in software
   * audio the reSIDfp backend takes them inside the emulation and none of them
   * reach the page. See usp_sid_register() in src/host/web_api.cpp.
   *
   * @param {number} chip 1 to 4
   * @param {number} reg  0 to 31
   */
  sidRegister(chip, reg) { return this._sidRegister(chip, reg); }

  /**
   * A CIA timer's latch, which is how often a CIA driven tune is called.
   *
   * A frame's cycles divided by this is the number of calls a frame, so about
   * 19654 is once and half of it is twice. `timing()` says which timer, if any,
   * is the one driving.
   *
   * @param {number} cia   1 or 2
   * @param {number} timer 0 for A, 1 for B
   */
  ciaLatch(cia = 1, timer = 0) { return this._ciaLatch(cia, timer); }

  /**
   * Hold one voice silent while the tune plays on.
   *
   * Masked inside the emulation, upstream of everything: it works the same in
   * software audio and over every board transport, and it costs the tune
   * nothing, because the writes still happen and only the gate is held down.
   *
   * @param {number} chip  1 to 4
   * @param {number} voice 1 to 3
   * @param {boolean} muted
   */
  setVoiceMute(chip, voice, muted) {
    this._setVoiceMute(chip, voice, muted ? 1 : 0);
  }

  /** The mute bits of one chip, bit 0 being voice 1. Chip counts from 1. */
  voiceMute(chip = 1) { return this._voiceMuteBits(chip); }

  /**
   * Hold a whole chip silent, dropping its writes.
   *
   * Not the same as muting its three voices, and not a shorthand for it. A voice
   * mute masks the gate and the sustain and lets every other write through; a chip
   * mute drops the writes, which is the only one of the two that reaches $18. A
   * tune playing samples through the volume register keeps sounding however many
   * voices are muted, and stops when its chip is.
   *
   * @param {number} chip 1 to 4
   * @param {boolean} muted
   */
  setChipMute(chip, muted) {
    this._setChipMute(chip, muted ? 1 : 0);
  }

  /** The muted chips as a bitmask, bit 0 being chip one. */
  chipMute() { return this._chipMuteBits(); }

  /**
   * What is driving the tune, and how it was started.
   *
   * Read from the chips as they stand, so it is the truth for the subtune
   * playing now rather than a guess from the file: a tune's init routine is
   * what decides whether a CIA timer, a raster compare or a TOD alarm calls the
   * play routine, and a later subtune can choose differently.
   *
   * @returns {{irq: string[], start: string, driver: number}}
   */
  timing() {
    const bits = this._irqSources();
    const irq = [];
    /* Kept in step with the USP_IRQ_* defines in src/api/usplayer.h. */
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
   * The Songlengths key for a tune: the MD5 of the whole file, as 32 hex
   * characters.
   *
   * **The plain MD5 of every byte of the .sid**, not the PSID MD5 that older
   * players use and that libsidplayfp carries two variants of. Getting it wrong
   * misses every entry in the database silently.
   *
   * Done in the wasm because there is no MD5 in a browser: WebCrypto leaves it
   * out deliberately, and this is the one place a page can get one without
   * shipping an implementation of its own.
   *
   * @param {Uint8Array} bytes the file, as loaded
   * @returns {string} 32 hex characters, or '' when it could not be computed
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
   * Hand over the Songlengths database, once, and keep it in the heap.
   *
   * It is four to five megabytes of text and every lookup walks it, so it is
   * copied in once and the pointer kept: a page that re-uploaded it per tune
   * would spend more time on memcpy than on emulation. `releaseSonglengths()`
   * gives it back if a page ever wants the memory.
   *
   * @param {string} text the file as fetched
   * @returns {boolean} false when it could not be allocated
   */
  loadSonglengths(text) {
    this.releaseSonglengths();
    if (!text) return false;
    /* Latin-1 rather than UTF-8: the file is ASCII apart from the comment lines
     * naming each tune, and the parser only ever looks at hex, '=' and digits.
     * One byte per character also means the length is known before encoding. */
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
   * Every song's length for one key, in milliseconds.
   *
   * @param {string} key 32 hex characters from `md5()`
   * @returns {number[]|null} one entry per song, or null when the key is absent
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
      /* The database counts songs from one, as the file's own subtune numbers
       * do. The array is zero based, so out[0] is song 1. */
      for (let s = 1; s <= count; s++) {
        out.push(this._songlenMs(this._slPtr, this._slLen, kp, s));
      }
      return out;
    } finally {
      this._freeBuf(kp);
    }
  }

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
