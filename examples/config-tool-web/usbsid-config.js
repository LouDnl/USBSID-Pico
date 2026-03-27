/**
 * USBSID-Pico Config Handler
 * Reads, displays and writes device configuration via native <select> elements.
 */

'use strict';

/* ─── Value label arrays ─────────────────────────────── */
const VA_ENABLED    = ['Disabled', 'Enabled'];
const VA_TRUEFALSE  = ['False', 'True'];
const VA_ONOFF      = ['Off', 'On'];
const VA_MONOSTEREO = ['Mono', 'Stereo'];
const VA_CHIPTYPES  = ['Real', 'Unknown', 'SKPico', 'ARMSID', 'ARM2SID', 'FPGASID', 'RedipSID', 'PDSID', 'BackSID'];
const VA_SIDTYPES   = ['Unknown', 'N/A', 'MOS8580', 'MOS6581', 'FMopl'];
const VA_CLONETYPES = ['Disabled', 'Other', 'SKPico', 'ARMSID', 'FPGASID', 'RedipSID'];
const VA_RGBSIDNO   = ['Off', '1', '2', '3', '4'];
const VA_CLOCKS     = ['1000000 (Default)', '985248 (PAL)', '1022727 (NTSC)', '1023440 (Drean)', '1022730 (NTSC2)'];

/* Clock speed Hz → array index */
const clkspeed_id = { 1000000: 0, 985248: 1, 1022727: 2, 1023440: 3, 1022730: 4 };

/* ─── Config byte-offset map (for reading) ───────────── */
const cfgmap = {
  CFG_CLOCKLOCK:       5,
  CFG_EXT_CLOCK:       6,   /* external_clock boolean */
  CFG_CLKRATE_H:       7,
  CFG_CLKRATE_M:       8,
  CFG_CLKRATE_L:       9,
  CFG_ONE_EN:         10,
  CFG_ONE_DUAL:       11,
  CFG_ONE_CHIP:       12,
  /* [13] = packed SID IDs (sid1.id | sid2.id<<4) — internal, read-only */
  CFG_ONE_SID1:       14,
  CFG_ONE_SID2:       15,
  CFG_TWO_EN:         20,
  CFG_TWO_DUAL:       21,
  CFG_MIRRORED:       22,   /* mirrored flag (pre-v0.7 backwards compat byte) */
  CFG_TWO_CHIP:       23,
  /* [24] = packed SID IDs (sid1.id | sid2.id<<4) — internal, read-only */
  CFG_TWO_SID1:       25,
  CFG_TWO_SID2:       26,
  CFG_LED_EN:         30,
  CFG_LED_BREATHE:    31,
  CFG_RGBLED_EN:      40,
  CFG_RGBLED_BREATHE: 41,
  CFG_RGBLED_BRIGHT:  42,
  CFG_RGBLED_SIDNO:   43,
  CFG_CDC_EN:         51,
  CFG_WEBUSB_EN:      52,
  CFG_ASID_EN:        53,
  CFG_MIDI_EN:        54,
  CFG_FMOPL_EN:       55,
  CFG_FMOPL_SIDNO:    56,
  CFG_AUSWITCH:       57,
  CFG_LOCK_AUDIOSW:   58,
  CFG_FLAGS:          60,   /* mirrored | (flipped<<1) | (mixed<<2) */
  CFG_FLIPPED:        60,   /* bit 1 of FLAGS */
  CFG_MIXED:          60,   /* bit 2 of FLAGS */
};

/**
 * selectConfigMap: each key is the HTML select element id.
 * group = SET_CONFIG nameval (config section)
 * item  = item within that section
 * special = optional: 'clockrate' | 'clocklock' | 'auswitch'
 *           special items swap item/value encoding per firmware expectations
 */
const selectConfigMap = {
  /* General */
  'sel-clockrate':       { group: 0, item: 0, special: 'clockrate' },
  'sel-clocklock':       { group: 0, item: 0, special: 'clocklock' },
  /* LEDs */
  'sel-led-en':          { group: 3, item: 0 },
  'sel-led-breathe':     { group: 3, item: 1 },
  'sel-rgbled-en':       { group: 4, item: 0 },
  'sel-rgbled-breathe':  { group: 4, item: 1 },
  'sel-rgbled-bright':   { group: 4, item: 2 },
  'sel-rgbled-sidno':    { group: 4, item: 3 },
  /* Socket One */
  'sel-one-en':          { group: 1, item: 0 },
  'sel-one-dual':        { group: 1, item: 1 },
  'sel-one-chip':        { group: 1, item: 2 },
  'sel-one-sid1':        { group: 1, item: 4 },
  'sel-one-sid2':        { group: 1, item: 5 },
  /* Socket Two */
  'sel-two-en':          { group: 2, item: 0 },
  'sel-two-dual':        { group: 2, item: 1 },
  'sel-two-chip':        { group: 2, item: 2 },
  'sel-two-sid1':        { group: 2, item: 4 },
  'sel-two-sid2':        { group: 2, item: 5 },
  'sel-two-mirrored':    { group: 2, item: 6 },
  /* Protocols — groups 7-8 use buffer[2] as value, so special='valueasitem' */
  'sel-asid-en':         { group: 7, item: 0, special: 'valueasitem' },
  'sel-midi-en':         { group: 8, item: 0, special: 'valueasitem' },
  /* FMOpl — group 9 uses buffer[2] as enabled value */
  'sel-fmopl-en':        { group: 9, item: 0, special: 'valueasitem' },
  /* Audio switch (PCB v1.3+) */
  'sel-auswitch':        { group: 10, item: 1, special: 'auswitch' },
  /* Lock audio switch — group 11 uses buffer[2] as value */
  'sel-lock-audiosw':    { group: 11, item: 0, special: 'valueasitem' },
  /* Flipped / Mixed — groups 13-14 use buffer[2] as value */
  'sel-flipped':         { group: 13, item: 0, special: 'valueasitem' },
  'sel-mixed':           { group: 14, item: 0, special: 'valueasitem' },
};

/* ─── Helper: build <select> options from array ──────── */
function buildSelectOptions(sel, arr) {
  sel.innerHTML = '';
  arr.forEach((label, i) => {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = label;
    sel.appendChild(opt);
  });
}

/* Build brightness select 0-255 */
function buildBrightnessOptions(sel) {
  sel.innerHTML = '';
  for (let i = 0; i <= 255; i++) {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = i;
    sel.appendChild(opt);
  }
}

/* Populate all config selects with their option labels */
function buildAllSelectOptions() {
  const get = (id) => document.getElementById(id);

  /* Clock */
  const sc = get('sel-clockrate'); if (sc) buildSelectOptions(sc, VA_CLOCKS);
  const sl = get('sel-clocklock'); if (sl) buildSelectOptions(sl, VA_TRUEFALSE);

  /* LEDs */
  const sle   = get('sel-led-en');         if (sle)  buildSelectOptions(sle, VA_ENABLED);
  const slb   = get('sel-led-breathe');    if (slb)  buildSelectOptions(slb, VA_ENABLED);
  const srge  = get('sel-rgbled-en');      if (srge) buildSelectOptions(srge, VA_ENABLED);
  const srgb  = get('sel-rgbled-breathe'); if (srgb) buildSelectOptions(srgb, VA_ENABLED);
  const srgbr = get('sel-rgbled-bright');  if (srgbr) buildBrightnessOptions(srgbr);
  const srgbs = get('sel-rgbled-sidno');   if (srgbs) buildSelectOptions(srgbs, VA_RGBSIDNO);

  /* Socket One */
  const s1e  = get('sel-one-en');    if (s1e)  buildSelectOptions(s1e, VA_TRUEFALSE);
  const s1d  = get('sel-one-dual');  if (s1d)  buildSelectOptions(s1d, VA_ENABLED);
  const s1c  = get('sel-one-chip');  if (s1c)  buildSelectOptions(s1c, VA_CHIPTYPES);
  const s1s1 = get('sel-one-sid1');  if (s1s1) buildSelectOptions(s1s1, VA_SIDTYPES);
  const s1s2 = get('sel-one-sid2');  if (s1s2) buildSelectOptions(s1s2, VA_SIDTYPES);

  /* Socket Two */
  const s2e  = get('sel-two-en');       if (s2e)  buildSelectOptions(s2e, VA_TRUEFALSE);
  const s2d  = get('sel-two-dual');     if (s2d)  buildSelectOptions(s2d, VA_ENABLED);
  const s2c  = get('sel-two-chip');     if (s2c)  buildSelectOptions(s2c, VA_CHIPTYPES);
  const s2s1 = get('sel-two-sid1');     if (s2s1) buildSelectOptions(s2s1, VA_SIDTYPES);
  const s2s2 = get('sel-two-sid2');     if (s2s2) buildSelectOptions(s2s2, VA_SIDTYPES);
  const s2mr = get('sel-two-mirrored'); if (s2mr) buildSelectOptions(s2mr, VA_ENABLED);

  /* Protocols */
  const sas = get('sel-asid-en');  if (sas) buildSelectOptions(sas, VA_ENABLED);
  const smd = get('sel-midi-en');  if (smd) buildSelectOptions(smd, VA_ENABLED);

  /* FMOpl */
  const sfe  = get('sel-fmopl-en');    if (sfe) buildSelectOptions(sfe, VA_ENABLED);
  const sfsn = get('sel-fmopl-sidno'); if (sfsn) buildSelectOptions(sfsn, VA_RGBSIDNO);

  /* Audio switch */
  const sau  = get('sel-auswitch');     if (sau)  buildSelectOptions(sau, VA_MONOSTEREO);
  const slaw = get('sel-lock-audiosw'); if (slaw) buildSelectOptions(slaw, VA_ENABLED);
  const sflp = get('sel-flipped');      if (sflp) buildSelectOptions(sflp, VA_ENABLED);
  const smix = get('sel-mixed');        if (smix) buildSelectOptions(smix, VA_ENABLED);
}

/* ─── Convert 3-byte clock rate to Hz ───────────────── */
function convertClockRate(cfg) {
  return ((cfg[cfgmap.CFG_CLKRATE_H] << 16)
        | (cfg[cfgmap.CFG_CLKRATE_M] << 8)
        |  cfg[cfgmap.CFG_CLKRATE_L]);
}

/* ─── Set a select's selectedIndex safely ─────────────── */
function setSelectIndex(id, index) {
  const el = document.getElementById(id);
  if (!el) return;
  if (index >= 0 && index < el.options.length) {
    el.selectedIndex = index;
  }
}

/* ─── Present config data into select elements ─────────── */
function presentConfig(cfgData) {
  if (!cfgData || cfgData.length < 64) {
    usbsidLog('presentConfig: data too short — got', cfgData ? cfgData.length : 0, 'bytes (need 64)');
    usbsidSetStatus('Config read error (' + (cfgData ? cfgData.length : 0) + ' bytes) — please retry', 'red');
    return false;
  }
  /* Validate magic bytes */
  if (cfgData[0] !== 0x30 || cfgData[1] !== 127) {
    usbsidLog('presentConfig: invalid magic', cfgData[0], cfgData[1]);
    usbsidSetStatus('Config validation failed — please retry', 'red');
    return false;
  }

  /* Clear first-open hint once config has loaded */
  const cfgHint = document.getElementById('config-status');
  if (cfgHint) cfgHint.textContent = '';

  /* Clock */
  const clockHz = convertClockRate(cfgData);
  const clockIdx = clkspeed_id[clockHz] !== undefined ? clkspeed_id[clockHz] : 0;
  setSelectIndex('sel-clockrate', clockIdx);
  setSelectIndex('sel-clocklock', cfgData[cfgmap.CFG_CLOCKLOCK] ? 1 : 0);

  /* Clock Hz display */
  const chEl = document.getElementById('cfg-clockhz');
  if (chEl) chEl.textContent = clockHz + ' Hz';

  /* LEDs */
  setSelectIndex('sel-led-en',        cfgData[cfgmap.CFG_LED_EN] ? 1 : 0);
  setSelectIndex('sel-led-breathe',   cfgData[cfgmap.CFG_LED_BREATHE] ? 1 : 0);
  setSelectIndex('sel-rgbled-en',     cfgData[cfgmap.CFG_RGBLED_EN] ? 1 : 0);
  setSelectIndex('sel-rgbled-breathe',cfgData[cfgmap.CFG_RGBLED_BREATHE] ? 1 : 0);
  setSelectIndex('sel-rgbled-bright', cfgData[cfgmap.CFG_RGBLED_BRIGHT]);
  setSelectIndex('sel-rgbled-sidno',  cfgData[cfgmap.CFG_RGBLED_SIDNO]);

  /* Socket One */
  setSelectIndex('sel-one-en',    cfgData[cfgmap.CFG_ONE_EN] ? 1 : 0);
  setSelectIndex('sel-one-dual',  cfgData[cfgmap.CFG_ONE_DUAL] ? 1 : 0);
  setSelectIndex('sel-one-chip',  cfgData[cfgmap.CFG_ONE_CHIP]);
  setSelectIndex('sel-one-sid1',  cfgData[cfgmap.CFG_ONE_SID1]);
  setSelectIndex('sel-one-sid2',  cfgData[cfgmap.CFG_ONE_SID2]);

  /* Socket Two */
  setSelectIndex('sel-two-en',       cfgData[cfgmap.CFG_TWO_EN] ? 1 : 0);
  setSelectIndex('sel-two-dual',     cfgData[cfgmap.CFG_TWO_DUAL] ? 1 : 0);
  setSelectIndex('sel-two-chip',     cfgData[cfgmap.CFG_TWO_CHIP]);
  setSelectIndex('sel-two-sid1',     cfgData[cfgmap.CFG_TWO_SID1]);
  setSelectIndex('sel-two-sid2',     cfgData[cfgmap.CFG_TWO_SID2]);
  setSelectIndex('sel-two-mirrored', cfgData[cfgmap.CFG_MIRRORED] ? 1 : 0);

  /* Protocols */
  setSelectIndex('sel-asid-en', cfgData[cfgmap.CFG_ASID_EN] ? 1 : 0);
  setSelectIndex('sel-midi-en', cfgData[cfgmap.CFG_MIDI_EN] ? 1 : 0);

  /* FMOpl */
  setSelectIndex('sel-fmopl-en',    cfgData[cfgmap.CFG_FMOPL_EN] ? 1 : 0);
  setSelectIndex('sel-fmopl-sidno', cfgData[cfgmap.CFG_FMOPL_SIDNO]);

  /* Audio switch */
  setSelectIndex('sel-auswitch',     cfgData[cfgmap.CFG_AUSWITCH] ? 1 : 0);
  setSelectIndex('sel-lock-audiosw', cfgData[cfgmap.CFG_LOCK_AUDIOSW] ? 1 : 0);

  /* Flags: mirrored | (flipped<<1) | (mixed<<2) */
  const flags = cfgData[cfgmap.CFG_FLAGS];
  setSelectIndex('sel-flipped', (flags >> 1) & 1);
  setSelectIndex('sel-mixed',   (flags >> 2) & 1);

  /* Show raw hex in debug */
  const rawEl = document.getElementById('cfg-raw-hex');
  if (rawEl) {
    rawEl.textContent = Array.from(cfgData)
      .map((b, i) => b.toString(16).padStart(2, '0') + ((i + 1) % 16 === 0 ? '\n' : ' '))
      .join('')
      .trim();
  }

  usbsidSetStatus('Config loaded', 'green');
  return true;
}

/* ─── Read config from device ────────────────────────── */
let _cfgReadInProgress = false;

async function doReadConfig() {
  if (!usbsidDevice.isOpen) {
    usbsidSetStatus('Not connected', 'red');
    return;
  }
  if (0 && _cfgReadInProgress) {
    usbsidLog('doReadConfig: already in progress, skipping');
    return;
  }
  _cfgReadInProgress = true;
  usbsidSetStatus('Reading config\u2026');
  try {
    const cfgData = await usbsidDevice.readConfig();
    usbsidLog('doReadConfig: received', cfgData.length, 'bytes');
    presentConfig(cfgData);
    window._lastCfgData = cfgData;
  } catch (e) {
    usbsidLog('doReadConfig error:', e);
    usbsidSetStatus('Config read failed', 'red');
  } finally {
    _cfgReadInProgress = false;
  }
}

/* ─── Write single config item from a <select> change ── */
async function setConfigFromSelect(selectEl) {
  const id  = selectEl.id;
  const map = selectConfigMap[id];
  if (!map) return;

  const value    = parseInt(selectEl.value, 10);
  const { group, item, special } = map;

  let cfg_item  = item;
  let cfg_value = value;

  if (special === 'clockrate') {
    /* item = clock array index, value = 0 */
    cfg_item  = value;
    cfg_value = 0;
  } else if (special === 'clocklock') {
    /* item = clkspeed_id for current clock rate, value = 0 */
    const cfgData = window._lastCfgData;
    if (cfgData) {
      const hz = convertClockRate(cfgData);
      cfg_item  = clkspeed_id[hz] !== undefined ? clkspeed_id[hz] : 0;
    }
    cfg_value = value;
  } else if (special === 'auswitch') {
    /* item = value (0=mono, 1=stereo), value = 0 */
    cfg_item  = value;
    cfg_value = 0;
  } else if (special === 'valueasitem') {
    /* Firmware groups 7-14 read buffer[2] (=cfg_item) as the value — no separate item index */
    cfg_item  = value;
    cfg_value = 0;
  }

  if (!usbsidDevice.isOpen) {
    usbsidSetStatus('Not connected', 'red');
    return;
  }

  const CC = (3 << 6) | 18; /* COMMAND | CONFIG */
  const buf = [CC, SET_CONFIG, group, cfg_item, cfg_value, 0];
  usbsidLog('setConfigFromSelect:', selectEl.id, buf);
  await usbsidDevice.write(buf);
  usbsidSetStatus(`Config: ${id} \u2192 ${selectEl.options[selectEl.selectedIndex].text}`);
}

/* ─── Attach change listeners to all config selects ──── */
function attachSelectListeners() {
  for (const id of Object.keys(selectConfigMap)) {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener('change', () => setConfigFromSelect(el));
    }
  }
}

/* ─── Preset buttons ─────────────────────────────────── */
const presetButtonMap = {
  'btn-preset-single':   async () => { await usbsidDevice.setSingleSID();   usbsidSetStatus('Preset: Single SID'); },
  'btn-preset-dual':     async () => { await usbsidDevice.setDualSID();     usbsidSetStatus('Preset: Dual SID'); },
  'btn-preset-mirrored': async () => { await usbsidDevice.setMirroredSID(); usbsidSetStatus('Preset: Mirrored SID'); },
  'btn-preset-triple':   async () => { await usbsidDevice.setTripleSID();   usbsidSetStatus('Preset: Triple SID (dual in socket 1)'); },
  'btn-preset-triple2':  async () => { await usbsidDevice.setTripleSIDTwo();usbsidSetStatus('Preset: Triple SID (dual in socket 2)'); },
  'btn-preset-quad':     async () => { await usbsidDevice.setQuadSID();     usbsidSetStatus('Preset: Quad SID'); },
  'btn-preset-dual1':    async () => { await usbsidDevice.setDualSocket1();  usbsidSetStatus('Preset: Dual SID socket 1'); },
  'btn-preset-dual2':    async () => { await usbsidDevice.setDualSocket2();  usbsidSetStatus('Preset: Dual SID socket 2'); },
  'btn-preset-dualflip': async () => { await usbsidDevice.setDualFlipped();  usbsidSetStatus('Preset: Dual SID (flipped)'); },
  'btn-preset-quadflip': async () => { await usbsidDevice.setQuadFlipped();  usbsidSetStatus('Preset: Quad SID (flipped)'); },
  'btn-preset-quadmix':  async () => { await usbsidDevice.setQuadMixed();    usbsidSetStatus('Preset: Quad SID (mixed)'); },
  'btn-preset-quadflipmix': async () => { await usbsidDevice.setQuadFlipMix(); usbsidSetStatus('Preset: Quad SID (flip+mix)'); },
};

/* ─── Config action buttons ──────────────────────────── */
const configButtonMap = {
  'btn-retrieve-config':   async () => { await doReadConfig(); },
  'btn-save-config':       async () => {
    await usbsidDevice.saveNoReset();
    usbsidSetStatus('Config saved (no reboot)');
  },
  'btn-save-reboot':       async () => {
    await usbsidDevice.saveConfig();
    usbsidSetStatus('Config saved, rebooting\u2026');
    setTimeout(() => window.location.reload(), 3000);
  },
  'btn-reset-config':      async () => {
    await usbsidDevice.resetConfig();
    usbsidSetStatus('Config reset to defaults');
    setTimeout(() => doReadConfig(), 500);
  },
  'btn-reload-config':     async () => {
    await usbsidDevice.reloadConfig();
    usbsidSetStatus('Config reloaded from flash');
    setTimeout(() => doReadConfig(), 500);
  },
  'btn-apply-config':      async () => { await usbsidDevice.applyConfig();   usbsidSetStatus('Config applied'); },
};

/* ─── Debug buttons ──────────────────────────────────── */
const debugButtonMap = {
  'btn-debug-detect-sids':   async () => { await usbsidDevice.detectSIDs();    usbsidSetStatus('Detecting SID types\u2026'); },
  'btn-debug-detect-clones': async () => { await usbsidDevice.detectClones();  usbsidSetStatus('Detecting clone SIDs\u2026'); },
  'btn-debug-auto-detect':   async () => { await usbsidDevice.autoDetect();    usbsidSetStatus('Auto-detection started\u2026'); },
  'btn-debug-stop-tests-sid': async () => { await usbsidDevice.stopTests();     usbsidSetStatus('Tests stopped'); },
  'btn-debug-test-all':      async () => { await usbsidDevice.configCmd(TEST_ALLSIDS); usbsidSetStatus('Testing all SIDs\u2026'); },
  'btn-debug-test-sid1':     async () => { await usbsidDevice.configCmd(TEST_SID1);    usbsidSetStatus('Testing SID 1\u2026'); },
  'btn-debug-test-sid2':     async () => { await usbsidDevice.configCmd(TEST_SID2);    usbsidSetStatus('Testing SID 2\u2026'); },
  'btn-debug-test-sid3':     async () => { await usbsidDevice.configCmd(TEST_SID3);    usbsidSetStatus('Testing SID 3\u2026'); },
  'btn-debug-test-sid4':     async () => { await usbsidDevice.configCmd(TEST_SID4);    usbsidSetStatus('Testing SID 4\u2026'); },
  'btn-debug-restart-bus':   async () => { await usbsidDevice.restartBus();    usbsidSetStatus('Bus restarted'); },
  'btn-debug-restart-clk':   async () => { await usbsidDevice.restartBusClock(); usbsidSetStatus('Bus clock restarted'); },
  'btn-debug-sync-pios':     async () => { await usbsidDevice.syncPIOs();      usbsidSetStatus('PIOs synced'); },
  'btn-debug-reset-sids':    async () => { await usbsidDevice.resetSID();      usbsidSetStatus('SIDs reset'); },
  'btn-debug-reset-mcu':     async () => {
    await usbsidDevice.resetMCU();
    usbsidSetStatus('MCU resetting\u2026');
    setTimeout(() => window.location.reload(), 3000);
  },
  'btn-debug-bootloader': async () => {
    const CC = (3 << 6) | 18;
    await usbsidDevice.write([CC, BOOTLOADER, 0, 0, 0, 0]);
    usbsidSetStatus('Entering bootloader\u2026');
    setTimeout(() => window.location.reload(), 3000);
  },
};

/* ─── Attach all button listeners ───────────────────── */
function attachButtonListeners() {
  const allMaps = [presetButtonMap, configButtonMap, debugButtonMap];
  allMaps.forEach(bmap => {
    for (const [id, fn] of Object.entries(bmap)) {
      const el = document.getElementById(id);
      if (el) {
        el.addEventListener('click', fn);
      }
    }
  });
}

/* ─── Check if PCB v1.3 (has audio switch) ──────────── */
function checkPCBVersion(vstr) {
  /* Firmware returns e.g. "1.3" — match that directly */
  return typeof vstr === 'string' && vstr.includes('1.3');
}

/* Show/hide audio section based on PCB version */
function applyPCBVersionUI(pcbver) {
  const isV13 = checkPCBVersion(pcbver);
  const box = document.getElementById('box-audio');
  if (box) box.classList.toggle('c64-hidden', !isV13);
}

/* ─── Debug panel visibility ─────────────────────────── */
function checkDebugMode() {
  const panel = document.getElementById('debug-panel');
  if (panel) panel.classList.add('visible');
}

/* ─── Init config UI ─────────────────────────────────── */
function initConfigUI() {
  buildAllSelectOptions();
  attachSelectListeners();
  attachButtonListeners();
  checkDebugMode();
}
