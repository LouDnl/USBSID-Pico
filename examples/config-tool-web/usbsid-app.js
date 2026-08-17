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
async function doLoadSID(url, displayName, subtune, lengths) {
  if (loadRefused()) return;
  _currentFile    = { url, name: displayName };
  _currentSubtune = subtune != null ? subtune : 1;
  /* Song lengths, if the caller has them. A tune out of the served library
   * does: SID/filelist.bb looked it up in songlengths.md5 and put the
   * milliseconds per song into sidfilelist.json. Anything else, a local folder,
   * an upload, a URL, cannot be known in advance and is looked up after the
   * load without holding it up. See resolveSongLengths(). */
  setSongLengths(lengths || null);
  /* Drop the previous tune's notes now rather than when the new ones arrive:
   * during the fetch the name above has already changed, and notes belonging to
   * the tune before it sitting under it read as this tune's. */
  _stilSeq++;
  _stilEntry = null;
  renderStil();
  const gen = ++_loadGen;
  _loading = true;
  updatePlaytimeDisplay();
  updateRegGridSIDCount(detectSIDCountFromName(displayName));

  /* SendSID mode: fetch bytes and upload to onboard player */
  if (_emulator === 'sendsid') {
    setNowPlayingName(displayName);
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

  setNowPlayingName(displayName);

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
      /* Subtune 0 asks for the file's own default song, and for plenty of tunes
       * that is not song 1: Mechanicus starts at song 3 of 18, which the
       * command line player has always shown correctly. The page assumed 1, so
       * it read "Tune 1/18" over song 3 and took the wrong song's length. */
      if (info.song && subtune == null) _currentSubtune = info.song;
      updateSubtuneDisplay();
      updateMetaDisplay(info);
      usbsidSetStatus('Playing: ' + displayName, 'green');
    } catch (_) {}
    setPlayerButtons(true);
    /* Only the newest load may declare itself playing. A slower earlier fetch
     * landing after a later one must not take the counter back. */
    if (gen === _loadGen) _loading = false;
    updatePlaytimeDisplay();
    /* Not awaited: see resolveSongLengths(). A tune out of the library arrived
     * with its lengths already and needs nothing here. */
    if (!_songLengths) resolveSongLengths();
    /* Unconditional, unlike the lengths: nothing arrives with its STIL notes
     * already, and the lookup needs only the MD5 the player just computed. */
    resolveStil();
  }, 100);
}

/* Load from local <input type=file> - wraps binary in a Blob URL */
async function loadSID(fileData, fileName) {
  /* Checked here as well as in doLoadSID(): the SendSID branch below uploads to
   * the board and returns without going near it. */
  if (loadRefused()) return;
  _loadedBytes = fileData;  /* keep raw bytes for onboard player upload */
  /* SendSID mode: upload directly, skip software player */
  if (_emulator === 'sendsid') {
    _currentFile    = { url: null, name: fileName };
    _currentSubtune = 1;
    setNowPlayingName(fileName);
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
        /* The poll is gated on _sendsidPlaying, so it stops by itself and the
         * clock holds where it was. */
      } else {
        await sendsidDev().playerStart();
        _sendsidPlaying = true;
        startSendsidTimeTimer();
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
    /* Polling stops, the last position stays on screen. The firmware freezes its
     * own figure the same way, so the two agree. */
    stopSendsidTimeTimer();
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
      await sendsidSubtuneChanged(-1);
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
    /* A different song, so a different length and a fresh end of song guard.
     * Disarmed as well as reset: this reloads the file, so for as long as that
     * takes the position still belongs to the song we are leaving. */
    _advancedAt = -1;
    _endArmed = false;
    updatePlaytimeDisplay();
  }
}

async function nextSubtune() {
  if (_emulator === 'sendsid') {
    if (sendsidDev()) {
      try { await sendsidDev().playerNext(); } catch (e) { usbsidLog('sendsid next error:', e); }
      await sendsidSubtuneChanged(1);
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
    /* A different song, so a different length and a fresh end of song guard.
     * Disarmed as well as reset: this reloads the file, so for as long as that
     * takes the position still belongs to the song we are leaving. */
    _advancedAt = -1;
    _endArmed = false;
    updatePlaytimeDisplay();
  }
}

/* ── Onboard player mute ───────────────────────────────────────────────────
 *
 * SID_PLAYER_MUTE takes a chip and a voice. Two forms work: chip 0 with voice 0
 * for everything, and chip 1 to 4 with voice 1 to 3 for one voice.
 *
 * There is no working whole-chip form. set_mutestate() rejects chip 2 to 4 with
 * voice 0, and chip 1 with voice 0 passes validation only to reach
 * Mos6581_8580::set_voice_mute(), which requires voice 1 to 3 and returns without
 * doing anything. So a chip is muted by sending its three voices, which is what
 * the driver's playerMuteChip() does and what the CHIP buttons here use.
 *
 * The board is the authority on what is muted: SID_PLAYER_MUTED reads it back as
 * one bitmask per chip. The buttons show that state rather than a guess, because
 * the two can part company easily enough (a new tune, a stop, another client).
 */
var _muteMask = [0, 0, 0, 0];   /* per chip, bits 0..2 = voices 1..3 */
/* Whole chips held silent, bit 0 for chip one. Separate from the voice masks
 * because it is a different mechanism: a muted chip has its writes dropped, which
 * is what silences a tune playing samples through the volume register. Muting all
 * three voices does not, because voice mute never touches $18. */
var _muteChipMask = 0;
var _muteChips = 1;             /* how many chip rows to offer */
var _muteSockets = { one: 0, two: 0 };  /* chips per socket, for the row labels */
var _muteFmopl = 0;             /* the slot the FM/OPL sits on, 0 for none */

function onboardMuteSupported() {
  const dev = sendsidDev();
  return !!(dev && typeof dev.playerMute === 'function');
}

/**
 * Ask the board how many SIDs it has, then build the rows.
 *
 * Two is the fallback, not a guess at the hardware: it is the common case and a
 * row too few is better than four rows of buttons that address nothing. The
 * count only matters for how many rows to draw, so a failed read costs nothing.
 */
/**
 * Build the grid without asking the board anything.
 *
 * The socket configuration, the SID count and the FM/OPL slot would say exactly
 * how many chips there are, and reading them at connect turned out to be a way to
 * wedge the whole tool: a config read that never answers cannot be cancelled, and
 * because reads are serialised it stops everything behind it. Connecting and
 * playing must not depend on a convenience.
 *
 * So four rows by default, which is every chip the firmware can address, and the
 * real layout is available on demand from the REFRESH button (see
 * refreshOnboardLayout). Muting a chip that is not fitted does nothing, which is a
 * far better failure than a tool that will not connect.
 */
function initOnboardMuteGridDefault() {
  _muteChips = 4;
  _muteSockets = { one: 0, two: 0 };
  _muteFmopl = 0;
  buildOnboardMuteGrid();
}

/** Ask the board what is actually fitted. On demand, never on the connect path. */
async function refreshOnboardLayout() {
  const dev = usbsidDevice;
  if (!dev || !dev.isOpen) return;
  let one = 0, two = 0, num = 0, fmopl = 0;
  try {
    if (typeof dev.readSocketConfig === 'function') {
      const sc = await dev.readSocketConfig();
      if (sc) { one = sc.one; two = sc.two; }
    }
    if (typeof dev.readNumSIDs === 'function') num = await dev.readNumSIDs();
    if (typeof dev.readFMOplSID === 'function') fmopl = await dev.readFMOplSID();
  } catch (e) {
    usbsidLog('Could not read the socket configuration:', e && e.message ? e.message : e);
  }

  /* The sockets are the truth about what is fitted, and they say which chip is
   * where: socket one's chips are numbered first, then socket two's, which is the
   * order the player's chip 1 to 4 follow. numsids counts the slots and includes
   * the FM/OPL one, so it is not the number of chips that have voices. */
  _muteSockets = { one, two };
  _muteFmopl = fmopl;
  const fromSockets = one + two;
  if (fromSockets >= 1 && fromSockets <= 4) {
    _muteChips = fromSockets;
  } else if (num >= 1 && num <= 4) {
    /* No socket answer: fall back to the slot count, minus the FM/OPL slot when
     * the board named one, since an OPL has no SID voices to mute. */
    _muteChips = Math.max(1, num - (fmopl >= 1 && fmopl <= num ? 1 : 0));
    usbsidLog('Socket configuration unavailable, using numsids:', num,
              fmopl ? '(FM/OPL on slot ' + fmopl + ')' : '');
  } else {
    usbsidLog('The board reported neither a socket configuration nor a SID count,'
              + ' showing one chip');
    _muteChips = 1;
  }
  usbsidLog('Onboard mute: socket one', one, 'socket two', two,
            '| numsids', num, '| FM/OPL slot', fmopl || 'none',
            '=> ' + _muteChips + ' chip' + (_muteChips === 1 ? '' : 's'));
  buildOnboardMuteGrid();
}

/** Build the per chip rows. Cheap, so it just rebuilds rather than diffing. */
function buildOnboardMuteGrid() {
  const grid = document.getElementById('sendsid-mute-grid');
  if (!grid) return;
  grid.textContent = '';
  for (let chip = 1; chip <= _muteChips; chip++) {
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;align-items:center;gap:4px;margin-top:3px;flex-wrap:wrap';
    const label = document.createElement('button');
    label.className = 'c64-btn c64-btn-sm';
    /* Which socket this chip is in. Socket one's chips are numbered first, so a
     * board with two in socket one and one in socket two reads 1 and 2 in socket
     * one and 3 in socket two, which is what the player's chip numbers mean. */
    const socket = (chip <= _muteSockets.one) ? 1
                 : (_muteSockets.one + _muteSockets.two >= chip) ? 2 : 0;
    label.textContent = 'CHIP ' + chip;
    label.title = 'Mute or unmute all three voices of chip ' + chip
                + (socket ? ' (socket ' + socket + ')' : '');
    label.addEventListener('click', async () => {
      /* The chip mask only, never the voice bits.
       *
       * Chip mute and voice mute are separate mechanisms and a chip can be held
       * while its voices carry their own state underneath. Testing both made this
       * button sticky: after a mute-all the voice bits stay set, so it read as
       * muted for ever and every click sent another unmute. */
      const muted = (_muteChipMask & (1 << (chip - 1))) !== 0;
      await onboardMuteChip(chip, !muted);
    });
    row.appendChild(label);
    for (let voice = 1; voice <= 3; voice++) {
      const b = document.createElement('button');
      b.className = 'c64-btn c64-btn-sm';
      b.dataset.chip = chip;
      b.dataset.voice = voice;
      b.textContent = 'V' + voice;
      b.title = 'Voice ' + voice + ' of chip ' + chip;
      b.addEventListener('click', async () => {
        const bit = 1 << (voice - 1);
        await onboardMuteVoice(chip, voice, (_muteMask[chip - 1] & bit) === 0);
      });
      row.appendChild(b);
    }
    if (socket) {
      const tag = document.createElement('span');
      tag.className = 'np-stil-key';
      tag.textContent = 'socket ' + socket;
      row.appendChild(tag);
    }
    grid.appendChild(row);
  }
  renderOnboardMute();
}

/** Paint the buttons from `_muteMask`. */
function renderOnboardMute() {
  const grid = document.getElementById('sendsid-mute-grid');
  if (!grid) return;
  for (const b of grid.querySelectorAll('button[data-voice]')) {
    const chip = Number(b.dataset.chip);
    const voice = Number(b.dataset.voice);
    const muted = (_muteChipMask & (1 << (chip - 1))) !== 0 ||
                  (_muteMask[chip - 1] & (1 << (voice - 1))) !== 0;
    b.classList.toggle('c64-btn-stop', muted);
    b.setAttribute('aria-pressed', muted ? 'true' : 'false');
  }
  const all = document.getElementById('btn-player-mute-all');
  if (all) {
    /* Everything is muted when every chip in use is held. The voice masks are not
     * part of this for the same reason the chip buttons do not use them. */
    const chipBits = (1 << _muteChips) - 1;
    const everything = (_muteChipMask & chipBits) === chipBits;
    all.classList.toggle('c64-btn-stop', everything);
    all.textContent = everything ? 'UNMUTE ALL' : 'ALL';
  }
}

async function onboardMuteVoice(chip, voice, mute) {
  if (!onboardMuteSupported()) return;
  try {
    await sendsidDev().playerMute(chip, voice, mute);
    const bit = 1 << (voice - 1);
    _muteMask[chip - 1] = mute ? (_muteMask[chip - 1] | bit)
                               : (_muteMask[chip - 1] & ~bit);
    renderOnboardMute();
  } catch (e) {
    usbsidLog('Onboard mute failed:', e && e.message ? e.message : e);
  }
}

async function onboardMuteChip(chip, mute) {
  if (!onboardMuteSupported()) return;
  try {
    /* One command: chip with voice 0 reaches usplayer_set_chip_mute(), which
     * drops the chip's writes and so silences the volume register too. Muting the
     * three voices, which is what this used to do, leaves a digi playing. */
    await sendsidDev().playerMuteChip(chip, mute);
    /* The chip mask is what changed, not the voice masks: a chip mute leaves the
     * tune's own voice mutes alone underneath it, and unmuting the chip puts
     * whatever they were back. */
    _muteChipMask = mute ? (_muteChipMask | (1 << (chip - 1)))
                         : (_muteChipMask & ~(1 << (chip - 1)));
    renderOnboardMute();
  } catch (e) {
    usbsidLog('Onboard chip mute failed:', e && e.message ? e.message : e);
  }
}

/**
 * Silence everything, including whatever is going through the volume register.
 *
 * Both commands, on purpose. The player's voice mute writes each voice's
 * sustain/release and control registers and never touches $18, so a tune playing
 * samples through the volume register stays audible with all three voices muted.
 * The board's own MUTE zeroes the volume nibble of $d418 on every chip and
 * remembers what was there, which covers the digi; the voice mute keeps the per
 * voice buttons showing the truth.
 */
async function onboardMuteAll(mute) {
  if (!onboardMuteSupported()) return;
  const dev = sendsidDev();
  try {
    await dev.playerMuteAll(mute);
    if (typeof dev.muteAll === 'function') {
      await (mute ? dev.muteAll() : dev.unmuteAll());
    }
    _muteMask = [0, 1, 2, 3].map(() => (mute ? 0x07 : 0x00));
    _muteChipMask = mute ? 0x0f : 0x00;
    renderOnboardMute();
  } catch (e) {
    usbsidLog('Onboard mute all failed:', e && e.message ? e.message : e);
  }
}

/** Read the board's own view and show that. */
async function refreshOnboardMute() {
  const dev = sendsidDev();
  if (!dev || typeof dev.playerMuteState !== 'function') return;
  try {
    const st = await dev.playerMuteState();
    if (!st) return;
    _muteMask = st.voices;
    _muteChipMask = st.chips;
    renderOnboardMute();
    usbsidLog('Onboard mute state: chips',
              (st.chips & 0x0f).toString(2).padStart(4, '0'), '| voices',
              st.voices.map(m => (m & 7).toString(2).padStart(3, '0')).join(' '));
  } catch (e) {
    usbsidLog('Could not read onboard mute state:', e && e.message ? e.message : e);
  }
}

/**
 * Songs and start song out of a SID file's own header.
 *
 * Needed only on the SendSID path. The emulated path asks the player, which has
 * parsed the file properly, but on the board path the page never loads the file
 * into anything and so used to leave `_maxSubtunes` at whatever the previous tune
 * had set: the transport read "Tune 1/1" over a six subtune tune, and with
 * lengths now switched on it would also have taken the wrong subtune's length.
 *
 * Both fields are big endian 16 bit at fixed offsets, PSID and RSID alike, and
 * `startSong` counts from one. See SID_file_format.txt in HVSC's DOCUMENTS.
 */
function sidHeaderSongs(bytes) {
  if (!bytes || bytes.length < 0x16) return null;
  const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
  if (magic !== 'PSID' && magic !== 'RSID') return null;
  const songs = (bytes[0x0e] << 8) | bytes[0x0f];
  const start = (bytes[0x10] << 8) | bytes[0x11];
  if (songs < 1 || songs > 256) return null;
  return { songs, start: (start >= 1 && start <= songs) ? start : 1 };
}

/**
 * The board has moved to another subtune, so follow it.
 *
 * These branches used to return before touching `_currentSubtune`, which was
 * harmless while the page could not show a position at all. It is not harmless
 * now: the length shown, and the point at which a subtune is treated as
 * finished, both come from `_currentSubtune`, and every subtune has its own
 * length. The board also keeps the previous subtune's maximum playtime until it
 * is told otherwise, so the new one has to be sent.
 */
async function sendsidSubtuneChanged(delta) {
  const next = _currentSubtune + delta;
  if (next < 1 || next > _maxSubtunes) return;
  _currentSubtune = next;
  updateSubtuneDisplay();
  _sendsidTimeMs = 0;
  _endArmed = false;
  _advancedAt = -1;
  _sendsidLastMs = -1;
  _sendsidSame = 0;
  const dev = sendsidDev();
  const lenMs = _songLengths ? currentSongLengthMs() : 0;
  if (dev && lenMs && typeof dev.playerSetPlaytime === 'function') {
    try { await dev.playerSetPlaytime(lenMs); } catch (e) {
      usbsidLog('Could not set onboard playtime:', e && e.message ? e.message : e);
    }
  }
  updatePlaytimeDisplay();
}

/* Navigating the library is the browser's job now.
 *
 * It walks what is on screen: the open directory, or the search results when
 * there is a search. The old pair walked a flat array of every line in
 * sidfilelist.txt and had to skip the section headings as it went, which meant
 * NEXT could leave the part of the list the user was looking at. */
async function prevSID() { await SidBrowser.prev(); }
async function nextSID() { await SidBrowser.next(); }

function updateSubtuneDisplay() {
  const el = document.getElementById('subtune-display');
  if (el) el.textContent = 'Tune ' + _currentSubtune + '/' + _maxSubtunes;
  /* STIL keeps per subtune titles and artists, so the panel follows the subtune.
   * Nothing is refetched: the entry is already held, only the part of it that is
   * shown changes. */
  renderStil();
}

/* Now playing: the file, the tune and the clock.
 *
 * All three used to be spread over the SID FILE box, which is where a tune is
 * chosen and not where it is played. They live in TRANSPORT now, beside the
 * buttons that act on them. */

function setNowPlayingName(name) {
  const el = document.getElementById('np-name');
  if (el) el.textContent = name || 'No file loaded';
}

function updateMetaDisplay(info) {
  const p = _player;
  const el = document.getElementById('np-meta');
  if (!el) return;
  try {
    const parts = [];
    if (info && info.songName)     parts.push(info.songName);
    if (info && info.songAuthor)   parts.push('by ' + info.songAuthor);
    if (info && info.songReleased) parts.push('(' + info.songReleased + ')');
    /* A hyphen and not an em dash: the project writes no em dashes. */
    el.textContent = parts.join(' - ') || '';
  } catch (_) {}

  const badges = document.getElementById('np-badges');
  if (badges) {
    badges.innerHTML = '';
    const add = (text) => {
      if (!text) return;
      const b = document.createElement('span');
      b.className = 'np-badge';
      b.textContent = text;
      badges.appendChild(b);
    };
    const n = info && info.numSids ? info.numSids : 0;
    if (n > 1) add(n + 'SID');

    /* What is driving the tune, and how it was started. Both are read out of
     * the emulated chips rather than the file, so they describe the subtune
     * that is playing: a tune's init routine chooses whether a CIA timer, a
     * raster compare or a TOD alarm calls the play routine, and another subtune
     * may choose differently. */
    const t = (p && typeof p.timing === 'function') ? p.timing() : null;
    if (t) {
      if (t.start) add(t.start + (t.driver ? ' $' + hex16(t.driver) : ''));
      for (const src of t.irq) add(src);
      if (!t.irq.length) add('no IRQ');
    } else if (info && info.isPrg) {
      add('PRG');
    }
  }
}

/** Four hex digits, for an address. */
function hex16(v) {
  return (v & 0xffff).toString(16).padStart(4, '0');
}

/* ------------------------------------------------------------------------ *
 * Song lengths
 *
 * HVSC ships a Songlengths database keyed by the **plain MD5 of the whole .sid
 * file**, with one time per song, so a tune with five subtunes has five times.
 * Where they come from depends on where the tune came from:
 *
 *   the served library   SID/filelist.bb did the lookup when it built
 *                        sidfilelist.json, so the numbers arrive with the
 *                        entry and cost the page nothing.
 *   a local folder,      no index exists, so the MD5 is computed from the bytes
 *   an upload, a URL     after the tune has loaded and looked up against the
 *                        database, which is fetched once, lazily, and never in
 *                        the way of playback.
 *
 * A length is what lets the transport show `0:07 / 3:24` and lets a finished
 * subtune move on by itself instead of looping for ever.
 * ------------------------------------------------------------------------ */

/* SendSID reports its position now, so this is on.
 *
 * The tune plays on the board and the page is not emulating it, so the page used
 * to have no idea where it had got to. The firmware answers SID_PLAYER_TIME with
 * the player's position, and takes UPLOAD_SID_PLAYTIME so it stops at the tune's
 * real length instead of its own five minute default. Between them the transport
 * can show `0:07 / 3:24` on the board as well, and a finished subtune moves on. */
const SENDSID_SONGLENGTHS = true;

/* The board's position, in milliseconds, from the last poll. Null when it has
 * not been asked yet.
 *
 * Polled rather than computed: reading it is a USB round trip, so it happens on
 * its own slower timer while the display keeps repainting from this value. */
var _sendsidTimeMs = null;
var _sendsidTimer = null;

var _songLengths   = null;   /* [ms per song], or null when unknown */
var _lengthSeq     = 0;      /* guards a late answer for a tune we have left */
var _advancedAt    = -1;     /* the subtune we have already auto advanced from */
var _songlengthDb  = null;   /* a promise, once something has needed it */

/* A load is in flight, and which one.
 *
 * `adapter.load()` fetches the file before it can hand the bytes to the
 * emulation, so between choosing a tune and the emulation holding it there is a
 * gap of however long the network takes. During that gap the *previous* tune is
 * still loaded and still being stepped, so `playtimeMs()` still answers for it.
 *
 * Two things went wrong with that. The counter carried on from the old tune's
 * position until the fetch finished, which is what "it does not go to zero"
 * was. And worse, the new tune's length was already in force, so a stale
 * position past the end of the *new* tune's length looked exactly like a
 * finished song and fired the auto advance, which started another load, whose
 * own priming filled the ring in bursts: the old tune audibly speeding up and
 * then jumping to a tune two further down the list. */
var _loadGen       = 0;      /* incremented per load */
var _loading       = false;  /* true from choosing a tune until it is playing */
var _endArmed      = false;  /* see armEndOfSong() */

function setSongLengths(arr) {
  _songLengths = (Array.isArray(arr) && arr.length) ? arr : null;
  _advancedAt = -1;
  _endArmed = false;
  updatePlaytimeDisplay();
}

/* What to play when the database has never heard of a song.
 *
 * Five minutes, the same figure the command line player uses, so the two behave
 * alike. Without it a tune with no entry plays until someone intervenes, which
 * means shuffle and auto advance stop dead at the first such tune and a
 * playlist never reaches its end. */
const DEFAULT_SONG_MS = 5 * 60 * 1000;

/** The current subtune's length in ms. The default when it is not known. */
function currentSongLengthMs() {
  if (!_songLengths) return DEFAULT_SONG_MS;
  /* Subtunes count from one here and the array is in song order, so song 1 is
   * index 0. A tune whose header claims more songs than the database lists
   * falls back to the last time rather than to nothing. */
  const i = Math.min(_songLengths.length, Math.max(1, _currentSubtune)) - 1;
  return _songLengths[i] || DEFAULT_SONG_MS;
}

/* How far before a whole-second length to move on. See songEndAtMs(). */
const END_MARGIN_MS = 350;

/**
 * When to move on, which is not the same as the length that gets displayed.
 *
 * `Songlengths.faq` gives the format as `mm:ss[.SSS]` with the milliseconds
 * **optional**, and in the database shipped here four out of five entries have
 * none:
 *
 *     with milliseconds  16960
 *     whole seconds      70114
 *
 * A whole-second figure is therefore only accurate to the second, and a SID tune
 * has no end of its own: the play routine loops. So playing all the way to a
 * whole-second length means playing past where the tune actually restarted, and
 * what you hear is a second of the tune beginning again before the player moves
 * on. That was reported as "plays one second too long", and a 2:26 tune audibly
 * restarting at 2:25 is exactly this.
 *
 * Stopping a little short instead. A clipped final fraction of a second is far
 * less noticeable than a restart, and entries that do carry milliseconds are
 * precise, so those are left exactly alone.
 *
 * The five minute fallback for a tune with no entry is not a measurement at all,
 * so it keeps its full value.
 */
function songEndAtMs() {
  const total = currentSongLengthMs();
  if (!_songLengths || !total) return total;
  return (total % 1000 === 0) ? Math.max(0, total - END_MARGIN_MS) : total;
}

/* ── STIL ──────────────────────────────────────────────────────────────────
 *
 * HVSC's SID Tune Information List: what a tune is a cover of, who wrote the
 * original, why a subtune sounds the way it does. `SID/stil.bb` turns
 * DOCUMENTS/STIL.txt into JSON keyed by MD5 and splits it across 256 files named
 * by the first two hex characters of that key.
 *
 * Keyed by MD5 rather than by path because the page never knows a tune's HVSC
 * path: the served library has its own layout and an opened file has none. The
 * MD5 the player already computes for song lengths is the one thing available
 * for both, so a served tune and a file out of the user's own HVSC copy look up
 * identically.
 *
 * Split into buckets because the whole database is 3.7 MB and no page should
 * download that to show one tune's credits. The bucket name comes out of the
 * key, so there is no index to fetch first: one request of about 15 kB, which
 * the browser then caches for every other tune whose hash starts the same way.
 */
const STIL_PATH = SID_PATH + 'stil/';
const STIL_BUCKET_CHARS = 2;

var _stilBuckets = {};   /* prefix -> promise of the parsed bucket */
var _stilEntry   = null; /* the current tune's entry, or null */
var _stilSeq     = 0;    /* guards a late answer for a tune we have left */

/** One bucket, fetched at most once. */
function stilBucket(prefix) {
  if (_stilBuckets[prefix]) return _stilBuckets[prefix];
  _stilBuckets[prefix] = (async () => {
    try {
      const resp = await fetch(STIL_PATH + prefix + '.json');
      /* Every prefix has a file, empty ones included, so anything but a 200 is
       * a deploy without the STIL data rather than a tune without an entry. */
      if (!resp.ok) return {};
      return await resp.json();
    } catch (e) {
      usbsidLog('STIL bucket', prefix, 'unavailable:', e && e.message ? e.message : e);
      return {};
    }
  })();
  return _stilBuckets[prefix];
}

/**
 * Look up the tune that has just loaded, and show what STIL says about it.
 *
 * Not awaited by the load path, exactly like resolveSongLengths(): a tune starts
 * playing at once and the notes appear when they appear.
 */
function resolveStil() {
  const p = _player;
  const seq = ++_stilSeq;
  _stilEntry = null;
  renderStil();
  if (!p || typeof p.md5 !== 'function') return;
  const key = p.md5();
  if (!key || key.length < STIL_BUCKET_CHARS) return;
  stilBucket(key.slice(0, STIL_BUCKET_CHARS)).then((bucket) => {
    /* Another tune was chosen while the bucket was on its way. */
    if (seq !== _stilSeq) return;
    _stilEntry = bucket[key] || null;
    renderStil();
  });
}

/**
 * Draw the current entry, for the subtune being played.
 *
 * Called on every subtune change as well as on load, because STIL keeps per
 * subtune titles and artists and the interesting one is the one you are hearing.
 * No refetch is involved: the entry is already in hand, only the section of it
 * that is shown changes.
 */
function renderStil() {
  const box = document.getElementById('np-stil');
  const body = document.getElementById('np-stil-body');
  const pathEl = document.getElementById('np-stil-path');
  if (!box || !body || !pathEl) return;

  const e = _stilEntry;
  if (!e) { box.style.display = 'none'; body.textContent = ''; return; }

  pathEl.textContent = e.p || '';
  body.textContent = '';

  /* textContent throughout, never innerHTML: these strings are HVSC prose and
   * contain angle brackets and ampersands as a matter of course ("<?>" is how
   * STIL writes an unknown artist). */
  const row = (into, label, value, cls) => {
    if (!value) return;
    const d = document.createElement('div');
    d.className = 'np-stil-row';
    const k = document.createElement('span');
    k.className = 'np-stil-key';
    k.textContent = label;
    const v = document.createElement('span');
    v.className = 'np-stil-val' + (cls ? ' ' + cls : '');
    v.textContent = value;
    d.appendChild(k);
    d.appendChild(v);
    into.appendChild(d);
  };

  const fields = (into, o) => {
    row(into, 'Title', o.t, 'np-stil-title');
    row(into, 'Artist', o.a);
    row(into, 'Name', o.n);
    row(into, 'Author', o.u);
    row(into, 'Note', o.c);
  };

  /* File level first: fields written above any (#n) apply to the whole tune. */
  fields(body, e);

  /* Then this subtune's own, when it has any. Keyed by subtune number as a
   * string, and STIL only lists the subtunes it has something to say about, so a
   * miss here is ordinary rather than a problem. */
  const sub = e.s && e.s[String(_currentSubtune)];
  if (sub) {
    const wrap = document.createElement('div');
    wrap.className = 'np-stil-sub';
    const head = document.createElement('div');
    head.className = 'np-stil-subhead';
    head.textContent = 'Tune ' + _currentSubtune;
    wrap.appendChild(head);
    fields(wrap, sub);
    body.appendChild(wrap);
  }

  box.style.display = body.childNodes.length ? '' : 'none';
}

/**
 * Fetch the Songlengths database, once, and hand it to the player.
 *
 * Four to five megabytes, so this is deliberately lazy: nothing fetches it
 * until a tune arrives that is not in the library index, and the promise is
 * cached so a second such tune waits on the same fetch rather than starting
 * another.
 */
function ensureSonglengthDb(p) {
  if (_songlengthDb) return _songlengthDb;
  _songlengthDb = (async () => {
    try {
      const resp = await fetch(SID_PATH + 'songlengths.md5');
      if (!resp.ok) { usbsidLog('No songlengths.md5, lengths unavailable'); return false; }
      const text = await resp.text();
      const ok = p.loadSonglengths(text);
      usbsidLog('Songlengths database:', Math.round(text.length / 1024) + ' kB',
                ok ? 'loaded' : 'could not be loaded');
      return ok;
    } catch (e) {
      usbsidLog('songlengths.md5 fetch failed:', e && e.message ? e.message : e);
      return false;
    }
  })();
  return _songlengthDb;
}

/**
 * Work out the lengths for a tune the library index knew nothing about.
 *
 * Deliberately **not** awaited by the load path. The tune starts playing at
 * once with no total shown, and the figure appears when it appears: a four
 * megabyte fetch in front of the first note would be a worse trade than a
 * timer that reads `0:07` for a moment.
 */
function resolveSongLengths() {
  const p = _player;
  if (!p || typeof p.md5 !== 'function') return;
  if (_emulator === 'sendsid' && !SENDSID_SONGLENGTHS) return;
  const seq = ++_lengthSeq;
  ensureSonglengthDb(p).then((ok) => {
    /* Another tune was chosen while the database was on its way. Its own
     * lookup is in flight; this answer is for a tune nobody is playing. */
    if (!ok || seq !== _lengthSeq) return;
    const key = p.md5();
    if (!key) return;
    const lens = p.songLengths(key);
    if (seq !== _lengthSeq) return;
    if (lens) {
      usbsidLog('Song lengths:', lens.map(ms => formatTime(ms)).join(', '));
      setSongLengths(lens);
    } else {
      usbsidLog('Not in songlengths.md5:', key);
    }
  });
}

/**
 * One subtune has played to its end. Move on.
 *
 * Next subtune while there is one, then the next tune in the library browser,
 * which is what a player does when left alone. Guarded by the subtune it fired
 * for, because the display ticks twice a second and the condition stays true
 * until the next tune has loaded.
 */
function songEnded() {
  /* A load already in flight will move the position itself. Advancing again on
   * top of it is how one click became a run of skipped tunes. */
  if (_loading) return;
  if (_advancedAt === _currentSubtune) return;
  _advancedAt = _currentSubtune;
  _endArmed = false;
  if (_currentSubtune < _maxSubtunes) {
    usbsidLog('End of song', _currentSubtune + ', next subtune');
    nextSubtune();
  } else if (SidBrowser.canStep()) {
    usbsidLog('End of tune, next in the list');
    nextSID();
  } else {
    /* Nowhere to go: the browser has no list, which is the case for a tune
     * opened straight from a file or a URL. Stop, rather than leave it running.
     *
     * SidBrowser.next() returns silently when there is nothing to step through,
     * and the guard above has already been consumed by the time it does, so
     * nothing ever asked again: the tune played on indefinitely with the clock
     * reading 5:24 against a length of 0:41. */
    usbsidLog('End of tune, and no list to continue into: stopping');
    stopPlay();
  }
}

/* mm:ss, or a dash when there is no figure. Over an hour it grows a field
 * rather than wrapping round, since some tunes really do run that long. */
function formatTime(ms) {
  if (ms == null || !isFinite(ms) || ms < 0) return '\u2014:\u2014\u2014';
  const total = Math.floor(ms / 1000);
  const s = total % 60;
  const m = Math.floor(total / 60) % 60;
  const h = Math.floor(total / 3600);
  const pad = (v) => String(v).padStart(2, '0');
  return h > 0 ? (h + ':' + pad(m) + ':' + pad(s)) : (m + ':' + pad(s));
}

/**
 * The play time, ticked from the emulation rather than from the wall clock.
 *
 * `playtimeMs()` is emulated time: it stops when the tune is paused, it jumps
 * when a seek does, and it stays right when the board makes the player wait.
 * A wall clock here would drift away from the tune within a minute.
 *
 * SendSID has no figure to give. There the tune is playing on the board and the
 * page is not emulating anything, so the display stays a dash until the
 * firmware can report it back.
 */
var _playtimeTimer = null;

function updatePlaytimeDisplay() {
  const el = document.getElementById('np-time');
  if (!el) return;
  const total = currentSongLengthMs();
  if (_emulator === 'sendsid') {
    /* The board is playing it, so the position comes from the board. Until the
     * first poll answers there is nothing to show but the length. */
    const ms = SENDSID_SONGLENGTHS ? _sendsidTimeMs : null;
    setTimeText(el, (ms == null ? '\u2014:\u2014\u2014' : formatTime(ms))
                    + (total ? ' / ' + formatTime(total) : ''));
    el.title = SENDSID_SONGLENGTHS
      ? 'Position read from the onboard player'
      : 'The onboard player does not report its position yet';
    if (!SENDSID_SONGLENGTHS) return;

    /* Same two step guard as the emulated path: a subtune only counts as ended
     * once it has been seen running before its end. */
    const endAt = songEndAtMs();
    if (endAt && ms != null && ms < endAt) _endArmed = true;
    if (_endArmed && endAt && ms != null && ms >= endAt && _sendsidPlaying) {
      songEnded();
    }
    return;
  }
  /* While a tune is on its way, the emulation still holds the previous one and
   * `playtimeMs()` still answers for it. Show the new tune's zero rather than
   * the old tune's position, which is what someone who has just clicked expects
   * to see, and do not test for the end of a song we are not playing yet. */
  if (_loading) {
    setTimeText(el, formatTime(0) + (total ? ' / ' + formatTime(total) : ''));
    el.title = '';
    return;
  }

  const p = _player;
  const ms = (p && typeof p.playtimeMs === 'function') ? p.playtimeMs() : null;
  /* The listed length is what gets shown, so the display still agrees with
   * HVSC even though songEndAtMs() may move on slightly earlier. */
  setTimeText(el, formatTime(ms) + (total ? ' / ' + formatTime(total) : ''));
  el.title = _songLengths
    ? ''
    : 'Not in songlengths.md5, so playing the default ' +
      formatTime(DEFAULT_SONG_MS);

  const endAt = songEndAtMs();

  /* The end of a song only counts once this song has been seen playing before
   * its end. A position inherited from the tune before it can be past the new
   * tune's length on the very first reading, and acting on that skips tunes. */
  if (endAt && ms != null && ms < endAt) _endArmed = true;

  /* A finished subtune moves on by itself. Only while actually playing: a
   * paused or stopped tune sitting past its end must stay where it is. */
  if (_endArmed && endAt && ms != null && ms >= endAt &&
      p && !p.stopped && !p.paused) {
    songEnded();
  }
}

/* Write the clock only when it reads differently.
 *
 * The tick runs at 100 ms so the end of a song is caught within a tenth of a
 * second rather than within half of one, which matters now that it stops just
 * short of the loop. Ten times the polling would have been ten times the DOM
 * writes, hence this: the text changes at most once a second, so nine ticks in
 * ten now touch nothing at all and this is cheaper than the old 500 ms tick was.
 */
var _lastTimeText = null;

function setTimeText(el, text) {
  if (text !== _lastTimeText) {
    _lastTimeText = text;
    el.textContent = text;
  }
}

/* Ask the board where it has got to.
 *
 * On its own timer and not from updatePlaytimeDisplay(), which runs ten times a
 * second: each of these is a USB write followed by a read with a timeout, and the
 * recovery path for a timed out read closes and reopens the device. Twice a
 * second is plenty for a display that only shows whole seconds.
 *
 * Reentrancy matters here. A slow or lost answer must not let a second request
 * overlap the first, because the driver reads whatever packet arrives next and
 * two outstanding reads can take each other's replies.
 */
var _sendsidPolling = false;
var _sendsidPollFails = 0;
/* The last position read, and how many times in a row it has come back the same.
 *
 * The board stops itself at the maximum playtime, and get_playtime() only samples
 * the player while it is running, so the figure freezes at whatever the last live
 * sample was. A reading that does not move is therefore the board saying it has
 * finished, and it is the only reliable signal: the frozen value lands anywhere in
 * the last poll interval, so it can easily be below any threshold we would test
 * against, and once it is the tune never advances. */
var _sendsidLastMs = -1;
var _sendsidSame = 0;

/* Give up after this many unanswered reads.
 *
 * Not just noise control. A timed out read leaves a transferIn pending that a
 * late reply can satisfy instead of the *next* read, and this driver recovers
 * from a timeout by closing and reopening the device. Asking twice a second
 * through that is worse than not asking: the clock simply stops updating, which
 * is the honest outcome when the board will not answer. */
const SENDSID_POLL_GIVEUP = 5;

async function pollSendsidTime() {
  if (_sendsidPolling) return;
  if (_emulator !== 'sendsid' || !SENDSID_SONGLENGTHS) return;
  if (_sendsidPollFails >= SENDSID_POLL_GIVEUP) return;
  const dev = sendsidDev();
  if (!dev || typeof dev.playerTime !== 'function') return;
  if (!_sendsidPlaying) return;
  _sendsidPolling = true;
  try {
    const ms = await dev.playerTime();
    if (ms == null) {
      if (++_sendsidPollFails >= SENDSID_POLL_GIVEUP) {
        usbsidLog('Onboard player is not reporting its position, stopped asking');
      }
    } else {
      _sendsidPollFails = 0;
      /* Once, so it is visible that readings are arriving and what they look
       * like. Everything after this is just the clock ticking. */
      if (_sendsidTimeMs === 0 && ms > 0) {
        usbsidLog('Onboard player position:', formatTime(ms), '(' + ms + ' ms)');
      }
      _sendsidTimeMs = ms;

      if (ms === _sendsidLastMs) {
        _sendsidSame++;
        /* Twice, not once: one repeat could be a poll that landed inside the same
         * millisecond, though at half a second apart it should not. Only once the
         * tune has been seen running, so a tune that has not started yet cannot
         * look finished. */
        if (_sendsidSame >= 2 && ms > 0 && _endArmed && _sendsidPlaying) {
          usbsidLog('Onboard player finished at', formatTime(ms));
          _sendsidSame = 0;
          songEnded();
        }
      } else {
        _sendsidSame = 0;
        _sendsidLastMs = ms;
      }
    }
  } catch (e) {
    if (++_sendsidPollFails >= SENDSID_POLL_GIVEUP) {
      usbsidLog('Onboard player time unavailable:', e && e.message ? e.message : e);
    }
  } finally {
    _sendsidPolling = false;
  }
}

function startSendsidTimeTimer() {
  if (_sendsidTimer) return;
  _sendsidTimer = setInterval(pollSendsidTime, 500);
}

/* The board may already be playing when this is reached: an upload starts the
 * timer, but a mode switch or a reconnect does not, and the poll is gated on
 * _sendsidPlaying anyway so a spare timer costs one comparison. */
function ensureSendsidTimeTimer() {
  if (_emulator === 'sendsid' && SENDSID_SONGLENGTHS) startSendsidTimeTimer();
}

function stopSendsidTimeTimer() {
  if (_sendsidTimer) { clearInterval(_sendsidTimer); _sendsidTimer = null; }
}

function startPlaytimeTimer() {
  if (_playtimeTimer) return;
  /* Ten times a second, but see setTimeText(): the DOM is only written when the
   * clock reads differently, so nine of those ten ticks do nothing but compare
   * two strings. The rate is set by the end-of-song test rather than by the
   * display, because the advance now lands deliberately close to the tune's own
   * loop point and half a second of slop there is audible. */
  _playtimeTimer = setInterval(updatePlaytimeDisplay, 100);
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

/**
 * Refuse to load a tune the selected mode has no way of playing.
 *
 * `playPause()` has always checked this, but loading a tune *starts* it, and
 * nothing on the load path asked. So choosing a tune from the library in a
 * WebUSB or Web Serial mode with nothing connected played it happily: the
 * emulation ran, the timer counted, the register grid moved, and every write
 * went into a transport with nowhere to send it. It looked like it was working
 * and no sound could ever come out.
 *
 * Guarding here rather than at each button covers all of it: the library, the
 * local playlist, BROWSE, a URL and the SendSID upload all funnel through
 * doLoadSID() or loadSID().
 *
 * Software audio needs nothing and is never refused, which is what
 * modeLinkReady() already says.
 *
 * @returns {boolean} true when the load must not go ahead
 */
function loadRefused() {
  if (modeLinkReady()) return false;
  const what = modeLinkName();
  usbsidSetStatus('Connect ' + what + ' first', 'red');
  usbsidLog('Load refused:', _emulator, 'has no', what);
  return true;
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
  const shown = (_emulator === 'sendsid' && usbsidDevice.isOpen && _hasSIDPlayer);
  if (shown) ensureSendsidTimeTimer();
  if (sendsidBtns) sendsidBtns.style.display = shown ? 'flex' : 'none';
  /* The mute grid rides with them, but only when the transport in use can carry
   * the command: the serial route to the onboard player has no playerMute(). */
  const muteBtns = document.getElementById('sendsid-mute-btns');
  if (muteBtns) {
    const usable = shown && onboardMuteSupported();
    muteBtns.style.display = usable ? 'block' : 'none';
    if (usable && !muteBtns.dataset.built) {
      muteBtns.dataset.built = '1';
      initOnboardMuteGridDefault();
    }
  }
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


/* SID library.
 *
 * The list, the panes, the search and the grouping all live in
 * usbsid-sidbrowser.js. What is left here is starting it and enabling the two
 * navigation buttons once it has something to navigate.
 *
 * It used to be a <select size=6> holding all 850 tunes as options, with the
 * section headings as disabled options and jump buttons that scrolled it by
 * multiplying scrollHeight by an option index. On a phone that select opens a
 * native picker over the page, which is why PREV SID and NEXT SID beside it
 * appeared dead: the picker was taking the taps. */
async function initSIDList() {
  await SidBrowser.init();
  const on = SidBrowser.ready();
  for (const id of ['btn-prev-sid', 'btn-next-sid']) {
    const b = document.getElementById(id);
    if (b) b.disabled = !on;
  }
}

/* The local playlist: a folder or a pile of files from the user's own machine.
 *
 * Two mechanisms, because no single one covers every platform.
 *
 *   showDirectoryPicker()  Chromium, desktop and Android. A real directory, and
 *                          the handle can be kept in IndexedDB so the same
 *                          folder is still there on the next visit. Reading it
 *                          again needs one click to re-grant permission, which
 *                          the browser insists on and which is correct.
 *   <input multiple>       everywhere, iOS Safari included. The user picks the
 *                          files themselves and nothing is remembered.
 *
 * Whichever is not usable is hidden rather than shown broken. The tunes end up
 * in the browser as a directory of their own, which is where a playlist is
 * actually usable: it searches and sorts with everything else.
 */
const LOCAL_DB   = 'usbsid-local';
const LOCAL_KEY  = 'folder';

function localFolderSupported() {
  return typeof window.showDirectoryPicker === 'function';
}

/* One tiny IndexedDB store, for the one handle. A directory handle cannot go in
 * localStorage: it is a live object and only structured clone survives it. */
function localIdb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(LOCAL_DB, 1);
    req.onupgradeneeded = () => req.result.createObjectStore('handles');
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

async function localHandleSave(handle) {
  try {
    const db = await localIdb();
    await new Promise((res, rej) => {
      const tx = db.transaction('handles', 'readwrite');
      tx.objectStore('handles').put(handle, LOCAL_KEY);
      tx.oncomplete = res;
      tx.onerror = () => rej(tx.error);
    });
  } catch (e) { usbsidLog('local folder not remembered:', e.message || e); }
}

async function localHandleLoad() {
  try {
    const db = await localIdb();
    return await new Promise((res, rej) => {
      const tx = db.transaction('handles', 'readonly');
      const rq = tx.objectStore('handles').get(LOCAL_KEY);
      rq.onsuccess = () => res(rq.result || null);
      rq.onerror = () => rej(rq.error);
    });
  } catch (_) { return null; }
}

async function localHandleForget() {
  try {
    const db = await localIdb();
    await new Promise((res) => {
      const tx = db.transaction('handles', 'readwrite');
      tx.objectStore('handles').delete(LOCAL_KEY);
      tx.oncomplete = res;
      tx.onerror = res;
    });
  } catch (_) {}
}

/* Walk a directory handle, subdirectories included, for playable files. */
async function localReadFolder(handle, prefix, out, depth) {
  if (depth > 4) return;   /* a deep tree is a mistake, not a music folder */
  for await (const [name, entry] of handle.entries()) {
    if (entry.kind === 'directory') {
      await localReadFolder(entry, prefix + name + '/', out, depth + 1);
    } else if (/\.(sid|prg)$/i.test(name)) {
      out.push({ name: prefix + name, handle: entry });
    }
  }
}

function setLocalStatus(text) {
  const el = document.getElementById('sid-local-status');
  if (el) el.textContent = text || '';
  const clear = document.getElementById('btn-local-clear');
  if (clear) clear.style.display = SidBrowser.localCount() ? '' : 'none';
}

async function localAdoptHandle(handle, announce) {
  const found = [];
  await localReadFolder(handle, '', found, 0);
  if (!found.length) {
    setLocalStatus('No .sid or .prg files in ' + handle.name);
    return false;
  }
  /* getFile() per entry, once, so the browser holds Files and not handles: a
   * handle read later can fail on a permission that has since lapsed, in the
   * middle of playback, which is the worst moment to find out. */
  const files = [];
  for (const f of found) {
    try { files.push({ name: f.name, file: await f.handle.getFile() }); }
    catch (_) { /* vanished or unreadable, skip it */ }
  }
  SidBrowser.setLocalFiles(files);
  setLocalStatus(handle.name + ': ' + files.length + ' file(s)');
  if (announce) usbsidLog('Local folder', handle.name + ':', files.length, 'files');
  return true;
}

function initLocalPlaylist() {
  const btnFolder = document.getElementById('btn-local-folder');
  const lblFiles  = document.getElementById('lbl-local-files');
  const input     = document.getElementById('local-files-input');
  const btnClear  = document.getElementById('btn-local-clear');

  if (localFolderSupported()) {
    if (lblFiles) lblFiles.style.display = 'none';
    if (btnFolder) {
      btnFolder.style.display = '';
      btnFolder.addEventListener('click', async () => {
        try {
          const handle = await window.showDirectoryPicker({ id: 'usbsid-sid' });
          if (await localAdoptHandle(handle, true)) await localHandleSave(handle);
        } catch (e) {
          /* AbortError is the user closing the dialog, which is not news. */
          if (e && e.name !== 'AbortError') usbsidLog('local folder error:', e.message || e);
        }
      });
    }
    /* A remembered folder is offered, not opened: reading it needs permission
     * the browser will only grant from a click, so re-asking on load would
     * either fail silently or throw a dialog at someone who came to press
     * play. */
    localHandleLoad().then(async (handle) => {
      if (!handle) return;
      try {
        const perm = await handle.queryPermission({ mode: 'read' });
        if (perm === 'granted') { await localAdoptHandle(handle, true); return; }
      } catch (_) {}
      setLocalStatus('Local folder "' + handle.name +
                     '" remembered. Press LOCAL FOLDER to open it again.');
    });
  } else {
    if (btnFolder) btnFolder.style.display = 'none';
    if (lblFiles)  lblFiles.style.display = '';
    if (input) {
      input.addEventListener('change', () => {
        const files = Array.from(input.files || [])
          .filter(f => /\.(sid|prg)$/i.test(f.name))
          .map(f => ({ name: f.name, file: f }));
        if (!files.length) { setLocalStatus('No .sid or .prg files picked'); return; }
        SidBrowser.setLocalFiles(files);
        setLocalStatus(files.length + ' local file(s), this visit only');
        usbsidLog('Local playlist:', files.length, 'files');
      });
    }
  }

  if (btnClear) {
    btnClear.addEventListener('click', async () => {
      SidBrowser.setLocalFiles([]);
      await localHandleForget();
      if (input) input.value = '';
      setLocalStatus('');
      usbsidLog('Local playlist cleared');
    });
  }
  setLocalStatus('');
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
      setNowPlayingName(name);
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
 *
 * This marks and returns; it does **not** change the selection. It used to, and
 * that put the decision in the wrong order: the markup default was checked and
 * downgraded before the saved preference had been read, so Firefox announced a
 * switch to a mode it then abandoned two lines later.
 *
 *     Mode usplayer needs WebUSB this browser does not have, switching to usplayer-serial
 *     Selected emulator: usplayer-serial
 *     Saved emulator: usplayer-audio
 *     Emulator switched to: usplayer-audio
 *
 * Returns the first mode this browser can run, for a caller that needs a
 * fallback, or null when it can run none of them.
 */
function applyBrowserSupportToModes() {
  const radios = emulatorRadios();
  if (!radios.length) return null;
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
  for (const input of radios) {
    const ok = (needs[input.value] !== false);
    input.disabled = !ok;
    const label = input.closest('.c64-radio');
    if (label) {
      label.classList.toggle('c64-radio-off', !ok);
      /* Say which API is missing, next to the mode it stops. A disabled radio
       * cannot be clicked to produce an error, so the label is the only place
       * the user ever finds out. Added once: this function runs again whenever
       * the browser's capabilities are re-checked. */
      let note = label.querySelector('.c64-radio-why');
      if (!ok) {
        if (!note) {
          note = document.createElement('span');
          note.className = 'c64-radio-why';
          label.appendChild(note);
        }
        note.textContent = `no ${why[input.value] || 'support'} here`;
      } else if (note) {
        note.remove();
      }
    }
    if (ok && firstUsable === null) firstUsable = input.value;
  }

  /* The reason a mode is unavailable, for whoever has to say so. Kept on the
   * function so the caller does not need a second copy of the table. */
  applyBrowserSupportToModes.why = why;
  return firstUsable;
}

/* The mode radios, as a plain array.
 *
 * The mode used to be a <select>, and the three accessors below are what a
 * <select> gave for free: the options, the chosen value, and setting it. Every
 * other caller goes through these, so the markup is described in one place. */
function emulatorRadios() {
  return Array.from(
    document.querySelectorAll('#emulator-modes input[name="emulator-mode"]'));
}

function emulatorRadio(value) {
  return emulatorRadios().find(r => r.value === value) || null;
}

function selectedEmulatorMode() {
  const on = emulatorRadios().find(r => r.checked);
  return on ? on.value : null;
}

/* Check one radio and mark its label. The label class is what makes the choice
 * visible: `:has(input:checked)` would do it in CSS, but Firefox only got that
 * in 121 and this page has to work on an older phone browser. */
function setEmulatorRadio(value) {
  for (const r of emulatorRadios()) {
    const on = (r.value === value);
    r.checked = on;
    const label = r.closest('.c64-radio');
    if (label) label.classList.toggle('c64-radio-on', on);
  }
}

/* Emulator mode radios */
function initEmulatorSelect() {
  const radios = emulatorRadios();
  if (!radios.length) return;
  /* Mark availability first, decide once, and only then say what happened.
   *
   * The order matters and used to be wrong. Availability marking also moved the
   * selection off a disabled option, which ran before the saved preference was
   * read, so the page reported switching to a mode that the very next statement
   * replaced. Three log lines for one decision, two of them describing states
   * the page was never actually in.
   *
   * Now: disable what this browser cannot run, apply the saved preference if it
   * is one of the survivors, and fall back exactly once if what is left
   * selected is still unusable. */
  const firstUsable = applyBrowserSupportToModes();
  const why = applyBrowserSupportToModes.why || {};

  /* A mode saved in Chrome must not be restored into Firefox. */
  const saved = localStorage.getItem('usbsid_emulator');
  const savedRadio = saved && emulatorRadio(saved);
  if (savedRadio && !savedRadio.disabled) {
    setEmulatorRadio(saved);
  } else if (savedRadio) {
    usbsidLog(`Saved mode ${saved} needs ${why[saved] || 'support'} this ` +
              `browser does not have`);
  }

  /* Whatever is selected now, the markup default included, has to be one this
   * browser can run. */
  let chosen = selectedEmulatorMode();
  const current = chosen && emulatorRadio(chosen);
  if (current && current.disabled && firstUsable !== null) {
    usbsidLog(`Mode ${chosen} needs ${why[chosen] || 'support'} this ` +
              `browser does not have, using ${firstUsable}`);
    chosen = firstUsable;
  }
  /* Paint the choice even when nothing moved: the markup's `checked` attribute
   * sets the radio but not the label class that shows which one it is. */
  setEmulatorRadio(chosen);

  /* No "selected mode is X" line here: switchEmulator() below logs the mode
   * that was actually adopted, and one line for one decision is the point. */
  switchEmulator(chosen);

  for (const r of radios) {
    r.addEventListener('change', () => {
      if (!r.checked || r.disabled) return;
      setEmulatorRadio(r.value);
      switchEmulator(r.value);
    });
  }
}

/* The volume slider is gone.
 *
 * It called setVolume() on the player, and USPlayerAdapter.setVolume is an
 * explicit no-op: pausing and stopping do the silencing, and there is no gain
 * stage between the emulation and either a board or the AudioWorklet. Since
 * jsSID was removed there is no backend left that implements it, so the control
 * moved a slider and did nothing at all. The browser's own tab volume and the
 * board's output are the real controls. */

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
  /* The board path never hands the file to a player, so the header is the only
   * thing that knows how many subtunes there are and which one is the default. */
  const hdr = sidHeaderSongs(_loadedBytes);
  if (hdr) {
    _maxSubtunes = hdr.songs;
    if (_currentSubtune < 1 || _currentSubtune > hdr.songs) _currentSubtune = hdr.start;
    updateSubtuneDisplay();
  }
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
    /* The length goes into the upload rather than after it. Sent afterwards the
     * tune is already running against the board's five minute default, and the
     * UART showed exactly that: SID_PLAYER_START, then UPLOAD_SID_PLAYTIME. */
    const lenMs = _songLengths ? currentSongLengthMs() : 0;
    await sendsidDev().uploadSIDFile(_loadedBytes, _currentSubtune, 0x01, (sent, total) => {
      if (statusEl) statusEl.textContent = Math.round(sent / total * 100) + '%';
    }, lenMs);
    if (lenMs) usbsidLog('Onboard player: max playtime', formatTime(lenMs));
    if (statusEl) statusEl.textContent = 'Done';
    usbsidSetStatus('SID uploaded and playing on device', 'green');
    _sendsidTimeMs = 0;
    _endArmed = false;
    _advancedAt = -1;
    _sendsidLastMs = -1;
    _sendsidSame = 0;
    _sendsidPollFails = 0;   /* a new tune is a fresh chance to be answered */
    _sendsidPlaying = true;
    startSendsidTimeTimer();
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
    /* Onboard player mute. The ALL button clears when everything is already
     * muted, so one control both mutes and restores. */
    'btn-player-mute-all': async () => {
      /* Same test the button paints itself with, so pressing it always does the
       * opposite of what it shows. */
      const chipBits = (1 << _muteChips) - 1;
      await onboardMuteAll((_muteChipMask & chipBits) !== chipBits);
    },
    'btn-player-mute-refresh': async () => {
      await refreshOnboardLayout();
      await refreshOnboardMute();
    },
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
    initLocalPlaylist();
    startPlaytimeTimer();
    initEmulatorSelect();
    initMIDI();
    initConfigUI();   /* from usbsid-config.js */
    initDeviceEvents();

    buildRegGrid();

    updateConnectButtonVisibility();
    updateConfTabVisibility();
    updateRegsTabVisibility();
    updatePlayerSideButtons();

    /* Warn about missing WebUSB only when the selected mode actually needs it.
     *
     * Three modes reach a board without it. `usplayer-serial` and `sendsid` go
     * over Web Serial, and `usplayer-audio` never touches a board at all: it
     * synthesises in the page. Warning there produced the worst version of this,
     * a page telling Firefox that "the connect button will not work" when the
     * selected mode has no connect button, as the last thing written to the
     * status line, so a mode that works perfectly looked broken.
     *
     * This is also the only place the browser's capabilities are reported. The
     * mode selector used to write its own status line while marking options,
     * before the mode had been decided, which this then overwrote. */
    const modeNeedsUsb = (_emulator !== 'usplayer-serial' &&
                          _emulator !== 'usplayer-audio' &&
                          _emulator !== 'sendsid');
    if (!navigator.usb && modeNeedsUsb) {
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
    } else if (!navigator.usb && _emulator === 'usplayer-audio') {
      /* Synthesised in the page. There is nothing to connect and nothing
       * missing, so this is a note and not a warning. */
      usbsidLog('Software audio needs no board. The config panels are WebUSB ' +
                'only and stay unavailable in this browser.');
      usbsidSetStatus('Ready - load a SID file and press play');
    } else if (!navigator.usb) {
      /* A Web Serial capable mode in a browser with no WebUSB: nothing wrong. */
      usbsidLog('No WebUSB here, which this mode does not need. The config ' +
                'panels are WebUSB only and stay unavailable.');
      usbsidSetStatus('Press connect and choose the board\u2019s serial port', 'yellow');
    } else if (_emulator === 'usplayer-audio') {
      /* WebUSB is here but this mode does not use it. "Click CONNECT" would be
       * an instruction to press a button that is hidden in this mode. */
      usbsidSetStatus('Ready - load a SID file and press play');
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
