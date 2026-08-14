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
 * owns a ring and nothing else, since anything it gets wrong is heard.
 *
 * Exported so a test can run the real ring outside an AudioWorklet, which is
 * the only way any of this can be exercised without a device: see
 * temp/ring_sim.mjs. */
export const PROCESSOR_SRC = `
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
    /* A second port, when the page hands one over.
     *
     * Samples normally arrive from the main thread, which is where the
     * emulation runs. A hidden page throttles that thread hard while this one
     * is never throttled, so the ring empties and the tune stutters with the
     * screen off. The cure is to run the emulation in a worker and let it talk
     * to this ring **directly**, with the main thread not in the path at all.
     *
     * So both ends of the conversation move: samples come in on _peer and the
     * requests for more go out on it, while this.port stays for the page's own
     * commands (flush, target) and for the stats it displays. */
    this._peer = null;
    const handle = (e) => this._onMessage(e);
    this.port.onmessage = handle;
    this._handle = handle;
  }

  _onMessage(e) {
      const d = e.data;
      /* The worker's end of the channel. Kept as a port rather than a flag: a
       * transferred MessagePort is the only way into this thread that does not
       * go through the main thread's task queue. */
      if (d && d.cmd === 'peer' && d.port) {
        this._peer = d.port;
        this._peer.onmessage = this._handle;
        if (this._peer.start) this._peer.start();
        this._since = 4;
        return;
      }
      if (d && d.cmd === 'stats') {
        this.port.postMessage({ starved: this._starved, queued: this._queued() });
        return;
      }
      /* Throw away everything not yet played.
       *
       * The page can drop what it has rendered but not yet sent; only this
       * side can drop what has already been sent. Without it a new tune is
       * appended behind up to a second of the old one, and the ring stays
       * above target so it asks for nothing more: the new tune is queued
       * rather than switched to. _since is set so the next process() reports
       * at once, otherwise the page waits up to four quanta to be told the
       * ring is empty. */
      /* How full the page should keep this. Raised while the page is hidden:
       * see setTarget() on the class. Only the level changes, nothing is
       * dropped, so it takes effect as the ring fills. */
      if (d && d.cmd === 'target' && typeof d.value === 'number') {
        this._target = Math.max(2048, Math.min(d.value | 0, this._buf.length - 1));
        this._since = 4;
        return;
      }
      if (d && d.cmd === 'flush') {
        this._head = 0;
        this._tail = 0;
        this._starved = 0;
        this._since = 4;
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
      const report = {
        need: short > 0 ? short : 0,
        queued: q,
        starved: this._starved,
      };
      /* To the worker when there is one, since it is the thing that can act on
       * it, and to the page as well so its status line keeps working. */
      if (this._peer) this._peer.postMessage(report);
      this.port.postMessage(report);
    }
    return true;
  }
}
registerProcessor('usp-audio', UspAudio);
`;

/* How much audio to keep ready, in seconds.
 *
 * The emulation runs on the **main thread**, driven by the worklet asking for
 * samples. The audio thread is never throttled; the main thread is throttled
 * hard as soon as the page is hidden, and on Android Firefox with the screen
 * off it barely runs at all. A fifth of a second of buffer is then nowhere near
 * enough: the ring empties between callbacks and the tune stutters.
 *
 * So when the page goes away the target becomes seconds rather than a fifth of
 * one, and each callback is allowed to emulate far more before returning. There
 * is no interactive cost to a long buffer while nobody is looking, and blocking
 * a hidden page's main thread for 40 ms at a time costs nothing either.
 */
const VISIBLE_SECONDS = 0.2;
const HIDDEN_SECONDS  = 2.0;
/* Frames of emulation one fill may do: about 160 ms visible, 2 s hidden. */
const VISIBLE_STEPS = 8;
const HIDDEN_STEPS  = 100;

/* Running through a tune's silent lead-in.
 *
 * Plenty of RSID tunes are a loader: the machine boots, BASIC RUNs a program and
 * that program spends up to a minute filling memory with samples before a note
 * is played. Emulated at one second per second, that is a minute of silence with
 * nothing to say it is working. The tune is not broken and neither is the
 * player, but nobody waits a minute to find that out.
 *
 * So the lead-in is run as fast as the machine will go and its audio thrown
 * away, and the moment the output stops being silent the player drops back to
 * real time. Only the lead-in: once a tune has made a sound this never engages
 * again, so a rest in the middle of a piece is played, not skipped.
 *
 * Measured rather than guessed (temp/silence_profile.mjs). Switching the chip on
 * is a step into the filter and rings down over about the first 25 frames,
 * reaching 13009 peak to peak on the first frame, so a decision cannot be taken
 * until that has passed. After it, a silent loader is *exactly* flat, 0 peak to
 * peak, and the first frame of music is 11094. A threshold of 128 sits three
 * orders of magnitude below the music and above nothing at all, which is why it
 * does not matter that it is not tuned finely.
 */
const SKIP_SETTLE_FRAMES = 50;    /* about a second, well past the ring down */
const SKIP_THRESHOLD     = 128;   /* int16 peak to peak, about -54 dBFS */
/* Give up after this much emulated silence and play on at one times speed.
 *
 * It has to exist, because a tune that makes no sound at all would otherwise be
 * run through at speed for its whole length.
 *
 * The tune this was first written against, `Beisikki_Demo_BASIC.sid`, turned out
 * **not** to be silent: it was being silenced by a bug of ours, where attaching
 * the software SID after a program had already set its registers built a chip
 * with the volume at zero. Fixed in `ResidFpSidBackend::attach()`. It is worth
 * remembering as the shape of the mistake: a skip that runs through a tune is
 * suspicious of the *player* first, not of the tune.
 *
 * Giving up is not free: the tune's clock is now this far in, so a five minute
 * tune has that much less to play. Ninety seconds is chosen against the case
 * this feature is for, loaders of "up to sixty seconds", with margin, and
 * against the cost of being wrong, which is a minute and a half of a tune that
 * was not going to be heard anyway. */
const SKIP_MAX_SECONDS   = 90;
/* How long one call may spend on this. It runs on the main thread, and the
 * worklet asks for samples about every 11 ms, so this is the share of the thread
 * the skip takes: enough to run perhaps fifty frames a call, tens of times
 * faster than real time, while leaving the page usable. */
const SKIP_BUDGET_MS     = 8;
/* How often the synthesis is switched back on for one frame to look at the
 * output, when nothing else suggests looking.
 *
 * Running through a lead-in costs 0.39 ms a frame with the synthesis off and
 * 3.81 ms with it on (temp/cost.mjs), so what this interval really sets is the
 * speed: one probe in eight is about 24x real time, one in four about 16x.
 *
 * It is a floor, not the rule. Every frame that writes the SID is probed
 * whichever frame it falls on, because a write is the only way a silent chip
 * can start making a noise and the counter is free to read. Measured on
 * c64_mp3.sid, whose loader writes the SID exactly **zero** times in its 11 s
 * lead-in and then 38 times on the frame the music starts, so in practice the
 * music is found on the frame it begins. The interval is what catches the
 * cases that are not like that. */
const SKIP_PROBE_EVERY   = 8;

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
    this.gain = null;      /* the volume stage, built with the graph */
    this._volume = 1;      /* survives a host setting it before there is one */
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
    this._filling = false;
    this._hidden = false;
    this._onVisibility = null;
    this._keepAlive = 0;
    this._handedOver = false;
    /* Running through a silent lead-in: see _skipSilence(). Per tune, set in
     * run(), and never set again once the tune has made a sound. */
    this._skipping = false;
    this._skippedFrames = 0;
    this._starveBase = 0;
    this._skipSince = 0;      /* frames since the output was last looked at */
    this._skipWrites = 0;     /* the SID write count at the last frame */
    this._skipFrom = null;    /* the frame the skip began at, once it has */
    /* Called with a reason when a skip ends, for a host that wants to say so. */
    this.onSkipEnd = null;
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
      /* flush and not discard: the worklet's own ring has to go too, or the
       * previous tune plays on and the new one waits behind it. */
      this.flush();
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

    /* Target a fifth of a second while the page is visible: long enough to ride
     * out a slow frame on a two or three chip tune, short enough that a stop is
     * not heard a beat later.
     *
     * Capacity is four seconds, well beyond that, because the target is raised
     * to seconds while the page is hidden and the ring has to be able to hold
     * it. See setTarget(). Four seconds of mono 48 kHz floats is 768 kB, which
     * is nothing beside the wasm heap. */
    const target = Math.max(2048, Math.round(rate * VISIBLE_SECONDS));
    this.node = new AudioWorkletNode(this.ctx, 'usp-audio', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [1],
      processorOptions: { capacity: Math.max(8192, rate * 4), target },
    });
    this._target = target;
    this.node.port.onmessage = (e) => this._onReport(e.data);
    /* A processor that throws is removed and simply stops being called, with no
     * other sign than the audio ending. Saying so beats silence. */
    this.node.onprocessorerror = (e) => {
      console.error('[usplayer-audio] the worklet processor failed', e);
    };
    /* Through a gain stage rather than straight out, so a host with a volume
     * control has something to turn. The emulation cannot do it: the tune owns
     * $d418 and writes it constantly, a digi tune hundreds of times a frame, so
     * anything written there is gone by the next frame and scaling it would
     * change the tune rather than its loudness. This is after the synthesis and
     * affects nothing the emulation does. Unity by default, so a host that never
     * touches it hears exactly what it heard before. */
    this.gain = this.ctx.createGain();
    this.gain.gain.value = this._volume;
    this.node.connect(this.gain);
    this.gain.connect(this.ctx.destination);

    this._ptr = M._usp_alloc(this._max * 2);
    return true;
  }

  /**
   * How loud, 0 to 1.
   *
   * Remembered when it is set before there is a graph to set it on, which is
   * what happens when a host restores a stored volume during startup.
   *
   * @param {number} value 0 for silence, 1 for the synthesis as it comes out
   */
  setVolume(value) {
    const v = Math.max(0, Math.min(1, Number(value)));
    this._volume = isNaN(v) ? 1 : v;
    if (this.gain) this.gain.gain.value = this._volume;
  }

  /**
   * A report from the worklet: how full its ring is, and how much silence it
   * has had to invent.
   *
   * A method rather than the closure it used to be so that it can be driven
   * without an AudioWorklet. No headless browser will run one (`addModule`
   * never completes), so the whole flow control below was untestable, and it is
   * the part that has been got wrong twice: once by acting on stale reports and
   * once by rendering nothing at all during a silent passage. See
   * temp/ring_sim.mjs, which drives this against the real processor source.
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
      /* How short the ring is, counting what is already on its way to it.
       *
       * `d.need` alone is what the ring was short of *when the report was
       * posted*. That is fine while the page is keeping up and wrong the
       * moment it is not: anything that blocks the main thread, rendering a
       * directory change for instance, leaves several reports queued, and
       * each one is then acted on as though the ring were still empty. The
       * ring fills, the worklet drops what will not fit, and dropped samples
       * are heard as the tune skipping ahead: "changing directories while
       * playing speeds it up".
       *
       * Subtracting what has been posted since the last report makes a stale
       * report harmless: it can only ever ask for what is genuinely missing. */
      const owed = this._target - d.queued - this._sentSince;
      this._sentSince = 0;
      this._owed = owed > 0 ? owed : 0;
      /* While a lead-in is being skipped there is nothing on its way to the ring
       * and the ring is not being asked for, so the report that starts the skip
       * off may well say it wants nothing. Run anyway: this is the only thing
       * that drives the skip forward. */
      if (this._owed > 0 || this._skipping) this._fill();
    }
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
      /* Samples the worklet has been sent but has not reported back on yet.
       * See the message handler: without this, a report that was already in
       * flight when these were posted is read as if the ring were still that
       * empty, and the ring is filled twice over. */
      this._sentSince += n;
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
    this._watchVisibility();
    /* Eight frames, about 160 ms of audio, is the most this will emulate in one
     * go. It runs on the main thread, so the cap is what stops a slow tune from
     * blocking the page for as long as it takes to fill a whole buffer. */
    this._maxSteps = opts.maxSteps || (this._hidden ? HIDDEN_STEPS : VISIBLE_STEPS);
    this._owed = 0;
    /* Arm the lead-in skip for this tune. Off when a worker owns the ring: the
     * filling happens there and this thread is not in the path.
     *
     * `skipSilence: false` turns it off for a caller that wants every second of
     * a tune played, whatever it sounds like. */
    this._skipping = (opts.skipSilence !== false) && !this._handedOver;
    this._skippedFrames = 0;
    this._starveBase = 0;
    this._skipSince = 0;
    this._skipFrom = null;
    this._skipWrites = player.sidWrites();
    /* A previous tune may have been stopped mid skip. */
    if (typeof this.M._usp_audio_render === 'function') this.M._usp_audio_render(1);
    /* Prime it, so playback does not start on an empty ring and a starve. */
    this._owed = Math.max(2048, Math.round(this.ctx.sampleRate * 0.2));
    this._fill();
  }

  /**
   * Hand the ring over to a worker, and stop driving it from here.
   *
   * Returns the worker's end of a MessageChannel. The processor gets the other
   * end and from then on the two talk directly: reports out, samples back, with
   * the main thread not in the path. That is the whole point, because a hidden
   * page's main thread is throttled to the point where it cannot keep a ring
   * fed and the audio thread is never throttled at all.
   *
   * The page keeps its own port for commands and for the stats it displays, so
   * nothing on screen changes.
   *
   * @returns {MessagePort|null} to be transferred to the worker
   */
  handOver() {
    if (!this.node) return null;
    /* Nothing on this thread should be clocking a player any more: two things
     * filling one ring would each see the other's samples as their own. */
    this.unrun();
    const ch = new MessageChannel();
    this.node.port.postMessage({ cmd: 'peer', port: ch.port2 }, [ch.port2]);
    this._handedOver = true;
    return ch.port1;
  }

  /** True once a worker owns the ring. */
  get handedOver() { return !!this._handedOver; }

  /**
   * How much audio to keep ready, in seconds, and how much work one fill may
   * do. Sent to the worklet, which only changes the level it asks for: nothing
   * already rendered is touched.
   */
  setTarget(seconds, maxSteps) {
    if (!this.node || !this.ctx) return;
    const want = Math.max(2048, Math.round(this.ctx.sampleRate * seconds));
    this._target = want;
    this._maxSteps = maxSteps;
    this.node.port.postMessage({ cmd: 'target', value: want });
    /* The worker keeps its own idea of how full to keep the ring, since it is
     * the side doing the filling. Whoever handed the ring over sets this. */
    if (this.onTargetChange) this.onTargetChange(want, maxSteps);
  }

  /**
   * Follow the page going away and coming back.
   *
   * Hidden, the main thread is throttled and the audio thread is not, so the
   * ring has to be deep enough to survive between whatever callbacks we still
   * get. Visible, a deep buffer would mean a stop is heard two seconds late, so
   * it goes back to a fifth of a second: the ring drains to the new level by
   * itself, which is why nothing needs to be discarded here.
   *
   * `visibilitychange` rather than blur or focus: it is the event that fires
   * for the screen going off and for switching app, which is the case being
   * fixed, and it does not fire for merely clicking another window.
   */
  _watchVisibility() {
    if (this._onVisibility || typeof document === 'undefined') return;
    this._onVisibility = () => {
      const hidden = (document.visibilityState === 'hidden');
      if (hidden === this._hidden) return;
      this._hidden = hidden;
      this.setTarget(hidden ? HIDDEN_SECONDS : VISIBLE_SECONDS,
                     hidden ? HIDDEN_STEPS : VISIBLE_STEPS);
      /* Going away, fill the deeper buffer now, while the thread still runs at
       * full speed. Waiting for the worklet to ask means the first request
       * arrives already throttled, which is the gap being closed. */
      if (hidden && this._driven) {
        this._owed = this._target;
        this._fill();
      }
      /* A worker feeds the ring itself and is not throttled the way this thread
       * is, so none of the main thread's compensation applies to it. The target
       * still moves, because a deeper ring is worth having either way. */
      if (hidden && !this._handedOver) this._startKeepAlive();
      else this._stopKeepAlive();
    };
    document.addEventListener('visibilitychange', this._onVisibility);
  }

  /**
   * Top the ring up on a timer while the page is hidden.
   *
   * The worklet asking for samples is the flow control, and it is enough while
   * the page is visible. Hidden, those messages land on a throttled main thread
   * and may be delivered late or in bursts, which is precisely when the ring is
   * emptying. A timer is throttled too, to about once a second, but once a
   * second is plenty when a single fill may render two seconds.
   *
   * The arithmetic is the message handler's, so it cannot over-produce: what is
   * wanted, less what is already on its way. The worklet drops anything that
   * will not fit anyway, and dropped samples are heard.
   */
  _startKeepAlive() {
    if (this._keepAlive) return;
    this._keepAlive = setInterval(() => {
      if (!this._driven || this._filling) return;
      const owed = this._target - this.queued - this._sentSince;
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

  /** Stop clocking a player. The context and node stay up. */
  unrun() {
    this._driven = null;
    this._owed = 0;
    this._sentSince = 0;
    this._stopKeepAlive();
    /* A skip in progress leaves the synthesis switched off between slices, and
     * letting go of the player is one of the ways a skip ends without reaching
     * its own end. Switching it back on here means the flag can only ever be off
     * while a skip is actually running: it is a global on the backend, and
     * everything after it would otherwise be silent. */
    this._skipping = false;
    if (this.M && typeof this.M._usp_audio_render === 'function') {
      this.M._usp_audio_render(1);
    }
  }

  /**
   * Emulate until the worklet has what it asked for.
   *
   * Bounded by `maxSteps` because this runs on the main thread: a tune that
   * cannot be synthesised in realtime would otherwise never give the page back.
   * Whatever is left over is asked for again a few quanta later, so a machine
   * that is merely slow degrades into a shorter buffer rather than a freeze.
   */
  /**
   * Take one chunk of rendered audio and say how loud it is.
   *
   * Peak to peak and not peak, because a silent chip is not a zero line: reSIDfp
   * has a DC offset that the external filter walks back towards zero over the
   * first half second, so measuring against zero would call the loudest thing in
   * the whole lead-in "sound". The difference between the extremes ignores any
   * offset, however large.
   *
   * @returns {{n: number, pp: number}} samples taken, and their peak to peak
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
   * Run through a silent lead-in, throwing its audio away.
   *
   * See the SKIP_ constants for why this exists and how the numbers were
   * arrived at. Nothing is posted to the ring while this runs, so the worklet
   * plays the silence it invents for itself, which is exactly what the tune
   * would have sounded like anyway.
   *
   * Time budgeted, and continued on the next report rather than run to
   * completion, because it is on the main thread and a minute long loader would
   * otherwise freeze the page until it finished.
   */
  _skipSilence() {
    const p = this._driven;
    const M = this.M;
    const canSkipRender = (typeof M._usp_audio_render === 'function');
    const now = () => ((typeof performance !== 'undefined') ? performance.now() : Date.now());
    const t0 = now();
    const limit = Math.round(p.refreshHz() * SKIP_MAX_SECONDS);

    try {
      /* Where the skip actually began, which is not frame zero: the first second
       * plays normally while the switch-on transient rings out. Without this the
       * settle counts as skipped and every tune reports a skip. */
      if (this._skipFrom === null) this._skipFrom = p.frames();

      while (this._skipping) {
        if (p.frames() - this._skipFrom >= limit) {
          this._endSkip(`nothing after ${SKIP_MAX_SECONDS}s, playing on`);
          return;
        }
        if (now() - t0 >= SKIP_BUDGET_MS) return;  /* more on the next report */

        /* Look at the output on this frame, or run it through blind?
         *
         * A silent chip can only start sounding if something writes to it, and
         * the write counter costs nothing to read, so a frame that wrote is
         * always looked at. The interval is the floor for everything else: an
         * older wasm with no render switch has no choice and looks at all of
         * them, which is simply the old speed. */
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
            /* The tune has started. Keep this frame: it is the first music, and
             * dropping it would clip the attack off the first note. */
            const chunk = new Int16Array(M.HEAPU8.buffer, this._ptr, n).slice();
            this.node.port.postMessage(chunk, [chunk.buffer]);
            this._sentSince += n;
            this._endSkip('sound at ' +
              (p.frames() / p.refreshHz()).toFixed(1) + 's');
            return;
          }
          if (n < this._max) break;
        }
      }
    } finally {
      /* Never leave the synthesis switched off, whatever happened above: it is
       * a global on the backend and everything that comes after would be
       * silent. */
      if (canSkipRender && !this._skipping) M._usp_audio_render(1);
    }
  }

  /** Stop skipping, and stop counting the silence against the ring. */
  _endSkip(why) {
    if (!this._skipping) return;
    this._skipping = false;
    if (typeof this.M._usp_audio_render === 'function') this.M._usp_audio_render(1);
    this._skippedFrames = (this._driven && this._skipFrom !== null)
      ? this._driven.frames() - this._skipFrom : 0;
    /* The ring ran dry for the whole skip and invented silence to fill it,
     * which is correct output and not a fault. Counting it would leave a
     * five figure starve total on the status line for the rest of the tune,
     * which is the number that says "this is not keeping up". */
    this._starveBase = this.starved;
    if (this.onSkipEnd) this.onSkipEnd(why, this._skippedFrames);
  }

  /** True while a silent lead-in is being run through. */
  get skipping() { return !!this._skipping; }

  _fill() {
    const p = this._driven;
    if (!p || !this.node || this._filling) return;
    if (p.paused) return;
    /* A silent lead-in is run through before anything is posted, and the ring is
     * left to invent its own silence meanwhile. Once it ends the normal fill
     * below runs in the same call, so the music starts without waiting for the
     * next report. */
    if (this._skipping && p.frames() >= SKIP_SETTLE_FRAMES) {
      this._filling = true;
      try { this._skipSilence(); } finally { this._filling = false; }
      if (this._skipping) return;
    }
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
    /* Silence invented while a lead-in was being skipped is not a fault and is
     * not counted: see _endSkip(). */
    const starved = Math.max(0, this.starved - this._starveBase);
    return {
      clipped,
      /* The FM/OPL side, straight from the emulation: an OPL is built inside
       * the wasm the first time a tune writes $df40, and the count staying at
       * zero simply means this tune has no FM. */
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

  /** Ask the worklet how it is doing. Answer arrives on the message handler. */
  requestStats() {
    if (this.node) this.node.port.postMessage({ cmd: 'stats' });
  }

  /**
   * Drop what the synthesis has rendered and the page has not yet sent.
   *
   * Only half of "throw the audio away": whatever has already been posted to
   * the worklet is on the audio thread and out of reach from here. Use
   * `flush()` for a hard reset and keep this for the fast forward path, where
   * the queued audio is deliberately left playing while the emulation seeks.
   */
  discard() {
    if (this.M && typeof this.M._usp_audio_discard === 'function') {
      this.M._usp_audio_discard();
    }
  }

  /**
   * Throw away everything not yet heard, both sides of the port.
   *
   * For a stop, and for loading another tune into a running context. The
   * worklet's ring holds up to a second, so without this a new tune is
   * appended behind the old one and, worse, the ring stays above target and
   * therefore asks for nothing more: the emulation advances by the priming
   * fill and then stalls. That is a switch that behaves like a queue, and each
   * further switch adds another slice behind the last.
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
    if (this.node) { try { this.node.disconnect(); } catch (_) {} this.node = null; }
    if (this.gain) { try { this.gain.disconnect(); } catch (_) {} this.gain = null; }
    if (this.ctx) { try { await this.ctx.close(); } catch (_) {} this.ctx = null; }
    if (this._url) { URL.revokeObjectURL(this._url); this._url = null; }
    if (this._ptr && this.M) { this.M._usp_free(this._ptr); this._ptr = 0; }
  }
}
