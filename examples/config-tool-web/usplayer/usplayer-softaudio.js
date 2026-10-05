/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usplayer-softaudio.js
 * Oscilloscope, stereo panning and filter of software audio (reSIDfp in the
 * worker), shared by usplayer-adapter.js and usplayer-adapter-deepsid.js.
 *
 * applySoftAudio() adds the methods to an adapter class. They use the
 * adapter's own `_isAudio`, `_worker`, `_call()`, `_drainRegisters()`,
 * `_audio`, `_player` and `_snap`. The adapter calls `_softAudioInit()` in its
 * constructor, `_softAudioReplay()` after the worker's `audioConfigure`,
 * `_scopeClear()` on a stop or a new tune, and `_scopeAppend()` when it
 * applies a register report that carries `scope` frames.
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

/* Scope frames kept per voice: a power of two, a third of a second at
 * 48 kHz, more than the widest oscilloscope window. */
export const SCOPE_RING = 16384;

/** Milliseconds on the clock the adapters time register reports with. */
function _now() {
  return (typeof performance !== 'undefined') ? performance.now() : Date.now();
}

/** The methods applySoftAudio() adds; never constructed itself. */
class SoftAudio {
  /** Set up the state, from the adapter's constructor. */
  _softAudioInit() {
    /* Oscilloscope frames from the worker's reports, see setScope(). One ring
     * per voice; `_scopeEnd` counts frames ever appended, `_scopeDue` and
     * `_scopeChunk` are the audible moment and size of the latest report. */
    this._scopeWanted = false;
    this._scopeRing = [];
    this._scopeEnd = 0;
    this._scopeDue = 0;
    this._scopeChunk = 0;
    /* Stereo panning and reSIDfp filter as last set, replayed per tune: see
     * setPanning() and setFilter(). Null until a host sets them. */
    this._panning = null;
    this._filter = null;
  }

  /** Send the panning, filter and scope wishes to the worker, per tune. */
  async _softAudioReplay() {
    if (this._panning) await this._call('panning', this._panning).catch(() => {});
    if (this._filter) await this._call('filter', this._filter).catch(() => {});
    if (this._scopeWanted) await this._call('scope', { on: true }).catch(() => {});
  }

  /**
   * Record each voice's own output for an oscilloscope, or stop.
   *
   * Software audio in a worker only: the frames come with the worker's register
   * reports, timed to the audio. Remembered across tunes.
   *
   * @param {boolean} on true to record, false to stop
   * @returns {boolean} true when this mode can record
   */
  setScope(on) {
    this._scopeWanted = !!on;
    if (!on) this._scopeClear();
    if (!this._isAudio || !this._worker) return false;
    this._call('scope', { on: !!on }).catch(() => {});
    return true;
  }

  /**
   * Place the chips in the stereo mix. Software audio in a worker only.
   * Remembered across tunes.
   *
   * @param {object} p { stereo: bool, layout: -1 tune or 0-3, mode: -1 tune or
   *                   0-3, single: 0 left, 1 center, 2 right }
   * @returns {boolean} true when this mode can pan
   */
  setPanning(p) {
    this._panning = {
      stereo: !!p.stereo, layout: p.layout | 0, mode: p.mode | 0, single: p.single | 0,
    };
    if (!this._isAudio || !this._worker) return false;
    this._call('panning', this._panning).then((r) => {
      if (r && r.info) this._snap = r.info;
    }).catch(() => {});
    return true;
  }

  /** Stereo position per chip as playing, 0 left, 1 center, 2 right. */
  panning() {
    return (this._snap && this._snap.pans) ? this._snap.pans : [];
  }

  /**
   * Set the reSIDfp filter. Software audio only. Remembered across tunes.
   *
   * @param {object} f { enabled: bool, curve6581, range6581, curve8580: 0-1,
   *                   waveforms: 0 average, 1 weak, 2 strong }
   * @returns {boolean} true when this mode has a filter to set
   */
  setFilter(f) {
    this._filter = {
      enabled: !!f.enabled, curve6581: +f.curve6581, range6581: +f.range6581,
      curve8580: +f.curve8580, waveforms: f.waveforms | 0,
    };
    if (!this._isAudio) return false;
    if (this._worker) {
      this._call('filter', this._filter).catch(() => {});
    } else if (this._player && typeof this._player.M._usp_audio_set_filter === 'function') {
      const v = this._filter;
      this._player.M._usp_audio_set_filter(v.enabled ? 1 : 0, v.curve6581, v.range6581,
                                           v.curve8580, v.waveforms);
    }
    return true;
  }

  /** Is a tune sounding: the worker says playing and the host has not paused? */
  scopeLive() {
    return !!(this._snap && this._snap.playing) && !this._paused;
  }

  /** Voices in the scope frames, three per chip, 0 when nothing arrived. */
  scopeVoices() {
    this._drainRegisters();
    return this._scopeRing.length;
  }

  /** Sample rate of the scope frames, the audio output's. */
  scopeRate() {
    return (this._audio && this._audio.ctx) ? this._audio.ctx.sampleRate : 48000;
  }

  /**
   * Fill `out` with one voice's latest scope samples, up to the audible point.
   *
   * @param {number} voice      0-based, chip 1 voices 1-3 first
   * @param {Float32Array} out  receives out.length samples, -1 to 1, oldest first
   * @returns {boolean} false when there is no data for this voice
   */
  scopeData(voice, out) {
    this._drainRegisters();
    const ring = this._scopeRing[voice];
    if (!ring) return false;
    /* The latest report is due when its last frame is heard: walk through it
     * at the audio rate rather than jump a report at a time. */
    const elapsed = Math.floor((_now() - this._scopeDue) * this.scopeRate() / 1000);
    const end = this._scopeEnd - this._scopeChunk +
                Math.max(0, Math.min(this._scopeChunk, elapsed));
    const n = out.length;
    for (let i = 0; i < n; i++) {
      const f = end - n + i;
      out[i] = (f < 0 || f < this._scopeEnd - SCOPE_RING) ? 0 : ring[f & (SCOPE_RING - 1)];
    }
    return true;
  }

  /**
   * Append one report's scope frames to the voice rings.
   *
   * @param {Int16Array} data  frames * voices values, interleaved by frame
   * @param {number} voices    values per frame
   * @param {number} due       when the last frame is heard, _now() ms
   */
  _scopeAppend(data, voices, due) {
    if (this._scopeRing.length !== voices) {
      this._scopeRing = Array.from({ length: voices }, () => new Float32Array(SCOPE_RING));
      this._scopeEnd = 0;
    }
    const frames = (data.length / voices) | 0;
    for (let v = 0; v < voices; v++) {
      const ring = this._scopeRing[v];
      for (let f = 0, k = v; f < frames; f++, k += voices) {
        ring[(this._scopeEnd + f) & (SCOPE_RING - 1)] = data[k] / 32768;
      }
    }
    this._scopeEnd += frames;
    this._scopeDue = due;
    this._scopeChunk = frames;
  }

  /** Forget the scope frames, on a stop, a new tune or setScope(false). */
  _scopeClear() {
    this._scopeRing = [];
    this._scopeEnd = 0;
    this._scopeChunk = 0;
  }
}

/**
 * Add the soft audio methods to an adapter class.
 *
 * @param {Function} cls  the adapter class
 */
export function applySoftAudio(cls) {
  for (const name of Object.getOwnPropertyNames(SoftAudio.prototype)) {
    if (name === 'constructor') continue;
    Object.defineProperty(cls.prototype, name,
      Object.getOwnPropertyDescriptor(SoftAudio.prototype, name));
  }
}
