/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usplayer-worker.js
 * The emulation and the USB writes, off the main thread.
 *
 * Why: the main thread is not a reliable clock. Playing on it, the tune
 * crackled slightly while the tab was in front and audibly broke up when the
 * tab went to the back, because a backgrounded page stops feeding the board and
 * the board runs out of writes. The AudioWorklet clock already ran on the audio
 * thread, but it could only hand a tick to the main thread, which is where the
 * work was.
 *
 * So the work moves here. This is a module worker holding the same
 * `USBSIDPlayerWeb` the page would have used, with two differences:
 *
 *   - the clock reaches it directly. The page transfers one end of a
 *     MessageChannel into the AudioWorkletProcessor and the other end here, so
 *     the audio thread posts ticks straight to this worker and the main thread
 *     is not in the path at all.
 *   - the board is opened here. WebUSB permission belongs to the origin rather
 *     than to a thread, so once the page has asked once, `getDevices()` finds
 *     the board from a worker without a picker. See connectGranted().
 *
 * Web MIDI has no worker API, so ASID cannot come along; the page falls back to
 * running on the main thread for that. See usplayer-worker-client.js.
 *
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Player)
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

import { USBSIDPlayerWeb } from './usplayer-web.js';
import { createTransport } from './usbsid-transport.js';

let player = null;
let transport = null;
let clockPort = null;

/* Software audio, when the worker is synthesising rather than driving a board.
 *
 * The ring lives in the AudioWorkletProcessor and asks for what it is short of.
 * Those requests arrive on `clockPort`, the same channel a board build uses for
 * its ticks, and the samples go back down it. The main thread is not involved,
 * which is the reason any of this is here: it is throttled hard while the page
 * is hidden and the audio thread never is, so a ring fed from the main thread
 * empties with the screen off and the tune stutters. */
let audioMode = false;
let audioPtr = 0;              /* scratch in the wasm heap for take() */
let audioMax = 8192;
let audioOwed = 0;
let audioSent = 0;             /* posted since the ring last reported */
let audioSentTotal = 0;        /* posted to the ring, ever */
let audioSteps = 24;           /* frames one fill may emulate */
let audioFilling = false;
let audioRate = 48000;         /* the ring's sample rate, for the register lead */
let audioChannels = 1;         /* samples per frame from _usp_audio_take() */
let audioCpuMs = 0;            /* time spent emulating, since audioConfigure */
let audioFrames = 0;           /* frames emulated, since audioConfigure */

/* SID register mirror reported to the page in audio mode, which feeds its
 * piano, graph and register views: the page's own player never steps.
 * Four chips at most, the size of the adapter's shadow. */
const REG_CHIPS_MAX = 4;
let regChips = 1;

/* RAM pages the page is reading (memory view), appended to each register
 * report to keep the page's mirror in step with this machine. Set by memWatch. */
const MEM_PAGES_MAX = 16;
let memPages = new Uint8Array(0);
let memPtr = 0;                /* MEM_PAGES_MAX pages of scratch in the wasm heap */

/* Per voice output for an oscilloscope, set by the scope message. Taken in
 * step with the audio and sent with the following register report: the page
 * shows it when the audio it belongs to is heard. At most SCOPE_VOICES_MAX
 * values per frame: chip 1 voices 1-3 first. */
const SCOPE_VOICES_MAX = REG_CHIPS_MAX * 3;
let scopeOn = false;
let scopePtr = 0;              /* audioMax frames of scratch in the wasm heap */
let scopeVoices = 0;           /* values per frame in scopeChunks */
let scopeChunks = [];          /* Int16Array per audio chunk since the last report */
let scopeFrames = 0;

/* The longest a single audioFill() burst may run, in wall clock ms, whatever
 * audioSteps says. A many-SID tune's frame cost scales with chip count, and
 * audioSteps alone does not: at VISIBLE_STEPS (8), a 15SID tune already
 * behind on real time could spend the better part of a second inside one
 * synchronous burst, and every control message this worker is sent - a tune
 * switch among them - waits in this thread's own queue until that burst
 * returns. Bounding the burst by time as well makes that wait a few
 * milliseconds instead, on any chip count: see audioFill(). */
const AUDIO_FILL_BUDGET_MS = 8;

/** Everything the page shows, in one message rather than a call per field. */
function snapshot() {
  if (player === null) return null;
  const info = player.info();
  return {
    playing: player.isPlaying(),
    prg: player.isPrg(),
    frames: player.frames(),
    sidWrites: player.sidWrites(),
    dropped: player.droppedWrites(),
    queueDepth: (transport && transport.queueDepth) || 0,
    refreshHz: player.refreshHz(),
    name: info.name,
    author: info.author,
    released: info.released,
    song: info.song,
    songs: info.songs,
    stats: player.stats(),
    /* Read here because in audio mode the page has no player of its own to
     * ask. Emulated time, not wall clock: see USBSIDPlayerWeb.playtimeMs(). */
    playtimeMs: (typeof player.playtimeMs === 'function') ? player.playtimeMs() : 0,
    timing: (typeof player.timing === 'function') ? player.timing() : null,
    /* FM/OPL writes of this tune: non zero marks an FM tune, with or without
     * the v5 header flag. */
    fmWrites: (audioMode && typeof player.M._usp_audio_fm_writes === 'function')
      ? player.M._usp_audio_fm_writes() : 0,
    /* Mean ms of emulation per frame since the tune was configured, against a
     * frame's budget of about 20 ms; audio mode only. */
    msPerFrame: audioFrames > 0 ? audioCpuMs / audioFrames : 0,
    clipped: (audioMode && typeof player.M._usp_audio_clipped === 'function')
      ? player.M._usp_audio_clipped() : 0,
    /* Stereo position per chip, 0 left, 1 center, 2 right: see panning. */
    pans: (audioMode && typeof player.M._usp_audio_pan === 'function')
      ? info.sids.map((_, c) => player.M._usp_audio_pan(c + 1)) : [],
  };
}

/**
 * Emulate until the ring has what it asked for, then send it.
 *
 * Bounded by `audioSteps` frames and by `AUDIO_FILL_BUDGET_MS` wall clock time,
 * whichever comes first, so one request cannot run for an unbounded time and
 * cannot hold this thread's own message queue - the one a tune switch's RPCs
 * arrive on - for longer than a few milliseconds regardless of chip count.
 * Whatever is left of `audioOwed` is asked for again on the next report,
 * possibly the very next one: a worker blocking itself briefly and often is
 * far less serious than the main thread blocking, but a tune that cannot be
 * synthesised in real time should degrade to a shorter buffer rather than to
 * a worker that is unresponsive for as long as it takes to catch up.
 */
function audioFill() {
  if (!audioMode || player === null || audioFilling || !clockPort) return 0;
  audioFilling = true;
  let steps = 0;
  try {
    const M = player.M;
    const deadline = performance.now() + AUDIO_FILL_BUDGET_MS;
    while (audioOwed > 0 && steps < audioSteps) {
      const t0 = performance.now();
      player.stepAndDrain();
      audioCpuMs += performance.now() - t0;
      audioFrames++;
      steps++;
      for (;;) {
        const n = M._usp_audio_take(audioPtr, audioMax);
        if (n <= 0) break;
        /* A copy, because the heap view is reused on the next call and may be
         * detached entirely if the heap grows. Transferred, so the audio thread
         * does not copy it again. */
        const chunk = new Int16Array(M.HEAPU8.buffer, audioPtr, n * audioChannels).slice();
        clockPort.postMessage(audioChannels === 2 ? { stereo: chunk } : chunk, [chunk.buffer]);
        if (scopeOn) takeScope(n);
        audioSent += n;
        audioSentTotal += n;
        audioOwed -= n;
        if (n < audioMax) break;
      }
      if (performance.now() >= deadline) break;
    }
  } finally {
    audioFilling = false;
  }
  return steps;
}

/**
 * Take the scope frames belonging to the last audio chunk sent.
 *
 * @param {number} frames  frames in that audio chunk
 */
function takeScope(frames) {
  const M = player.M;
  const v = Math.min(SCOPE_VOICES_MAX, M._usp_audio_scope_voices() | 0);
  if (v === 0) return;
  if (v !== scopeVoices) { scopeChunks = []; scopeFrames = 0; scopeVoices = v; }
  const k = M._usp_audio_scope_take(scopePtr, frames, v);
  if (k <= 0) return;
  scopeChunks.push(new Int16Array(M.HEAPU8.buffer, scopePtr, k * v).slice());
  scopeFrames += k;
}

/**
 * Join the scope chunks taken since the last report and start over.
 *
 * @returns {Int16Array|null} frames * scopeVoices values, null when none
 */
function drainScope() {
  if (scopeFrames === 0) return null;
  const out = new Int16Array(scopeFrames * scopeVoices);
  let at = 0;
  for (const c of scopeChunks) { out.set(c, at); at += c.length; }
  scopeChunks = [];
  scopeFrames = 0;
  return out;
}

/**
 * Send the SID register mirror to the page, with how far ahead of the audible
 * output it is.
 *
 * @param {number} leadSamples  samples between the ring's playhead and the
 *                              point the emulation has reached
 */
function postRegisters(leadSamples) {
  const regs = new Uint8Array(regChips * 32);
  for (let c = 0; c < regChips; c++) {
    for (let r = 0; r < 32; r++) regs[(c << 5) | r] = player.sidRegister(c + 1, r) & 0xff;
  }
  const payload = {
    regs,
    ciaLatch: player.ciaLatch(1, 0),
    leadMs: (leadSamples * 1000) / audioRate,
  };
  const transfer = [regs.buffer];
  const scope = scopeOn ? drainScope() : null;
  if (scope) {
    payload.scope = scope;
    payload.scopeVoices = scopeVoices;
    transfer.push(scope.buffer);
  }
  const mem = readWatchedPages();
  if (mem) {
    payload.mem = mem;
    payload.memPages = memPages.slice();
    transfer.push(mem.buffer, payload.memPages.buffer);
  }
  self.postMessage({ type: 'regs', payload }, transfer);
}

/**
 * Read the watched RAM pages, in memPages order.
 *
 * @returns {Uint8Array|null} 256 bytes per page, null when none are watched
 */
function readWatchedPages() {
  const n = memPages.length;
  if (n === 0) return null;
  const M = player.M;
  const mem = new Uint8Array(n * 256);
  if (typeof M._usp_read_memory_block === 'function') {
    if (!memPtr) memPtr = M._usp_alloc(MEM_PAGES_MAX * 256);
    for (let k = 0; k < n; k++) {
      M._usp_read_memory_block(memPtr + (k << 8), memPages[k] << 8, 256);
    }
    mem.set(new Uint8Array(M.HEAPU8.buffer, memPtr, n * 256));
  } else {
    for (let k = 0; k < n; k++) {
      const base = memPages[k] << 8;
      for (let i = 0; i < 256; i++) mem[(k << 8) | i] = player.readMemory(base + i) & 0xff;
    }
  }
  return mem;
}

/**
 * Apply a speed multiplier to the player and, in audio mode, to the
 * synthesis output rate. See usp_audio_set_speed().
 *
 * @param {number} mult speed multiplier, 1 is normal
 */
function applySpeed(mult) {
  player.setSpeed(mult);
  applyAudioSpeed();
}

/** Follow the player's speed with the synthesis output rate, audio mode only. */
function applyAudioSpeed() {
  if (audioMode && typeof player.M._usp_audio_set_speed === 'function') {
    player.M._usp_audio_set_speed(player.speed);
  }
}

/** A report from the ring: how short it is, counting what is already on its way. */
function onAudioReport(d) {
  if (!d || typeof d.queued !== 'number') return;
  /* In flight from the ring's own received count when it has one: a backlog
   * of stale reports after the worker was throttled then asks for the
   * shortfall once, not once per report. */
  const inFlight = (typeof d.received === 'number')
    ? Math.max(0, audioSentTotal - d.received) : audioSent;
  const owed = audioTarget - d.queued - inFlight;
  audioSent = 0;
  audioOwed = owed > 0 ? owed : 0;
  if (audioOwed > 0 && audioFill() > 0) {
    postRegisters(d.queued + ((typeof d.received === 'number')
      ? Math.max(0, audioSentTotal - d.received) : audioSent));
  }
  /* The page has no player of its own in this mode, so everything it displays
   * comes from here. Reports arrive about ninety times a second; this is three
   * times a second, which is what a clock and a status line need. */
  if (++sinceReport >= 32) {
    sinceReport = 0;
    post('state', snapshot());
  }
}

let audioTarget = 8192;

function post(type, payload, id) {
  self.postMessage({ type, payload, id });
}

/* The tick has to be cheap and must never throw into the audio thread's port,
 * so the state the page wants is sampled on a slow beat rather than every one
 * of the ~344 ticks a second. */
let sinceReport = 0;
/* Report once, so "did a tick ever arrive" is answerable from the page log
 * rather than only from a debugger. Every way of losing the clock looks the
 * same from outside: nothing plays and nothing complains. */
let firstTick = true;

function onTick() {
  if (player === null) return;
  if (firstTick) {
    firstTick = false;
    post('log', { message: 'first tick arrived, the clock is running' });
  }
  player.tick();
  if (++sinceReport >= 32) {
    sinceReport = 0;
    post('state', snapshot());
  }
}

let fallbackTimer = 0;

const handlers = {
  /**
   * Load the wasm and open the board. `wasmUrl` is passed in rather than
   * hard coded because the page decides where the build artefacts live.
   */
  async init({ wasmUrl, prefer }) {
    const { default: USBSIDPlayer } = await import(wasmUrl);
    const M = await USBSIDPlayer();
    /* WebUSB where it exists, Web Serial where it does not, which is Firefox.
     * Both re-acquire what the page was granted without a picker. `prefer`
     * overrides, which is how bench.html measures one against the other. */
    transport = createTransport({ prefer });
    post('log', { message: 'transport built: ' +
                           (transport ? transport.kind : 'none') });
    const opened = transport ? await transport.connectGranted() : false;
    post('log', { message: 'board ' + (opened ? 'opened' : 'NOT opened') });
    player = new USBSIDPlayerWeb(M, transport);
    /* The worker owns the board here, so it is the only one that can ask what
     * is in it. Done at init, before any tune loads.
     *
     * Never let it stop the worker coming up: reading the board is a
     * convenience, and without it the sockets default and FM/OPL is off,
     * which is a worse tune, not a dead player. */
    let board = null;
    if (opened) {
      try { board = await player.applyBoardConfig(); }
      catch (err) { post('log', { message: 'board config read failed: ' + err }); }
      post('log', { message: 'board config: ' + JSON.stringify(board) });
    }
    return { opened, board, transport: transport ? transport.kind : null };
  },

  /**
   * Pace the worker from its own timer instead of the audio thread.
   *
   * The audio clock is the one that matters, because a worker's timers are
   * throttled in a backgrounded tab and that is the whole reason the worker
   * exists. This is the fallback for when no ticks arrive at all: a page with no
   * Web Audio, or an AudioContext the browser never allowed to start, or a
   * browser that will not hand a MessagePort to an AudioWorkletProcessor.
   * Without it, any of those is silence with nothing logged.
   */
  fallbackClock({ on }) {
    if (fallbackTimer) { clearInterval(fallbackTimer); fallbackTimer = 0; }
    if (on) fallbackTimer = setInterval(onTick, 4);
    post('log', { message: 'fallback timer ' + (fallbackTimer ? 'on' : 'off') });
    return { ok: true, on: !!fallbackTimer };
  },

  /** The audio thread's end of the clock. */
  clock({ port }) {
    /* If a browser will not transfer a MessagePort this far, `port` is not one
     * and the silence starts here rather than at the audio thread. Worth saying
     * which, because the two need different fixes. */
    const kind = Object.prototype.toString.call(port);
    post('log', { message: 'clock port received: ' + kind });
    if (!port || typeof port.postMessage !== 'function') {
      return { ok: false, kind };
    }
    clockPort = port;
    /* In audio mode the same channel carries the ring's reports one way and the
     * samples the other. A board build gets a bare tick and nothing else. */
    clockPort.onmessage = audioMode
      ? (e) => onAudioReport(e.data)
      : onTick;
    clockPort.start && clockPort.start();
    return { ok: true, kind };
  },

  /**
   * Come up with no board at all: reSIDfp in this worker, out through the
   * page's AudioWorklet.
   *
   * Separate from init() because that one builds a transport and opens a board,
   * which is exactly what this mode does not want: a picker, a permission and a
   * device that would sit claimed for nothing.
   */
  async audioInit({ wasmUrl }) {
    const { default: USBSIDPlayer } = await import(wasmUrl);
    const M = await USBSIDPlayer();
    transport = null;
    player = new USBSIDPlayerWeb(M);
    audioMode = true;
    audioPtr = M._usp_alloc(audioMax * 2 * 2);   /* room for stereo frames */
    post('log', { message: 'worker audio: wasm up, no board' });
    return { ok: true, audio: true };
  },

  /**
   * Point the synthesis at a tune, and say how full to keep the ring.
   *
   * `target` and `steps` come from the page because it is the side that knows
   * whether it is visible: hidden, it asks for a deeper ring and bigger fills.
   */
  audioConfigure({ chips, rate, quality, model, target, steps, stereo }) {
    if (player === null) return { ok: false };
    if (typeof player.M._usp_audio_set_stereo === 'function') {
      player.M._usp_audio_set_stereo(stereo ? 1 : 0);
    }
    const ok = !!player.M._usp_audio_configure(
      (chips || 1) | 0, rate | 0,
      (quality === undefined ? 1 : quality) | 0, (model || 0) | 0);
    if (target) audioTarget = target | 0;
    if (steps) audioSteps = steps | 0;
    if (rate) audioRate = rate | 0;
    audioChannels = (typeof player.M._usp_audio_channels === 'function')
      ? (player.M._usp_audio_channels() | 0) || 1 : 1;
    audioCpuMs = 0;
    audioFrames = 0;
    audioOwed = 0;
    audioSent = 0;
    scopeChunks = [];
    scopeFrames = 0;
    post('log', { message: 'worker audio: ' + rate + ' Hz, ' + (chips || 1) +
                           ' chip(s), target ' + audioTarget });
    return { ok };
  },

  /** A deeper ring while the page is hidden, and bigger fills to match. */
  audioTarget({ target, steps }) {
    if (target) audioTarget = target | 0;
    if (steps) audioSteps = steps | 0;
    return { ok: true, target: audioTarget, steps: audioSteps };
  },

  /** Drop what has been rendered and not yet sent, on a stop or a new tune. */
  audioDiscard() {
    if (player && player.M && player.M._usp_audio_discard) {
      player.M._usp_audio_discard();
    }
    audioOwed = 0;
    audioSent = 0;
    scopeChunks = [];
    scopeFrames = 0;
    return { ok: true };
  },

  /**
   * Place the chips in the stereo mix, see usp_audio_set_panning(). Kept
   * across tunes by the wasm.
   */
  panning({ stereo, layout, mode, single }) {
    if (player === null || typeof player.M._usp_audio_set_panning !== 'function') {
      return { ok: false };
    }
    player.M._usp_audio_set_panning(stereo ? 1 : 0, layout | 0, mode | 0, single | 0);
    return { ok: true, info: snapshot() };
  },

  /**
   * Set the reSIDfp filter, see usp_audio_set_filter(). Kept across tunes by
   * the wasm.
   */
  filter({ enabled, curve6581, range6581, curve8580, waveforms }) {
    if (player === null || typeof player.M._usp_audio_set_filter !== 'function') {
      return { ok: false };
    }
    player.M._usp_audio_set_filter(enabled ? 1 : 0, +curve6581, +range6581,
                                   +curve8580, waveforms | 0);
    return { ok: true };
  },

  /**
   * Record each voice's own output for an oscilloscope, or stop.
   *
   * Frames go out with the register reports, see postRegisters().
   */
  scope({ on }) {
    if (player === null || !audioMode ||
        typeof player.M._usp_audio_scope !== 'function') {
      return { ok: false };
    }
    const M = player.M;
    if (on && !scopePtr) scopePtr = M._usp_alloc(audioMax * SCOPE_VOICES_MAX * 2);
    scopeOn = !!on;
    scopeChunks = [];
    scopeFrames = 0;
    M._usp_audio_scope(scopeOn ? 1 : 0);
    return { ok: true, on: scopeOn };
  },

  /** The Songlengths key of what is loaded, and the lookup, both in here. */
  md5() {
    return { key: (player && player._bytesForMd5)
      ? player.md5(player._bytesForMd5) : '' };
  },

  loadSID({ bytes, subtune }) {
    const buf = new Uint8Array(bytes);
    /* Kept for md5(): the Songlengths key is the digest of the whole file, and
     * the page cannot compute one because WebCrypto leaves MD5 out. */
    player._bytesForMd5 = buf;
    const ok = player.loadSID(buf, subtune || 0);
    regChips = Math.min(REG_CHIPS_MAX, Math.max(1, player.info().sids.length));
    return { ok, info: snapshot() };
  },

  loadPRG({ bytes }) {
    const ok = player.loadPRG(new Uint8Array(bytes));
    regChips = 1;
    return { ok, info: snapshot() };
  },

  async start() {
    await player.start({ externalClock: true });
    post('log', { message: 'player.start done, playing=' + player.isPlaying() +
                           ', clock port ' + (clockPort ? 'held' : 'MISSING') });
    return { ok: true };
  },
  stop() { player.stop(); return { ok: true }; },
  pause({ on }) { player.pause(!!on); return { ok: true }; },

  /* The worker holds the player that is actually sounding, so a voice held down
   * on the page's copy would silence nothing. */
  voiceMute({ chip, voice, muted }) {
    player.setVoiceMute(chip, voice, !!muted);
    return { ok: true };
  },
  /* Same reason as voiceMute above: the worker holds the player that is sounding. */
  chipMute({ chip, muted }) {
    player.setChipMute(chip, !!muted);
    return { ok: true };
  },
  /* The SID and FM sides of the mix are scaled inside the wasm that renders,
   * which in audio mode is this one. See usp_audio_set_sid_volume(). */
  sidVolume({ percent }) {
    if (player && player.M._usp_audio_set_sid_volume) {
      player.M._usp_audio_set_sid_volume(percent | 0);
    }
    return { ok: true };
  },
  fmVolume({ percent }) {
    if (player && player.M._usp_audio_set_fm_volume) {
      player.M._usp_audio_set_fm_volume(percent | 0);
    }
    return { ok: true };
  },
  speed({ mult }) { applySpeed(mult); return { ok: true, speed: player.speed }; },
  fastForward({ on, mult }) {
    player.fastForward(!!on, mult);
    applyAudioSpeed();
    return { ok: true, speed: player.speed };
  },
  /* RAM pages to report with the registers, see readWatchedPages(). */
  memWatch({ pages }) {
    const list = Array.from(pages || []).slice(0, MEM_PAGES_MAX);
    memPages = Uint8Array.from(list, (pg) => pg & 0xff);
    return { ok: true, pages: memPages.length };
  },
  nextSubtune() { player.nextSubtune(); return { ok: true }; },
  prevSubtune() { player.prevSubtune(); return { ok: true }; },
  runStop() { return { ok: player.runStop() }; },
  forceSocketTwo() { player.forceSocketTwo(); return { ok: true }; },
  setClock({ rateId }) { player.setClock(rateId); return { ok: true }; },
  setSidConfig({ numsids, one, two, fmopl }) {
    player.setSidConfig(numsids || 0, one || 0, two || 0,
                        (fmopl === undefined) ? -1 : fmopl);
    return { ok: true, board: player.boardConfig() };
  },
  async applyBoardConfig() { return { board: await player.applyBoardConfig() }; },
  state() { return snapshot(); },
  resetStats() { player.resetStats(); return { ok: true }; },
};

self.onmessage = async (e) => {
  const { type, payload, id } = e.data || {};
  const fn = handlers[type];
  if (!fn) { post('error', { message: 'unknown message ' + type }, id); return; }
  try {
    const result = await fn(payload || {});
    post('reply', result, id);
  } catch (err) {
    post('error', { message: String(err && err.message ? err.message : err) }, id);
  }
};


