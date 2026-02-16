/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * main.js
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

var $ = jQuery.noConflict();

var isMobile = $("body").attr("data-mobile") !== "0";
var isNotips = $("body").attr("data-notips") !== "0";

const PATH = window.location.pathname.replace(window.location.pathname.match().input, "/SID/");
const LIST = PATH + "sidfilelist.txt";
var files;
let max_file_idx = 0;

const queryString = window.location.search;
const urlParams = new URLSearchParams(queryString);
var emulator;
let storedEmulator;
let playState = false;
let selectedsid = null;
let sidfilepath = null;
let sidfilename = null;
let selectedsididx = null;
var songInfo;
let currentSubtune = 0, maxSubtune = 0;
let needmidiaction = false;

/* WEBSID */
var websid_em;
/* END */

// var targetObj = {};
// var targetProxy = new Proxy(targetObj, {
//   set: function (target, key, value) {
//       if (target[key] == playState)
//         console.log(`${key} set to ${value}`);
//       target[key] = value;
//       return true;
//   }
// });

function retrieve_files() {
  try {
    return fetch(LIST, {})
    .then((response) => {
      return response.text().then((data) => {
        return data.trim().split(/\n/);
      })
    })
  } catch (error) {
    console.error(error);
  }
}

async function populate_sidfilelist() {
  let sidfilesList = $("#sidfiles");
  let listItem = document.createElement('li');
  listItem.setAttribute("id", "sidfilelist");
  await retrieve_files().then((data) => { files = data });
  files.map((file, _) => {
    let opt = document.createElement("span");
    opt.className = "dropdown-item sidfile";
    if (~file.indexOf(".sid")) { opt.setAttribute('name', file); }
    opt.innerHTML = file;
    listItem.append(opt);
    max_file_idx++;
  });
  sidfilesList.append(listItem);
  change_sidfile_handler();
}

function change_sidfile_handler() {
  $("ul[id='sidfiles'].dropdown-menu *> span.sidfile").on('click', function () {
    var selText = $(this).text();
    var selVal = $(this).attr('name');
    if (!~selVal.indexOf(".sid")) return;
    var selBtn = $(this).parents('.dropdown').find('.dropdown-toggle');
    selBtn.val(selVal);
    selBtn.text(selText);

    if (emulator != "websid") {
      SID.setVolume(0);
      SID.stop();
    }

    var sid = $(this).attr('name');
    var tmp = sid.split('/');
    sidfilepath = tmp.length > 1 ? tmp[0] : null;
    sidfilename = tmp.length > 1 ? tmp[1] : tmp[0]
    selectedsid = PATH + sid;
    console.log("Selected " + selectedsid);
    files.find((s, idx) => {
      if (s === sid) {
        selectedsididx = idx;;
      }
    });
    if (configavailable) { disableConfigThings(); };
    currentSubtune = 0;
    loadSID();
  });
}

function change_sidfile(sid, idx) {
  var $this = $("ul[id='sidfiles'].dropdown-menu *> span.sidfile");
  var selBtn = $this.parents('.dropdown').find('.dropdown-toggle');
  selBtn.val(sid);
  selBtn.text(sid);
  if (emulator != "websid") {
    SID.setVolume(0);
    SID.stop();
  }
  var tmp = sid.split('/');
  sidfilepath = tmp.length > 1 ? tmp[0] : null;
  sidfilename = tmp.length > 1 ? tmp[1] : tmp[0]
  selectedsid = PATH + sid;
  console.log("Selected " + selectedsid);
  if (configavailable) { disableConfigThings(); };
  currentSubtune = 0;
  selectedsididx = idx;
  loadSID();
}

// const dropdowns = document.querySelectorAll('.dropdown-toggle')
// const dropdown = [...dropdowns].map((dropdownToggleEl) => new bootstrap.Dropdown(dropdownToggleEl, {
//   popperConfig(defaultBsPopperConfig) {
//       return { ...defaultBsPopperConfig, strategy: 'fixed' };
//   }
// }));

function select_emulator(emu) {
  const emulatorButton = $("#emulatorButton");
  const newValue = $("#emulators *> a[name='"+emu+"']").text();
  emulatorButton.text(newValue);
  emulatorButton.val(emu);
  emulator = emu;
}

async function change_emulator() {
  $("ul[id='emulators'].dropdown-menu li a").click(function() {
    var selText = $(this).text();
    var selVal = $(this).attr('name');
    var selBtn = $(this).parents('.dropdown').find('.dropdown-toggle');
    selBtn.val(selVal);
    selBtn.text(selText);
    emulator = selVal;
    localStorage.setItem("emulator", emulator);
    urlParams.set('player', emulator);
    window.location.search = urlParams.toString();
    return false;
  });
}

$(function () { // DOM ready

  detectBrowser();

  populate_sidfilelist();

  storedEmulator = localStorage.getItem("emulator");
  if (storedEmulator == null) {
    storedEmulator = "hermit";
  }

  emulator = urlParams.get('player');
  if ($.inArray(emulator, [
    "websid",
    "hermit",
    "webusb",
    "asid",
  ]) === -1) emulator = storedEmulator;

  HandleEmulator(emulator)

  if (emulator != "websid") {
    SID = new SIDPlayer(emulator);
  }

  select_emulator(emulator);
  change_emulator();

  $("#load-file").on('click', function () {
    if (configavailable) { disableConfigThings(); };
    currentSubtune = 0;
    loadSID();
  })

  $("#stop").on('click', function () {
    if ($('#toggle-config').text() != 'Open config') {
      if (!configavailable) { enableConfigThings(); };
    };
    stopPlay();
  })

  $("#play-pause").on('click', function () {
    if (configavailable) { disableConfigThings(); };
    playPause();
  })

  $("#toggle-previous-tune").on('click', function () {
    if (currentSubtune > 0) {
      currentSubtune -= 1;
    } else {
      currentSubtune = maxSubtune;
    };
    loadSID();
  });

  $("#toggle-next-tune").on('click', function () {
    if (currentSubtune < maxSubtune) {
      currentSubtune += 1;
    } else {
      currentSubtune = 0;
    };
    loadSID();
  });

  $("#toggle-previous-sid").on('click', function () {
    nextSID(selectedsididx-1, false);
  });

  $("#toggle-next-sid").on('click', function () {
    nextSID(selectedsididx+1, true);
  });

})

function nextSID(idx, up = true) {
  if (up && idx >= max_file_idx) idx = 0;
  if (!up && idx < 0) idx = max_file_idx-1;
  if (!~files[idx].indexOf(".sid")) {
    if (up) idx++;
    else idx--;
  }
  if (up && idx >= max_file_idx) {
    idx = 0;
  }
  if (!up && idx < 0) {
    idx = max_file_idx-1;
  }
  /* console.log(idx, files[idx]); */
  change_sidfile(files[idx], idx);
}

function setSubTuneInfo() {
  if (emulator != "websid") {
    songInfo = SID.getSongInfo();
  } else {
    songInfo = websid_em.songInfo;
  }
  maxSubtune = ((emulator != "websid") ? songInfo.maxSubsong : (songInfo.maxSubsong - 1));
  currentSubtune = songInfo.actualSubsong;
  $('#current-subtune').text(currentSubtune + 1);
  $('#max-subtunes').text(maxSubtune + 1);
}

async function stopPlay() {
  if (emulator != "websid") {
    SID.setVolume(0);
    SID.stop();
  } else {
    await websid_em.Cmd('stop');
    // websid_em.StopWorker();
  }
  setButtonPlay();
  playState = false;
}

async function loadSID() {
  if (emulator == "websid") {
    // websid_em.StopWorker();
    // websid_em.StartWorker();
    var sidpath = PATH + (sidfilepath != null ? sidfilepath + "/" : "");
    websid_em.Cmd('usb');
    websid_em.setPathFilename(sidpath,sidfilename);
    websid_em.setTrack(currentSubtune);
    setTimeout(() => {websid_em.Cmd('init');}, 100);
    setTimeout(() => {websid_em.Cmd('start');}, 300);
    setTimeout(() => {websid_em.Cmd('play');}, 500);
  }

  if (emulator == "webusb") {
    $("#sid-memory").addClass("d-none");
    let sidmem = document.querySelector("#SIDMEMORY");
    sidmem.innerHTML = '';
    sidmemory();
    $("#sid-memory").removeClass("d-none");
  }

  if (emulator != "websid") {
    if (emulator != 'webusb') {
      SID.setVolume(1);
    } else {
      SID.setVolume(1);
    }
  }
  setButtonPause();
  if (emulator != "websid") {
    SID.load(currentSubtune, 1000, selectedsid, null);
  }
  // if (emulator == "webusb") sidmemory();
  playState = true;
  setTimeout(function() { setSubTuneInfo(); }, (emulator != "websid" ? 100 : 800));
}

function playPause() {
  if (!playState) {
    if (emulator != 'webusb' && emulator != 'websid') {
      SID.setVolume(0);
    } else if (emulator != 'websid') {
      SID.setVolume(1);
    }
    if (emulator != 'websid') { SID.play(); } // Has the power to resume after pause
    if (emulator == 'websid') {
      websid_em.Cmd('resume');
    }
    setButtonPause();
    playState = true;
  } else {
    if (emulator != 'websid') {
      SID.setVolume(0);
      SID.pause();
    }
    if (emulator == 'websid') {
      websid_em.Cmd('pause');
    }
    setButtonPlay();
    playState = false;
  }
}

function HandleEmulator(emulator) {
  $("#play-pause").removeClass('disabled');
  $("#stop").removeClass('disabled');

  if (emulator == "websid") {
    websid_em = new USPlayer();
    websid_em.StartWorker();
    $("#websidusb-connect").show();
  } else
  if (emulator == "webusb") {
    $('#mute-buttons').removeClass('d-none');
    $("#webusb-connect").show();
    // $("#sidbuttons").show();
    // $("#ledbuttons").show();

    /* Moved to jsSID-webusb.js webusb.connect(); */
    /* $('#webusb-audio-buttons').removeClass('d-none'); */

    // $('#mute-buttons').removeClass('d-none');
  }
  else
  if (emulator == "asid") {
    $('#mute-buttons').removeClass('d-none');
    navigator.permissions.query({ name: "midi", sysex: true }).then((result) => {
      if (result.state === "granted") {
        $("#asid-midi").show();
      } else if (result.state === "prompt") {
        $("#asid-midi").show();
      } else {
        $("#asid-midi").hide();
      }
    });
  }
  else {
    $("#websidusb-connect").hide();
    $("#webusb-connect").hide();
    $("#sid-memory").addClass("d-none");
    // $("#sidbuttons").hide();
    // $("#ledbuttons").hide();
    $('#webusb-audio-buttons').addClass('d-none');
    $('#mute-buttons').addClass('d-none');
    $("#asid-midi").hide();
  }
}

function setButtonPause() {
  $(".button-ctrls").removeClass("button-idle button-selected").addClass("button-idle");
  $("#play-pause").addClass("button-selected");
  $("#play").hide();
  $("#pause").show();
}

/**
 * Show 'Play' and hide the 'Pause' button.
 */
function setButtonPlay() {
  $(".button-ctrls").removeClass("button-idle button-selected").addClass("button-idle");
  $("#play-pause").addClass("button-selected");
  $("#play").show();
  $("#pause").hide();
}

function detectBrowser() {
  if ((navigator.userAgent.indexOf("Opera") || navigator.userAgent.indexOf('OPR')) != -1) {
    // Do nothing
    needmidiaction = false;
  } else if (navigator.userAgent.indexOf("Edg") != -1) {
    // Do nothing
    needmidiaction = false;
  } else if (navigator.userAgent.indexOf("Chrome") != -1) {
    // Do nothing
    needmidiaction = false;
  } else if (navigator.userAgent.indexOf("Safari") != -1) {
    // Do nothing
    needmidiaction = false;
  } else if (navigator.userAgent.indexOf("Firefox") != -1) {
    needmidiaction = true;
  } else if ((navigator.userAgent.indexOf("MSIE") != -1) || (!!document.documentMode == true)) //IF IE > 10
  {
    needmidiaction = null;
    alert('Unsupported browser');
  } else {
    needmidiaction = null;
    alert('Unsupported browser');
  }
}
