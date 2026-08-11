/*
 * USBSID-Player: software audio in the browser.
 *
 * The page synthesises with reSIDfp inside the wasm and plays it through an
 * AudioWorklet, so a tune can be heard with no board attached at all.
 *
 * THE CONSTRAINT THAT SHAPES THIS FILE
 *
 * An AudioWorkletProcessor **cannot call into the wasm module**. It runs on the
 * audio rendering thread; the module lives on the main thread or in a worker.
 * So samples cannot be pulled from inside `process()`, which is what a naive
 * design would do. Instead:
 *
 *   main thread            worklet (audio thread)
 *   -----------            ----------------------
 *   usp_audio_take()  ->   postMessage(Int16Array)  ->  ring
 *                                                       process() drains it
 *
 * The worklet keeps its own ring and plays whatever it has been given. If it
 * runs dry it emits silence and counts it, because silence that is quietly
 * inserted is indistinguishable from a quiet tune.
 *
 * `postMessage` and not a SharedArrayBuffer, which was the decision taken when
 * this was planned: SAB would let the worklet read the wasm heap directly with
 * no copy, and needs COOP/COEP response headers that the usual local dev server
 * does not send. The copy is a few hundred samples a frame and is not the
 * expensive part of anything here.
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

/* The processor, as source, because an AudioWorklet module has to be fetched by
 * URL and a blob is how a single file library ships one. Deliberately small: it
 * owns a ring and nothing else, since anything it gets wrong is heard. */
const PROCESSOR_SRC = `
class UspAudio extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const opt = (options && options.processorOptions) || {};
    const cap = opt.capacity || 32768;
    this._buf = new Float32Array(cap);
    this._head = 0;   /* written by onmessage */
    this._tail = 0;   /* read by process() */
    this._starved = 0;
    /* How full this wants to be kept, and how often it is allowed to say so.
     * The ring asking for what it is short of is the whole flow control: the
     * page cannot work the level out for itself without either a round trip or
     * arithmetic on currentTime that drifts by exactly the amount of any
     * silence that has already been invented. */
    this._target = opt.target || 8192;
    this._since = 0;
    this.port.onmessage = (e) => {
      const d = e.data;
      if (d && d.cmd === 'stats') {
        this.port.postMessage({ starved: this._starved, queued: this._queued() });
        return;
      }
      if (!(d instanceof Int16Array)) return;
      /* Int16 to float here rather than on the main thread: it is the audio
       * thread's own format and doing it here keeps the transfer half the size. */
      for (let i = 0; i < d.length; i++) {
        const next = (this._head + 1) % this._buf.length;
        if (next === this._tail) break;   /* full: drop, the page is too far ahead */
        this._buf[this._head] = d[i] / 32768;
        this._head = next;
      }
    };
  }

  _queued() {
    return (this._head - this._tail + this._buf.length) % this._buf.length;
  }

  process(inputs, outputs) {
    const out = outputs[0][0];
    if (!out) return true;
    const have = this._queued();
    const n = Math.min(out.length, have);
    for (let i = 0; i < n; i++) {
      out[i] = this._buf[this._tail];
      this._tail = (this._tail + 1) % this._buf.length;
    }
    if (n < out.length) {
      out.fill(0, n);
      this._starved += out.length - n;
    }

    /* Ask for more when short, at most every fourth quantum so a busy page is
     * not buried in messages it cannot answer. The starve count rides along
     * because the page has no other way to know it happened. */
    /* Report every fourth quantum whether or not anything is wanted. Reporting
     * only when short leaves the page holding a stale zero for as long as the
     * ring is full, which reads as "nothing has ever gone wrong" and is the one
     * thing a diagnostic must not do. */
    if (++this._since >= 4) {
      this._since = 0;
      const q = this._queued();
      const short = this._target - q;
      this.port.postMessage({
        need: short > 0 ? short : 0,
        queued: q,
        starved: this._starved,
      });
    }
    return true;
  }
}
registerProcessor('usp-audio', UspAudio);
`;

/**
 * Software audio output for USBSIDPlayerWeb.
 *
 * Usage, once a tune is loaded so the clock is known:
 *
 *   const audio = new UsPlayerAudio(player);
 *   await audio.start({ chips: 1, model: 0 });
 *   // then call audio.pump() after every player step
 */
export class UsPlayerAudio {
  /**
   * @param {object} player an instantiated USBSIDPlayerWeb (for its module)
   */
  constructor(player) {
    this.player = player;
    this.M = player.M;
    this.ctx = null;
    this.node = null;
    this._ptr = 0;
    this._max = 8192;
    this.starved = 0;
    this.queued = 0;
    this.fillMs = 0;      /* how long the last fill blocked the main thread */
    this._cpuMs = 0;      /* total spent emulating, and frames it bought */
    this._frames = 0;
    this._url = null;
    this._driven = null;   /* the player this is clocking, if any */
    this._owed = 0;        /* samples the worklet has asked for and not had */
    this._filling = false;
  }

  get running() { return this.node !== null; }

  /**
   * Point the synthesis at a tune. Also resets the cost counters, which are
   * per tune: a 3SID tune's ms/frame says nothing about the 1SID one before it.
   *
   * @returns {boolean} false when reSIDfp refuses the rate
   */
  _configure(opts, rate) {
    const ok = this.M._usp_audio_configure(
      (opts.chips || 1) | 0, rate,
      (opts.quality === undefined ? 1 : opts.quality) | 0,
      (opts.model || 0) | 0);
    if (!ok) return false;
    this._cpuMs = 0;
    this._frames = 0;
    return true;
  }

  /**
   * Open an AudioContext and configure the synthesis for its real rate.
   *
   * @param {object} opts { chips = 1, quality = 1 (sinc), model = 0 (6581) }
   * @returns {Promise<boolean>}
   */
  async start(opts = {}) {
    const M = this.M;
    if (typeof M._usp_audio_configure !== 'function') {
      throw new Error('this wasm has no software audio: rebuild with reSIDfp');
    }
    /* Already up: reconfigure for the new tune rather than returning early.
     * Chip count, model and clock all come from the tune, so a second tune
     * loaded into a running context would otherwise be synthesised with the
     * first one's settings, and a 2SID tune after a 1SID one would play as
     * one. The context and the worklet are kept: they are per device, not per
     * tune, and tearing them down costs a gesture we may not have. */
    if (this.node) {
      this.unrun();
      this.discard();
      return this._configure(opts, this.ctx.sampleRate | 0);
    }

    /* Must follow a user gesture, which is not this file's business to arrange
     * but is the first thing to check when nothing plays: iOS in particular
     * gives a context that stays suspended for ever otherwise. */
    this.ctx = new (window.AudioContext || window.webkitAudioContext)();
    if (this.ctx.state === 'suspended') await this.ctx.resume();

    this._url = URL.createObjectURL(
      new Blob([PROCESSOR_SRC], { type: 'application/javascript' }));
    await this.ctx.audioWorklet.addModule(this._url);

    /* The device's rate, not a wish. Asking for 44100 on a context fixed at
     * 48000 gets a resampler for free whether or not that was wanted, so the
     * synthesis is configured for what the context actually runs at. */
    const rate = this.ctx.sampleRate | 0;
    if (!this._configure(opts, rate)) {
      await this.stop();
      return false;
    }

    /* Target a fifth of a second. Long enough to ride out a slow frame on a
     * two or three chip tune, short enough that a stop is not heard a beat
     * later. Capacity is well above it so a burst is never dropped. */
    const target = Math.max(2048, Math.round(rate * 0.2));
    this.node = new AudioWorkletNode(this.ctx, 'usp-audio', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [1],
      processorOptions: { capacity: Math.max(8192, rate), target },
    });
    this._target = target;
    this.node.port.onmessage = (e) => {
      const d = e.data;
      if (!d) return;
      this.msgs++;
      this._lastMsg = (typeof performance !== 'undefined') ? performance.now() : Date.now();
      if (typeof d.starved === 'number') this.starved = d.starved;
      if (typeof d.queued === 'number') this.queued = d.queued;
      if (d.need > 0 && this._driven) {
        this._owed = d.need;
        this._fill();
      }
    };
    /* A processor that throws is removed and simply stops being called, with no
     * other sign than the audio ending. Saying so beats silence. */
    this.node.onprocessorerror = (e) => {
      console.error('[usplayer-audio] the worklet processor failed', e);
    };
    this.node.connect(this.ctx.destination);

    this._ptr = M._usp_alloc(this._max * 2);
    return true;
  }

  /**
   * Move whatever has been synthesised to the worklet. Call after each step.
   *
   * Cheap when there is nothing: one call into the wasm that returns zero.
   */
  pump() {
    if (!this.node || !this._ptr) return 0;
    const M = this.M;
    let moved = 0;
    for (;;) {
      const n = M._usp_audio_take(this._ptr, this._max);
      if (n <= 0) break;
      /* A copy, because the heap view is reused on the next call and may be
       * detached entirely if the heap grows. */
      const chunk = new Int16Array(
        M.HEAPU8.buffer, this._ptr, n).slice();
      this.node.port.postMessage(chunk, [chunk.buffer]);
      moved += n;
      if (n < this._max) break;
    }
    return moved;
  }

  /**
   * Clock a player from the ring instead of from the wall clock.
   *
   * WHY THIS EXISTS, because it looks like duplication of the player's own
   * clock and is not.
   *
   * `USBSIDPlayerWeb._tick()` advances by however much real time has passed,
   * capped at two frames, and then **discards the remainder**
   * (`if (this._acc > period) this._acc = period`). For a board that is right:
   * its queue is the buffer, and a burst of frames sent late is heard late and
   * cannot be taken back, so dropping is better than overshooting.
   *
   * For software audio it is exactly wrong. Here the ring **is** the clock, and
   * a frame that is never emulated is not a frame played late, it is a hole in
   * the audio. On a two SID tune, where a frame costs about twice what it does
   * on one, that shows up as a burst of sound, a long silence, another burst.
   *
   * So the player is started with `externalClock: true` and stepped from here,
   * on demand, until the ring has what it asked for. Falling behind then
   * self corrects instead of compounding: the ring asks for more, and more is
   * what it gets. The command line player does the same thing by setting
   * `no_device` so its pacer never paces.
   *
   * @param {object} player started with `{ externalClock: true }`
   * @param {object} opts   { maxSteps } frames per fill, the guard against
   *                        blocking the main thread for an unbounded time
   */
  run(player, opts = {}) {
    this._driven = player;
    /* Eight frames, about 160 ms of audio, is the most this will emulate in one
     * go. It runs on the main thread, so the cap is what stops a slow tune from
     * blocking the page for as long as it takes to fill a whole buffer. */
    this._maxSteps = opts.maxSteps || 8;
    this._owed = 0;
    /* Prime it, so playback does not start on an empty ring and a starve. */
    this._owed = Math.max(2048, Math.round(this.ctx.sampleRate * 0.2));
    this._fill();
  }

  /** Stop clocking a player. The context and node stay up. */
  unrun() { this._driven = null; this._owed = 0; }

  /**
   * Emulate until the worklet has what it asked for.
   *
   * Bounded by `maxSteps` because this runs on the main thread: a tune that
   * cannot be synthesised in realtime would otherwise never give the page back.
   * Whatever is left over is asked for again a few quanta later, so a machine
   * that is merely slow degrades into a shorter buffer rather than a freeze.
   */
  _fill() {
    const p = this._driven;
    if (!p || !this.node || this._filling) return;
    if (p.paused) return;
    this._filling = true;
    const t0 = (typeof performance !== 'undefined') ? performance.now() : 0;
    try {
      /* Fast forward means seeking, not playing faster: nothing can render
       * ahead of a ring that plays at one times speed. So the extra frames are
       * emulated and their audio thrown away, which is what the command line
       * player does for the same reason. */
      const mult = Math.max(1, Math.round(p.speed || 1));
      let steps = 0;
      while (this._owed > 0 && steps < this._maxSteps) {
        this._frames++;
        p.stepAndDrain();
        if (mult > 1) {
          for (let k = 1; k < mult; k++) p.stepAndDrain();
          this.discard();
          steps += mult;
          continue;
        }
        this._owed -= this.pump();
        steps++;
      }
      if (mult > 1) this._owed = 0;
    } finally {
      this._filling = false;
      if (t0) {
        this.fillMs = performance.now() - t0;
        this._cpuMs += this.fillMs;
      }
    }
  }

  /**
   * @returns {object} what the output is doing, for a status line.
   *
   * `starved` is the one that matters. It counts samples of silence the worklet
   * had to invent, and a number that climbs while playing means the emulation
   * is not keeping up: not a wrong note anywhere, just holes, which at this
   * granularity is heard as crackle rather than as a gap.
   */
  stats() {
    const rate = this.ctx ? this.ctx.sampleRate : 48000;
    /* Milliseconds of work per emulated frame, against the frame's own budget
     * of about 20 ms on PAL. Above that and no buffer can save it: the ring
     * drains faster than it can be refilled, for ever. This is the number that
     * says "too slow" as opposed to "badly paced", and the average over the
     * whole run rather than the last fill, so the first fill's cold code does
     * not stand for the steady state. */
    const perFrame = this._frames > 0 ? (this._cpuMs / this._frames) : 0;
    /* Older wasm builds have no clipped counter, so ask rather than assume. */
    const clipped = (typeof this.M._usp_audio_clipped === 'function')
                  ? this.M._usp_audio_clipped() : -1;
    return {
      clipped,
      starved: this.starved,
      starvedMs: Math.round((this.starved / rate) * 1000),
      queuedMs: Math.round((this.queued / rate) * 1000),
      fillMs: Math.round(this.fillMs),
      msPerFrame: perFrame,
      frames: this._frames,
      rate,
    };
  }

  /** Ask the worklet how it is doing. Answer arrives on the message handler. */
  requestStats() {
    if (this.node) this.node.port.postMessage({ cmd: 'stats' });
  }

  /** Drop anything rendered but not yet taken, on a stop or a seek. */
  discard() {
    if (this.M && typeof this.M._usp_audio_discard === 'function') {
      this.M._usp_audio_discard();
    }
  }

  async stop() {
    this.unrun();
    if (this.node) { try { this.node.disconnect(); } catch (_) {} this.node = null; }
    if (this.ctx) { try { await this.ctx.close(); } catch (_) {} this.ctx = null; }
    if (this._url) { URL.revokeObjectURL(this._url); this._url = null; }
    if (this._ptr && this.M) { this.M._usp_free(this._ptr); this._ptr = 0; }
  }
}
