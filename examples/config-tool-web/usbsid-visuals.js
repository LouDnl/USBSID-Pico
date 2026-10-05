/**
 * USBSID-Pico Web Config - Visuals tab
 * USBSID-Player (ResidFp) mode only: oscilloscope per voice, reSIDfp filter
 * and stereo panning, as sub-tabs.
 *
 * usbsid-app.js owns the tab and the mode. It calls
 * window.usbsidVisuals.attach(adapter) when it builds a player, and
 * setActive(on) when the tab shows or hides. The filter and stereo settings
 * are kept in localStorage and handed to every ResidFp adapter, which applies
 * them per tune, whether or not this tab is ever opened.
 */

import { ScopeMosaic } from './usplayer/usplayer-scope.js';

/* reSIDfp's own filter defaults, see usp_audio_set_filter(). */
const FILTER_DEFAULTS = {
  enabled: true, curve6581: 0.5, range6581: 19 / 39, curve8580: 0.5, waveforms: 0,
};
/* Mono, as the player has always started; layout and mode -1 follow the tune. */
const PANNING_DEFAULTS = { stereo: false, layout: -1, mode: -1, single: 1 };
const VIEW_DEFAULTS = { sub: 'scope', zoom: 3, sync: true };

/**
 * A stored settings object, with defaults for whatever it lacks.
 *
 * @param {string} key       localStorage key
 * @param {object} defaults  the settings it has to have
 * @returns {object} a new object
 */
function stored(key, defaults) {
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem(key)) || {}; } catch (_) { saved = {}; }
  const out = {};
  for (const k of Object.keys(defaults)) {
    out[k] = typeof saved[k] === typeof defaults[k] ? saved[k] : defaults[k];
  }
  return out;
}

/** Write a settings object to localStorage, ignoring a full or blocked store. */
function save(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); } catch (_) { /* not kept */ }
}

const $ = (id) => document.getElementById(id);

const state = {
  filter: stored('usplayer_filter', FILTER_DEFAULTS),
  panning: stored('usplayer_panning', PANNING_DEFAULTS),
  view: stored('usplayer_visuals', VIEW_DEFAULTS),
  adapter: null,
  active: false,
  filterTimer: 0,
  pollTimer: 0,
  pansShown: '',
};

/* The mosaic reads through this: a new adapter needs no new mosaic. */
const source = {
  scopeData: (voice, out) => !!state.adapter && state.adapter.scopeData(voice, out),
  scopeRate: () => (state.adapter ? state.adapter.scopeRate() : 48000),
};

let mosaic = null;

/** Trace color: the page's cyan. */
function traceColor() {
  const c = getComputedStyle(document.documentElement).getPropertyValue('--c64-cyan').trim();
  return c || '#70daff';
}

/** Chip model the loaded tune asks for: 6581, 8580, or 0 when unknown. */
function tuneModel() {
  const bytes = state.adapter && typeof state.adapter.bytes === 'function'
    ? state.adapter.bytes() : null;
  if (!bytes || bytes.length < 0x78) return 0;
  return (bytes[0x77] & 0x30) >= 0x20 ? 8580 : 6581;
}

/** Chips the loaded tune uses, 1 to 4 for the scope. */
function tuneChips() {
  const info = state.adapter && typeof state.adapter.getSongInfo === 'function'
    ? state.adapter.getSongInfo() : null;
  return Math.min(4, Math.max(1, (info && info.numSids) | 0));
}

/* ---- the tap: on only while the Scope sub-tab shows ---------------------- */

function scopeWanted() {
  return state.active && state.view.sub === 'scope';
}

function applyScope() {
  if (state.adapter) state.adapter.setScope(scopeWanted());
  if (!mosaic) return;
  if (scopeWanted()) {
    mosaic.start(() => !!state.adapter && state.adapter.scopeLive());
  } else {
    mosaic.stop();
  }
}

/* ---- filter ---------------------------------------------------------------- */

const SLIDERS = ['curve6581', 'range6581', 'curve8580'];

function showFilter() {
  const f = state.filter;
  $('filter-enabled').checked = f.enabled;
  $('filter-waveforms').value = String(f.waveforms);
  for (const name of SLIDERS) {
    $('filter-' + name).value = String(f[name]);
    $('filter-' + name + '-val').textContent = f[name].toFixed(3);
  }
  /* The curve of the other chip model does nothing for this tune. */
  const model = tuneModel();
  $('row-filter-curve6581').classList.toggle('disabled', model === 8580);
  $('row-filter-range6581').classList.toggle('disabled', model === 8580);
  $('row-filter-curve8580').classList.toggle('disabled', model === 6581);
}

/** Store the filter and send it once input has rested: a curve or range
 * change rebuilds reSIDfp's filter tables. */
function applyFilter() {
  save('usplayer_filter', state.filter);
  clearTimeout(state.filterTimer);
  state.filterTimer = setTimeout(() => {
    if (state.adapter) state.adapter.setFilter(state.filter);
  }, 60);
}

/* ---- stereo ---------------------------------------------------------------- */

function showStereo() {
  const p = state.panning;
  $('stereo-output').value = p.stereo ? '1' : '0';
  $('stereo-layout').value = String(p.layout);
  $('stereo-mode').value = String(p.mode);
  $('stereo-single').value = String(p.single);
  for (const id of ['stereo-layout', 'stereo-mode', 'stereo-single']) $(id).disabled = !p.stereo;
  state.pansShown = '';
  showPans();
}

/** Where each SID chip of the playing tune sits, as "SID 1: L  SID 2: R". */
function showPans() {
  const pans = state.adapter ? state.adapter.panning() : [];
  const text = pans.length
    ? pans.map((pan, i) => 'SID ' + (i + 1) + ': ' + (['L', 'C', 'R'][pan] || 'C')).join('  ')
    : 'No tune playing';
  if (text === state.pansShown) return;
  state.pansShown = text;
  $('stereo-playing').textContent = text;
}

/* ---- sub-tabs -------------------------------------------------------------- */

function showSub() {
  for (const btn of document.querySelectorAll('.visuals-subtab')) {
    const on = btn.dataset.sub === state.view.sub;
    btn.setAttribute('aria-selected', on ? 'true' : 'false');
  }
  for (const sub of ['scope', 'filter', 'stereo']) {
    $('visuals-' + sub).classList.toggle('active', sub === state.view.sub);
  }
  if (state.view.sub === 'filter') showFilter();
  if (state.view.sub === 'stereo') showStereo();
  applyScope();
}

/** Slow refresh of what follows the tune: chip count, mutes, pans, model. */
function poll() {
  if (!state.active) return;
  if (state.view.sub === 'scope' && mosaic) {
    mosaic.layout(tuneChips());
    if (state.adapter) mosaic.showMutes((chip) => state.adapter.voiceMute(chip));
  }
  if (state.view.sub === 'stereo') showPans();
  if (state.view.sub === 'filter') showFilter();
}

/* ---- wiring ---------------------------------------------------------------- */

function bind() {
  for (const btn of document.querySelectorAll('.visuals-subtab')) {
    btn.addEventListener('click', () => {
      state.view.sub = btn.dataset.sub;
      save('usplayer_visuals', state.view);
      showSub();
    });
  }

  mosaic = new ScopeMosaic($('visuals-scope-boxes'), source, {
    onVoiceClick: (chip, voice) => {
      if (!state.adapter) return;
      const muted = (state.adapter.voiceMute(chip) & (1 << (voice - 1))) !== 0;
      state.adapter.setVoiceMute(chip, voice, !muted);
      mosaic.showMutes((c) => state.adapter.voiceMute(c));
    },
  });
  mosaic.setColor(traceColor());
  mosaic.setZoom(state.view.zoom);
  mosaic.setSyncMode(state.view.sync);
  mosaic.layout(1);

  $('scope-zoom').value = String(state.view.zoom);
  $('scope-zoom').addEventListener('input', (e) => {
    state.view.zoom = Number(e.target.value) || 3;
    mosaic.setZoom(state.view.zoom);
    save('usplayer_visuals', state.view);
  });
  $('scope-sync').checked = state.view.sync;
  $('scope-sync').addEventListener('change', (e) => {
    state.view.sync = e.target.checked;
    mosaic.setSyncMode(state.view.sync);
    save('usplayer_visuals', state.view);
  });

  for (const name of SLIDERS) {
    $('filter-' + name).addEventListener('input', (e) => {
      state.filter[name] = Number(e.target.value);
      $('filter-' + name + '-val').textContent = state.filter[name].toFixed(3);
      applyFilter();
    });
  }
  $('filter-enabled').addEventListener('change', (e) => {
    state.filter.enabled = e.target.checked;
    applyFilter();
  });
  $('filter-waveforms').addEventListener('change', (e) => {
    state.filter.waveforms = Number(e.target.value) | 0;
    applyFilter();
  });
  $('filter-defaults').addEventListener('click', () => {
    state.filter = Object.assign({}, FILTER_DEFAULTS);
    showFilter();
    applyFilter();
  });

  for (const id of ['stereo-output', 'stereo-layout', 'stereo-mode', 'stereo-single']) {
    $(id).addEventListener('change', () => {
      state.panning = {
        stereo: $('stereo-output').value === '1',
        layout: Number($('stereo-layout').value),
        mode: Number($('stereo-mode').value),
        single: Number($('stereo-single').value),
      };
      save('usplayer_panning', state.panning);
      if (state.adapter) state.adapter.setPanning(state.panning);
      showStereo();
    });
  }
}

/**
 * Hand the settings to a ResidFp adapter, which applies them per tune.
 *
 * @param {object|null} adapter  the USPlayerAdapter, null when the mode has none
 */
function attach(adapter) {
  state.adapter = (adapter && typeof adapter.setScope === 'function') ? adapter : null;
  if (!state.adapter) return;
  state.adapter.setFilter(state.filter);
  state.adapter.setPanning(state.panning);
  applyScope();
}

/**
 * The tab shows or hides.
 *
 * @param {boolean} on true while the Visuals tab is the active one
 */
function setActive(on) {
  state.active = !!on;
  clearInterval(state.pollTimer);
  state.pollTimer = 0;
  if (state.active) {
    showSub();
    poll();
    state.pollTimer = setInterval(poll, 300);
  } else {
    applyScope();
  }
}

bind();
window.usbsidVisuals = { attach, setActive };

/* usbsid-app.js may have built its player before this module loaded. */
if (typeof window.currentPlayer === 'function') attach(window.currentPlayer());
if (typeof window.visualsTabActive === 'function' && window.visualsTabActive()) setActive(true);
