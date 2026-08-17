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
let audioSteps = 24;           /* frames one fill may emulate */
let audioFilling = false;

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
  };
}

/**
 * Emulate until the ring has what it asked for, then send it.
 *
 * Bounded by `audioSteps` so one request cannot run for an unbounded time, and
 * whatever is left is asked for again on the next report. A worker blocking
 * itself is far less serious than the main thread blocking, but a tune that
 * cannot be synthesised in real time should degrade to a shorter buffer rather
 * than to a worker that never returns.
 */
function audioFill() {
  if (!audioMode || player === null || audioFilling || !clockPort) return;
  audioFilling = true;
  try {
    const M = player.M;
    let steps = 0;
    while (audioOwed > 0 && steps < audioSteps) {
      player.stepAndDrain();
      steps++;
      for (;;) {
        const n = M._usp_audio_take(audioPtr, audioMax);
        if (n <= 0) break;
        /* A copy, because the heap view is reused on the next call and may be
         * detached entirely if the heap grows. Transferred, so the audio thread
         * does not copy it again. */
        const chunk = new Int16Array(M.HEAPU8.buffer, audioPtr, n).slice();
        clockPort.postMessage(chunk, [chunk.buffer]);
        audioSent += n;
        audioOwed -= n;
        if (n < audioMax) break;
      }
    }
  } finally {
    audioFilling = false;
  }
}

/** A report from the ring: how short it is, counting what is already on its way. */
function onAudioReport(d) {
  if (!d || typeof d.queued !== 'number') return;
  const owed = audioTarget - d.queued - audioSent;
  audioSent = 0;
  audioOwed = owed > 0 ? owed : 0;
  if (audioOwed > 0) audioFill();
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
     * Never let it stop the worker coming up. Reading the board is a
     * convenience: without it the sockets default and FM/OPL is off, which is
     * a worse tune, not a dead player. This used to be an unbounded wait and
     * it hung init for ever when the board left a config read unanswered. */
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
    audioPtr = M._usp_alloc(audioMax * 2);
    post('log', { message: 'worker audio: wasm up, no board' });
    return { ok: true, audio: true };
  },

  /**
   * Point the synthesis at a tune, and say how full to keep the ring.
   *
   * `target` and `steps` come from the page because it is the side that knows
   * whether it is visible: hidden, it asks for a deeper ring and bigger fills.
   */
  audioConfigure({ chips, rate, quality, model, target, steps }) {
    if (player === null) return { ok: false };
    const ok = !!player.M._usp_audio_configure(
      (chips || 1) | 0, rate | 0,
      (quality === undefined ? 1 : quality) | 0, (model || 0) | 0);
    if (target) audioTarget = target | 0;
    if (steps) audioSteps = steps | 0;
    audioOwed = 0;
    audioSent = 0;
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
    return { ok: true };
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
    return { ok, info: snapshot() };
  },

  loadPRG({ bytes }) {
    const ok = player.loadPRG(new Uint8Array(bytes));
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
  speed({ mult }) { player.setSpeed(mult); return { ok: true }; },
  fastForward({ on, mult }) { player.fastForward(!!on, mult); return { ok: true }; },
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


