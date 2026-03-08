/**
 * USBSID-Pico Web Config — Main Application
 * Handles device connection, player integration, and config UI wiring.
 */

'use strict';

/* ── Globals expected by jsSID-webusb.js / player.js ─── */
/* NOTE: jsSID-webusb.js declares these with 'let' — do NOT redeclare here:
 *   webusbplaying, webusbconnected, configavailable, webusb, port, savedport
 * We may safely redeclare 'var'-declared ones: webusb_enabled, usbsid, Mute_SID
 */
var webusb_enabled = false;   /* jsSID-webusb.js uses var — safe to initialize here */
var Mute_SID       = 0;       /* jsSID-webusb.js uses var — safe to initialize here */
var usbsid         = { version: '', nosids: 1, fmoplsid: 0 }; /* var in jsSID-webusb.js */

/* ── Global stubs for functions removed from jsSID-webusb.js ── */
/* setClock(rateId) — called by jsSID after parsing SID file header.
 * rateId: 0=DEFAULT, 1=PAL, 2=NTSC, 3=DREAN (matches usbsidDevice.setClock) */
function setClock(rateId) {
  if (webusb_enabled && usbsidDevice.isOpen) {
    usbsidDevice.setClock(rateId).catch(e => usbsidLog('setClock error:', e));
  }
}

/* ── App state ─────────────────────────────────────────── */
var _browser        = null;
var _player         = null;
var _emulator       = 'hermit';  /* matches the default selected option in index.html */
var _hasSIDPlayer   = false;     /* true when connected device productName contains 'Pico2' */
var _loadedBytes    = null;      /* Uint8Array of the currently loaded SID file */
var _sendsidPlaying = false;     /* playback state for SendSID onboard player mode */
var _websid         = null;  /* variable that holds the websid object when used */
var _currentFile    = null;  /* { url, name } — url is blob: or http: */
var _currentBlob    = null;  /* active Blob URL to revoke on next local load */
var _currentSubtune = 0;
var _maxSubtunes    = 1;
var _sidFiles       = [];    /* entries from SID/sidfilelist.txt */
var _sidFileIdx     = -1;    /* currently selected index in _sidFiles */

/* Path to SID library — same directory as this page, then SID/ */
const SID_PATH = (function() {
  const base = window.location.pathname.replace(/\/[^/]*$/, '/');
  return base + 'SID/';
}());

/* ── Utility: log to debug panel + console ────────────── */
function usbsidLog(...args) {
  console.log('[USBSID]', ...args);
  const line = args.join(' ') + '\n';
  ['debug-log', 'player-log'].forEach(id => {
    const el = document.getElementById(id);
    if (el) { el.textContent += line; el.scrollTop = el.scrollHeight; }
  });
}

/* ── Utility: set status bar text ─────────────────────── */
function usbsidSetStatus(msg, color) {
  const el = document.getElementById('status-text');
  if (el) {
    el.textContent = msg;
    el.style.color = color === 'red'   ? 'var(--c64-red)'
                   : color === 'green' ? 'var(--c64-green)'
                   : color === 'yellow'? 'var(--c64-yellow)'
                   : 'var(--c64-cyan)';
  }
}

/* ── Utility: set LED state ───────────────────────────── */
function setLED(connected) {
  const led = document.getElementById('status-led');
  if (!led) return;
  if (connected) led.classList.add('connected');
  else led.classList.remove('connected');
}

/* ── Tab switching ─────────────────────────────────────── */
function initTabs() {
  const tabs   = document.querySelectorAll('.c64-tab');
  const panels = document.querySelectorAll('.c64-panel');
  tabs.forEach(tab => {
    tab.addEventListener('click', () => {
      const target = tab.dataset.tab;
      tabs.forEach(t => t.classList.remove('active'));
      panels.forEach(p => p.classList.remove('active'));
      tab.classList.add('active');
      const panel = document.getElementById('panel-' + target);
      if (panel) panel.classList.add('active');
    });
  });
  /* Activate first tab */
  if (tabs.length) tabs[0].click();
}

/* ── Device connection ─────────────────────────────────── */
async function connectDevice() {
  if (!navigator.usb) {
    usbsidSetStatus('WebUSB not available — use HTTPS or localhost', 'red');
    usbsidLog('ERROR: navigator.usb is undefined. WebUSB requires a secure context (HTTPS or localhost).');
    return;
  }
  usbsidSetStatus('Connecting\u2026');
  try {
    const ok = await usbsidDevice.connect();
    if (ok) {
      await onDeviceConnected();
    } else {
      usbsidSetStatus('Connection failed or cancelled', 'red');
      setLED(false);
    }
  } catch (e) {
    usbsidSetStatus('Connect error: ' + e.message, 'red');
    usbsidLog('connectDevice error:', e);
    setLED(false);
  }
}

async function reconnectDevice() {
  if (usbsidDevice.isOpen) return;
  const ok = await usbsidDevice.reconnect();
  if (ok) await onDeviceConnected();
}

async function disconnectDevice() {
  if (!usbsidDevice.isOpen) return;
  stopPlay();
  await usbsidDevice.close();
  onDeviceDisconnected();
}

async function onDeviceConnected() {
  usbsidLog('Device connected');
  setLED(true);

  /* Override webusb.writeReg so jsSID-webusb.js uses our driver */
  webusb.writeReg = function(array) {
    usbsidDevice.write(array);
    /* Update live register display */
    if (array && array.length >= 3) {
      const cmd  = array[0] & 0xC0;
      const addr = array[1];
      const val  = array[2];
      if (cmd === 0x00 || cmd === 0x80) {
        updateSIDReg((addr >> 5) & 0x03, addr & 0x1F, val);
      }
    }
  };
  webusb.readReg = function(_array) {
    /* reads handled separately via usbsidDevice.configCmdRead */
  };
  webusb_enabled  = (_emulator === 'webusb');
  webusbplaying   = false;
  webusbconnected = true;   /* jsSID-webusb.js checks this; keep in sync with our connection */
  savedport       = 'usbsidpico'; /* non-null → prevents "Autoconnect not actived yet" alert */

  /* Update connect button IMMEDIATELY — before any async reads so the UI
   * reflects the actual state before the user can click again */
  const btn = document.getElementById('btn-connect');
  if (btn) {
    btn.textContent = 'DISCONNECT';
    btn.classList.remove('c64-btn-connect');
    btn.classList.add('c64-btn-warn');
  }
  usbsidSetStatus('Connected — reading device info\u2026', 'green');

  /* Read version */
  try {
    const ver    = await usbsidDevice.readVersion();
    const pcbver = await usbsidDevice.readPCBVersion();
    usbsid.version = ver;
    const verEl = document.getElementById('version-display');
    if (verEl) verEl.textContent = 'FW: ' + (ver || '—') + '  PCB: ' + (pcbver || '—');
    usbsidLog('FW version:', ver, '  PCB:', pcbver);

    applyPCBVersionUI(pcbver);

    /* Read number of SIDs */
    const n = await usbsidDevice.readNumSIDs();
    if (n > 0) usbsid.nosids = n;

    /* Read FMOpl SID */
    const fm = await usbsidDevice.readFMOplSID();
    usbsid.fmoplsid = fm;

  } catch (e) {
    usbsidLog('Version read error:', e);
  }

  /* Enable config panel */
  configavailable = true;
  const configPanel = document.getElementById('panel-config');
  if (configPanel) configPanel.classList.remove('c64-hidden');

  /* Detect onboard SID player capability — only available on Pico2 firmware builds.
   * Check USB productName descriptor (set from USBSID_PRODUCT in CMakeLists,
   * e.g. "USBSID-Pico2 v1.3"). Fall back to checking the firmware version string
   * returned by readVersion() if productName is empty (browser/platform dependent). */
  const pname = usbsidDevice.productName;
  usbsidLog('USB productName:', JSON.stringify(pname));
  _hasSIDPlayer = pname.includes('Pico2') || usbsid.version.includes('Pico2');
  usbsidLog('Onboard SID player:', _hasSIDPlayer ? 'available' : 'not available');

  usbsidSetStatus('Connected — ' + (usbsid.version || 'USBSID-Pico'), 'green');

  updateRegsTabVisibility();
  updateConfTabVisibility();
  updatePlayerSideButtons();

  /* In SendSID mode, enable transport buttons immediately on connect */
  if (_emulator === 'sendsid') {
    setPlayerButtons(true);
    updateSendSIDPlayButton();
  }

  /* Auto-read config */
  setTimeout(() => doReadConfig(), 300);
}

function onDeviceDisconnected() {
  webusb_enabled  = false;
  webusbplaying   = false;
  webusbconnected = false;
  configavailable = false;
  setLED(false);

  const btn = document.getElementById('btn-connect');
  if (btn) {
    btn.textContent = 'CONNECT';
    btn.classList.add('c64-btn-connect');
    btn.classList.remove('c64-btn-warn');
  }

  const verEl = document.getElementById('version-display');
  if (verEl) verEl.textContent = '';

  _hasSIDPlayer = false;
  _sendsidPlaying = false;
  usbsidSetStatus('Disconnected', 'red');
  usbsidLog('Device disconnected');
  if (_emulator === 'sendsid') {
    setPlayerButtons(false);
    updateSendSIDPlayButton();
  }
  updateRegsTabVisibility();
  updateConfTabVisibility();
  updatePlayerSideButtons();
}

/* ── Player integration ────────────────────────────────── */
function createPlayer(emulator) {
  if (typeof SIDPlayer !== 'undefined') {
    return new SIDPlayer(emulator);
  }
  return null;
}

function getPlayer() {
  if (!_player || _player.emulator !== _emulator) {
    _player = createPlayer(_emulator);
  }
  return _player;
}

/* Workaround to always subtract 1 from subtune as the player expects this */
function _loadTune(subtune, timeout, file, callback) {
  const p = getPlayer();
  if (!p) return;
  subtune = (subtune != 0 ? (subtune - 1) : subtune);
  p.load(subtune, timeout, file, callback);
}

/* Core load — url must be a full URL (blob: or http:) */
async function doLoadSID(url, displayName, subtune) {
  _currentFile    = { url, name: displayName };
  _currentSubtune = subtune != null ? subtune : 1;

  /* SendSID mode: fetch bytes and upload to onboard player */
  if (_emulator === 'sendsid') {
    const nameEl = document.getElementById('sid-file-name');
    if (nameEl) nameEl.textContent = displayName;
    usbsidSetStatus('Fetching: ' + displayName + '\u2026');
    try {
      const resp = await fetch(url);
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      _loadedBytes = new Uint8Array(await resp.arrayBuffer());
    } catch (e) {
      usbsidSetStatus('Fetch failed: ' + e.message, 'red');
      usbsidLog('sendsid fetch error:', e);
      return;
    }
    await uploadCurrentSID();
    return;
  }

  const p = getPlayer();
  if (!p) { usbsidLog('No player available'); return; }

  const nameEl = document.getElementById('sid-file-name');
  if (nameEl) nameEl.textContent = displayName;

  usbsidSetStatus('Loading: ' + displayName);
  if (usbsidDevice.isOpen) await usbsidDevice.resetSID();

  p.setVolume(1);
  p.load(_currentSubtune, 1000, url, null);
  webusbplaying = true;

  /* Give hermit time to parse the file headers */
  setTimeout(() => {
    try {
      const info = ((_emulator !== "websid") ? p.getSongInfo() : p.webusbsid.songInfo);
      /* console.log("songInfo: " + info); */
      _maxSubtunes = Math.max(1, (info.maxSubsong || 0) + 1);
      updateSubtuneDisplay();
      updateMetaDisplay(info);
      usbsidSetStatus('Playing: ' + displayName, 'green');
    } catch (_) {}
    setPlayerButtons(true);
  }, ((_emulator !== "websid") ? 100 : 800));
  // }, ((_emulator !== "websid") ? 300 : 1000));
}

/* Load from local <input type=file> — wraps binary in a Blob URL */
async function loadSID(fileData, fileName) {
  _loadedBytes = fileData;  /* keep raw bytes for onboard player upload */
  /* SendSID mode: upload directly, skip software player */
  if (_emulator === 'sendsid') {
    _currentFile    = { url: null, name: fileName };
    _currentSubtune = 1;
    const nameEl = document.getElementById('sid-file-name');
    if (nameEl) nameEl.textContent = fileName;
    await uploadCurrentSID();
    return;
  }
  if (_currentBlob) { URL.revokeObjectURL(_currentBlob); _currentBlob = null; }
  const blob = new Blob([fileData], { type: 'application/octet-stream' });
  const url  = URL.createObjectURL(blob);
  _currentBlob = url;
  await doLoadSID(url, fileName);
}

async function playPause() {
  if (_emulator === 'sendsid') {
    if (!usbsidDevice.isOpen) return;
    try {
      if (_sendsidPlaying) {
        await usbsidDevice.playerPause();
        _sendsidPlaying = false;
      } else {
        await usbsidDevice.playerStart();
        _sendsidPlaying = true;
      }
      updateSendSIDPlayButton();
    } catch (e) { usbsidLog('sendsid playPause error:', e); }
    return;
  }
  const p = getPlayer();
  if (!p) return;
  try {
    if (!_currentFile) {
      /* nothing loaded yet */
    } else if (!webusbplaying) {
      /* not playing — resume from pause or start fresh */
      p.setVolume(1);
      if (p.paused) {
        p.play();
      } else {
        // p.load(_currentSubtune, 1000, _currentFile.url, null);
        _loadTune(_currentSubtune, 1000, _currentFile.url, null);
      }
      webusbplaying = true;
    } else {
      /* currently playing — pause */
      p.setVolume(0);
      p.pause();
      webusbplaying = false;
    }
    setTimeout(() => updatePlayButton(p), 50);
  } catch (e) {
    usbsidLog('playPause error:', e);
  }
}

async function stopPlay() {
  if (_emulator === 'sendsid') {
    if (usbsidDevice.isOpen) {
      try { await usbsidDevice.playerStop(); } catch (e) { usbsidLog('sendsid stop error:', e); }
    }
    _sendsidPlaying = false;
    updateSendSIDPlayButton();
    usbsidSetStatus('Stopped');
    return;
  }
  const p = getPlayer();
  if (!p) return;
  try {
    p.setVolume(0);
    p.stop();
    webusbplaying = false;
    if (usbsidDevice.isOpen) await usbsidDevice.resetSID();
    updatePlayButton(p);
    usbsidSetStatus('Stopped');
  } catch (e) {
    usbsidLog('stopPlay error:', e);
  }
}

async function prevSubtune() {
  if (_emulator === 'sendsid') {
    if (usbsidDevice.isOpen) {
      try { await usbsidDevice.playerPrev(); } catch (e) { usbsidLog('sendsid prev error:', e); }
    }
    return;
  }
  if (_currentSubtune > 1) {
    _currentSubtune--;
    const p = getPlayer();
    if (p && _currentFile) {
      // p.load(_currentSubtune, 1000, _currentFile.url, null);
      _loadTune(_currentSubtune, 1000, _currentFile.url, null);
      p.setVolume(1);
    }
    updateSubtuneDisplay();
  }
}

async function nextSubtune() {
  if (_emulator === 'sendsid') {
    if (usbsidDevice.isOpen) {
      try { await usbsidDevice.playerNext(); } catch (e) { usbsidLog('sendsid next error:', e); }
    }
    return;
  }
  if (_currentSubtune < _maxSubtunes) {
    _currentSubtune++;
    const p = getPlayer();
    if (p && _currentFile) {
      // p.load(_currentSubtune, 1000, _currentFile.url, null);
      _loadTune(_currentSubtune, 1000, _currentFile.url, null);
      p.setVolume(1);
    }
    updateSubtuneDisplay();
  }
}

/* Navigate SID list by index — skips non-.sid entries */
async function selectSIDByIndex(idx) {
  if (idx < 0 || idx >= _sidFiles.length) return;
  _sidFileIdx = idx;
  const sel = document.getElementById('sid-list-select');
  if (sel) sel.value = idx;
  const entry = _sidFiles[idx];
  await doLoadSID(SID_PATH + entry, entry.split('/').pop());
}

async function prevSID() {
  let idx = _sidFileIdx - 1;
  while (idx >= 0 && !_sidFiles[idx].toLowerCase().endsWith('.sid')) idx--;
  if (idx < 0) {
    idx = _sidFiles.length - 1;
    while (idx > _sidFileIdx && !_sidFiles[idx].toLowerCase().endsWith('.sid')) idx--;
  }
  if (idx >= 0 && _sidFiles[idx].toLowerCase().endsWith('.sid')) selectSIDByIndex(idx);
}

async function nextSID() {
  let idx = _sidFileIdx + 1;
  while (idx < _sidFiles.length && !_sidFiles[idx].toLowerCase().endsWith('.sid')) idx++;
  if (idx >= _sidFiles.length) {
    idx = 0;
    while (idx < _sidFileIdx && !_sidFiles[idx].toLowerCase().endsWith('.sid')) idx++;
  }
  if (idx < _sidFiles.length && _sidFiles[idx].toLowerCase().endsWith('.sid')) selectSIDByIndex(idx);
}

function updateSubtuneDisplay() {
  const el = document.getElementById('subtune-display');
  if (el) el.textContent = 'Tune ' + _currentSubtune + '/' + _maxSubtunes;
}

function updateMetaDisplay(info) {
  const el = document.getElementById('sid-meta');
  if (!el) return;
  try {
    const parts = [];
    if (info && info.songName)     parts.push(info.songName);
    if (info && info.songAuthor)   parts.push('by ' + info.songAuthor);
    if (info && info.songReleased) parts.push('(' + info.songReleased + ')');
    el.textContent = parts.join(' \u2014 ') || '';
  } catch (_) {}
}

function updatePlayButton(p) {
  const btn = document.getElementById('btn-play');
  if (!btn) return;
  if (!p || p.stopped) {
    btn.textContent = 'PLAY';
    btn.classList.remove('c64-btn-stop');
    btn.classList.add('c64-btn-play');
  } else if (p.paused) {
    btn.textContent = 'RESUME';
    btn.classList.remove('c64-btn-stop');
    btn.classList.add('c64-btn-play');
  } else {
    btn.textContent = 'PAUSE';
    btn.classList.add('c64-btn-stop');
    btn.classList.remove('c64-btn-play');
  }
}

function updateSendSIDPlayButton() {
  const btn = document.getElementById('btn-play');
  if (!btn) return;
  if (_sendsidPlaying) {
    btn.textContent = 'PAUSE';
    btn.classList.add('c64-btn-stop');
    btn.classList.remove('c64-btn-play');
  } else {
    btn.textContent = 'PLAY';
    btn.classList.remove('c64-btn-stop');
    btn.classList.add('c64-btn-play');
  }
}

function setPlayerButtons(enabled) {
  ['btn-play', 'btn-stop', 'btn-prev-tune', 'btn-next-tune'].forEach(id => {
    const el = document.getElementById(id);
    if (!el) return;
    /* In SendSID mode the stop button is always enabled so the user can
     * halt playback even after a page refresh when the device is still playing. */
    el.disabled = (!enabled && !(id === 'btn-stop' && _emulator === 'sendsid'));
  });
}

/* ── Visibility helpers for emulator-dependent UI ──────── */
function updateConnectButtonVisibility() {
  const btn = document.getElementById('btn-connect');
  if (!btn) return;
  const show = (_emulator === 'webusb' || _emulator === 'sendsid'/*  || _emulator === 'websid' */);
  btn.style.display = show ? '' : 'none';
}

function updateConfTabVisibility() {
  /* Config tab is shown whenever a WebUSB mode is selected
   * Needs an open device, which avoids auto-connect timing races. */
  const tab = document.querySelector('.c64-tab[data-tab="config"]');
  const panel = document.getElementById('panel-regs');
  const show = (_emulator === 'webusb' || _emulator === 'sendsid');
  if (tab)   tab.style.display   = show ? '' : 'none';
  if (panel && !show) {
    /* If config panel is active and we're hiding it, switch to player tab */
    if (panel.classList.contains('active')) {
      const playerTab = document.querySelector('.c64-tab[data-tab="player"]');
      if (playerTab) playerTab.click();
    }
    panel.classList.remove('active');
  }
}

function updateRegsTabVisibility() {
  /* Registers tab is shown whenever WebUSB mode is selected — no need to wait
   * for device open, which avoids auto-connect timing races. */
  const tab = document.querySelector('.c64-tab[data-tab="regs"]');
  const panel = document.getElementById('panel-regs');
  const show = (_emulator === 'webusb');
  if (tab)   tab.style.display   = show ? '' : 'none';
  if (panel && !show) {
    /* If registers panel is active and we're hiding it, switch to player tab */
    if (panel.classList.contains('active')) {
      const playerTab = document.querySelector('.c64-tab[data-tab="player"]');
      if (playerTab) playerTab.click();
    }
    panel.classList.remove('active');
  }
}

function updatePlayerSideButtons() {
  const webusbBtns  = document.getElementById('webusb-player-btns');
  const asidBtns    = document.getElementById('asid-player-btns');
  const sendsidBtns = document.getElementById('sendsid-player-btns');
  if (webusbBtns)  webusbBtns.style.display  = (_emulator === 'webusb')  ? 'flex' : 'none';
  if (asidBtns)    asidBtns.style.display    = (_emulator === 'asid')   ? 'flex' : 'none';
  if (sendsidBtns) sendsidBtns.style.display = (_emulator === 'sendsid' && usbsidDevice.isOpen) ? 'flex' : 'none';
}

/* ── Emulator switching ────────────────────────────────── */
function switchEmulator(em) {
  stopPlay();
  /* Stop websid WASM worker before discarding the player to prevent memory leaks */
  if (_player && _player.emulator === 'websid' && _player.webusbsid) {
    _player.webusbsid.StopWorker();
  }
  _emulator = em;
  _player   = null;
  webusb_enabled = (em === 'webusb');
  localStorage.setItem('usbsid_emulator', em);
  usbsidLog('Emulator switched to:', em);
  /* Show MIDI selector only for ASID mode */
  const midiRow = document.getElementById('asid-midi-row');
  if (midiRow) midiRow.style.display = (em === 'asid') ? '' : 'none';
  /* Re-evaluate button disabled state now that _emulator is set.
   * This ensures the stop button is active in sendsid mode regardless of
   * connection state (so the user can stop a still-playing device after refresh). */
  setPlayerButtons(false);
  /* Mode-specific prompts and button state */
  if ((em === 'webusb' || em === 'sendsid') && !usbsidDevice.isOpen) {
    usbsidSetStatus('Connect device for ' + (em === 'sendsid' ? 'SendSID' : 'WebUSB') + ' playback', 'yellow');
  }
  /* Enable all transport buttons immediately in sendsid mode when already connected */
  if (em === 'sendsid' && usbsidDevice.isOpen) {
    setPlayerButtons(true);
    updateSendSIDPlayButton();
  }
  /* Enable WASM if websid */
  if (em === 'websid') {
    const p = getPlayer();
    p.webusbsid.StartWorker();
  }
  updateConnectButtonVisibility();
  updateConfTabVisibility();
  updateRegsTabVisibility();
  updatePlayerSideButtons();
}

/* ── Volume control ────────────────────────────────────── */
function setVolume(val) {
  const p = getPlayer();
  if (p && p.setVolume) p.setVolume(val / 100);
}

/* ── SID register display ──────────────────────────────── */
const SID_REG_NAMES = [
  'FR1L','FR1H','PW1L','PW1H','CR1','AT1','SU1',
  'FR2L','FR2H','PW2L','PW2H','CR2','AT2','SU2',
  'FR3L','FR3H','PW3L','PW3H','CR3','AT3','SU3',
  'CFL','CFH','RES','MCH','V1','V2','V3','RNG','ENV'
];

function buildRegGrid() {
  const container = document.getElementById('sid-regs-container');
  if (!container) return;
  container.innerHTML = '';

  for (let sid = 0; sid < 2; sid++) {
    const title = document.createElement('div');
    title.className = 'c64-box-title';
    title.textContent = 'SID ' + (sid + 1);
    container.appendChild(title);

    const grid = document.createElement('div');
    grid.className = 'sid-regs-grid';
    grid.id = 'sid-grid-' + sid;

    for (let r = 0; r < 32; r++) {
      const cell = document.createElement('div');
      cell.className = 'sid-reg-cell';
      cell.id = 'sreg-' + sid + '-' + r;
      const addr = document.createElement('div');
      addr.className = 'reg-addr';
      addr.textContent = '$' + ((sid * 0x20) + r).toString(16).padStart(2, '0');
      const val = document.createElement('div');
      val.className = 'reg-val';
      val.textContent = '00';
      cell.appendChild(addr);
      cell.appendChild(val);
      grid.appendChild(cell);
    }
    container.appendChild(grid);
  }
}

function updateSIDReg(sid, reg, val) {
  const cell = document.getElementById('sreg-' + sid + '-' + reg);
  if (!cell) return;
  const valEl = cell.querySelector('.reg-val');
  if (valEl) {
    valEl.textContent = val.toString(16).padStart(2, '0').toUpperCase();
    cell.classList.add('reg-written');
    setTimeout(() => cell.classList.remove('reg-written'), 400);
  }
}


/* ── SID library list (SID/sidfilelist.txt) ────────────── */
async function initSIDList() {
  const sel = document.getElementById('sid-list-select');
  if (!sel) return;
  /* On touch devices (Firefox Android etc.) a multi-row <select> triggers an
   * extra native picker overlay, causing a double-select. Use a single-row
   * dropdown instead — the native picker is the correct UX on mobile anyway. */
  if ('ontouchstart' in window || navigator.maxTouchPoints > 0) {
    sel.size = 1;
    sel.style.height = '';
  }
  try {
    const resp = await fetch(SID_PATH + 'sidfilelist.txt');
    if (!resp.ok) {
      usbsidLog('SID list not found:', SID_PATH + 'sidfilelist.txt');
      const box = document.getElementById('sid-list-box');
      if (box) box.style.display = 'none';
      return;
    }
    const text = await resp.text();
    _sidFiles = text.trim().split(/\r?\n/).filter(l => l.trim());
    sel.innerHTML = '';
    let count = 0;
    _sidFiles.forEach((entry, i) => {
      const opt = document.createElement('option');
      if (entry.toLowerCase().endsWith('.sid')) {
        opt.value       = i;
        opt.textContent = entry;
        count++;
      } else {
        /* Section heading */
        opt.value       = '';
        opt.textContent = '\u2500 ' + entry + ' \u2500';
        opt.disabled    = true;
        opt.style.color = 'var(--c64-yellow)';
      }
      sel.appendChild(opt);
    });
    const countEl = document.getElementById('sid-list-count');
    if (countEl) countEl.textContent = '(' + count + ' files)';
    sel.addEventListener('change', async () => {
      const idx = parseInt(sel.value, 10);
      if (!isNaN(idx)) await selectSIDByIndex(idx);
    });
    const pbtn = document.getElementById('btn-prev-sid');
    const nbtn = document.getElementById('btn-next-sid');
    if (pbtn) pbtn.removeAttribute('disabled');
    if (nbtn) nbtn.removeAttribute('disabled');
  } catch (e) {
    usbsidLog('initSIDList error:', e);
  }
}

/* ── File loading ──────────────────────────────────────── */
function initFileLoading() {
  const fileInput = document.getElementById('sid-file-input');
  if (fileInput) {
    fileInput.addEventListener('change', async (e) => {
      const file = e.target.files[0];
      if (!file) return;
      const buf = await file.arrayBuffer();
      const name = file.name;
      const nameEl = document.getElementById('sid-file-name');
      if (nameEl) nameEl.textContent = name;
      await loadSID(new Uint8Array(buf), name);
    });
  }

  const urlInput  = document.getElementById('sid-url-input');
  const urlButton = document.getElementById('btn-load-url');
  if (urlInput && urlButton) {
    urlButton.addEventListener('click', async () => {
      const url = urlInput.value.trim();
      if (!url) return;
      const name = url.split('/').pop().split('?')[0] || 'remote.sid';
      usbsidSetStatus('Fetching: ' + name + '\u2026');
      /* CORS proxy fallbacks — tried in order when direct fetch is blocked.
       * Own server proxy uses path-based routing (no encoding) so nginx receives
       * the target URL verbatim without needing to URL-decode a query parameter. */
      const CORS_PROXIES = [
        u => 'https://usbsid.loudai.nl/sidproxy/' + u,
        u => 'https://corsproxy.io/?' + encodeURIComponent(u),
        u => 'https://api.allorigins.win/raw?url=' + encodeURIComponent(u),
      ];
      try {
        /* 1. Try direct fetch */
        let resp = null;
        let usedProxy = null;
        try {
          resp = await fetch(url);
        } catch (corsErr) {
          if (!(corsErr instanceof TypeError)) throw corsErr;
          /* 2. CORS blocked — try each proxy in turn */
          let lastErr = corsErr;
          for (const makeProxy of CORS_PROXIES) {
            const proxyUrl = makeProxy(url);
            usbsidSetStatus('CORS blocked — retrying via proxy\u2026', 'yellow');
            usbsidLog('Trying proxy:', proxyUrl);
            try {
              const pr = await fetch(proxyUrl);
              if (pr.ok) { resp = pr; usedProxy = proxyUrl; break; }
              lastErr = new Error('HTTP ' + pr.status + ' from proxy');
            } catch (pe) { lastErr = pe; }
          }
          if (!resp) throw lastErr;
        }
        if (!resp.ok) throw new Error('HTTP ' + resp.status);
        if (usedProxy) usbsidLog('Loaded via proxy:', usedProxy);
        const buf = await resp.arrayBuffer();
        const bytes = new Uint8Array(buf);
        /* Validate SID/MUS magic bytes before handing to player */
        const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
        if (magic !== 'PSID' && magic !== 'RSID') {
          throw new Error('not a SID file (got "' + magic + '") — URL may point to an HTML page or redirect');
        }
        await loadSID(bytes, name);
      } catch (e) {
        usbsidSetStatus('Load failed: ' + e.message + ' — download the file and use Browse', 'red');
        usbsidLog('URL load error:', e);
      }
    });
  }
}

/* ── ASID MIDI port listing ────────────────────────────── */
/* NOTE: jsSID-webusb.js also populates #asid-midi-outputs when ASID player is
 * created (new jsSID(0,0,true,false)).  Our initMIDI only runs at startup to
 * pre-populate the list for modes that don't trigger jsSID ASID init.
 * The element id MUST match what jsSID-webusb.js expects: 'asid-midi-outputs'.
 *
 * Problem: when jsSID creates an ASID player (on SID file load) it repopulates
 * the list and resets selectedMidiOutput to outputs[0], losing the user's choice.
 * Fix: MutationObserver watches for list repopulation and restores the saved index. */
var _savedAsidMidiIdx = parseInt(localStorage.getItem('usbsid_asid_midi_idx') || '0', 10);

function initMIDI() {
  const sel = document.getElementById('asid-midi-outputs');
  if (!sel) return;
  if (!navigator.requestMIDIAccess) {
    sel.innerHTML = '<option value="">MIDI not supported</option>';
    return;
  }
  navigator.requestMIDIAccess({ sysex: true }).then(access => {
    /* Pre-set jsSID's midiAccessObj global so it is available immediately when
     * jsSID's init() runs for the first tune — avoids a null-dereference crash
     * that occurs when jsSID's own requestMIDIAccess hasn't resolved yet. */
    if (typeof midiAccessObj !== 'undefined') window.midiAccessObj = access;
    sel.innerHTML = '';
    const outputs = Array.from(access.outputs.values());
    if (!outputs.length) {
      sel.innerHTML = '<option value="">— no MIDI outputs —</option>';
      return;
    }
    outputs.forEach((output, i) => {
      const opt = document.createElement('option');
      opt.value       = i;
      opt.textContent = output.name;
      sel.appendChild(opt);
    });
    /* Restore saved MIDI output index (persisted across page reloads).
     * This ensures select.value is correct before jsSID's init() reads it. */
    if (_savedAsidMidiIdx > 0 && outputs[_savedAsidMidiIdx]) {
      sel.value = String(_savedAsidMidiIdx);
      selectedMidiOutput = outputs[_savedAsidMidiIdx];
    } else {
      selectedMidiOutput = outputs[0];
    }
    sel.addEventListener('change', () => {
      const idx = parseInt(sel.value, 10);
      if (!isNaN(idx)) {
        _savedAsidMidiIdx = idx;
        localStorage.setItem('usbsid_asid_midi_idx', idx);
        selectedMidiOutput = outputs[idx];
        usbsidLog('ASID MIDI output:', outputs[idx] && outputs[idx].name);
        updatePlayerSideButtons();
      }
    });
    /* Watch for jsSID repopulating the list (happens on every ASID player creation)
     * and restore the user's previously selected output. */
    new MutationObserver(() => {
      if (sel.querySelector('option[value="' + _savedAsidMidiIdx + '"]')) {
        sel.value = String(_savedAsidMidiIdx);
        selectedMidiOutput = outputs[_savedAsidMidiIdx] || outputs[0];
        usbsidLog('ASID MIDI: restored output', _savedAsidMidiIdx, selectedMidiOutput && selectedMidiOutput.name);
      }
    }).observe(sel, { childList: true });
  }).catch(e => {
    usbsidLog('MIDI access error:', e);
  });
}

/* ── Emulator select ───────────────────────────────────── */
function initEmulatorSelect() {
  const sel = document.getElementById('sel-emulator');
  if (!sel) return;
  usbsidLog('Selected emulator:', sel.value);
  /* Restore last-used emulator from localStorage */
  const saved = localStorage.getItem('usbsid_emulator');
  usbsidLog('Saved emulator:', saved);
  if (saved && sel.querySelector('option[value="' + saved + '"]')) {
    sel.value = saved;
    switchEmulator(saved);
  } else {
    /* Apply initial MIDI row visibility based on default selection */
    const midiRow = document.getElementById('asid-midi-row');
    if (midiRow) midiRow.style.display = (sel.value === 'asid') ? '' : 'none';
  }
  sel.addEventListener('change', () => {
    switchEmulator(sel.value);
  });
}

/* ── Volume slider ─────────────────────────────────────── */
function initVolumeSlider() {
  const slider = document.getElementById('volume-slider');
  if (!slider) return;
  slider.addEventListener('input', () => setVolume(parseInt(slider.value, 10)));
}

/* ── Connect button ────────────────────────────────────── */
function initConnectButton() {
  const btn = document.getElementById('btn-connect');
  if (!btn) { console.error('[USBSID] btn-connect not found'); return; }
  btn.addEventListener('click', async () => {
    /* In websid mode the USB connection is managed by USBSIDBackendAdapter
     * (registered on this same button by USPlayer). Do not also open the
     * WebUSB usbsidDevice — that would claim the device and prevent the
     * WASM libusb path (emu_connect_USBSID) from opening it. */
    if (_emulator === 'websid') return;
    try {
      if (typeof usbsidDevice === 'undefined') {
        usbsidSetStatus('Driver not loaded', 'red');
        usbsidLog('ERROR: usbsidDevice is undefined — usbsid-driver.js may have failed to load.');
        return;
      }
      if (usbsidDevice.isOpen) {
        await disconnectDevice();
      } else {
        await connectDevice();
      }
    } catch (e) {
      console.error('[USBSID] connect button error:', e);
      usbsidSetStatus('Error: ' + e.message, 'red');
      usbsidLog('Connect button error:', e);
    }
  });
}

/* ── Onboard SID player upload ─────────────────────────── */
async function uploadCurrentSID() {
  if (!usbsidDevice.isOpen) { usbsidSetStatus('Not connected', 'red'); return; }
  if (!_loadedBytes) { usbsidSetStatus('No SID file loaded', 'yellow'); return; }
  const statusEl = document.getElementById('sid-upload-status');
  const btn      = document.getElementById('btn-upload-sid');
  if (btn) btn.disabled = true;
  if (statusEl) statusEl.textContent = 'Uploading\u2026';
  usbsidSetStatus('Uploading SID to device\u2026', 'yellow');
  try {
    await usbsidDevice.uploadSIDFile(_loadedBytes, _currentSubtune, 0x01, (sent, total) => {
      if (statusEl) statusEl.textContent = Math.round(sent / total * 100) + '%';
    });
    if (statusEl) statusEl.textContent = 'Done';
    usbsidSetStatus('SID uploaded and playing on device', 'green');
    _sendsidPlaying = true;
    updateSendSIDPlayButton();
    setPlayerButtons(true);
    usbsidLog('Onboard player: upload complete, playing subtune', _currentSubtune);
  } catch (e) {
    if (statusEl) statusEl.textContent = 'Error';
    usbsidSetStatus('Upload failed: ' + e.message, 'red');
    usbsidLog('Onboard player upload error:', e);
  } finally {
    if (btn) btn.disabled = false;
  }
}

/* ── Transport buttons ─────────────────────────────────── */
function initTransportButtons() {
  const btns = {
    'btn-play':      () => playPause(),
    'btn-stop':      () => stopPlay(),
    'btn-prev-tune': () => prevSubtune(),
    'btn-next-tune': () => nextSubtune(),
    'btn-prev-sid':  () => prevSID(),
    'btn-next-sid':  () => nextSID(),
    /* Onboard player upload */
    'btn-upload-sid': async () => { await uploadCurrentSID(); },
    /* WebUSB player-area buttons */
    'btn-player-toggle-audio': async () => {
      if (usbsidDevice.isOpen) await usbsidDevice.toggleAudio().catch(e => usbsidLog('toggleAudio error:', e));
    },
    'btn-player-hotflip': async () => {
      if (usbsidDevice.isOpen) await usbsidDevice.hotFlipSockets().catch(e => usbsidLog('hotFlip error:', e));
    },
    'btn-player-mute': () => {
      Mute_SID = Mute_SID ? 0 : 1;
      const btn = document.getElementById('btn-player-mute');
      if (btn) btn.textContent = Mute_SID ? 'UNMUTE SID' : 'MUTE SID';
    },
    /* ASID player-area buttons */
    'btn-player-sysex-audio': () => {
      if (typeof sysexCommand === 'function' && selectedMidiOutput) sysexCommand(1);
      else usbsidLog('ASID: no MIDI output selected');
    },
  };
  for (const [id, fn] of Object.entries(btns)) {
    const el = document.getElementById(id);
    if (el) el.addEventListener('click', fn);
  }
  setPlayerButtons(false);
}

/* ── WebUSB device events ──────────────────────────────── */
function initDeviceEvents() {
  if (navigator.usb) {
    navigator.usb.addEventListener('connect', async (e) => {
      if (e.device.vendorId === USBSID_VID && e.device.productId === USBSID_PID) {
        usbsidLog('USB device appeared');
        await reconnectDevice();
      }
    });
    navigator.usb.addEventListener('disconnect', (e) => {
      if (e.device.vendorId === USBSID_VID && e.device.productId === USBSID_PID) {
        usbsidLog('USB device disconnected');
        onDeviceDisconnected();
      }
    });
  }
}

/* ── Browser verification ──────────────────────────────── */
function detectBrowser() {
  if ((navigator.userAgent.indexOf("Opera") || navigator.userAgent.indexOf('OPR')) != -1) {
    _browser = 'opera';
  } else if (navigator.userAgent.indexOf("Edg") != -1) {
    _browser = 'edge';
  } else if (navigator.userAgent.indexOf("Chrome") != -1) {
    _browser = 'chromium';
  } else if (navigator.userAgent.indexOf("Safari") != -1) {
    _browser = 'safari';
  } else if (navigator.userAgent.indexOf("Firefox") != -1) {
    _browser = 'firefox';
  } else if ((navigator.userAgent.indexOf("MSIE") != -1) || (!!document.documentMode == true)) //IF IE > 10
  {
    _browser = 'msie';
    alert('Unsupported browser');
  } else {
    _browser = 'unknown';
    alert('Unsupported browser');
  }
}


/* ── DOMContentLoaded ──────────────────────────────────── */
document.addEventListener('DOMContentLoaded', () => {
  try {

    /* jsSID-webusb.js's DOMContentLoaded has already run at this point.
     * Neutralize its USB connection management so it doesn't conflict
     * with usbsidDevice (both can't claim the same USB interface).
     * Promises queued by autoConnect() haven't resolved yet at this point,
     * so overriding connect/autoConnect here catches those calls too.
     */
    if (typeof webusb === 'object') {
      webusb.autoConnect  = function() {};
      webusb.connect      = function() {};
      webusb.connectNow   = function() {};
      /* Safe stubs — overridden by onDeviceConnected() once connected */
      webusb.writeReg     = function() {};
      webusb.readReg      = function() {};
    }

    detectBrowser();

    initTabs();
    initConnectButton();
    initTransportButtons();
    initFileLoading();
    initSIDList();
    initEmulatorSelect();
    initVolumeSlider();
    initMIDI();
    initConfigUI();   /* from usbsid-config.js */
    initDeviceEvents();

    buildRegGrid();

    updateConnectButtonVisibility();
    updateConfTabVisibility();
    updateRegsTabVisibility();
    updatePlayerSideButtons();

    /* Warn if WebUSB is not available */
    if (!navigator.usb) {
      if (_browser != 'chrome' && _browser != 'edge') {
        usbsidSetStatus('WebUSB unavailable — WebUSB requires an Edge or Chromium based browser', 'yellow');
        usbsidLog('WARNING: navigator.usb is not defined. WebUSB requires an Edge or Chromium based browser. The connect button will not work.');
      } else {
        usbsidSetStatus('WebUSB unavailable — WebUSB requires permissions and a secure context (HTTPS or localhost)', 'yellow');
        usbsidLog('WARNING: navigator.usb is not defined. WebUSB requires permissions and a secure context (HTTPS or localhost). The connect button will not work.');
      }
    } else {
      usbsidSetStatus('Ready — click CONNECT to connect device');
      /* Try auto-reconnect (previously-permitted devices) */
      if ((_emulator !== 'websid') && (_emulator !== 'hermit')) { /* No autoconnect for websid & hermit */
        setTimeout(() => reconnectDevice(), 200);
      }
    }

    if (webusbconnected) {
      usbsidLog('USBSID-Pico Web Config ready');
    }
  } catch (e) {
    console.error('[USBSID] DOMContentLoaded init error:', e);
    usbsidLog('INIT ERROR:', e.message || e);
    usbsidSetStatus('Init error: ' + (e.message || e), 'red');
  }
});
