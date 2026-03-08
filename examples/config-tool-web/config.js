/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * config.js
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024-2026 LouD
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
 *
 */

/* Classes */
class DynamicBufferAppender {
  constructor() {
    this.chunks = [];
    this.totalLength = 0;
  }

  append(incomingBuffer) {
    this.chunks.push(new Uint8Array(incomingBuffer));
    this.totalLength += incomingBuffer.byteLength;
  }

  empty() {
    this.chunks = [];
    this.totalLength = 0;
  }

  chunkymunky() {
    return this.chunks;
  }

  length() {
    return this.totalLength;
  }

  getCombinedBuffer() {
    const result = new ArrayBuffer(this.totalLength);
    const view = new Uint8Array(result);
    let offset = 0;
    for (const chunk of this.chunks) {
      view.set(chunk, offset);
      offset += chunk.length;
    }
    return result;
  }
}

/* Objects */
const textDecoder = new TextDecoder();
const appender = new DynamicBufferAppender();

/* Vars */
let cfg, version, pcbversion, readcfg, readver, readpcbver, readnosids, readfmoplsid;

/* Command constants */
const COMMAND          = 3;
const RESET_SID        = 14;
const CONFIG           = 18;
const RESET_MCU        = 19;
const BOOTLOADER       = 20;
const CFG_RESERVED     = 0;
const RESET_USBSID     = 0x20;
const READ_CONFIG      = 0x30;  /* Read full config as bytes */
const APPLY_CONFIG     = 0x31;  /* Apply config from memory */
const SET_CONFIG       = 0x32;  /* Set single config item */
const SAVE_CONFIG      = 0x33;  /* Save and load config and then reboot */
const SAVE_NORESET     = 0x34;  /* Save; load and apply config */
const RESET_CONFIG     = 0x35;  /* Reset to default settings */
const WRITE_CONFIG     = 0x36;  /* Write full config as bytes */
const READ_SOCKETCFG   = 0x37;  /* Read socket config as bytes */
const RELOAD_CONFIG    = 0x38;  /* Reload and apply stored config from flash */
const SINGLE_SID       = 0x40;
const DUAL_SID         = 0x41;
const QUAD_SID         = 0x42;
const TRIPLE_SID       = 0x43;
const TRIPLE_SID_TWO   = 0x44;
const MIRRORED_SID     = 0x45;
const DUAL_SOCKET1     = 0x46;
const DUAL_SOCKET2     = 0x47;
const FLIP_SOCKETS     = 0x48;
const SET_CLOCK        = 0x50;
const DETECT_SIDS      = 0x51;
const TEST_ALLSIDS     = 0x52;
const TEST_SID1        = 0x53;
const TEST_SID2        = 0x54;
const TEST_SID3        = 0x55;
const TEST_SID4        = 0x56;
const LOCK_CLOCK       = 0x58;  /* Locks the clockrate from being changed, saved in config */
const STOP_TESTS       = 0x59;  /* Interrupt any running SID tests */
const TOGGLE_AUDIO     = 0x88;  /* Toggle mono <-> stereo (v1.2+ boards only) */
/* Value constants */
const config_min_version = "0.2.4";
const enabled = ["Disabled", "Enabled"];
const intext = ["Internal", "External"];
const onoff = ["Off", "On"];
const truefalse = ["False", "True"];
const monostereo = ["Mono", "Stereo"];
const socket = ["Single SID", "Dual SID"];
const chiptypes = ["Real", "Clone"];
const sidtypes = ["Unknown", "N/A", "MOS8580", "MOS6581", "FMopl"];
const clonetypes = ["Disabled", "Other", "SKPico", "ARMSID", "FPGASID", "RedipSID"];
const rgbsidtouse = ["Choose", "1", "2", "3", "4"]
const clockspeeds = ["1000000 (DEFAULT)", "985248 (PAL)", "1022727 (NTSC)", "1023440 (DREAN)", "1022730 (NTSC2)"]
const clkspeed_id = {
  1000000: 0,
  985248: 1,
  1022727: 2,
  1023440: 3,
  1022730: 4
}
const cfgmap = {
  CFG_CLOCKLOCK: 5,
  CFG_CLOCK: 6,
  CFG_CLKRATE_H: 7,
  CFG_CLKRATE_M: 8,
  CFG_CLKRATE_L: 9,
  CFG_ONE_EN: 10,
  CFG_ONE_DUAL: 11,
  CFG_ONE_CHIP: 12,
  CFG_ONE_CLONE: 13,
  CFG_ONE_SID1: 14,
  CFG_ONE_SID2: 15,
  CFG_TWO_EN: 20,
  CFG_TWO_DUAL: 21,
  CFG_TWO_CHIP: 23,
  CFG_TWO_CLONE: 24,
  CFG_TWO_SID1: 25,
  CFG_TWO_SID2: 26,
  CFG_TWO_ASONE: 22,
  CFG_LED_EN: 30,
  CFG_LED_BREATHE: 31,
  CFG_RGBLED_EN: 40,
  CFG_RGBLED_BREATHE: 41,
  CFG_RGBLED_BRIGHT: 42,
  CFG_RGBLED_SIDNO: 43,
  CFG_CDC_EN: 51,
  CFG_WEBUSB_EN: 52,
  CFG_ASID_EN: 53,
  CFG_MIDI_EN: 54,
  CFG_FMOPL_EN: 55,
  CFG_FMOPL_SIDNO: 56,
  CFG_AUSWITCH: 57,
}
const cfgids = {
  /* Config selectors */
  CFG_CLOCKLOCK: "us_clocklock",
  CFG_CLOCK: "",
  CFG_CLKRATE: "us_clockrate",
  CFG_ONE_EN: "us_socketone",
  CFG_ONE_DUAL: "us_socketonedual",
  CFG_ONE_CHIP:  "us_socketonechip",
  CFG_ONE_CLONE: "us_socketoneclone",
  CFG_ONE_SID1: "us_socketonesidone",
  CFG_ONE_SID2: "us_socketonesidtwo",
  CFG_TWO_EN: "us_sockettwo",
  CFG_TWO_DUAL: "us_sockettwodual",
  CFG_TWO_CHIP: "us_sockettwochip",
  CFG_TWO_CLONE: "us_sockettwoclone",
  CFG_TWO_SID1: "us_sockettwosidone",
  CFG_TWO_SID2: "us_sockettwosidtwo",
  CFG_TWO_ASONE: "us_sockettwoactasone",
  CFG_LED_EN: "us_picoled",
  CFG_LED_BREATHE: "us_picoledbreathe",
  CFG_RGBLED_EN: "us_rgbled",
  CFG_RGBLED_BREATHE: "us_rgbledbreathe",
  CFG_RGBLED_BRIGHT: "us_rgbledbrightness",
  CFG_RGBLED_SIDNO: "us_rgbledsidno",
  CFG_CDC_EN: "us_cdc",
  CFG_WEBUSB_EN: "us_webusb",
  CFG_ASID_EN: "us_asid",
  CFG_MIDI_EN: "us_midi",
  CFG_FMOPL_EN: "us_fmopl",
  CFG_FMOPL_SIDNO: "us_fmoplsidno",
  CFG_AUSWITCH: "us_audioswitch",
  /* Buttons */
  CFG_RETRIEVE_CFG: 'retrieve-config',
  CFG_SAVE_CFG: 'save-config',
  CFG_DEFAULT_CFG: 'default-config',
  CFG_TOGGLE_CFG: 'toggle-config',
  CFG_TOGGLEAU_CFG: 'audio-toggle',
  CFG_CARDS_BTN: 'config-cards-button',
  /* Presets */
  CFG_SINGLE_SID: "preset-one-sid",
  CFG_DUAL_SID: "preset-dual-sid",
  CFG_MIRRORED_SID: "preset-mirror-sid",
  CFG_QUAD_SID: "preset-quad-sid",
  CFG_TRIPLE_SID: "preset-triple-sid",
  CFG_TRIPLE2_SID: "preset-triple2-sid",
  CFG_DUAL1_SID: "preset-dual1-sid",
  CFG_DUAL2_SID: "preset-dual2-sid",
  /* Debugging */
  CFG_STOP_TESTS: "debug-stop-tests",
  CFG_TEST_ALL: "debug-test-all",
  CFG_TEST_ONE: "debug-test-one",
  CFG_TEST_TWO: "debug-test-two",
  CFG_TEST_THREE: "debug-test-three",
  CFG_TEST_FOUR: "debug-test-four",
  CFG_APPLY_CFG: "debug-apply-config",
  CFG_DETECT_SIDS: "debug-detect-sids",
  CFG_RESET_SIDS: "debug-reset-sids",
  CFG_RESET_USBSID: "debug-reset-usbsid",
  CFG_BOOTLOADER: "debug-bootloader",
}

/* Elements */
let sidaddr = document.querySelector("#addr");
let configDiv = document.querySelector("#usbsid-config");
let statusDisplay = document.querySelector('#config-status');
let connectButton = document.querySelector("#device-connect");
/* Buttons */
let connectStatus = document.querySelector("#toggle-status-text");
let configButton = document.querySelector('#'+cfgids.CFG_RETRIEVE_CFG);
let saveConfig = document.querySelector('#'+cfgids.CFG_SAVE_CFG);
let defaultConfig = document.querySelector('#'+cfgids.CFG_DEFAULT_CFG);
/* Variables */
let clicked = false;

/* Utils */
function convertClockRate(h, m, l, arr) {
  return ((arr[h] << 16) | (arr[m] << 8) | arr[l]);
}

function fillCfgElement(elementid, configid, c_arr, v_arr) {
  $('[id="'+elementid+'"')
    .text(
      (typeof(v_arr) == 'object'
      ? v_arr[c_arr[configid]]
      : c_arr[configid])
    );
}

function dec2Hex(dec) {
  return Math.abs(dec).toString(16).padStart(2, '0');
}

function toHexString(byteArray) {
  return Array.from(byteArray, function(byte) { return ('0' + (byte & 0xFF).toString(16)).slice(-2); }).join(',')
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function createSIDTable(name = 'SID1') {

  var tbl = document.createElement('table');
  tbl.classList.add('flex-row');
  tbl.setAttribute('border', '1');
  tbl.setAttribute('id', name);
  tbl.style.display = 'none';
  tbl.style.width = '50%';
  tbl.style.borderColor = "#000000";
  tbl.style.borderWidth = '0.5px';
  tbl.style.borderSpacing = '0';
  tbl.style.tableLayout = 'fixed';
  tbl.style.textAlign = 'center';

  var thed = document.createElement('thead');
  {
    var tr = document.createElement('tr');
    var th = document.createElement('th')
    th.setAttribute('colspan', 8);
    th.appendChild(document.createTextNode(name))
    tr.appendChild(th);
    thed.appendChild(tr);
  }

  var tbdy = document.createElement('tbody');
  /* var colno = 0; */
  var nameN = parseInt(name.slice(3));
  var colno = 0;
  for (var i = 1; i <= 5; i++) {
    var tr = document.createElement('tr');
    tr.setAttribute('id', name+'row'+i);
    var th = document.createElement('th');
    if (i <= 3) th.appendChild(document.createTextNode('Voice ' + i));
    if (i == 4) th.appendChild(document.createTextNode('Filter'));
    if (i == 5) {
      th.appendChild(document.createTextNode('Misc'));
      tr.setAttribute('disabled', true);
      tr.style.display = 'none';
    }
    tr.appendChild(th)
    for (var j = 1; j <= (i <= 3 ? 7 : 4); j++) {
      var td = document.createElement('td');
      td.setAttribute('id', name+'col'+colno);
      td.appendChild(document.createTextNode('$'+dec2Hex((nameN * 0x20) + colno)+':00'))
      tr.appendChild(td)
      colno++;
    }
    tbdy.appendChild(tr);
  }
  tbl.appendChild(thed);
  tbl.appendChild(tbdy);
  return tbl;
}

function createTable(name = 'SID0', rows = 2, cols = 2) {
  var tbl = document.createElement('table');
  tbl.setAttribute('border', '1');
  tbl.setAttribute('id', name);
  tbl.style.display = 'none';
  tbl.style.width = '50%';
  tbl.style.borderColor = "#000000";
  tbl.style.borderWidth = '0.5px';
  tbl.style.borderSpacing = '0';
  tbl.style.tableLayout = 'fixed';
  tbl.style.textAlign = 'center';

  var thed = document.createElement('thead');
  {
    var tr = document.createElement('tr');
    var th = document.createElement('th')
    th.setAttribute('colspan', cols);
    th.appendChild(document.createTextNode(name))
    tr.appendChild(th);
    thed.appendChild(tr);
  }

  var tbdy = document.createElement('tbody');
  var colno = 1;
  for (var i = 1; i <= rows; i++) {
    var tr = document.createElement('tr');
    tr.setAttribute('id', name+'row'+i);
    for (var j = 1; j <= cols; j++) {
      var td = document.createElement('td');
      td.setAttribute('id', name+'col'+colno);
      td.appendChild(document.createTextNode('$'+dec2Hex(colno)+':00'))
      tr.appendChild(td)
      colno++;
    }
    tbdy.appendChild(tr);
  }
  tbl.appendChild(thed);
  tbl.appendChild(tbdy);
  return tbl;
}

const sidmemory = function () {  /* TODO: Must be relative to the amount of SID's in a SID tune */
  // let nosidsintune = 1 + (SID_address[1] > 0 ? 1 : 0) + (SID_address[2] > 0 ? 1 : 0) + (SID_address[3] > 0 ? 1 : 0);
  let sidmem = document.querySelector("#SIDMEMORY");
  sidmem.innerHTML = '';  // clear first
  for (var t = 1; t < 5; t++) {
    sidmem.appendChild(createSIDTable('SID'+t));
  }
}

const fillsidmemory = function (array) {
  if (array[1] >= 0x0 && array[1] < 0x20) {
    document.querySelector('#SID1col'+array[1]).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
    // document.querySelector('#SID0col'+array[1]).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(memory[0xD400 + array[1]]);
  } else if (array[1] >= 0x20 && array[1] < 0x40) {
    document.querySelector('#SID2col'+(array[1] - 0x20)).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
  } else if (array[1] >= 0x40 && array[1] < 0x60) {
    document.querySelector('#SID3col'+(array[1] - 0x40)).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
  } else if (array[1] >= 0x60 && array[1] < 0x80) {
    document.querySelector('#SID4col'+(array[1] - 0x60)).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
  }
}

const enablesidmemory = function (s, SIDamount) {
  if (s < SIDamount) {
    document.querySelector("#SID"+(s+1)).style.display = '';
  } else {
    document.querySelector("#SID"+(s+1)).style.display = 'none';
  }
}

function enableConfigThings() {
  if (usbsid.version >= config_min_version) {
    console.log(`Device firmware version ${usbsid.version} verified against minimal config version ${config_min_version}`);
    $("#toggle-config").text('Close config');
    $("#toggle-status-text").text('');
    appender.empty();
    configDiv.classList.remove("d-none");
    configButton.removeAttribute("disabled");
    saveConfig.removeAttribute("disabled");
    defaultConfig.removeAttribute("disabled");
    enableSidButtons();  // TODO: REMOVE!
    configavailable = true;
    return true;
  } else {
    $('#usbsid-config').addClass('d-none');
    var message = `Warning, your firmware version ${usbsid.version} is lower then ${config_min_version}, configuration elements are disabled! Please update your firmware!`;
    console.log(message);
    alert(message);
    configavailable = false;
    return false;
  }
}

function disableConfigThings() {
  $("#toggle-config").text('Open config');
  $("#toggle-status-text").text('');
  enableSidButtons(false);  // TODO: REMOVE!
  configButton.setAttribute("disabled", true);
  $(configDiv).addClass("d-none");
  configavailable = false;
}

function toggleElement(buttonid, elementid){
  if (!$('#'+elementid).hasClass('d-none')) {
    $('#'+elementid).addClass('d-none');
    $('#'+buttonid).text('Show');
  } else {
    $('#'+elementid).removeClass('d-none');
    $('#'+buttonid).text('Hide');
  }
}

function readCfgData() {
  const readbuffer = appender.getCombinedBuffer();
  return new Uint8Array(readbuffer);
}

function presentConfig(success = true) {
  const urlParams = new URLSearchParams(queryString);
  let debug = urlParams.get('debug');
  if (debug === 'usbsidpico') {
    var debugElements = document.getElementsByName("debugel");
    for (var i = 0, max = debugElements.length; i < max; i++) {
        debugElements[i].style.display = '';
    }
  }
  cfg = readCfgData();
  let configStatus = document.querySelector('#config-status');
  let configList = document.querySelector('#config-list');
  if (success == false || typeof(cfg) == 'object' && cfg[0] != 0x30 && cfg[1] != 127) {
    if (!$('#current-config').hasClass('d-none')) {
      $('#current-config').addClass('d-none');
    }
    configStatus.innerHTML = 'Error reading config, refresh or click retrieve to try again!';
    readcfg = false;
  } else {
    configStatus.textContent = 'Config loaded';
    $('#current-config').removeClass('d-none');

    /* General */
    $('[id="' + cfgids.CFG_CLKRATE + '"')
    .text(convertClockRate(cfgmap.CFG_CLKRATE_H, cfgmap.CFG_CLKRATE_M, cfgmap.CFG_CLKRATE_L, cfg));
    fillCfgElement(cfgids.CFG_CLOCKLOCK, cfgmap.CFG_CLOCKLOCK, cfg, truefalse);
    fillCfgElement(cfgids.CFG_LED_EN, cfgmap.CFG_LED_EN, cfg, enabled);
    fillCfgElement(cfgids.CFG_LED_BREATHE, cfgmap.CFG_LED_BREATHE, cfg, enabled);
    fillCfgElement(cfgids.CFG_RGBLED_EN, cfgmap.CFG_RGBLED_EN, cfg, enabled);
    fillCfgElement(cfgids.CFG_RGBLED_BREATHE, cfgmap.CFG_RGBLED_BREATHE, cfg, enabled);
    fillCfgElement(cfgids.CFG_RGBLED_BRIGHT, cfgmap.CFG_RGBLED_BRIGHT, cfg);
    fillCfgElement(cfgids.CFG_RGBLED_SIDNO, cfgmap.CFG_RGBLED_SIDNO, cfg);
    /* Socket One */
    fillCfgElement(cfgids.CFG_ONE_EN, cfgmap.CFG_ONE_EN, cfg, truefalse);
    fillCfgElement(cfgids.CFG_ONE_DUAL, cfgmap.CFG_ONE_DUAL, cfg, enabled);
    fillCfgElement(cfgids.CFG_ONE_CHIP, cfgmap.CFG_ONE_CHIP, cfg, chiptypes);
    fillCfgElement(cfgids.CFG_ONE_CLONE, cfgmap.CFG_ONE_CLONE, cfg, clonetypes);
    fillCfgElement(cfgids.CFG_ONE_SID1, cfgmap.CFG_ONE_SID1, cfg, sidtypes);
    fillCfgElement(cfgids.CFG_ONE_SID2, cfgmap.CFG_ONE_SID2, cfg, sidtypes);
    /* Socket Two */
    fillCfgElement(cfgids.CFG_TWO_EN, cfgmap.CFG_TWO_EN, cfg, truefalse);
    fillCfgElement(cfgids.CFG_TWO_DUAL, cfgmap.CFG_TWO_DUAL, cfg, enabled);
    fillCfgElement(cfgids.CFG_TWO_CHIP, cfgmap.CFG_TWO_CHIP, cfg, chiptypes);
    fillCfgElement(cfgids.CFG_TWO_CLONE, cfgmap.CFG_TWO_CLONE, cfg, clonetypes);
    fillCfgElement(cfgids.CFG_TWO_SID1, cfgmap.CFG_TWO_SID1, cfg, sidtypes);
    fillCfgElement(cfgids.CFG_TWO_SID2, cfgmap.CFG_TWO_SID2, cfg, sidtypes);
    fillCfgElement(cfgids.CFG_TWO_ASONE, cfgmap.CFG_TWO_ASONE, cfg, enabled);
    /* Protocols */
    fillCfgElement(cfgids.CFG_CDC_EN, cfgmap.CFG_CDC_EN, cfg, enabled);
    fillCfgElement(cfgids.CFG_WEBUSB_EN, cfgmap.CFG_WEBUSB_EN, cfg, enabled);
    fillCfgElement(cfgids.CFG_ASID_EN, cfgmap.CFG_ASID_EN, cfg, enabled);
    fillCfgElement(cfgids.CFG_MIDI_EN, cfgmap.CFG_MIDI_EN, cfg, enabled);
    /* FMOpl */
    fillCfgElement(cfgids.CFG_FMOPL_EN, cfgmap.CFG_FMOPL_EN, cfg, enabled);
    fillCfgElement(cfgids.CFG_FMOPL_SIDNO, cfgmap.CFG_FMOPL_SIDNO, cfg);
    /* Audio switch */
    fillCfgElement(cfgids.CFG_AUSWITCH, cfgmap.CFG_AUSWITCH, cfg, monostereo);

    configList.textContent = toHexString(cfg);
    readcfg = false;
  }
}

function presentVersion(version) {
  let versionDisplay = document.querySelector('#version');
  if (version == false || typeof(version) == 'object' && version[1] == 0) {
    version = 'error';
    readver = false;
    versionDisplay.innerHTML = 'Error reading version, refresh or click retrieve to try again!';
  } else {
    let versionLength = version[1];
    let versionText = version.slice(2, (2 + versionLength));
    versionDisplay.textContent = "v" + textDecoder.decode(versionText);
    readver = false;
  }
}

function setConfig (arr, text, arrtwo = null, duringplay = false) {
  if (!webusbplaying || duringplay) {
    if (!clicked) {
      clicked = true;
      console.log("write: " + arr);
      webusb.writeReg(arr);
      if (arrtwo) {
        webusb.writeReg(arrtwo);
      }
      if (text != null) statusDisplay.textContent = text;
      clicked = false;
      return true;
    }
  } else {
    statusDisplay.textContent = 'Press stop playing first';
    return false;
  }
}

async function setButtonItem(command, cmd_text, non_config = false) {
  if (!non_config) {
    setConfig([((COMMAND << 6) | CONFIG), command, CFG_RESERVED, CFG_RESERVED, CFG_RESERVED, CFG_RESERVED], `(${cmd_text})`);
  } else if (non_config) {
    setConfig([((COMMAND << 6) | command), CFG_RESERVED, CFG_RESERVED], `(${cmd_text})`);
  };
}

async function enableAndListen(id, enabled, command, cmd_text, non_config = false) {
  $('#' + id).prop('disabled', enabled);
  $('#' + id).on('click', function () {
    setButtonItem(command, cmd_text, non_config);
    thisid = $(this).attr('id');
    if (thisid == cfgids.CFG_RESET_USBSID
      || thisid == cfgids.CFG_BOOTLOADER) {
      window.location.reload();
    }
  });
}

function enableSidButtons(enable = true) {
  const urlParams = new URLSearchParams(queryString);
  let debug = urlParams.get('debug');

  enable = (enable == true ? enable : false);


  enableAndListen(cfgids.CFG_SINGLE_SID, !enable, SINGLE_SID, 'Config set to single SID');
  enableAndListen(cfgids.CFG_DUAL_SID, !enable, DUAL_SID, 'Config set to dual SID');
  enableAndListen(cfgids.CFG_MIRRORED_SID, !enable, MIRRORED_SID, 'Config set to mirrored SID');
  enableAndListen(cfgids.CFG_QUAD_SID, !enable, QUAD_SID, 'Config set to quad SID');
  enableAndListen(cfgids.CFG_TRIPLE_SID, !enable, TRIPLE_SID, 'Config set to triple SID - dual in socket 1');
  enableAndListen(cfgids.CFG_TRIPLE2_SID, !enable, TRIPLE_SID_TWO, 'Config set to triple SID - dual in socket 2');
  enableAndListen(cfgids.CFG_DUAL1_SID, !enable, DUAL_SOCKET1, 'Config set to dual SID in socket 1');
  enableAndListen(cfgids.CFG_DUAL2_SID, !enable, DUAL_SOCKET2, 'Config set to dual SID in socket 2');

  enableAndListen(cfgids.CFG_SAVE_CFG, !enable, SAVE_NORESET, 'Config saved');
  enableAndListen(cfgids.CFG_DEFAULT_CFG, !enable, RESET_CONFIG, 'Reset config to default');


  if (debug === 'usbsidpico') {
    $('#debug-buttons').removeClass('d-none');

    enableAndListen(cfgids.CFG_TEST_ALL, !enable, TEST_ALLSIDS, 'Testing all SIDs');
    enableAndListen(cfgids.CFG_TEST_ONE, !enable, TEST_SID1, 'Test SID one');
    enableAndListen(cfgids.CFG_TEST_TWO, !enable, TEST_SID2, 'Test SID two');
    enableAndListen(cfgids.CFG_TEST_THREE, !enable, TEST_SID3, 'Test SID three');
    enableAndListen(cfgids.CFG_TEST_FOUR, !enable, TEST_SID4, 'Test SID four');

    enableAndListen(cfgids.CFG_APPLY_CFG, !enable, APPLY_CONFIG, 'Applying config');
    enableAndListen(cfgids.CFG_DETECT_SIDS, !enable, DETECT_SIDS, 'Detecting SIDs');
    enableAndListen(cfgids.CFG_RESET_SIDS, !enable, RESET_SID, 'Resetting SIDs', true);
    enableAndListen(cfgids.CFG_RESET_USBSID, !enable, RESET_MCU, 'Resetting USBSID', true);
    enableAndListen(cfgids.CFG_BOOTLOADER, !enable, BOOTLOADER, 'Resetting to bootloader', true);
  }
};

async function setConfigItem(element) {
  var text = $(element).text();
  let button = $(element).parents('div').children('button');
  let buttonid = button.attr('id');
  let cfg_element = $(element).parents('ul').children('#config-name');
  let cfg_name = cfg_element.text();
  let cfg_nameval = cfg_element.val();
  let cfg_item = parseInt(button.val());
  var cfg_value = parseInt($(element).attr('name'));
  if (buttonid == 'us_B_clockrate' || buttonid == 'us_B_fmop' || buttonid == 'us_B_auswitch') {
    cfg_item = cfg_value;
    cfg_value = 0;
  }
  if (buttonid == 'us_B_clocklock') {
    cfg_item = clkspeed_id[convertClockRate(cfgmap.CFG_CLKRATE_H, cfgmap.CFG_CLKRATE_M, cfgmap.CFG_CLKRATE_L, cfg)];
  }
  console.log([((COMMAND << 6) | CONFIG), SET_CONFIG, cfg_nameval, cfg_item, cfg_value, CFG_RESERVED], `(${cfg_name} to ${text})`);
  if (setConfig([((COMMAND << 6) | CONFIG), SET_CONFIG, cfg_nameval, cfg_item, cfg_value, CFG_RESERVED], `(${cfg_name} to ${text})`)) {
    $("button[id='" + buttonid).text(text);
  };
}

async function fill_dropdown(id, arr) {
  let ul_to_fill = $('#' + id);
  let listItem = document.createElement('li');
  arr.map((item, n) => {
    let option = document.createElement('span');
    option.innerText = item;
    option.className = "dropdown-item";
    option.setAttribute('name', n);
    listItem.append(option);
  });
  ul_to_fill.append(listItem);
}

function checkPCBVersion(versionstring) {
  if (versionstring.indexOf("USBSID") == 0) {
    if (versionstring.indexOf("v1.3") > 0) {
      return true;
    }
  } else {
    return false;
  }
}

/* Event listeners */
(function () {
  'use strict';

  document.addEventListener('DOMContentLoaded', _ => {

    $('#mute-sid').on('click', function () {
      Mute_SID = (Mute_SID == 0) ? 1 : 0;
    });

    $('select[id="asid-midi-outputs"]').on('change', function () {
      var selectedOutput = $(this).find("option:selected").text();
      if (checkPCBVersion(selectedOutput)) {
        $('#asid-audio-buttons').removeClass('d-none');
      } else {
        $('#asid-audio-buttons').addClass('d-none');
      }
    })

    $('#sysex-toggle-audio').on('click', function () {
      sysexCommand(1);
    })

    /* Doesn't work from here, moved to jsSID-webusb.js webusb.connect(); */
    /* if (checkPCBVersion(webusb.name)) {
      $('#webusb-audio-buttons').removeClass('d-none');
    } else {
      $('#webusb-audio-buttons').addClass('d-none');
    } */

    $('#toggle-audio').on('click', function () {
      setConfig([((COMMAND << 6) | CONFIG), TOGGLE_AUDIO, 0, 0, 0, 0], null, null, true);
    })

    $('#flip-sockets').on('click', function () {
      setConfig([((COMMAND << 6) | CONFIG), FLIP_SOCKETS, 0, 0, 0, 0], null, null, true);
    })

    sidmemory();

    /* General */
    fill_dropdown('us_set_clockrate', clockspeeds);
    fill_dropdown('us_set_clocklock', truefalse);
    fill_dropdown('us_set_led', enabled);
    fill_dropdown('us_set_ledbreathe', enabled);
    fill_dropdown('us_set_rgbled', enabled);
    fill_dropdown('us_set_rgbledbreathe', enabled);
    fill_dropdown('us_set_rgbledbrightness', [...Array(256).keys()]);
    fill_dropdown('us_set_rgbledsidtouse', rgbsidtouse);
    fill_dropdown('us_set_fmopl', enabled);
    fill_dropdown('us_set_auswitch', monostereo);
    /* Socket One */
    fill_dropdown('us_set_one_enabled', truefalse);
    fill_dropdown('us_set_one_chiptype', chiptypes);
    fill_dropdown('us_set_one_clonetype', clonetypes);
    fill_dropdown('us_set_one_dualsid', enabled);
    fill_dropdown('us_set_one_sidone', sidtypes);
    fill_dropdown('us_set_one_sidtwo', sidtypes);
    /* Socket Two */
    fill_dropdown('us_set_two_enabled', truefalse);
    fill_dropdown('us_set_two_chiptype', chiptypes);
    fill_dropdown('us_set_two_clonetype', clonetypes);
    fill_dropdown('us_set_two_dualsid', enabled);
    fill_dropdown('us_set_two_sidone', sidtypes);
    fill_dropdown('us_set_two_sidtwo', sidtypes);
    fill_dropdown('us_set_two_actasone', enabled);

    $('#'+cfgids.CFG_CARDS_BTN).on('click', function (){
      toggleElement($(this).attr('id'), 'config-cards');
    });

    $("ul[id^='us_set_'].dropdown-menu *> span.dropdown-item").on('click', function () {
      setConfigItem(this);
    });

    async function retrieveConfig() {
      if (connectButton.textContent === 'Disconnect') {
        if (!webusbplaying) {
          setTimeout(function() { readConfig(); }, 100);
          if(version == null || version === 'undefined')
            setTimeout(function() { readVersion(); }, 200);
        } else {
          connectStatus.textContent = 'Press stop playing first';
        }
      }
    }

    $('#'+cfgids.CFG_RETRIEVE_CFG).on('click', function () {
      retrieveConfig();
    });

    $('#'+cfgids.CFG_TOGGLE_CFG).on('click', function () {
      if (!webusbplaying) {
        if ($(this).text() == 'Open config') {
          enableConfigThings();
          retrieveConfig();
        } else {
          disableConfigThings();
        };
      } else {
        connectStatus.textContent = 'Press stop playing first';
      };
    });

  })
})();
