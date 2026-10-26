/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * usplayer-softaudio.js
 * Oscilloscope, stereo panning and filter of software audio (reSIDfp in the worker)
 * Shared by usplayer-adapter.js and usplayer-adapter-deepsid.js
 *
 * applySoftAudio() adds the methods to an adapter class
 * Uses _isAudio, _worker, _call(), _drainRegisters(), _audio, _player and _snap
 * Adapter calls _softAudioInit() in the constructor, _softAudioReplay() after
 * the worker audioConfigure, _scopeClear() on stop or new tune and
 * _scopeAppend() for a register report with scope frames
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

/* Scope frames kept per voice, power of two (1/3 s at 48 kHz) */
export const SCOPE_RING = 16384;

/* Milliseconds on the clock used for register report timing */
function _now() {
  return (typeof performance !== 'undefined') ? performance.now() : Date.now();
}

/* Methods added by applySoftAudio(), never constructed */
class SoftAudio {
  /* Set up the state from the adapter constructor */
  _softAudioInit() {
    /* Scope frames from worker reports, one ring per voice, see setScope()
     * _scopeEnd = frames appended, _scopeDue and _scopeChunk = audible time and size of the latest report */
    this._scopeWanted = false;
    this._scopeRing = [];
    this._scopeEnd = 0;
    this._scopeDue = 0;
    this._scopeChunk = 0;
    /* Last set panning and filter, replayed per tune, null until set */
    this._panning = null;
    this._filter = null;
  }

  /* Send panning, filter and scope settings to the worker per tune */
  async _softAudioReplay() {
    if (this._panning) await this._call('panning', this._panning).catch(() => {});
    if (this._filter) await this._call('filter', this._filter).catch(() => {});
    if (this._scopeWanted) await this._call('scope', { on: true }).catch(() => {});
  }

  /* Record voice output for the oscilloscope or stop, returns true when this mode can record
   * Software audio in a worker only, remembered across tunes */
  setScope(on) {
    this._scopeWanted = !!on;
    if (!on) this._scopeClear();
    if (!this._isAudio || !this._worker) return false;
    this._call('scope', { on: !!on }).catch(() => {});
    return true;
  }

  /**
   * Place the chips in the stereo mix, returns true when this mode can pan
   * Software audio in a worker only, remembered across tunes
   *
   * @param {object} p { stereo: bool, layout: -1 tune or 0-3, mode: -1 tune or
   *                   0-3, single: 0 left, 1 center, 2 right }
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

  /* Stereo position per chip as playing, 0 left, 1 center, 2 right */
  panning() {
    return (this._snap && this._snap.pans) ? this._snap.pans : [];
  }

  /**
   * Set the reSIDfp filter, returns true when this mode has a filter
   * Software audio only, remembered across tunes
   *
   * @param {object} f { enabled: bool, curve6581, range6581, curve8580: 0-1,
   *                   waveforms: 0 average, 1 weak, 2 strong }
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

  /* Tune is sounding when the worker is playing and not paused */
  scopeLive() {
    return !!(this._snap && this._snap.playing) && !this._paused;
  }

  /* Voices in the scope frames, 3 per chip, 0 when nothing arrived */
  scopeVoices() {
    this._drainRegisters();
    return this._scopeRing.length;
  }

  /* Sample rate of the scope frames (audio output rate) */
  scopeRate() {
    return (this._audio && this._audio.ctx) ? this._audio.ctx.sampleRate : 48000;
  }

  /**
   * Fill out with the latest scope samples of one voice, up to the audible point
   * Returns false when there is no data for the voice
   *
   * @param {number} voice      0-based, chip 1 voices 1-3 first
   * @param {Float32Array} out  receives out.length samples, -1 to 1, oldest first
   */
  scopeData(voice, out) {
    this._drainRegisters();
    const ring = this._scopeRing[voice];
    if (!ring) return false;
    /* Walk through the latest report at the audio rate, due when its last frame is heard */
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
   * Append the scope frames of one report to the voice rings
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

  /* Forget the scope frames on stop, new tune or setScope(false) */
  _scopeClear() {
    this._scopeRing = [];
    this._scopeEnd = 0;
    this._scopeChunk = 0;
  }
}

/**
 * Add the soft audio methods to an adapter class
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
