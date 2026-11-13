/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * Synthesis by reSIDfp in the wasm, played through an AudioWorklet.
 * No board needed.
 *
 * An AudioWorkletProcessor cannot call into the wasm module, it runs on the
 * audio thread. Samples are pulled on the main thread (or a worker) and posted:
 * usp_audio_take()  ->  postMessage(Int16Array)  ->  ring  ->  process()
 *
 * The worklet plays what is in its ring. When it runs dry it emits silence
 * and counts it.
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

/* AudioWorklet processor source, loaded from a blob URL */
export const PROCESSOR_SRC = `
class UspAudio extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const opt = (options && options.processorOptions) || {};
    const cap = opt.capacity || 32768;
    this._buf = new Float32Array(cap);
    /* Right channel, a copy of _buf for mono chunks */
    this._bufR = new Float32Array(cap);
    this._head = 0;   /* written by onmessage */
    this._tail = 0;   /* read by process() */
    this._starved = 0;
    /* Fill level to keep ~ the ring requests what it is short of (flow control) */
    this._target = opt.target || 8192;
    this._since = 0;
    /* Optional second port from a worker ~ a hidden page throttles the main
     * thread and the ring runs dry. Samples come in on _peer and requests go
     * out on it, this.port keeps the page commands (flush, target) and stats */
    this._peer = null;
    /* Samples received per sender, reported back to track what is in flight */
    this._recvPort = 0;
    this._recvPeer = 0;
    this.port.onmessage = (e) => this._onMessage(e, false);
  }

  _onMessage(e, fromPeer) {
      const d = e.data;
      /* Worker channel, a transferred MessagePort bypasses the main thread */
      if (d && d.cmd === 'peer' && d.port) {
        this._peer = d.port;
        this._peer.onmessage = (ev) => this._onMessage(ev, true);
        if (this._peer.start) this._peer.start();
        this._since = 4;
        return;
      }
      if (d && d.cmd === 'stats') {
        this.port.postMessage({ starved: this._starved, queued: this._queued() });
        return;
      }
      /* Fill level, raised while the page is hidden. Nothing is dropped */
      if (d && d.cmd === 'target' && typeof d.value === 'number') {
        this._target = Math.max(2048, Math.min(d.value | 0, this._buf.length - 1));
        this._since = 4;
        return;
      }
      /* Drop the oldest samples down to 'keep' */
      if (d && d.cmd === 'trim' && typeof d.keep === 'number') {
        const excess = this._queued() - Math.max(0, d.keep | 0);
        if (excess > 0) this._tail = (this._tail + excess) % this._buf.length;
        this._since = 4;
        return;
      }
      /* Throw away everything not yet played, report at once */
      if (d && d.cmd === 'flush') {
        this._head = 0;
        this._tail = 0;
        this._starved = 0;
        this._since = 4;
        return;
      }
      /* A bare Int16Array is mono, { stereo: Int16Array } is L/R pairs */
      const ch = (d && d.stereo instanceof Int16Array) ? 2 : 1;
      const pcm = (ch === 2) ? d.stereo : d;
      if (!(pcm instanceof Int16Array)) return;
      const frames = (ch === 2) ? (pcm.length >> 1) : pcm.length;
      if (fromPeer) this._recvPeer += frames;
      else this._recvPort += frames;
      /* Int16 to float on this thread, halves the transfer size */
      for (let i = 0; i < frames; i++) {
        const next = (this._head + 1) % this._buf.length;
        if (next === this._tail) break;   /* Full, drop the rest */
        const l = pcm[i * ch] / 32768;
        this._buf[this._head] = l;
        this._bufR[this._head] = (ch === 2) ? pcm[i * 2 + 1] / 32768 : l;
        this._head = next;
      }
  }

  _queued() {
    return (this._head - this._tail + this._buf.length) % this._buf.length;
  }

  process(inputs, outputs) {
    const out = outputs[0][0];
    if (!out) return true;
    const outR = outputs[0][1];
    const have = this._queued();
    const n = Math.min(out.length, have);
    for (let i = 0; i < n; i++) {
      out[i] = this._buf[this._tail];
      if (outR) outR[i] = this._bufR[this._tail];
      this._tail = (this._tail + 1) % this._buf.length;
    }
    if (n < out.length) {
      out.fill(0, n);
      if (outR) outR.fill(0, n);
      this._starved += out.length - n;
    }

    /* Report every fourth quantum, wanted or not. The starve count rides along */
    if (++this._since >= 4) {
      this._since = 0;
      const q = this._queued();
      const short = this._target - q;
      const report = {
        need: short > 0 ? short : 0,
        queued: q,
        starved: this._starved,
      };
      /* To the worker if present, and to the page for its status line */
      if (this._peer) this._peer.postMessage(Object.assign({ received: this._recvPeer }, report));
      this.port.postMessage(Object.assign({ received: this._recvPort }, report));
    }
    return true;
  }
}
registerProcessor('usp-audio', UspAudio);
`;

/* Seconds of audio to keep ready. A hidden page throttles the main thread
 * (Android Firefox with the screen off barely runs it). The target goes up
 * to seconds and one fill may emulate far more */
const VISIBLE_SECONDS = 0.2;
const HIDDEN_SECONDS  = 2.0;
/* Frames of emulation one fill may do: about 160 ms visible, 2 s hidden */
const VISIBLE_STEPS = 8;
const HIDDEN_STEPS  = 100;

/* Max wall clock ms of one _fill() burst, whatever maxSteps says. Main thread
 * fallback only, see AUDIO_FILL_BUDGET_MS in usplayer-worker.js */
const FILL_BUDGET_MS = 8;

/* Skip a silent lead-in
 * RSID loaders can spend up to a minute filling memory before the first note.
 * Run it at full speed with the audio thrown away, drop back to real time at
 * the first sound. Only the lead-in, once a tune made a sound this never engages again.
 * Switch-on rings down over the first ~25 frames (13009 peak to peak on the first),
 * a silent loader is 0 peak to peak after that, the first music frame is 11094.
 * Threshold 128 sits far below the music */
const SKIP_SETTLE_FRAMES = 50;    /* about a second, well past the ring down */
const SKIP_THRESHOLD     = 128;   /* int16 peak to peak, about -54 dBFS */
/* Give up after this much emulated silence and play on at normal speed.
 * Chosen against loaders of up to sixty seconds with margin */
const SKIP_MAX_SECONDS   = 90;
/* Max ms per call on the main thread, about fifty frames a call at tens of
 * times real time */
const SKIP_BUDGET_MS     = 8;
/* Frames between probes of the output while skipping
 * Skipping costs 0.39 ms a frame with synthesis off and 3.81 ms with it on,
 * one probe in eight is about 24x real time, one in four about 16x.
 * A frame that writes the SID is always probed (c64_mp3.sid writes 0 times in
 * its 11 s lead-in and 38 times on the first music frame) */
const SKIP_PROBE_EVERY   = 8;

/**
 * Software audio output for USBSIDPlayerWeb
 *
 * Usage:
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
    this.gain = null;      /* the volume stage, built with the graph */
    this._volume = 1;      /* survives a host setting it before there is one */
    this._sidVolume = 100; /* percent, see setSidVolume() */
    this._fmVolume = 50;   /* percent, see setFmVolume() */
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
    this._sentSince = 0;   /* posted to the worklet since its last report */
    this._sentTotal = 0;   /* posted to the worklet, ever */
    this._received = null; /* worklet's count of those received, per its report */
    this._filling = false;
    this._hidden = false;
    this._onVisibility = null;
    this._keepAlive = 0;
    this._handedOver = false;
    /* Silent lead-in skip, see _skipSilence(). Armed per tune in run() */
    this._skipping = false;
    this._speed = 1;       /* last speed handed to the synthesis, see _applySpeed() */
    this._skippedFrames = 0;
    this._starveBase = 0;
    this._skipSince = 0;      /* frames since the output was last looked at */
    this._skipWrites = 0;     /* the SID write count at the last frame */
    this._skipFrom = null;    /* the frame the skip began at, once it has */
    /* Called with a reason when a skip ends */
    this.onSkipEnd = null;
  }

  get running() { return this.node !== null; }

  /**
   * Point the synthesis at a tune and reset the cost counters
   * Return false when reSIDfp refuses the rate
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
   * Open an AudioContext and configure the synthesis for its rate
   * Resolve false when that fails
   *
   * @param {object} opts { chips = 1, quality = 1 (sinc), model = 0 (6581) }
   */
  async start(opts = {}) {
    const M = this.M;
    if (typeof M._usp_audio_configure !== 'function') {
      throw new Error('this wasm has no software audio: rebuild with reSIDfp');
    }
    /* Already up: reconfigure for the new tune, keep the context and worklet */
    if (this.node) {
      this.unrun();
      /* Flush the worklet ring too */
      this.flush();
      return this._configure(opts, this.ctx.sampleRate | 0);
    }

    /* Needs a user gesture, iOS keeps the context suspended otherwise.
     * resume() never rejects, race it against a timeout */
    this.ctx = new (window.AudioContext || window.webkitAudioContext)();
    /* audioWorklet only exists in a secure context (https or localhost) */
    if (!this.ctx.audioWorklet) {
      this.ctx.close().catch(() => {});
      this.ctx = null;
      throw new Error('AudioWorklet is not available: the page must be served over '
        + 'https:// or from http://localhost (secure context)');
    }
    if (this.ctx.state === 'suspended') {
      await Promise.race([this.ctx.resume(), new Promise((r) => setTimeout(r, 2000))]);
    }

    this._url = URL.createObjectURL(
      new Blob([PROCESSOR_SRC], { type: 'application/javascript' }));
    await this.ctx.audioWorklet.addModule(this._url);

    /* Use the device rate, the synthesis is configured for what the context runs at */
    const rate = this.ctx.sampleRate | 0;
    if (!this._configure(opts, rate)) {
      await this.stop();
      return false;
    }

    /* Target 0.2 s while visible. Capacity is 4 s (768 kB per channel at 48 kHz),
     * the target goes up to seconds while hidden, see setTarget() */
    const target = Math.max(2048, Math.round(rate * VISIBLE_SECONDS));
    this.node = new AudioWorkletNode(this.ctx, 'usp-audio', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions: { capacity: Math.max(8192, rate * 4), target },
    });
    this._target = target;
    this.node.port.onmessage = (e) => this._onReport(e.data);
    /* A throwing processor is removed and the audio stops, log it */
    this.node.onprocessorerror = (e) => {
      console.error('[usplayer-audio] the worklet processor failed', e);
    };
    /* Gain stage for the host volume control. Scaling $d418 in the emulation
     * would change the tune (written constantly, digis hundreds of times a frame).
     * Unity by default */
    this.gain = this.ctx.createGain();
    this.gain.gain.value = this._volume;
    this.node.connect(this.gain);
    this.gain.connect(this.ctx.destination);

    this._ptr = M._usp_alloc(this._max * 2);
    return true;
  }

  /**
   * Set the volume, 0 to 1
   * Remembered when set before there is a graph
   *
   * @param {number} value 0 silence, 1 unity
   */
  setVolume(value) {
    const v = Math.max(0, Math.min(1, Number(value)));
    this._volume = isNaN(v) ? 1 : v;
    if (this.gain) this.gain.gain.value = this._volume;
  }

  /**
   * Set the SID side of the mix, independent of FM/OPL
   *
   * Scales inside the wasm mix before the chips are summed, see
   * usp_audio_set_sid_volume() (web_api.cpp). Applied straight to the module.
   *
   * @param {number} percent 100 is unity (default), 0 silences it
   */
  setSidVolume(percent) {
    const v = Number(percent);
    this._sidVolume = isNaN(v) ? 100 : v;
    if (typeof this.M._usp_audio_set_sid_volume === 'function') {
      this.M._usp_audio_set_sid_volume(this._sidVolume | 0);
    }
  }

  /**
   * Set the FM/OPL side of the mix, independent of SID
   *
   * Default 50, the OPL is louder, see OplChip::set_gain()
   *
   * @param {number} percent 100 is unity, 0 silences it
   */
  setFmVolume(percent) {
    const v = Number(percent);
    this._fmVolume = isNaN(v) ? 50 : v;
    if (typeof this.M._usp_audio_set_fm_volume === 'function') {
      this.M._usp_audio_set_fm_volume(this._fmVolume | 0);
    }
  }

  /**
   * Handle a worklet report (ring fill level and starve count)
   *
   * A method, to drive it without an AudioWorklet
   *
   * @param {{need:number, queued:number, starved:number}} d
   */
  _onReport(d) {
    if (!d) return;
    this.msgs++;
    this._lastMsg = (typeof performance !== 'undefined') ? performance.now() : Date.now();
    if (typeof d.starved === 'number') this.starved = d.starved;
    if (typeof d.queued === 'number') this.queued = d.queued;
    if (this._driven && typeof d.queued === 'number') {
      /* Shortfall minus what is in flight. Stale queued reports would each ask
       * for the same shortfall, overfilling the ring and dropping samples
       * (heard as the tune skipping ahead) */
      const owed = this._target - d.queued - this._inFlight(d);
      this._sentSince = 0;
      this._owed = owed > 0 ? owed : 0;
      /* While skipping nothing is in flight and the ring asks for nothing,
       * keep running, this drives the skip */
      if (this._owed > 0 || this._skipping) this._fill();
    }
  }

  /**
   * Return samples posted to the worklet and not yet received by it
   *
   * Exact with a received count in the report, else estimated from the
   * posted since last report count.
   *
   * @param {object|null} d worklet report, null for the last one seen
   */
  _inFlight(d) {
    if (d && typeof d.received === 'number') this._received = d.received;
    if (this._received === null) return this._sentSince;
    return Math.max(0, this._sentTotal - this._received);
  }

  /**
   * Move synthesised audio to the worklet, call after each step
   */
  pump() {
    if (!this.node || !this._ptr) return 0;
    const M = this.M;
    let moved = 0;
    for (;;) {
      const n = M._usp_audio_take(this._ptr, this._max);
      if (n <= 0) break;
      /* Copy, the heap view is reused and detaches when the heap grows */
      const chunk = new Int16Array(
        M.HEAPU8.buffer, this._ptr, n).slice();
      this.node.port.postMessage(chunk, [chunk.buffer]);
      /* In flight until the worklet reports back */
      this._sentSince += n;
      this._sentTotal += n;
      moved += n;
      if (n < this._max) break;
    }
    return moved;
  }

  /**
   * Clock a player from the ring, not the wall clock
   *
   * USBSIDPlayerWeb._tick() caps at two frames and discards the remainder
   * (_acc = period). Fine for a board, wrong for the ring: it is the clock and a
   * frame never emulated is a hole in the audio (2SID tunes show it as bursts
   * and silence). The player is started with externalClock: true and stepped
   * in this module until the ring has what it asked for.
   *
   * @param {object} player started with { externalClock: true }
   * @param {object} opts { maxSteps } frames per fill
   */
  run(player, opts = {}) {
    this._driven = player;
    this._watchVisibility();
    /* Cap the frames per fill, this runs on the main thread */
    this._maxSteps = opts.maxSteps || (this._hidden ? HIDDEN_STEPS : VISIBLE_STEPS);
    this._owed = 0;
    /* Arm the lead-in skip. Off when a worker owns the ring or skipSilence is false */
    this._skipping = (opts.skipSilence !== false) && !this._handedOver;
    this._skippedFrames = 0;
    this._starveBase = 0;
    this._skipSince = 0;
    this._skipFrom = null;
    this._skipWrites = player.sidWrites();
    /* A previous tune may have been stopped mid skip */
    if (typeof this.M._usp_audio_render === 'function') this.M._usp_audio_render(1);
    /* Prime the ring */
    this._owed = Math.max(2048, Math.round(this.ctx.sampleRate * 0.2));
    this._fill();
  }

  /**
   * Hand the ring over to a worker and stop driving it in this module
   *
   * Return the worker's end of a MessageChannel (null without a node), to be
   * transferred to the worker. The processor gets the other end. A hidden
   * page throttles the main thread, the audio thread is not throttled.
   */
  handOver() {
    if (!this.node) return null;
    /* Two fillers on one ring would see each other's samples */
    this.unrun();
    const ch = new MessageChannel();
    this.node.port.postMessage({ cmd: 'peer', port: ch.port2 }, [ch.port2]);
    this._handedOver = true;
    return ch.port1;
  }

  /**
   * Drop buffered audio beyond the current target, oldest first
   * Used on return from hidden
   */
  trim() {
    if (!this.node) return;
    this.node.port.postMessage({ cmd: 'trim', keep: this._target });
  }

  /** True once a worker owns the ring */
  get handedOver() { return !!this._handedOver; }

  /**
   * Set seconds of audio to keep ready and the work one fill may do
   * Only the level changes, rendered audio is kept
   */
  setTarget(seconds, maxSteps) {
    if (!this.node || !this.ctx) return;
    const want = Math.max(2048, Math.round(this.ctx.sampleRate * seconds));
    this._target = want;
    this._maxSteps = maxSteps;
    this.node.port.postMessage({ cmd: 'target', value: want });
    /* Worker needs the target too, the host sets it through this hook */
    if (this.onTargetChange) this.onTargetChange(want, maxSteps);
  }

  /**
   * Follow the page going hidden and visible
   *
   * Hidden: deeper ring, the main thread is throttled. Visible: back to 0.2 s,
   * the ring drains to the new level. Uses visibilitychange, it fires for
   * screen off and app switch.
   */
  _watchVisibility() {
    if (this._onVisibility || typeof document === 'undefined') return;
    this._onVisibility = () => {
      const hidden = (document.visibilityState === 'hidden');
      if (hidden === this._hidden) return;
      this._hidden = hidden;
      this.setTarget(hidden ? HIDDEN_SECONDS : VISIBLE_SECONDS,
                     hidden ? HIDDEN_STEPS : VISIBLE_STEPS);
      /* Hidden: fill the deeper buffer at once while the thread still runs at full speed */
      if (hidden && this._driven) {
        this._owed = this._target;
        this._fill();
      }
      /* A worker is not throttled, no keep alive for it. The target still moves */
      if (hidden && !this._handedOver) this._startKeepAlive();
      else this._stopKeepAlive();
    };
    document.addEventListener('visibilitychange', this._onVisibility);
  }

  /**
   * Top up the ring on a timer while the page is hidden
   *
   * Worklet requests land late on a throttled main thread. Timers run about
   * once a second, enough when one fill renders two seconds. Same arithmetic
   * as the report handler: wanted minus in flight.
   */
  _startKeepAlive() {
    if (this._keepAlive) return;
    this._keepAlive = setInterval(() => {
      if (!this._driven || this._filling) return;
      const owed = this._target - this.queued - this._inFlight(null);
      if (owed > 0) { this._owed = owed; this._fill(); }
    }, 500);
  }

  _stopKeepAlive() {
    if (this._keepAlive) { clearInterval(this._keepAlive); this._keepAlive = 0; }
  }

  _unwatchVisibility() {
    if (!this._onVisibility || typeof document === 'undefined') return;
    document.removeEventListener('visibilitychange', this._onVisibility);
    this._onVisibility = null;
    this._hidden = false;
    this._stopKeepAlive();
  }

  /** Stop clocking a player, context and node stay up */
  unrun() {
    this._driven = null;
    this._owed = 0;
    this._sentSince = 0;
    this._stopKeepAlive();
    /* A skip leaves the synthesis off between slices, switch it back on
     * (global on the backend) */
    this._skipping = false;
    if (this.M && typeof this.M._usp_audio_render === 'function') {
      this.M._usp_audio_render(1);
    }
  }

  /**
   * Emulate until the worklet has what it asked for, bounded by maxSteps
   * (main thread). Leftover is asked for again a few quanta later.
   */
  /**
   * Take one chunk of rendered audio, return samples taken and peak to peak
   *
   * Peak to peak because reSIDfp has a DC offset that the external filter
   * walks back to zero over the first half second.
   */
  _takeMeasured() {
    const n = this.M._usp_audio_take(this._ptr, this._max);
    if (n <= 0) return { n: 0, pp: 0 };
    const v = new Int16Array(this.M.HEAPU8.buffer, this._ptr, n);
    let lo = 32767, hi = -32768;
    for (let i = 0; i < n; i++) {
      const s = v[i];
      if (s < lo) lo = s;
      if (s > hi) hi = s;
    }
    return { n, pp: hi - lo };
  }

  /**
   * Run through a silent lead-in, throwing its audio away
   *
   * See the SKIP_ constants. Nothing is posted to the ring, the worklet
   * invents silence. Time budgeted, continued on the following report.
   */
  _skipSilence() {
    const p = this._driven;
    const M = this.M;
    const canSkipRender = (typeof M._usp_audio_render === 'function');
    const now = () => ((typeof performance !== 'undefined') ? performance.now() : Date.now());
    const t0 = now();
    const limit = Math.round(p.refreshHz() * SKIP_MAX_SECONDS);

    try {
        /* Skip starts after the first second, the switch-on transient plays normally */
      if (this._skipFrom === null) this._skipFrom = p.frames();

      while (this._skipping) {
        if (p.frames() - this._skipFrom >= limit) {
          this._endSkip(`nothing after ${SKIP_MAX_SECONDS}s, playing on`);
          return;
        }
        if (now() - t0 >= SKIP_BUDGET_MS) return;  /* more on the following report */

        /* Probe the output on a frame that wrote the SID, else every
         * SKIP_PROBE_EVERY frames. Older wasm without render switch probes all */
        const writes = p.sidWrites();
        const wrote = writes !== this._skipWrites;
        this._skipWrites = writes;
        const probe = !canSkipRender || wrote ||
                      (++this._skipSince >= SKIP_PROBE_EVERY);
        if (probe) this._skipSince = 0;
        if (canSkipRender) M._usp_audio_render(probe ? 1 : 0);

        this._frames++;
        p.stepAndDrain();
        if (!probe) continue;

        for (;;) {
          const { n, pp } = this._takeMeasured();
          if (n <= 0) break;
          if (pp >= SKIP_THRESHOLD) {
            /* Music started, keep this frame to not clip the first attack */
            const chunk = new Int16Array(M.HEAPU8.buffer, this._ptr, n).slice();
            this.node.port.postMessage(chunk, [chunk.buffer]);
            this._sentSince += n;
            this._sentTotal += n;
            this._endSkip('sound at ' +
              (p.frames() / p.refreshHz()).toFixed(1) + 's');
            return;
          }
          if (n < this._max) break;
        }
      }
    } finally {
      /* Always switch the synthesis back on, it is global on the backend */
      if (canSkipRender && !this._skipping) M._usp_audio_render(1);
    }
  }

  /** Stop skipping and exclude the silence from the starve count */
  _endSkip(why) {
    if (!this._skipping) return;
    this._skipping = false;
    if (typeof this.M._usp_audio_render === 'function') this.M._usp_audio_render(1);
    this._skippedFrames = (this._driven && this._skipFrom !== null)
      ? this._driven.frames() - this._skipFrom : 0;
    /* The ring invented silence for the whole skip, do not count it as starved */
    this._starveBase = this.starved;
    if (this.onSkipEnd) this.onSkipEnd(why, this._skippedFrames);
  }

  /** True while a silent lead-in is being run through */
  get skipping() { return !!this._skipping; }

  /**
   * Follow a player speed change with the synthesis output rate
   *
   * Pitch follows speed, see usp_audio_set_speed().
   *
   * @param {number} mult player speed multiplier
   */
  _applySpeed(mult) {
    if (mult === this._speed) return;
    this._speed = mult;
    if (typeof this.M._usp_audio_set_speed === 'function') {
      this.M._usp_audio_set_speed(mult);
    }
  }

  _fill() {
    const p = this._driven;
    if (!p || !this.node || this._filling) return;
    if (p.paused) return;
    /* Run a silent lead-in first, the normal fill follows in the same call */
    if (this._skipping && p.frames() >= SKIP_SETTLE_FRAMES) {
      this._filling = true;
      try { this._skipSilence(); } finally { this._filling = false; }
      if (this._skipping) return;
    }
    this._filling = true;
    const t0 = (typeof performance !== 'undefined') ? performance.now() : 0;
    try {
      this._applySpeed(p.speed || 1);
      const deadline = t0 + FILL_BUDGET_MS;
      let steps = 0;
      while (this._owed > 0 && steps < this._maxSteps) {
        this._frames++;
        p.stepAndDrain();
        this._owed -= this.pump();
        steps++;
        if (t0 && performance.now() >= deadline) break;
      }
    } finally {
      this._filling = false;
      if (t0) {
        this.fillMs = performance.now() - t0;
        this._cpuMs += this.fillMs;
      }
    }
  }

  /**
   * Return what the output is doing, for a status line
   *
   * starved counts samples of silence the worklet invented. Climbing while
   * playing means the emulation is not keeping up (heard as crackle).
   */
  stats() {
    const rate = this.ctx ? this.ctx.sampleRate : 48000;
    /* ms of work per emulated frame, averaged over the run. Above the
     * ~20 ms PAL frame the ring cannot keep up */
    const perFrame = this._frames > 0 ? (this._cpuMs / this._frames) : 0;
    /* Older wasm builds have no clipped counter */
    const clipped = (typeof this.M._usp_audio_clipped === 'function')
                  ? this.M._usp_audio_clipped() : -1;
    /* Skip silence is not counted, see _endSkip() */
    const starved = Math.max(0, this.starved - this._starveBase);
    return {
      clipped,
      /* FM/OPL writes, zero means no FM in this tune */
      fmWrites: (typeof this.M._usp_audio_fm_writes === 'function')
        ? this.M._usp_audio_fm_writes() : 0,
      skipping: !!this._skipping,
      skippedMs: this._skippedFrames && this._driven
        ? Math.round((this._skippedFrames / this._driven.refreshHz()) * 1000) : 0,
      starved,
      starvedMs: Math.round((starved / rate) * 1000),
      queuedMs: Math.round((this.queued / rate) * 1000),
      fillMs: Math.round(this.fillMs),
      msPerFrame: perFrame,
      frames: this._frames,
      rate,
    };
  }

  /** Ask the worklet for stats, the answer arrives on the message handler */
  requestStats() {
    if (this.node) this.node.port.postMessage({ cmd: 'stats' });
  }

  /**
   * Drop what the synthesis rendered and the page has not sent
   *
   * Audio already posted to the worklet stays. Use flush() for a hard reset,
   * this one is for fast forward where queued audio keeps playing.
   */
  discard() {
    if (this.M && typeof this.M._usp_audio_discard === 'function') {
      this.M._usp_audio_discard();
    }
  }

  /**
   * Throw away everything not yet heard, both sides of the port
   *
   * For a stop and for loading a tune into a running context. Without it the
   * new tune queues behind the old ring and the ring stays above target.
   */
  flush() {
    this.discard();
    if (this.node) this.node.port.postMessage({ cmd: 'flush' });
    this._owed = 0;
    this._sentSince = 0;
    this.queued = 0;
  }

  async stop() {
    this.unrun();
    this._unwatchVisibility();
    this._handedOver = false;
    /* Give the machine back to the web backend, a later ASID or WebUSB play
     * in this page writes to the ring again (older wasm has no release) */
    if (this.M && typeof this.M._usp_audio_release === 'function') this.M._usp_audio_release();
    if (this.node) { try { this.node.disconnect(); } catch (_) {} this.node = null; }
    if (this.gain) { try { this.gain.disconnect(); } catch (_) {} this.gain = null; }
    if (this.ctx) { try { await this.ctx.close(); } catch (_) {} this.ctx = null; }
    if (this._url) { URL.revokeObjectURL(this._url); this._url = null; }
    if (this._ptr && this.M) { this.M._usp_free(this._ptr); this._ptr = 0; }
  }
}
