/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * usplayer-worker.js
 * Emulation and USB writes in a worker, off the main thread
 *
 * A backgrounded page on the main thread stops feeding the board, the tune breaks up
 *
 * Module worker holding the USBSIDPlayerWeb the page would have used
 *   - clock ticks come straight from the audio thread over a MessageChannel
 *   - board is opened in the worker, WebUSB permission belongs to the origin,
 *     getDevices() finds the board without a picker, see connectGranted()
 *
 * Web MIDI has no worker API, ASID runs on the main thread, see usplayer-worker-client.js
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

import { USBSIDPlayerWeb } from './usplayer-web.js';
import { createTransport } from './usbsid-transport.js';

let player = null;
let transport = null;
let clockPort = null;

/**
 * Instantiate the wasm module, usbsid.wasm is fetched with the query of wasmUrl
 * (the loader skips the cache buster, a cached older wasm breaks the export names)
 *
 * @param {string} wasmUrl  URL of usbsid.esm.js, cache buster included
 */
async function loadModule(wasmUrl) {
  const { default: USBSIDPlayer } = await import(wasmUrl);
  const base = new URL(wasmUrl, self.location.href);
  return USBSIDPlayer({
    locateFile: (p) => {
      const u = new URL(p, base);
      u.search = base.search;
      return u.href;
    },
  });
}

/* Software audio (worker synthesises, no board)
 * The ring lives in the AudioWorkletProcessor and requests samples on clockPort
 * Samples go back on the same port, the main thread is throttled when the page is hidden */
let audioMode = false;
let audioPtr = 0;              /* scratch in the wasm heap for take() */
let audioMax = 8192;
let audioOwed = 0;
let audioSent = 0;             /* posted since the ring last reported */
let audioSentTotal = 0;        /* posted to the ring, ever */
let audioSteps = 24;           /* max frames per fill */
let audioFilling = false;
let audioRate = 48000;         /* ring sample rate, for the register lead */
let audioChannels = 1;         /* samples per frame from _usp_audio_take() */
let audioCpuMs = 0;            /* emulation time since audioConfigure */
let audioFrames = 0;           /* frames emulated since audioConfigure */

/* SID register mirror for the page in audio mode (piano, graph and register views), 4 chips max */
const REG_CHIPS_MAX = 4;
let regChips = 1;

/* RAM pages for the memory view, appended to each register report, set by memWatch */
const MEM_PAGES_MAX = 16;
let memPages = new Uint8Array(0);
let memPtr = 0;                /* MEM_PAGES_MAX pages of scratch in the wasm heap */

/* Per voice scope output, set by the scope message
 * Taken with the audio and sent with the following register report
 * SCOPE_VOICES_MAX values per frame max, chip 1 voices 1-3 first */
const SCOPE_VOICES_MAX = REG_CHIPS_MAX * 3;
let scopeOn = false;
let scopePtr = 0;              /* audioMax frames of scratch in the wasm heap */
let scopeVoices = 0;           /* values per frame in scopeChunks */
let scopeChunks = [];          /* Int16Array per audio chunk since the last report */
let scopeFrames = 0;

/* Max wall clock ms of one audioFill() burst, frame cost scales with chip count
 * A long burst delays control messages (tune switch), see audioFill() */
const AUDIO_FILL_BUDGET_MS = 8;

/* All page state in one message */
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
    /* Emulated time (see USBSIDPlayerWeb.playtimeMs()), read in the worker, the page has no player in audio mode */
    playtimeMs: (typeof player.playtimeMs === 'function') ? player.playtimeMs() : 0,
    timing: (typeof player.timing === 'function') ? player.timing() : null,
    /* FM/OPL writes of this tune, non zero = FM tune (with or without the v5 header flag) */
    fmWrites: (audioMode && typeof player.M._usp_audio_fm_writes === 'function')
      ? player.M._usp_audio_fm_writes() : 0,
    /* Mean ms of emulation per frame since configure (budget about 20 ms), audio mode only */
    msPerFrame: audioFrames > 0 ? audioCpuMs / audioFrames : 0,
    clipped: (audioMode && typeof player.M._usp_audio_clipped === 'function')
      ? player.M._usp_audio_clipped() : 0,
    /* Stereo position per chip, 0 left, 1 center, 2 right */
    pans: (audioMode && typeof player.M._usp_audio_pan === 'function')
      ? info.sids.map((_, c) => player.M._usp_audio_pan(c + 1)) : [],
  };
}

/*
 * Emulate until the ring has what it asked for, then send it
 * Limited by audioSteps frames and AUDIO_FILL_BUDGET_MS, whichever comes first
 * The remainder of audioOwed is requested again on the following report
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
        /* Copy, the heap view is reused and detaches when the heap grows, transferred to the audio thread */
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
 * Take the scope frames of the last audio chunk sent
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

/* Join the scope chunks taken since the last report, returns frames * scopeVoices values or null */
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
 * Send the SID register mirror to the page with its lead on the audible output
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

/* Read the watched RAM pages in memPages order, returns 256 bytes per page or null */
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
 * Apply a speed multiplier to the player and in audio mode to the synthesis output rate
 * See usp_audio_set_speed()
 *
 * @param {number} mult speed multiplier, 1 is normal
 */
function applySpeed(mult) {
  player.setSpeed(mult);
  applyAudioSpeed();
}

/* Follow the player speed with the synthesis output rate, audio mode only */
function applyAudioSpeed() {
  if (audioMode && typeof player.M._usp_audio_set_speed === 'function') {
    player.M._usp_audio_set_speed(player.speed);
  }
}

/* Ring report, how short it is minus what is already on its way */
function onAudioReport(d) {
  if (!d || typeof d.queued !== 'number') return;
  /* In flight from the received count of the ring, a backlog of stale reports asks once */
  const inFlight = (typeof d.received === 'number')
    ? Math.max(0, audioSentTotal - d.received) : audioSent;
  const owed = audioTarget - d.queued - inFlight;
  audioSent = 0;
  audioOwed = owed > 0 ? owed : 0;
  if (audioOwed > 0 && audioFill() > 0) {
    postRegisters(d.queued + ((typeof d.received === 'number')
      ? Math.max(0, audioSentTotal - d.received) : audioSent));
  }
  /* The page displays this worker state, about 90 reports per second, post 3 per second */
  if (++sinceReport >= 32) {
    sinceReport = 0;
    post('state', snapshot());
  }
}

let audioTarget = 8192;

function post(type, payload, id) {
  self.postMessage({ type, payload, id });
}

/* The tick must be cheap and never throw, state is sampled every 32 of the ~344 ticks per second */
let sinceReport = 0;
/* Log the first tick once */
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
  /* Load the wasm and open the board, wasmUrl is set by the page */
  async init({ wasmUrl, prefer }) {
    const M = await loadModule(wasmUrl);
    /* WebUSB, else Web Serial (Firefox), both re-acquire the granted device without a picker
     * prefer overrides (bench.html) */
    transport = createTransport({ prefer });
    post('log', { message: 'transport built: ' +
                           (transport ? transport.kind : 'none') });
    const opened = transport ? await transport.connectGranted() : false;
    post('log', { message: 'board ' + (opened ? 'opened' : 'NOT opened') });
    player = new USBSIDPlayerWeb(M, transport);
    /* Read the board config at init before any tune loads
     * A failure must not stop the worker, sockets default and FM/OPL is off */
    let board = null;
    if (opened) {
      try { board = await player.applyBoardConfig(); }
      catch (err) { post('log', { message: 'board config read failed: ' + err }); }
      post('log', { message: 'board config: ' + JSON.stringify(board) });
    }
    return { opened, board, transport: transport ? transport.kind : null };
  },

  /*
   * Fallback clock, a worker timer (throttled in a background tab)
   * For no Web Audio, an AudioContext that never starts or a refused MessagePort transfer
   */
  fallbackClock({ on }) {
    if (fallbackTimer) { clearInterval(fallbackTimer); fallbackTimer = 0; }
    if (on) fallbackTimer = setInterval(onTick, 4);
    post('log', { message: 'fallback timer ' + (fallbackTimer ? 'on' : 'off') });
    return { ok: true, on: !!fallbackTimer };
  },

  /* Audio thread end of the clock */
  clock({ port }) {
    /* port is not a MessagePort when the browser refuses the transfer */
    const kind = Object.prototype.toString.call(port);
    post('log', { message: 'clock port received: ' + kind });
    if (!port || typeof port.postMessage !== 'function') {
      return { ok: false, kind };
    }
    clockPort = port;
    /* Audio mode, ring reports one way and samples the other, a board build gets bare ticks */
    clockPort.onmessage = audioMode
      ? (e) => onAudioReport(e.data)
      : onTick;
    clockPort.start && clockPort.start();
    return { ok: true, kind };
  },

  /* Start without a board, reSIDfp in this worker out through the page AudioWorklet */
  async audioInit({ wasmUrl }) {
    const M = await loadModule(wasmUrl);
    transport = null;
    player = new USBSIDPlayerWeb(M);
    audioMode = true;
    audioPtr = M._usp_alloc(audioMax * 2 * 2);   /* room for stereo frames */
    post('log', { message: 'worker audio: wasm up, no board' });
    return { ok: true, audio: true };
  },

  /* Point the synthesis at a tune and set the ring fill, target and steps come from the page (hidden = deeper ring, bigger fills) */
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

  /* Deeper ring and bigger fills while the page is hidden */
  audioTarget({ target, steps }) {
    if (target) audioTarget = target | 0;
    if (steps) audioSteps = steps | 0;
    return { ok: true, target: audioTarget, steps: audioSteps };
  },

  /* Drop rendered audio not yet sent, on stop or new tune */
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

  /* Place the chips in the stereo mix, see usp_audio_set_panning(), kept across tunes by the wasm */
  panning({ stereo, layout, mode, single }) {
    if (player === null || typeof player.M._usp_audio_set_panning !== 'function') {
      return { ok: false };
    }
    player.M._usp_audio_set_panning(stereo ? 1 : 0, layout | 0, mode | 0, single | 0);
    return { ok: true, info: snapshot() };
  },

  /* Set the reSIDfp filter, see usp_audio_set_filter(), kept across tunes by the wasm */
  filter({ enabled, curve6581, range6581, curve8580, waveforms }) {
    if (player === null || typeof player.M._usp_audio_set_filter !== 'function') {
      return { ok: false };
    }
    player.M._usp_audio_set_filter(enabled ? 1 : 0, +curve6581, +range6581,
                                   +curve8580, waveforms | 0);
    return { ok: true };
  },

  /* Record the output of each voice for the oscilloscope or stop, frames go out with the register reports */
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

  /* Songlengths key of the loaded tune */
  md5() {
    return { key: (player && player._bytesForMd5)
      ? player.md5(player._bytesForMd5) : '' };
  },

  loadSID({ bytes, subtune, song }) {
    const buf = new Uint8Array(bytes);
    /* Kept for md5(), the Songlengths key is the digest of the file (WebCrypto has no MD5) */
    player._bytesForMd5 = buf;
    const ok = player.loadSID(buf, subtune || 0, song || 0);
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

  /* The worker holds the sounding player */
  voiceMute({ chip, voice, muted }) {
    player.setVoiceMute(chip, voice, !!muted);
    return { ok: true };
  },
  /* Chip mute, see voiceMute */
  chipMute({ chip, muted }) {
    player.setChipMute(chip, !!muted);
    return { ok: true };
  },
  /* SID and FM volume are scaled in the rendering wasm, see usp_audio_set_sid_volume() */
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
  /* RAM pages to report with the registers, see readWatchedPages() */
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


