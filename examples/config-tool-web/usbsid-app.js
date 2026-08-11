/**
 * USBSID-Pico Web Config - Main Application
 * Handles device connection, player integration, and config UI wiring.
 */

'use strict';

/* Globals shared with asid-sysex.js and player.js.
 *
 * asid-sysex.js declares these with 'let', so they must NOT be redeclared here:
 *   webusbplaying, webusbconnected, configavailable, webusb, port, savedport
 * The 'var' declared ones may safely be initialised here.
 */
var webusb_enabled = false;
var Mute_SID       = 0;
var usbsid         = { version: '', nosids: 1, fmoplsid: 0 };

/* setClock(rateId) - was called by the old engine after parsing a SID header,
 * kept because the config paths still use it.
 * rateId: 0=DEFAULT, 1=PAL, 2=NTSC, 3=DREAN (matches usbsidDevice.setClock) */
function setClock(rateId) {
  if (webusb_enabled && usbsidDevice.isOpen) {
    usbsidDevice.setClock(rateId).catch(e => usbsidLog('setClock error:', e));
  }
}

/* App state */
var _browser        = null;
var _player         = null;
var _pcbver         = '';        /* PCB version string from device descriptor */
var _emulator       = 'usplayer';  /* matches the default selected option in index.html */
var _hasSIDPlayer   = false;     /* true when connected device productName contains 'Pico2' */
var _loadedBytes    = null;      /* Uint8Array of the currently loaded SID file */
var _sendsidPlaying = false;     /* playback state for SendSID onboard player mode */
var _currentFile    = null;      /* { url, name } - url is blob: or http: */
var _currentBlob    = null;      /* active Blob URL to revoke on next local load */
var _currentSubtune = 0;
var _maxSubtunes    = 1;
var _sidFiles          = [];    /* entries from SID/sidfilelist.txt */
var _sidFileIdx        = -1;    /* currently selected index in _sidFiles */
var _webusbSidOffset   = 0;     /* SID address offset for WebUSB play-on-SID selector (0x00/0x20/0x40/0x60) */

/* Path to SID library - same directory as this page, then SID/ */
const SID_PATH = (function() {
  const base = window.location.pathname.replace(/\/[^/]*$/, '/');
  return base + 'SID/';
}());

/* Utility: log to debug panel + console */
function usbsidLog(...args) {
  console.log('[USBSID]', ...args);
  const line = args.join(' ') + '\n';
  ['debug-log', 'player-log'].forEach(id => {
    const el = document.getElementById(id);
    if (el) { el.textContent += line; el.scrollTop = el.scrollHeight; }
  });
}

/* Utility: set status bar text */
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

/* Utility: set LED state */
function setLED(connected) {
  const led = document.getElementById('status-led');
  if (!led) return;
  if (connected) led.classList.add('connected');
  else led.classList.remove('connected');
}

/* Tab switching */
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

/* Device connection */
var _connecting = false;   /* guards manual connect vs the load-time autoconnect */
async function connectDevice() {
  if (_connecting || usbsidDevice.isOpen) return;   /* autoconnect may be in flight / already done */
  _connecting = true;
  try {
    await _connectDeviceInner();
  } finally { _connecting = false; }
}
async function _connectDeviceInner() {
  if (!navigator.usb) {
    usbsidSetStatus('WebUSB not available - use HTTPS or localhost', 'red');
    usbsidLog('ERROR: navigator.usb is undefined. WebUSB requires a secure context (HTTPS or localhost).');
    return;
  }
  usbsidSetStatus('Connecting\u2026');
  try {
    const ok = await usbsidDevice.connect();
    if (ok) {
      await onDeviceConnected();
    } else if (usbsidDevice.isOpen) {
      /* autoconnect won the race - reflect the real (connected) state */
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
  if (_connecting || usbsidDevice.isOpen) return;
  _connecting = true;
  try {
    const ok = await usbsidDevice.reconnect();
    if (ok) await onDeviceConnected();
  } finally { _connecting = false; }
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

  /* Point the old engine's write shim at our driver. Nothing calls it any more
   * now that Hermit jsSID is gone (see asid-sysex.js), but it costs nothing and
   * the shim must exist for this assignment not to throw under 'use strict'. */
  webusb.writeReg = function(array) {
    /* Apply SID address offset if user selected a non-zero play-on-SID slot */
    if (_webusbSidOffset > 0 && array && array.length >= 3) {
      array = Array.from(array);
      array[1] = (_webusbSidOffset + (array[1] & 0x1F)) & 0x7F;
    }
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
  webusbconnected = true;   /* kept in sync with our connection state */
  savedport       = 'usbsidpico'; /* non-null -> prevents "Autoconnect not actived yet" alert */

  /* Update connect button IMMEDIATELY - before any async reads so the UI
   * reflects the actual state before the user can click again */
  const btn = document.getElementById('btn-connect');
  if (btn) {
    btn.textContent = 'DISCONNECT';
    btn.classList.remove('c64-btn-connect');
    btn.classList.add('c64-btn-warn');
  }
  usbsidSetStatus('Connected - reading device info\u2026', 'green');
  /* Parse FW and PCB version from USB device descriptor strings - no USB
   * bulk transfers needed.  Both strings are part of the device descriptor
   * and are fetched by the browser during enumeration.
   *   productName:      "USBSID-Pico2 v1.3"           -> PCB ver = last token
   *   manufacturerName: "LouD (v0.7.0-20260308)"       -> FW ver  = text in () */
  const pname  = usbsidDevice.productName;
  const mname  = usbsidDevice.manufacturerName;
  const pcbver = pname.split(' ').pop();                          /* "v1.3" */
  const fwMatch = mname.match(/\(([^)]+)\)/);
  const ver    = fwMatch ? fwMatch[1] : '';                       /* "v0.7.0-20260308" */
  usbsid.version = ver;
  const verEl = document.getElementById('version-display');
  if (verEl) verEl.textContent = 'FW: ' + (ver || '-') + '  PCB: ' + (pcbver || '-');
  usbsidLog('FW version:', ver, '  PCB:', pcbver);

  applyPCBVersionUI(pcbver);
  _pcbver = pcbver;
  let isv15 = isPCBv15plus(pcbver);

  /* v1.5+: check if config acknowledgment is needed */
  if (isv15 && webusbconnected) {
    await checkConfigAck(pcbver);
  }

  /* Detect onboard SID player capability - Pico2 firmware builds only.
   * readFMOplSID() is intentionally deferred to after config load so we only
   * issue it when the config confirms FMOpl is enabled on a SID socket. */
  _hasSIDPlayer = pname.includes('Pico2');
  usbsidLog('USB productName:', JSON.stringify(pname));
  usbsidLog('Onboard SID player:', _hasSIDPlayer ? 'available' : 'not available');

  /* Enable config panel */
  configavailable = true;
  const configPanel = document.getElementById('panel-config');
  if (configPanel) configPanel.classList.remove('c64-hidden');

  usbsidSetStatus('Connected - ' + (ver || pname || 'USBSID-Pico'), 'green');

  updateRegsTabVisibility();
  updateConfTabVisibility();
  updatePlayerSideButtons();

  /* In SendSID mode, enable transport buttons immediately on connect */
  if (_emulator === 'sendsid') {
    if (_hasSIDPlayer) {
      setPlayerButtons(true);
      setLoadButtons(true);
      updateSendSIDPlayButton();
    } else {
      setPlayerButtons(false);
      setLoadButtons(false);
      usbsidSetStatus('Incompatible: SendSID requires Pico 2 firmware', 'red');
    }
  }

  /* The link just came up: a tune loaded while disconnected can play now. */
  refreshTransportButtons();

  /* Auto-read config */
  /* setTimeout(() => doReadConfig(), 300); */
}

/* Check if v1.5+ board needs config confirmation and update UI accordingly */
async function checkConfigAck(pcbver) {
  if (!usbsidDevice.isOpen) return;
  /* Wait for device to finish any post-connect initialisation before sending
   * READ_CONFIGACK. Without this delay the command can arrive while the
   * device endpoint is still draining and the firmware may not respond in
   * time, triggering configCmdRead's reopen path which then causes a disconnect. */
  await us_delay(400);
  if (!usbsidDevice.isOpen) return;  /* may have disconnected during delay */
  try {
    usbsidLog('v1.5+: checking config acknowledgment status…');
    const ack = await usbsidDevice.readConfigAck();
    usbsidLog('Config ACK status:', ack);
    if (ack === 1) {
      /* Show persistent warning banner */
      const warnEl = document.getElementById('warn-confirmation');
      if (warnEl) warnEl.classList.remove('c64-hidden');
      /* Navigate to config tab so user sees the warning in context */
      const configTab = document.querySelector('.c64-tab[data-tab="config"]');
      if (configTab) configTab.click();
      /* Glow on retrieve-config button to guide the user */
      const btnRetrieve = document.getElementById('btn-retrieve-config');
      if (btnRetrieve) btnRetrieve.classList.add('c64-btn-glow');
      usbsidLog('Config needs confirmation - SID socket power is off');
      usbsidSetStatus('Config confirmation required - load and confirm config', 'red');
    }
  } catch (e) {
    usbsidLog('checkConfigAck error:', e);
  }
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

  const cfgHint = document.getElementById('config-status');
  if (cfgHint) cfgHint.textContent = 'Click \u201cRETRIEVE CONFIG\u201d to load the current device configuration.';

  _hasSIDPlayer = false;
  _sendsidPlaying = false;
  _webusbSidOffset = 0;
  /* Clear v1.5 state */
  const warnEl = document.getElementById('warn-confirmation');
  if (warnEl) warnEl.classList.add('c64-hidden');
  const btnRetrieve = document.getElementById('btn-retrieve-config');
  if (btnRetrieve) btnRetrieve.classList.remove('c64-btn-glow');
  applyV15UI('');   /* hides box-v15 and confirm button, resets controls */
  _pcbver = '';
  /* Reset SID address selector */
  const selAddr = document.getElementById('sel-player-sid-addr');
  if (selAddr) { selAddr.innerHTML = '<option value="0">$00</option>'; selAddr.disabled = true; }
  const btnRet = document.getElementById('btn-player-retrieve-numsids');
  if (btnRet) btnRet.disabled = false;
  usbsidSetStatus('Disconnected', 'red');
  usbsidLog('Device disconnected');
  if (_emulator === 'sendsid') {
    setPlayerButtons(false);
    updateSendSIDPlayButton();
  }
  setLoadButtons(true);
  /* The link went away: the transport has to go down with it in every mode
   * that needs the device, not just sendsid. */
  refreshTransportButtons();
  updateRegsTabVisibility();
  updateConfTabVisibility();
  updatePlayerSideButtons();
}

/* ---- SendSID: the board's own player, over either transport --------------- *
 *
 * The onboard player is not the emulation. A file goes to the board and the
 * RP2350 plays it, so all the host does is upload and press buttons, and either
 * transport can carry that: the command encoding is the same and the Web Serial
 * transport implements the same seven step upload.
 *
 * WebUSB is preferred when the app already has the board open, because that
 * connection exists anyway for the config panels and costs no second dialog.
 * Web Serial is what makes SendSID work in Firefox at all.
 */
var _sendsidSerial = null;    /* built on demand, only for this mode */

/** Whichever object can drive the onboard player right now, or null. */
function sendsidDev() {
  if (typeof usbsidDevice !== 'undefined' && usbsidDevice.isOpen) return usbsidDevice;
  if (_sendsidSerial && _sendsidSerial.isOpen) return _sendsidSerial;
  return null;
}

/** Is a SendSID connection up, by either route? */
function sendsidReady() {
  if (typeof usbsidDevice !== 'undefined' && usbsidDevice.isOpen) {
    return _hasSIDPlayer;   /* the descriptor says whether it is a Pico 2 */
  }
  return !!(_sendsidSerial && _sendsidSerial.isOpen);
}

/**
 * Open a serial port for SendSID.
 *
 * Must run inside the click, for requestPort()'s user gesture. Unlike the
 * WebUSB route there is no product string to check, so whether this board has
 * the onboard player firmware cannot be known in advance: probe() confirms it is
 * a USBSID-Pico and the board ignores the player commands if it has no player.
 * Said out loud rather than guessed at.
 */
async function connectSendsidSerial() {
  usbsidLog('SendSID: opening a serial port');
  if (!navigator.serial) {
    usbsidSetStatus('This browser has no Web Serial', 'red');
    return false;
  }
  const { USBSIDWebSerialTransport } =
    await import('./usplayer/usbsid-webserial.js');
  _sendsidSerial = new USBSIDWebSerialTransport();
  let ok = false;
  try {
    ok = await _sendsidSerial.connect();
  } catch (e) {
    usbsidSetStatus('No port chosen', 'yellow');
    usbsidLog('SendSID: no port chosen (' + (e && e.name ? e.name : e) + ')');
    _sendsidSerial = null;
    return false;
  }
  if (!ok) {
    const why = _sendsidSerial.lastError || 'that port did not answer';
    usbsidSetStatus(why, 'red');
    usbsidLog('SendSID: ' + why);
    _sendsidSerial = null;
    return false;
  }
  setLED(true);
  setSerialButton(true);
  usbsidSetStatus('SendSID over Web Serial. Load a tune to upload it.', 'green');
  usbsidLog('SendSID: port open. Whether this board carries the onboard ' +
            'player firmware cannot be read over serial; if nothing plays, ' +
            'that is what to check.');
  setLoadButtons(true);
  setPlayerButtons(true);
  updateSendSIDPlayButton();
  return true;
}

/* Player integration */
function createPlayer(emulator) {
  /* USBSID-Player (WASM) backend for the usplayer / usplayer-asid modes.
   * Reuses the app's already-connected usbsidDevice for the WebUSB variant. */
  if (emulator === 'usplayer' || emulator === 'usplayer-asid' ||
      emulator === 'usplayer-serial' || emulator === 'usplayer-audio') {
    if (typeof window.USPlayerAdapter !== 'undefined') {
      /* Web Serial opens its own port on the board's CDC interface. This app's
       * driver owns a WebUSB device on the vendor interface and knows nothing
       * about CDC, so there is no device to hand over: pass null and let the
       * adapter ask for a port. Software audio has no device at all. */
      const dev = (emulator === 'usplayer-serial' ||
                   emulator === 'usplayer-audio') ? null : usbsidDevice;
      return new window.USPlayerAdapter(emulator, dev);
    }
    usbsidLog('USPlayerAdapter not loaded yet (usplayer/usplayer-adapter.js)');
    return null;
  }
  /* Nothing else builds a player object. SendSID uploads to the board's own
   * onboard player and returns long before this is reached, and the Hermit
   * jsSID and WebSID backends that used to land here are both gone. */
  usbsidLog('No player backend for mode:', emulator);
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
  const p = _player;  /* use existing player only - do NOT call getPlayer() here, as that
                        would create a new player for the current (possibly wrong) emulator */
  if (!p) return;
  subtune = (subtune != 0 ? (subtune - 1) : subtune);
  /* usbsidLog(subtune, timeout, file, callback); */
  p.load(subtune, timeout, file, callback);
  /* A load in usplayer-serial mode may have opened the port itself, in which
   * case nothing else has told the UI. See syncSerialUi(). */
  syncSerialUi();
}

/* Core load - url must be a full URL (blob: or http:) */
async function doLoadSID(url, displayName, subtune) {
  _currentFile    = { url, name: displayName };
  _currentSubtune = subtune != null ? subtune : 1;
  updateRegGridSIDCount(detectSIDCountFromName(displayName));

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
  /* p.load(_currentSubtune, 1000, url, null); */
  _loadTune(_currentSubtune, 1000, url, null);
  webusbplaying = true;

  /* Give the player time to parse the file headers */
  setTimeout(() => {
    try {
      const info = p.getSongInfo();
      _maxSubtunes = Math.max(1, (info.maxSubsong || 0) + 1);
      updateSubtuneDisplay();
      updateMetaDisplay(info);
      usbsidSetStatus('Playing: ' + displayName, 'green');
    } catch (_) {}
    setPlayerButtons(true);
  }, 100);
}

/* Load from local <input type=file> - wraps binary in a Blob URL */
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
  /* The button is disabled without a link, but the spacebar shortcut and any
   * programmatic caller reach this directly. */
  if (!modeLinkReady()) {
    usbsidSetStatus('Connect ' + modeLinkName() + ' first', 'red');
    usbsidLog('Play refused:', _emulator, 'has no', modeLinkName());
    return;
  }
  if (_emulator === 'sendsid') {
    if (!sendsidDev()) return;
    try {
      if (_sendsidPlaying) {
        await sendsidDev().playerPause();
        _sendsidPlaying = false;
      } else {
        await sendsidDev().playerStart();
        _sendsidPlaying = true;
      }
      updateSendSIDPlayButton();
    } catch (e) { usbsidLog('sendsid playPause error:', e); }
    return;
  }
  const p = _player;  /* use existing player only - do NOT call getPlayer() here, as that
                      would create a new player for the current (possibly wrong) emulator */
  if (!p) return;
  try {
    if (!_currentFile) {
      /* nothing loaded yet */
    } else if (!webusbplaying) {
      /* not playing - resume from pause or start fresh */
      p.setVolume(1);
      if (p.paused) {
        p.play();
      } else {
        // p.load(_currentSubtune, 1000, _currentFile.url, null);
        _loadTune(_currentSubtune, 1000, _currentFile.url, null);
      }
      webusbplaying = true;
      syncSerialUi();   /* the resume path too, see syncSerialUi() */
    } else {
      /* currently playing - pause */
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
    if (sendsidDev()) {
      try { await sendsidDev().playerStop(); } catch (e) { usbsidLog('sendsid stop error:', e); }
    }
    _sendsidPlaying = false;
    updateSendSIDPlayButton();
    usbsidSetStatus('Stopped');
    return;
  }
  const p = _player;  /* use existing player only - do NOT call getPlayer() here, as that
                         would create a new player for the current (possibly wrong) emulator */
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
    if (sendsidDev()) {
      try { await sendsidDev().playerPrev(); } catch (e) { usbsidLog('sendsid prev error:', e); }
    }
    return;
  }
  if (_currentSubtune > 1) {
    _currentSubtune--;
    const p = _player;  /* use existing player only - do NOT call getPlayer() here, as that
                         would create a new player for the current (possibly wrong) emulator */
    if (!p) return;
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
    if (sendsidDev()) {
      try { await sendsidDev().playerNext(); } catch (e) { usbsidLog('sendsid next error:', e); }
    }
    return;
  }
  if (_currentSubtune < _maxSubtunes) {
    _currentSubtune++;
    const p = _player;  /* use existing player only - do NOT call getPlayer() here, as that
                         would create a new player for the current (possibly wrong) emulator */
    if (!p) return;
    if (p && _currentFile) {
      // p.load(_currentSubtune, 1000, _currentFile.url, null);
      _loadTune(_currentSubtune, 1000, _currentFile.url, null);
      p.setVolume(1);
    }
    updateSubtuneDisplay();
  }
}

/* Navigate SID list by index - skips non-.sid entries */
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

/* Is a MIDI output actually picked in the ASID selector?  The placeholder
 * options ('- no MIDI -', 'MIDI not supported', '- no MIDI outputs -') all
 * carry an empty value, so a non-empty value means a real port. */
function midiOutputSelected() {
  const sel = document.getElementById('asid-midi-outputs');
  if (!sel || sel.selectedIndex < 0) return false;
  return sel.value !== '';
}

/* Every mode but 'usplayer-audio' plays through something attached: a
 * USBSID-Pico over WebUSB or Web Serial, or a MIDI output for the ASID
 * variants.  Report whether that link is up, so the transport can refuse to
 * start a tune that would go nowhere. */
function modeLinkReady() {
  /* usbsid-driver.js may have failed to load; treat that as no device. */
  const usbOpen = (typeof usbsidDevice !== 'undefined') && !!usbsidDevice.isOpen;
  switch (_emulator) {
    case 'usplayer-audio':  /* pure software, plays out of the browser */
      return true;
    case 'sendsid':         /* needs the onboard player, so Pico 2 firmware */
      /* Either transport can drive it. Over WebUSB the product string says
       * whether this is a Pico 2; over serial there is no such string, so a
       * connected port is taken at face value. */
      return sendsidReady();
    case 'webusb':
    case 'usplayer':
      return usbOpen;
    case 'asid':
    case 'usplayer-asid':
      return midiOutputSelected();
    case 'usplayer-serial':
      /* Its own port, not the app's device, so this mode is usable in a browser
       * with no WebUSB at all. "Ready" means the port is open, not that the API
       * exists: before that there is nowhere for a write to go. */
      if (typeof navigator === 'undefined' || !navigator.serial) return false;
      return !!(_player && typeof _player.isConnected === 'function' &&
                _player.isConnected());
    default:
      return true;
  }
}

/* What to name in the "connect first" message. */
function modeLinkName() {
  switch (_emulator) {
    case 'asid':
    case 'usplayer-asid': return 'a MIDI output';
    case 'usplayer-serial': return 'a serial port for the board';
    case 'sendsid':       return 'a Pico 2 with sidplayer firmware';
    default:              return 'a USBSID-Pico';
  }
}

/* True once a tune is loaded for the current mode.  Kept apart from the link
 * state so the two can be re-combined whenever either one changes. */
var _transportArmed = false;

function setPlayerButtons(enabled) {
  _transportArmed = enabled;
  refreshTransportButtons();
}

/* Transport is live only when a tune is loaded AND the mode's output link is
 * up: playing into nothing is never what the user meant. */
function refreshTransportButtons() {
  const live = _transportArmed && modeLinkReady();
  ['btn-play', 'btn-stop', 'btn-prev-tune', 'btn-next-tune', 'btn-ffwd'].forEach(id => {
    const el = document.getElementById(id);
    if (!el) return;
    /* In SendSID mode the stop button is always enabled so the user can
     * halt playback even after a page refresh when the device is still playing. */
    if (id === 'btn-stop' && _emulator === 'sendsid') { el.disabled = false; return; }
    el.disabled = !live;
  });

  /* The board's audio switch is only offered where it can actually be sent: the
   * two board transports. ASID has no such concept, and a software SID drives its
   * own adapters. Hidden rather than greyed when the mode cannot do it, because a
   * permanently dead button invites clicking. */
  /* Only the two board transports carry this command. ASID is excluded not
   * because it cannot switch the relay, which it can over MIDI sysex, but because
   * it already has its own control for it (btn-player-sysex-audio) and two
   * buttons for one relay is what this change was cleaning up. An explicit
   * whitelist rather than an exclusion list, so a mode added later is hidden
   * until someone decides otherwise. */
  const asw = document.getElementById('btn-audio-switch');
  if (asw) {
    const boardMode = (_emulator === 'usplayer' || _emulator === 'usplayer-serial');
    const can = live && boardMode &&
                !!(_player && typeof _player.hasAudioSwitch === 'function' &&
                   _player.hasAudioSwitch());
    asw.style.display = can ? '' : 'none';
    asw.disabled = !can;
  }
}

/* Enable/disable file browse + URL load buttons (shared across emulator modes) */
function setLoadButtons(enabled) {
  const fileInput = document.getElementById('sid-file-input');
  const urlInput  = document.getElementById('sid-url-input');
  const urlBtn    = document.getElementById('btn-load-url');
  const browseLabel = fileInput && fileInput.closest('label');
  if (fileInput)   fileInput.disabled = !enabled;
  if (urlInput)    urlInput.disabled  = !enabled;
  if (urlBtn)      urlBtn.disabled    = !enabled;
  document.querySelectorAll('.sid-url-preset').forEach(b => b.disabled = !enabled);
  if (browseLabel) browseLabel.style.opacity = enabled ? '' : '0.4';
  if (browseLabel) browseLabel.style.pointerEvents = enabled ? '' : 'none';
}

/* Visibility helpers for emulator-dependent UI */
function updateConnectButtonVisibility() {
  const btn = document.getElementById('btn-connect');
  if (!btn) return;
  const show = (_emulator === 'webusb' || _emulator === 'usplayer' ||
               _emulator === 'usplayer-serial' || _emulator === 'sendsid');
  btn.style.display = show ? '' : 'none';
}

function updateConfTabVisibility() {
  /* Config tab is shown whenever a WebUSB mode is selected
   * Needs an open device, which avoids auto-connect timing races. */
  const tab = document.querySelector('.c64-tab[data-tab="config"]');
  const panel = document.getElementById('panel-regs');
  const show = (_emulator === 'webusb' || _emulator === 'usplayer' ||
               _emulator === 'usplayer-serial' || _emulator === 'sendsid');
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
  /* Registers tab is shown whenever WebUSB mode is selected - no need to wait
   * for device open, which avoids auto-connect timing races. */
  const tab = document.querySelector('.c64-tab[data-tab="regs"]');
  const panel = document.getElementById('panel-regs');
  const show = (_emulator === 'webusb' || _emulator === 'usplayer' ||
               _emulator === 'usplayer-serial');
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
  if (sendsidBtns) sendsidBtns.style.display = (_emulator === 'sendsid' && usbsidDevice.isOpen && _hasSIDPlayer) ? 'flex' : 'none';
}

/* Emulator switching */
function switchEmulator(em) {
  /* Dropping the serial player without closing its port leaves the board's CDC
   * interface claimed by a page that is no longer using it, and nothing else can
   * have it until the tab goes away. */
  if (_emulator === 'usplayer-serial' && em !== 'usplayer-serial' &&
      _player && typeof _player.isConnected === 'function' &&
      _player.isConnected() && _player._transport) {
    try { _player._transport.disconnect(); } catch (_) {}
    setLED(false);
  }
  /* Same for SendSID's own port. */
  if (_emulator === 'sendsid' && em !== 'sendsid' &&
      _sendsidSerial && _sendsidSerial.isOpen) {
    try { _sendsidSerial.disconnect(); } catch (_) {}
    _sendsidSerial = null;
    setLED(false);
  }
  stopPlay();   /* uses _player directly now - safe to call before changing _emulator */
  /* Hide the audio switch straight away rather than waiting for the transport
   * state to settle: refreshTransportButtons() re-shows it if the new mode can
   * carry it, and a button that lingers into ASID mode is worse than one that
   * flickers. */
  const aswNow = document.getElementById('btn-audio-switch');
  if (aswNow) { aswNow.style.display = 'none'; aswNow.disabled = true; }
  _emulator = em;
  _player   = null;
  webusb_enabled = (em === 'webusb');
  localStorage.setItem('usbsid_emulator', em);
  usbsidLog('Emulator switched to:', em);
  /* Show MIDI selector for any ASID mode (jsSID or USBSID-Player) */
  const midiRow = document.getElementById('asid-midi-row');
  if (midiRow) midiRow.style.display = (em === 'asid' || em === 'usplayer-asid') ? '' : 'none';
  /* Show force-socket-2 checkbox only for SendSID mode */
  const socketRow = document.getElementById('sendsid-socket-row');
  if (socketRow) socketRow.style.display = (em === 'sendsid') ? 'inline-flex' : 'none';
  /* Re-evaluate button disabled state now that _emulator is set.
   * This ensures the stop button is active in sendsid mode regardless of
   * connection state (so the user can stop a still-playing device after refresh). */
  setPlayerButtons(false);
  setLoadButtons(true);  /* always restore load buttons on emulator switch; incompatible path re-disables if needed */
  /* Mode-specific prompts and button state */
  if ((em === 'webusb' || em === 'usplayer' || em === 'sendsid') && !usbsidDevice.isOpen) {
    usbsidSetStatus('Connect device for ' + (em === 'sendsid' ? 'SendSID' : 'WebUSB') + ' playback', 'yellow');
  }
  if ((em === 'asid' || em === 'usplayer-asid') && !midiOutputSelected()) {
    usbsidSetStatus('Select a MIDI output for ASID playback', 'yellow');
  }
  if (em === 'usplayer-serial') {
    /* The button means the serial port here, not usbsidDevice, so it starts from
     * whatever this mode's own connection state is rather than the app's. */
    const open = !!(_player && typeof _player.isConnected === 'function' &&
                    _player.isConnected());
    setSerialButton(open);
    if (!open) {
      usbsidSetStatus('Press connect and choose the board\u2019s serial port', 'yellow');
    }
  }
  if (em === 'sendsid' && !navigator.usb) {
    /* No WebUSB, so the button means a serial port here too. */
    const open = !!(_sendsidSerial && _sendsidSerial.isOpen);
    setSerialButton(open);
    if (!open) {
      usbsidSetStatus('Press connect and choose the board\u2019s serial port ' +
                      'to send tunes to the onboard player', 'yellow');
    }
  }
  /* Enable all transport buttons immediately in sendsid mode when already connected */
  if (em === 'sendsid' && typeof usbsidDevice !== 'undefined' && usbsidDevice.isOpen) {
    if (_hasSIDPlayer) {
      setPlayerButtons(true);
      setLoadButtons(true);
      updateSendSIDPlayButton();
    } else {
      setPlayerButtons(false);
      setLoadButtons(false);
      usbsidSetStatus('Incompatible: SendSID requires a Pico2 with onboard sidplayer firmware', 'red');
    }
  }
  updateConnectButtonVisibility();
  updateConfTabVisibility();
  updateRegsTabVisibility();
  updatePlayerSideButtons();
}

/* Volume control */
function setVolume(val) {
  const p = _player;  /* use existing player only - do NOT call getPlayer() here, as that
                      would create a new player for the current (possibly wrong) emulator */
  if (!p) return;
  if (p && p.setVolume) p.setVolume(val / 100);
}

/* SID register display */
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

  for (let sid = 0; sid < 4; sid++) {
    const section = document.createElement('div');
    section.id = 'sid-section-' + sid;
    if (sid > 0) section.style.display = 'none';

    const title = document.createElement('div');
    title.className = 'c64-box-title';
    title.textContent = 'SID ' + (sid + 1);
    section.appendChild(title);

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
      const name = document.createElement('div');
      name.className = 'reg-name';
      name.textContent = SID_REG_NAMES[r] || '';
      const val = document.createElement('div');
      val.className = 'reg-val';
      val.textContent = '00';
      cell.appendChild(addr);
      cell.appendChild(name);
      cell.appendChild(val);
      grid.appendChild(cell);
    }
    section.appendChild(grid);
    container.appendChild(section);
  }
}

/* Show/hide SID register panels to match the number of SIDs a tune uses */
function updateRegGridSIDCount(n) {
  for (let sid = 0; sid < 4; sid++) {
    const sec = document.getElementById('sid-section-' + sid);
    if (sec) sec.style.display = sid < n ? '' : 'none';
  }
}

/* Detect SID count from filename: 2sid->2, 3sid->3, 4sid->4, default 1 */
function detectSIDCountFromName(name) {
  if (!name) return 1;
  const m = name.match(/([234])sid/i);
  return m ? parseInt(m[1], 10) : 1;
}

/* Register grid, made cheap enough to drive from a player.
 *
 * This used to do a getElementById, a querySelector, a string format and a
 * setTimeout on every call. Driven by the USBSID-Player adapter that is one
 * per SID write, about thirty thousand a second on a digi, and it held the tab
 * at 102% of a core with twelve thousand timers pending at any moment. The
 * adapter now coalesces its writes, but the per call cost was worth removing
 * anyway: jsSID drives this too.
 *
 * The cells are static in index.html, so their elements are looked up once.
 * The highlight is swept by one shared timer instead of a timer per call. */
const _REG_HEX = Array.from({ length: 256 },
  (_, i) => i.toString(16).padStart(2, '0').toUpperCase());
const _regCells = new Map();
const _regFlash = new Map();   /* cell -> when the highlight expires */
let   _regSweep = 0;

/* Short enough to read as a flash rather than a light that is always on. At
 * twenty updates a second a 400 ms highlight never goes out on a busy tune. */
const _REG_FLASH_MS = 180;

function _regEls(sid, reg) {
  const key = (sid << 5) | reg;
  let e = _regCells.get(key);
  if (e !== undefined) return e;
  const cell = document.getElementById('sreg-' + sid + '-' + reg);
  e = cell ? { cell, valEl: cell.querySelector('.reg-val') } : null;
  _regCells.set(key, e);
  return e;
}

function _regSweepTick() {
  const now = performance.now();
  for (const [cell, until] of _regFlash) {
    if (until > now) continue;
    cell.classList.remove('reg-written');
    _regFlash.delete(cell);
  }
  if (_regFlash.size === 0) { clearInterval(_regSweep); _regSweep = 0; }
}

function updateSIDReg(sid, reg, val) {
  const e = _regEls(sid, reg);
  if (!e || !e.valEl) return;

  const txt = _REG_HEX[val & 0xff];
  if (e.valEl.textContent !== txt) e.valEl.textContent = txt;

  if (!_regFlash.has(e.cell)) e.cell.classList.add('reg-written');
  _regFlash.set(e.cell, performance.now() + _REG_FLASH_MS);
  if (!_regSweep) _regSweep = setInterval(_regSweepTick, 60);
}

/* Exposed for the USBSID-Player adapter (usplayer/usplayer-adapter.js), whose
 * writes bypass the webusb.writeReg path used by jsSID. It taps every player
 * write to keep the live register grid in sync. */
window.updateSIDReg = updateSIDReg;
window.updateRegGridSIDCount = updateRegGridSIDCount;


/* SID library list (SID/sidfilelist.txt) */
async function initSIDList() {
  const sel = document.getElementById('sid-list-select');
  if (!sel) return;
  /* On touch devices (Firefox Android etc.) a multi-row <select> triggers an
   * extra native picker overlay, causing a double-select. Use a single-row
   * dropdown instead - the native picker is the correct UX on mobile anyway. */
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
    const sections = [];  /* { name, headingOptIdx } - populated below */
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
        sections.push({ name: entry, headingOptIdx: i });
      }
      sel.appendChild(opt);
    });

    const countEl = document.getElementById('sid-list-count');
    if (countEl) countEl.textContent = '(' + count + ' files)';

    /* Section jump buttons - recreate on every list load */
    let sectionBar = document.getElementById('sid-section-buttons');
    if (!sectionBar) {
      sectionBar = document.createElement('div');
      sectionBar.id = 'sid-section-buttons';
      sectionBar.style.cssText = 'display:flex;flex-wrap:wrap;gap:4px;margin-top:6px';
      sel.insertAdjacentElement('afterend', sectionBar);
    }
    sectionBar.innerHTML = '';
    sections.forEach(({ name, headingOptIdx }) => {
      const btn = document.createElement('button');
      btn.className = 'c64-btn c64-btn-sm';
      btn.textContent = name;
      btn.style.fontSize = '0.75rem';
      btn.addEventListener('click', () => {
        /* Scroll so the section heading sits at the top of the visible list.
         * scrollHeight / options.length gives the per-option row height.
         * We do NOT set selectedIndex - no change event, no auto-play. */
        if (sel.options.length === 0) return;
        const rowH = sel.scrollHeight / sel.options.length;
        sel.scrollTop = Math.round(rowH * headingOptIdx);
      });
      sectionBar.appendChild(btn);
    });

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

/* File loading */
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

  /* Prefill buttons: drop the example base url in the input and park the
   * caret at the end so the file name can be typed/pasted straight after. */
  document.querySelectorAll('.sid-url-preset').forEach(btn => {
    btn.addEventListener('click', () => {
      if (!urlInput || urlInput.disabled) return;
      urlInput.value = btn.dataset.url || '';
      urlInput.focus();
      urlInput.setSelectionRange(urlInput.value.length, urlInput.value.length);
    });
  });

  if (urlInput && urlButton) {
    urlButton.addEventListener('click', async () => {
      const url = urlInput.value.trim();
      if (!url) return;
      const name = url.split('/').pop().split('?')[0] || 'remote.sid';
      usbsidSetStatus('Fetching: ' + name + '\u2026');
      /* CORS proxy fallbacks - tried in order when direct fetch is blocked.
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
          /* 2. CORS blocked - try each proxy in turn */
          let lastErr = corsErr;
          for (const makeProxy of CORS_PROXIES) {
            const proxyUrl = makeProxy(url);
            usbsidSetStatus('CORS blocked - retrying via proxy\u2026', 'yellow');
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
          throw new Error('not a SID file (got "' + magic + '") - URL may point to an HTML page or redirect');
        }
        await loadSID(bytes, name);
      } catch (e) {
        usbsidSetStatus('Load failed: ' + e.message + ' - download the file and use Browse', 'red');
        usbsidLog('URL load error:', e);
      }
    });
  }
}

/* ASID MIDI port listing.
 *
 * This is now the only thing that populates #asid-midi-outputs: the old engine
 * used to repopulate it whenever an ASID player was created, resetting
 * selectedMidiOutput to outputs[0] and losing the user's choice. The
 * MutationObserver below was the fix for that and is kept as a guard: the ASID
 * transport in usplayer/asid-midi.js reads the selected option by name and does
 * not touch the list, so it should never fire now. */
var _savedAsidMidiIdx = parseInt(localStorage.getItem('usbsid_asid_midi_idx') || '0', 10);

function initMIDI() {
  const sel = document.getElementById('asid-midi-outputs');
  if (!sel) return;
  if (!navigator.requestMIDIAccess) {
    sel.innerHTML = '<option value="">MIDI not supported</option>';
    return;
  }
  navigator.requestMIDIAccess({ sysex: true }).then(access => {
    /* Share the MIDIAccess with asid-sysex.js, which sends the sysex config
     * commands over the selected output. */
    if (typeof midiAccessObj !== 'undefined') window.midiAccessObj = access;
    sel.innerHTML = '';
    const outputs = Array.from(access.outputs.values());
    if (!outputs.length) {
      sel.innerHTML = '<option value="">- no MIDI outputs -</option>';
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
        refreshTransportButtons();  /* an ASID mode's link is its MIDI port */
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
      refreshTransportButtons();
    }).observe(sel, { childList: true });
    /* Ports were only discovered now, after switchEmulator ran. */
    refreshTransportButtons();
  }).catch(e => {
    usbsidLog('MIDI access error:', e);
  });
}

/**
 * Which modes this browser can actually run, and disable the rest.
 *
 * Not cosmetic. Chromium has WebUSB and Web Serial and can do everything.
 * **Firefox has Web Serial and no WebUSB**, and no plans for it, so every mode
 * that reaches the board through `usbsidDevice` is dead there: this whole config
 * tool's driver is WebUSB, so the panels do not work either, but the player can
 * still play over Web Serial. Leaving those modes selectable means picking one
 * and getting silence with no explanation.
 *
 * `usplayer-audio` needs nothing, it synthesises in the page and plays through
 * Web Audio. The ASID modes need Web MIDI, which Firefox does not have by
 * default either, so they are checked separately rather than assumed.
 */
function applyBrowserSupportToModes() {
  const sel = document.getElementById('sel-emulator');
  if (!sel) return;
  const hasUsb    = (typeof navigator !== 'undefined') && !!navigator.usb;
  const hasSerial = (typeof navigator !== 'undefined') && !!navigator.serial;
  const hasMidi   = (typeof navigator !== 'undefined') && !!navigator.requestMIDIAccess;

  /* What each mode needs to reach a board at all. */
  const needs = {
    'usplayer':        hasUsb,
    'usplayer-serial': hasSerial,
    'usplayer-asid':   hasMidi,
    'webusb':          hasUsb,
    /* Either transport drives the onboard player, so this mode survives a
     * browser with no WebUSB. */
    'sendsid':         hasUsb || hasSerial,
    'asid':            hasMidi,
    'usplayer-audio':  true,
  };
  const why = {
    'usplayer': 'WebUSB', 'webusb': 'WebUSB',
    'sendsid': 'WebUSB or Web Serial',
    'usplayer-serial': 'Web Serial',
    'usplayer-asid': 'Web MIDI', 'asid': 'Web MIDI',
  };

  let firstUsable = null;
  for (const opt of sel.options) {
    const ok = (needs[opt.value] !== false);
    opt.disabled = !ok;
    /* Say which API is missing, once. The label is the only place the user
     * finds out, since a disabled option cannot be clicked to get an error. */
    if (!ok && !/ - no /.test(opt.textContent)) {
      opt.textContent += ` - no ${why[opt.value] || 'support'} here`;
    }
    if (ok && firstUsable === null) firstUsable = opt.value;
  }

  /* A disabled option can still be the selected one, restored from
   * localStorage or left as the markup default, and then nothing works and the
   * selector looks fine. Move off it. */
  const current = sel.querySelector(`option[value="${sel.value}"]`);
  if (current && current.disabled && firstUsable !== null) {
    usbsidLog(`Mode ${sel.value} needs ${why[sel.value] || 'support'} this ` +
              `browser does not have, switching to ${firstUsable}`);
    sel.value = firstUsable;
  }
  if (!hasUsb) {
    usbsidSetStatus('No WebUSB in this browser: the config panels need it. ' +
                    'The player works over Web Serial.', 'yellow');
  }
}

/* Emulator select */
function initEmulatorSelect() {
  const sel = document.getElementById('sel-emulator');
  if (!sel) return;
  /* Before anything reads the selection: disabling has to happen first so the
   * restore below cannot land on a mode this browser cannot run. */
  applyBrowserSupportToModes();
  usbsidLog('Selected emulator:', sel.value);
  /* Restore last-used emulator from localStorage, but only if this browser can
   * run it: a mode saved in Chrome must not be restored into Firefox. */
  const saved = localStorage.getItem('usbsid_emulator');
  usbsidLog('Saved emulator:', saved);
  const savedOpt = saved && sel.querySelector('option[value="' + saved + '"]');
  if (savedOpt && !savedOpt.disabled) {
    sel.value = saved;
  } else if (saved) {
    usbsidLog('Saved emulator', saved, 'is not available here, keeping', sel.value);
  }
  /* Sync app state to the (restored or default) selection. */
  switchEmulator(sel.value);
  sel.addEventListener('change', () => {
    switchEmulator(sel.value);
  });
}

/* Volume slider */
function initVolumeSlider() {
  const slider = document.getElementById('volume-slider');
  if (!slider) return;
  slider.addEventListener('input', () => setVolume(parseInt(slider.value, 10)));
}

/* Connect button */
function initConnectButton() {
  const btn = document.getElementById('btn-connect');
  if (!btn) { console.error('[USBSID] btn-connect not found'); return; }
  btn.addEventListener('click', async () => {
    /* Web Serial mode reaches the board over its CDC interface and does not use
     * usbsidDevice at all, so opening a WebUSB device here would show a second
     * picker for a connection playback never uses, and would show it *first*.
     * Ask for the port instead, from this click, which is the gesture
     * requestPort() needs.
     *
     * The config panels are a separate matter: they are WebUSB and there is no
     * Web Serial path to them, so in this mode they stay unavailable and the
     * status line says so rather than a second dialog appearing unbidden. */
    if (_emulator === 'usplayer-serial') {
      await connectSerialPlayer();
      return;
    }
    /* SendSID prefers the WebUSB device, since the config panels need it anyway
     * and it costs no extra dialog. With no WebUSB at all, the onboard player is
     * still reachable over serial, and that is the only route in Firefox. */
    if (_emulator === 'sendsid' && !navigator.usb) {
      if (_sendsidSerial && _sendsidSerial.isOpen) {
        try { await _sendsidSerial.disconnect(); } catch (_) {}
        _sendsidSerial = null;
        setLED(false); setSerialButton(false); setPlayerButtons(false);
        usbsidSetStatus('Serial port closed');
      } else {
        await connectSendsidSerial();
      }
      return;
    }
    try {
      if (typeof usbsidDevice === 'undefined') {
        usbsidSetStatus('Driver not loaded', 'red');
        usbsidLog('ERROR: usbsidDevice is undefined - usbsid-driver.js may have failed to load.');
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

/**
 * Open the board's serial port for the Web Serial player mode.
 *
 * Must run inside the click: `requestPort()` will not show a picker otherwise.
 * The adapter owns the transport, so it does the asking; all this does is drive
 * it from the button and put the answer on screen.
 */
async function connectSerialPlayer() {
  usbsidLog('Web Serial: connect requested');
  if (!navigator.serial) {
    usbsidSetStatus('This browser has no Web Serial', 'red');
    usbsidLog('ERROR: navigator.serial is undefined. Web Serial needs Firefox ' +
              '151 or later, or Chrome/Edge 89 or later, over HTTPS or localhost.');
    return;
  }
  const p = getPlayer();
  if (!p || typeof p.connect !== 'function') {
    usbsidSetStatus('USBSID-Player is not loaded yet, try again', 'yellow');
    usbsidLog('ERROR: USPlayerAdapter is not loaded, so there is nothing to ' +
              'open a port with. usplayer/usplayer-adapter.js may have failed.');
    return;
  }
  if (p.isConnected()) {          /* already open: this click means disconnect */
    if (typeof p.stop === 'function') p.stop();
    if (p._transport && p._transport.disconnect) await p._transport.disconnect();
    setLED(false);
    setSerialButton(false);
    setPlayerButtons(false);
    usbsidSetStatus('Serial port closed');
    return;
  }
  usbsidSetStatus('Choose the board\u2019s serial port\u2026', 'yellow');
  try {
    const ok = await p.connect();
    if (!ok) {
      const why = p.lastError || 'that port did not answer as a USBSID-Pico';
      usbsidSetStatus(why, 'red');
      usbsidLog('Web Serial: ' + why);
      setLED(false);
      return;
    }
  } catch (e) {
    /* Cancelling the picker lands here, and is not an error worth shouting. */
    usbsidSetStatus('No port chosen', 'yellow');
    usbsidLog('Web Serial: no port chosen (' + (e && e.name ? e.name : e) + ')');
    return;
  }
  usbsidLog('Web Serial: port open');
  setLED(true);
  setSerialButton(true);
  usbsidSetStatus('Connected over Web Serial. The config panels need WebUSB ' +
                  'and stay unavailable in this mode.', 'green');
  setLoadButtons(true);
  refreshTransportButtons();
}

/** The connect button, for the serial mode's own idea of connected. */
/**
 * Reconcile the Web Serial UI with whether the port is actually open.
 *
 * The port can be opened by either of two routes: the CONNECT button, which
 * calls connectSerialPlayer() and updates the UI itself, or **lazily by a load**,
 * which is what happens when a tune is picked in usplayer-serial mode without
 * connecting first. Only the first route ever touched the UI, so the second left
 * CONNECT reading "CONNECT" and the transport controls greyed out while the tune
 * played perfectly well. That is the bug LouD hit after switching from SendSID.
 *
 * So this reads the truth rather than being told, and is called after anything
 * that might have opened or closed the port. Cheap and idempotent, which is what
 * lets it be called from more than one place without reasoning about order.
 */
function syncSerialUi() {
  if (_emulator !== 'usplayer-serial') return;
  const open = !!(_player && typeof _player.isConnected === 'function' &&
                  _player.isConnected());
  setSerialButton(open);
  if (open) {
    setPlayerButtons(true);
    setLoadButtons(true);
    setLED(true);
  }
}

/**
 * Wire the two controls the transport row was missing.
 *
 * Fast forward is press and hold rather than a toggle, which is what the CLI's
 * `f` key does and what a seek control should feel like: releasing it puts the
 * speed back rather than leaving the tune running fast because a second click was
 * missed. pointerup and pointerleave both release it, since a pointer dragged off
 * the button never sends pointerup to it.
 */
function wireExtraTransport() {
  const ff = document.getElementById('btn-ffwd');
  if (ff && !ff._wired) {
    ff._wired = true;
    let held = false;
    const set = (on) => {
      /* Idempotent. The first version logged on every event, which is what showed
       * up the bug: `off` lines arrived with no `on` before them, because
       * pointerleave fires on plain hover-out and not only during a press. */
      if (on === held) return;
      if (!_player) { usbsidLog('FFWD: no player'); return; }
      if (typeof _player.fastForward !== 'function' &&
          typeof _player.setSpeed !== 'function') {
        usbsidLog('FFWD: this player has neither fastForward nor setSpeed');
        return;
      }
      held = on;
      if (typeof _player.fastForward === 'function') _player.fastForward(on);
      else _player.setSpeed(on ? 4 : 1);
      ff.classList.toggle('c64-btn-warn', on);
    };

    /* Pointer capture, and **no pointerleave**. That combination is the whole
     * fix.
     *
     * pointerleave fires whenever the pointer crosses out of the element,
     * pressed or not, so it was turning the hold off the moment the mouse moved
     * a pixel, and the class toggle above shifts the button enough on its own to
     * cause that. LouD's log showed it plainly: `on (4x)` immediately followed by
     * `off`, and bare `off` lines from simply hovering across the button.
     *
     * With the pointer captured the element keeps receiving events until the
     * press ends wherever it ends, so leaving is no longer an event worth
     * listening for. lostpointercapture is the backstop for a capture taken away
     * by the browser. */
    ff.addEventListener('pointerdown', (e) => {
      if (ff.setPointerCapture) {
        try { ff.setPointerCapture(e.pointerId); } catch (_) {}
      }
      set(true);
    });
    ff.addEventListener('pointerup', () => set(false));
    ff.addEventListener('pointercancel', () => set(false));
    ff.addEventListener('lostpointercapture', () => set(false));
    /* A press that ends outside any element, or a tab switch mid hold, would
     * otherwise leave the tune running at 4x for ever. */
    window.addEventListener('blur', () => set(false));
  }

  const asw = document.getElementById('btn-audio-switch');
  if (asw && !asw._wired) {
    asw._wired = true;
    /* The board is the authority on its own switch and cannot be read back over
     * serial, so this tracks what it was last told rather than claiming to know.
     * Starts at mono, which is the firmware default (`stereo_en = false`). */
    asw._stereo = false;
    asw.addEventListener('click', () => {
      if (!_player || typeof _player.setAudioSwitch !== 'function') return;
      const want = !asw._stereo;
      if (_player.setAudioSwitch(want) === false) {
        usbsidLog('This transport cannot set the audio switch');
        return;
      }
      asw._stereo = want;
      asw.textContent = want ? 'STEREO' : 'MONO';
      usbsidLog('Board audio switch:', want ? 'stereo' : 'mono',
                '(v1.3+ PCBs; older boards ignore it)');
    });
  }
}

function setSerialButton(on) {
  const btn = document.getElementById('btn-connect');
  if (!btn) return;
  btn.textContent = on ? 'DISCONNECT' : 'CONNECT';
  btn.classList.toggle('c64-btn-warn', on);
  btn.classList.toggle('c64-btn-connect', !on);
}

/* Onboard SID player upload */
async function uploadCurrentSID() {
  const dev = sendsidDev();
  if (!dev) { usbsidSetStatus('Not connected', 'red'); return; }
  /* Only the WebUSB route can tell a Pico from a Pico 2 before trying. */
  if (dev === usbsidDevice && !_hasSIDPlayer) {
    usbsidSetStatus('Incompatible: SendSID requires Pico 2 firmware', 'red'); return;
  }
  if (!_loadedBytes) { usbsidSetStatus('No SID file loaded', 'yellow'); return; }
  const statusEl = document.getElementById('sid-upload-status');
  const btn      = document.getElementById('btn-upload-sid');
  if (btn) btn.disabled = true;
  if (statusEl) statusEl.textContent = 'Uploading\u2026';
  usbsidSetStatus('Uploading SID to device\u2026', 'yellow');
  try {
    /* Send SID_PLAYER_TWO before load if the user requested socket 2 playback */
    const forceTwo = document.getElementById('chk-sendsid-socket2');
    if (forceTwo && forceTwo.checked) {
      await sendsidDev().playerSocketTwo();
    }
    await sendsidDev().uploadSIDFile(_loadedBytes, _currentSubtune, 0x01, (sent, total) => {
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

/* Transport buttons */
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
    /* WebUSB player-area buttons.
     *
     * The old TOGGLE AUDIO button was here and is gone: the transport row's
     * btn-audio-switch does the same relay and does it over Web Serial as well,
     * which this one could not because it went through the app's WebUSB driver
     * rather than the player's transport. The SendSID and ASID modes keep their
     * own audio buttons, since btn-audio-switch is deliberately hidden in both. */
    'btn-player-hotflip': async () => {
      if (usbsidDevice.isOpen) await usbsidDevice.hotFlipSockets().catch(e => usbsidLog('hotFlip error:', e));
    },
    'btn-player-mute': () => {
      Mute_SID = Mute_SID ? 0 : 1;
      const btn = document.getElementById('btn-player-mute');
      if (btn) btn.textContent = Mute_SID ? 'UNMUTE SID' : 'MUTE SID';
    },
    /* SendSID player-area buttons */
    'btn-player-sendsid-toggle-audio': async () => {
      if (usbsidDevice.isOpen) await usbsidDevice.toggleAudio().catch(e => usbsidLog('toggleAudio error:', e));
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

  /* WebUSB: retrieve number of SIDs and populate address selector */
  const btnRetrieve = document.getElementById('btn-player-retrieve-numsids');
  const selAddr     = document.getElementById('sel-player-sid-addr');
  if (btnRetrieve && selAddr) {
    btnRetrieve.addEventListener('click', async () => {
      if (!usbsidDevice.isOpen) return;
      const n = await usbsidDevice.readNumSIDs().catch(() => 1);
      usbsidLog('WebUSB: number of SIDs =', n);
      selAddr.innerHTML = '';
      const addrs = [0x00, 0x20, 0x40, 0x60];
      const labels = ['$00 (SID 1)', '$20 (SID 2)', '$40 (SID 3)', '$60 (SID 4)'];
      for (let i = 0; i < Math.max(n, 1) && i < 4; i++) {
        const opt = document.createElement('option');
        opt.value = addrs[i];
        opt.textContent = labels[i];
        selAddr.appendChild(opt);
      }
      selAddr.disabled = false;
      btnRetrieve.disabled = true;
    });
    selAddr.addEventListener('change', () => {
      _webusbSidOffset = parseInt(selAddr.value, 10) || 0;
      usbsidLog('WebUSB: SID play address offset =', '0x' + _webusbSidOffset.toString(16));
    });
  }

  setPlayerButtons(false);
}

/* WebUSB device events */
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

/* Browser verification */
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


/* DOMContentLoaded */
document.addEventListener('DOMContentLoaded', () => {
  try {

    /* The old engine managed its own USB connection and would fight
     * usbsidDevice for the same interface, so its entry points were stubbed out
     * here. The engine is gone; the stubs stay because `webusb` is still the
     * object onDeviceConnected() writes its shim into. */
    if (typeof webusb === 'object') {
      webusb.autoConnect  = function() {};
      webusb.connect      = function() {};
      webusb.connectNow   = function() {};
      /* Safe stubs - overridden by onDeviceConnected() once connected */
      webusb.writeReg     = function() {};
      webusb.readReg      = function() {};
    }

    detectBrowser();

    initTabs();
    initConnectButton();
    initTransportButtons();
    wireExtraTransport();   /* fast forward and the board's audio switch */
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

    /* Warn if WebUSB is not available.
     *
     * Only where it matters. With a Web Serial mode selected the connect button
     * works perfectly well and says so a few lines below, so "the connect button
     * will not work" is both wrong and the last thing written to the status line,
     * which is how a working mode came to look broken in Firefox. */
    if (!navigator.usb && _emulator !== 'usplayer-serial' &&
        _emulator !== 'sendsid') {
      if (_browser != 'chrome' && _browser != 'edge') {
        usbsidSetStatus('WebUSB unavailable - WebUSB requires an Edge or Chromium based browser', 'yellow');
        usbsidLog('WARNING: navigator.usb is not defined. WebUSB requires an Edge or Chromium based browser. The connect button will not work.');
        if (navigator.serial) {
          usbsidLog('This browser does have Web Serial: choose the Web Serial ' +
                    '(USBSID-Player) mode to play. The config panels are WebUSB ' +
                    'only and stay unavailable.');
        }
      } else {
        usbsidSetStatus('WebUSB unavailable - WebUSB requires permissions and a secure context (HTTPS or localhost)', 'yellow');
        usbsidLog('WARNING: navigator.usb is not defined. WebUSB requires permissions and a secure context (HTTPS or localhost). The connect button will not work.');
      }
    } else if (!navigator.usb) {
      /* A Web Serial capable mode in a browser with no WebUSB: nothing wrong. */
      usbsidLog('No WebUSB here, which this mode does not need. The config ' +
                'panels are WebUSB only and stay unavailable.');
      usbsidSetStatus('Press connect and choose the board\u2019s serial port', 'yellow');
    } else {
      usbsidSetStatus('Ready - click CONNECT to connect device');
      /* Try auto-reconnect (previously-permitted devices) */
      /* No autoconnect for software audio, and none for the Web Serial mode
       * either: claiming the WebUSB device there gains nothing and it was what
       * put a WebUSB picker in front of the serial one. */
      if ((_emulator !== 'usplayer-audio') &&
          (_emulator !== 'usplayer-serial')) {
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
