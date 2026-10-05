/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usplayer-scope.js
 * Oscilloscope boxes for software audio: one canvas per voice, drawn from the
 * adapter's scope frames (see usplayer-softaudio.js).
 *
 * VoiceScope draws one voice. ScopeMosaic lays out the boxes for a tune: three
 * rows for one chip, a column per chip and a row per voice for more.
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

/* Most chips a mosaic shows: the worker sends four chips' voices at most. */
const MAX_CHIPS = 4;

/**
 * One voice's oscilloscope on a canvas.
 *
 * The data runs a 45th of a second longer than shown; in sync mode the window
 * starts at the last rising crossing of the wave's midline in that extra part,
 * which holds a periodic wave in place.
 */
export class VoiceScope {
  /**
   * @param {HTMLCanvasElement} canvas  where to draw
   * @param {object} source    scopeData(voice, out) and scopeRate(), the adapter
   * @param {number} voice     0-based, chip 1 voices 1-3 first
   */
  constructor(canvas, source, voice) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.source = source;
    this.voice = voice;
    this.syncMode = true;
    this.zoom = 3;
    this.color = '#7c70da';
    this.data = new Float32Array(0);
  }

  /**
   * Set how much time the box shows: zoom + 1 sixtieths of a second.
   *
   * @param {number} zoom 1 (closest) to 5 (farthest)
   */
  setZoom(zoom) { this.zoom = Math.min(5, Math.max(1, zoom | 0)); }

  /** Size the drawing buffer to the canvas' size on screen. */
  fit() {
    const r = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(this.canvas.clientWidth * r));
    const h = Math.max(1, Math.round(this.canvas.clientHeight * r));
    if (this.canvas.width !== w) this.canvas.width = w;
    if (this.canvas.height !== h) this.canvas.height = h;
  }

  /** Draw the latest output of the voice. */
  draw() {
    const rate = this.source.scopeRate();
    const scan = Math.floor(rate / 45);
    const total = Math.min(16384, Math.floor(rate / 60 * (this.zoom + 1)));
    if (total <= scan) return;
    if (this.data.length !== total) this.data = new Float32Array(total);
    if (!this.source.scopeData(this.voice, this.data)) return;

    const data = this.data, shown = total - scan;
    let start = scan, min = data[scan], max = data[scan];
    for (let i = scan + 1; i < total; i++) {
      if (data[i] < min) min = data[i];
      if (data[i] > max) max = data[i];
    }
    const mid = (min + max) / 2;
    if (this.syncMode && max - min > 0.01) {
      for (let i = scan; i > 0; i--) {
        if (data[i - 1] < mid && data[i] >= mid) { start = i; break; }
      }
    }

    /* A full swing of one voice spans about 1, drawn at 90% of the height. */
    const w = this.canvas.width, h = this.canvas.height;
    const scale = h * 0.9, step = w / shown, center = h / 2;
    const ctx = this.ctx;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = this.color;
    ctx.lineWidth = Math.max(1, Math.round(window.devicePixelRatio || 1));
    ctx.beginPath();
    for (let i = 0; i < shown; i++) {
      const y = center - (data[start + i] - mid) * scale;
      if (i === 0) ctx.moveTo(0, y);
      else ctx.lineTo(i * step, y);
    }
    ctx.stroke();
  }

  /** Draw a faded midline, for a stopped or paused tune. */
  idle() {
    const w = this.canvas.width, h = this.canvas.height;
    const ctx = this.ctx;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = this.color;
    ctx.globalAlpha = 0.4;
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(w, h / 2);
    ctx.stroke();
    ctx.globalAlpha = 1;
  }
}

/**
 * The oscilloscope boxes of a tune, in a container element.
 *
 * Builds `canvas.usp-scope` elements inside a `div.usp-scope-mosaic` grid with
 * `--usp-scope-cols` set to the column count; the page's CSS sizes them.
 */
export class ScopeMosaic {
  /**
   * @param {HTMLElement} container  emptied and filled with the boxes
   * @param {object} source          the adapter, see VoiceScope
   * @param {object} [opts]          { onVoiceClick(chip, voice) }, both from 1
   */
  constructor(container, source, opts = {}) {
    this.container = container;
    this.source = source;
    this.onVoiceClick = opts.onVoiceClick || null;
    this.chips = 0;
    this.scopes = [];
    this.zoom = 3;
    this.syncMode = true;
    this.color = '#7c70da';
    this._raf = 0;
    this._playing = () => true;
  }

  /**
   * Lay out the boxes for a chip count, when it differs from the current one.
   *
   * @param {number} chips 1 to 4
   */
  layout(chips) {
    chips = Math.min(MAX_CHIPS, Math.max(1, chips | 0));
    if (chips === this.chips) return;
    this.chips = chips;
    this.scopes = [];
    this.container.textContent = '';
    const grid = document.createElement('div');
    grid.className = 'usp-scope-mosaic';
    grid.style.setProperty('--usp-scope-cols', String(chips));
    /* Row by row: voice 1 of every chip, then voice 2, then voice 3. */
    for (let voice = 1; voice <= 3; voice++) {
      for (let chip = 1; chip <= chips; chip++) {
        const canvas = document.createElement('canvas');
        canvas.className = 'usp-scope';
        canvas.dataset.chip = String(chip);
        canvas.dataset.voice = String(voice);
        canvas.title = (chips > 1 ? 'SID ' + chip + ' ' : '') + 'voice ' + voice +
                       ' (click to mute)';
        if (this.onVoiceClick) {
          canvas.addEventListener('click', () => this.onVoiceClick(chip, voice));
        }
        grid.appendChild(canvas);
        const scope = new VoiceScope(canvas, this.source, (chip - 1) * 3 + voice - 1);
        scope.setZoom(this.zoom);
        scope.syncMode = this.syncMode;
        scope.color = this.color;
        this.scopes.push(scope);
      }
    }
    this.container.appendChild(grid);
  }

  /** @param {number} zoom 1 (closest) to 5 (farthest) */
  setZoom(zoom) {
    this.zoom = zoom;
    for (const s of this.scopes) s.setZoom(zoom);
  }

  /** @param {boolean} on hold periodic waves in place */
  setSyncMode(on) {
    this.syncMode = !!on;
    for (const s of this.scopes) s.syncMode = this.syncMode;
  }

  /** @param {string} color CSS color of the traces */
  setColor(color) {
    this.color = color;
    for (const s of this.scopes) s.color = color;
  }

  /**
   * Dim the boxes of muted voices.
   *
   * @param {function(number): number} maskOf  chip (from 1) to mute bits,
   *                                           bit 0 = voice 1
   */
  showMutes(maskOf) {
    for (const s of this.scopes) {
      const chip = Number(s.canvas.dataset.chip), voice = Number(s.canvas.dataset.voice);
      const muted = (maskOf(chip) & (1 << (voice - 1))) !== 0;
      s.canvas.classList.toggle('usp-scope-muted', muted);
    }
  }

  /**
   * Draw every animation frame until stop().
   *
   * @param {function(): boolean} playing  false draws idle midlines
   */
  start(playing) {
    if (playing) this._playing = playing;
    if (this._raf) return;
    const frame = () => {
      this._raf = requestAnimationFrame(frame);
      const live = this._playing();
      for (const s of this.scopes) {
        s.fit();
        if (live) s.draw();
        else s.idle();
      }
    };
    this._raf = requestAnimationFrame(frame);
  }

  /** Stop drawing. */
  stop() {
    if (this._raf) cancelAnimationFrame(this._raf);
    this._raf = 0;
  }
}
