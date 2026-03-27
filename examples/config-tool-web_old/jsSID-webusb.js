//jsSID by Hermit (Mihaly Horvath) : a javascript SID emulator and player for the Web Audio API
//(Year 2016) http://hermit.sidrip.com

//This version 0.9.1 modified .8 by JCH for DeepSID
// - Added multiplier for fast forward (VBI and CIA timing)
// - Return 16-bit CIA timer value
// - Read any SID register
// - Added a buffer callback
// - Voice mask for toggling voices ON or OFF (includes 2SID and 3SID)
// - Read SID chip address
// - Now handles suspend/resume

// Also modified by Thomas Jansson for DeepSID
// - Added ASID support, to playback on real SID hardware
// - Note that it needs a Web MIDI capable browser (such as Chrome)
// - Additionally it supports up to 3 SIDs and multispeed, as well as
//   SID+FM songs (uses OPL regs according to SFX Sound expander and FM-YAM)
// - To use multispeed or multiSID, a Turbo MIDI interface (like Elektron TM-1)
//   is needed to utilize the higher than normal MIDI-bandwidth.
//
// - Added playback support for SFX Sound Expander (YM3526/OPL) & FM-YAM (YM3812/OPL2) songs
// - These are regular SID files which also writes to the DF40/DF50 registers

// Modified by LouD to add WebUSB support and some code clean-up

/*
 * ASID buffers and structures
 */
ASID_NUM_REGS = 28;
ASID_BUFFER_SIZE = ASID_NUM_REGS + 12; // with added protocol overhead bytes
var asidRegisterBuffer = [new Uint8Array(ASID_NUM_REGS).fill(0), new Uint8Array(ASID_NUM_REGS).fill(0), new Uint8Array(ASID_NUM_REGS).fill(0), new Uint8Array(ASID_NUM_REGS).fill(0)];
var asidRegisterUpdated = [new Array(ASID_NUM_REGS).fill(true), new Array(ASID_NUM_REGS).fill(true), new Array(ASID_NUM_REGS).fill(true), new Array(ASID_NUM_REGS).fill(true)];
var asidOutBuffer = [new Uint8Array(ASID_BUFFER_SIZE), new Uint8Array(ASID_BUFFER_SIZE), new Uint8Array(ASID_BUFFER_SIZE), new Uint8Array(ASID_BUFFER_SIZE)];
var selectedMidiOutput = null;
var midiAccessObj = null;
const asidRegMap = [
  0x00, 0x01, 0x02, 0x03, 0x16, 0x04, 0x05,
  0x06, 0x07, 0x08, 0x09, 0x17, 0x0a, 0x0b,
  0x0c, 0x0d, 0x0e, 0x0f, 0x18, 0x10, 0x11,
  0x12, 0x13, 0x14, 0x15, 0x19, 0x1a, 0x1b
];

/*
 * ASID FM buffers and structures
 */
ASID_FM_NUM_REGS = 16; // FM addr/data pairs per message
ASID_FM_BUFFER_SIZE = ASID_FM_NUM_REGS * 2 + 3 + 1 + 5 + 1; // with added protocol overhead bytes
var asidFMregisterBuffer = new Uint8Array(ASID_FM_NUM_REGS * 2);
var asidFMoutBuffer = new Uint8Array(ASID_FM_BUFFER_SIZE).fill(0);
var asidFMcurrentAddress = 0x00;
var asidFMregBufferSize = 0;

/*
 * OPL FM structures
 */
var oplFMcurrentAddress = 0x00;
var isOplFMwritten = false;

/*
 * ASID & regular emulator common data
 */
var SID_address = [0xD400, 0, 0];

var Mute_SID = 0;

/*
 * Write one SID register to buffer
 */
function asidWriteReg(chip, sidRegister, data) {

  if (sidRegister > 0x18) return;
  if (Mute_SID == 1) {
    data = 0;
  }

  // Get the ASID transformed register
  var mappedAddr = asidRegMap[sidRegister];

  // If a write occurs to a waveform register, check if first block is already allocated
  if ((mappedAddr >= 0x16) && (mappedAddr <= 0x18) && asidRegisterUpdated[chip][mappedAddr]) {
    // ...and if so instead use the second block
    mappedAddr += 3;

    // If second block is also updated, move it to the first to make sure to always keep the last one
    if (asidRegisterUpdated[chip][mappedAddr])
      asidRegisterBuffer[chip][mappedAddr - 3] = asidRegisterBuffer[chip][mappedAddr];
  }

  // If we're trying to update a control register that is already mapped, flush it directly
  if (asidRegisterUpdated[chip][mappedAddr]) {
    if (mappedAddr >= 0x16) {
      asidSend(chip);
    }
  }

  // Store the data
  asidRegisterBuffer[chip][mappedAddr] = data;
  asidRegisterUpdated[chip][mappedAddr] = true;
}

/*
 * Write one FM register to buffer
 */
function asidFMwriteReg(isDataValue, data) {
  if (!isDataValue) {
    asidFMcurrentAddress = data;
  } else {
    asidFMregisterBuffer[asidFMregBufferSize++] = asidFMcurrentAddress;
    asidFMregisterBuffer[asidFMregBufferSize++] = data;
    if (asidFMregBufferSize == ASID_FM_NUM_REGS * 2) {
      asidFMSend();
    }
  }
}

/*
 * Write one FM register to buffer (OPL FM chip emulator)
 */
function oplFMwriteReg(isDataValue, data, opl_obj) {
  if (!isDataValue) {
    oplFMcurrentAddress = data;
  } else {
    if (opl_obj !== undefined) {
      opl_obj.write(oplFMcurrentAddress, data);
    }
    isOplFMwritten = true;
  }
}

/*
 * Build ASID FM structure if FM buffer updated, and if so send it
 */
function asidFMSend() {
  if (asidFMregBufferSize <= 0) {
    return;
  }

  // Sysex start data for an ASID FM message
  asidFMoutBuffer[0] = 0xf0;
  asidFMoutBuffer[1] = 0x2d;
  asidFMoutBuffer[2] = 0x60;
  asidFMoutBuffer[3] = asidFMregBufferSize / 2 - 1;

  let numMaskBytes = Math.floor((asidFMregBufferSize - 1) / 7) + 1;

  for (let i = 0; i < asidFMregBufferSize; i++) {
    // Create MSB for pairs
    asidFMoutBuffer[4 + Math.floor(i / 7)] |= asidFMregisterBuffer[i] & 0x80 ? (1 << (i % 7)) : 0;
    // Add the actual byte, without MSB
    asidFMoutBuffer[4 + numMaskBytes + i] = asidFMregisterBuffer[i] & 0x7f;
  }
  asidFMoutBuffer[4 + numMaskBytes + asidFMregBufferSize] = 0xf7;

  selectedMidiOutput.send(asidFMoutBuffer.slice(0, asidFMregBufferSize + 5 + numMaskBytes));
  asidFMregBufferSize = 0;
  asidFMoutBuffer.fill(0);

}

/*
 * Build ASID structure if buffer updated, and if so send it
 */
function asidSend(chip) {
  // Update needed?
  let update = false;
  for (let i = 0; i < asidRegisterUpdated[chip].length; i++) {
    if (asidRegisterUpdated[chip][i]) {
      update = true;
      break;
    }
  }
  if (!update) return;

  // Sysex start data for an ASID message. Extended to support
  // more than one chip
  asidOutBuffer[chip][0] = 0xf0;
  asidOutBuffer[chip][1] = 0x2d;
  asidOutBuffer[chip][2] = chip == 0 ? 0x4e : (0x4f + chip);
  let index = 3;

  // Setup mask bytes (one bit per register)
  for (let mask = 0; mask < 4; mask++) {
    let reg = 0x00;
    for (let regOffset = 0; regOffset < 7; regOffset++) {
      if (asidRegisterUpdated[chip][mask * 7 + regOffset]) {
        reg |= (1 << regOffset);
      }
    }
    asidOutBuffer[chip][index++] = reg;
  }

  // Setup the MSB bits, one per register (since MIDI only allows for 7-bit data bytes)
  for (let msb = 0; msb < 4; msb++) {
    let reg = 0x00;
    for (let regOffset = 0; regOffset < 7; regOffset++) {
      if (asidRegisterBuffer[chip][msb * 7 + regOffset] & 0x80) {
        reg |= (1 << regOffset);
      }
    }
    asidOutBuffer[chip][index++] = reg;
  }

  // Add data for all updated registers (the 7 LSB bits)
  for (let i = 0; i < ASID_NUM_REGS; i++) {
    if (asidRegisterUpdated[chip][i]) {
      asidOutBuffer[chip][index++] = asidRegisterBuffer[chip][i] & 0x7f;
    }
  }

  // Sysex end marker
  asidOutBuffer[chip][index++] = 0xf7;

  // Send to physical MIDI port. Only use configured SIDs
  if (SID_address[chip]) {
    selectedMidiOutput.send(asidOutBuffer[chip].slice(0, index));
  }

  // Prepare for next buffer
  asidRegisterUpdated[chip].fill(false);
}

function asidSendEnv() {
  let sidspeed = (filedata[18] << 24) | (filedata[18 + 1] << 16) | (filedata[18 + 2] << 8) | filedata[18 + 3];
  let curr_sidspeed = sidspeed & (1 << subtune);  // Not 0? CIA speed
  let custom_speed = (timermode[subtune] || memory[0xDC05]);
  let clock_type = SID.hermit.getclockspeed();  // 0 = 1MHz, 1 = PAL, 2 = NTSC, 3 = PAL&NTSC
  let cia_speed = SID.hermit.getcia();  // Must be non zero if curr_sidspeed == 1
  let isPAL = (clock_type == 2 ? false : true);


  // Physical out buffer, including protocol overhead
  var ASidOutBuffer = new Uint8Array(8);
  var index = 0;

  // No UI exist to request buffering or change speed multiplier
  var isBufferingRequested = false;
  var speedMultiplier = SIDamount + 1; // 1x

  const EMULATION_CYCLES_PER_SECOND_DFLT = 1000000;
  const EMULATION_CYCLES_PER_SECOND_PAL = 985248;
  const EMULATION_CYCLES_PER_SECOND_NTSC = 1022727
  const DFLT_FRAMEDELTA = 1000000*63*312/EMULATION_CYCLES_PER_SECOND_DFLT;
  const PAL_FRAMEDELTA = 1000000*63*312/EMULATION_CYCLES_PER_SECOND_PAL;
  const NTSC_FRAMEDELTA = 1000000*65*263/EMULATION_CYCLES_PER_SECOND_NTSC;
  const DELTA_SPEEDS = [DFLT_FRAMEDELTA, PAL_FRAMEDELTA, NTSC_FRAMEDELTA, PAL_FRAMEDELTA];

  // Time between two frames
  const frameDeltaUs = (custom_speed ? cia_speed : DELTA_SPEEDS[clock_type]);

  // Sysex start data for an ASID message
  ASidOutBuffer[index++] = 0xf0;
  ASidOutBuffer[index++] = 0x2d;
  ASidOutBuffer[index++] = 0x31; // SID environment

  // Payload

  /* data0: settings
    bit0    : 0 = PAL, 1 = NTSC
    bits4-1 : speed, 1x to 16x (value 0-15)
    bit5    : 1 = custom speed (only framedelta valid)
    bit6    : 1 = buffering requested by user
    */
  ASidOutBuffer[index++] =
    ((isBufferingRequested ? 1:0)   << 6) |
    ((custom_speed? 1:0)            << 5) |
    (((speedMultiplier - 1) & 0x0f) << 1) |
    ((isPAL? 0:1)                   << 0);

  /* data1: framedelta uS, total 7+7+2=16 bits, slowest time = 65535us = 15Hz
    bits6-0: framedelta uS (LSB)

      data2:
    bits6-0: framedelta uS

      data3:
    bits1-0: framedelta uS (MSB)
    bits6-2: 5 bits (reserved)
  */
  ASidOutBuffer[index++] = (frameDeltaUs >> 7*0) & 0x7f;
  ASidOutBuffer[index++] = (frameDeltaUs >> 7*1) & 0x7f;
  ASidOutBuffer[index++] = (frameDeltaUs >> 7*2) & 0x03;

  // Sysex end marker
  ASidOutBuffer[index++] = 0xf7;
  console.log(ASidOutBuffer);
  selectedMidiOutput.send(ASidOutBuffer.slice(0, 8));
}

function listInputsAndOutputs(midiAccess) {
  for (const entry of midiAccess.inputs) {
    const input = entry[1];
    console.log(
      `Input port [type:'${input.type}']` +
      ` id:'${input.id}'` +
      ` manufacturer:'${input.manufacturer}'` +
      ` name:'${input.name}'` +
      ` version:'${input.version}'`,
    );
  }
  for (const entry of midiAccess.outputs) {
    const output = entry[1];
    console.log(
      `Output port [type:'${output.type}'] id:'${output.id}' manufacturer:'${output.manufacturer}' name:'${output.name}' version:'${output.version}'`,
    );
  }
}
/*
 * MIDI available - setup output ports
 */
function onMIDISuccess(midiAccess) {
  midiAccessObj = midiAccess;
  const select = document.getElementById('asid-midi-outputs');
  const outputs = Array.from(midiAccessObj.outputs.values());

  // Remove any previous ports from dropdown
  while (select.options.length > 0) {
    select.remove(0);
  }

  // Populate dropdown with available MIDI ports
  outputs.forEach((output, index) => {
    const option = document.createElement('option');
    option.text = output.name;
    option.value = index;
    select.appendChild(option);
  });

  // Set a valid initial selected port
  if (outputs.length) {
    selectedMidiOutput = outputs[select.value];
  }
  else {
    alert("No MIDI devices found.");
  }
}

/*
 * MIDI not available
 */
function onMIDIFailure() {
  alert("Browser supports MIDI, but could not access your devices.");
}

/* Sysex */

const SYSEX_BUFFER_SIZE = 8; /* 0xF0, 0x19, 0, 0, 0 ,0, 0, 0xF7 */
var sysexOutBuffer = new Uint8Array(SYSEX_BUFFER_SIZE);

function sysexCommand(command, a = 0, b = 0, c = 0, d = 0) {
  sysexOutBuffer[0] = 0xF0;
  sysexOutBuffer[1] = 0x50;    /* The eighties baby! */
  sysexOutBuffer[2] = command; /* config command */
  sysexOutBuffer[3] = a;       /* struct setting e.g. socketOne, clock_rate or additional command */
  sysexOutBuffer[4] = b;       /* setting entry e.g. dualsid */
  sysexOutBuffer[5] = c;       /* new value */
  sysexOutBuffer[6] = d;       /* reserved */
  sysexOutBuffer[7] = 0xF7;
  console.log(sysexOutBuffer);
  selectedMidiOutput.send(sysexOutBuffer.slice(0, SYSEX_BUFFER_SIZE));
}

/* WebUSB */

let port,
    savedport;
let webusbconnected = false,
    webusbplaying = false
let configavailable = false;
var webusb = {};
var version_regex = "";
var usbsid = {};
const webusb_min_version = "0.2.3";

/* (function () {
  document.addEventListener('DOMContentLoaded', event => {

  })
}); */

(function () {

  document.addEventListener('DOMContentLoaded', event => {

    savedport = localStorage.getItem("webusbport");
    let statusDisplay = document.querySelector('#status-text');

    webusb.getPorts = function () {
      return navigator.usb.getDevices().then(devices => {
        return devices.map(device => new webusb.Port(device));
      });
    };

    webusb.requestPort = function () {
      const filters = [
        { 'vendorId': 0xcafe, 'productId': 0x4011 },  // USBSID-Pico
      ];
      return navigator.usb.requestDevice({ 'filters': filters }).then(
        device => new webusb.Port(device)
      );
    }

    webusb.Port = function (device) {
      this.device_ = device;
      this.interfaceNumber = 0;
      this.endpointIn = 0;
      this.endpointOut = 0;
      this.name = webusb.name = device.productName;
    };

    webusb.readsingle = 0;
    webusb.datareadresult = null;
    webusb.datainsize = 64;

    webusb.Port.prototype.connect = function () {
      let readLoop = async () => {
        if (webusbconnected) { // ISSUE: NEED TO TEST IF THIS INTERFERES WITH AUTO CONFIG LOAD
          await this.device_.transferIn(this.endpointIn, webusb.datainsize).then(result => {
            this.onReceive(result.data);
            // console.log(result.data);
            readLoop();  /* Recursion */
          }, error => {
            if (webusbconnected) {
              this.onReceiveError(error);
            }
          });
        } else {
          console.log("Not connected to WebUSB");
        };
      };

      return this.device_.open()
        .then(() => {
          if (this.device_.configuration === null) {
            return this.device_.selectConfiguration(1);
          }
        })
        .then(() => {
          var interfaces = this.device_.configuration.interfaces;
          interfaces.forEach(element => {
            element.alternates.forEach(elementalt => {
              if (elementalt.interfaceClass == 0xFF) {
                this.interfaceNumber = element.interfaceNumber;
                elementalt.endpoints.forEach(elementendpoint => {
                  if (elementendpoint.direction == "out") {
                    this.endpointOut = elementendpoint.endpointNumber;
                  }
                  if (elementendpoint.direction == "in") {
                    this.endpointIn = elementendpoint.endpointNumber;
                  }
                })
              }
            })
          })
        })
        .then(() => this.device_.claimInterface(this.interfaceNumber))
        .then(() => this.device_.selectAlternateInterface(this.interfaceNumber, 0))
        .then(() => this.device_.controlTransferOut({
          'requestType': 'class',
          'recipient': 'interface',
          'request': 0x22,
          'value': 0x01,
          'index': this.interfaceNumber
        }))
        .then(() => {
          statusDisplay.textContent = '';
          usbsid.productName = this.device_.productName;
          usbsid.manufacturerName = this.device_.manufacturerName;
          usbsid.serialNumber = this.device_.serialNumber;
          usbsid.fullversion = usbsid.manufacturerName.match(/v[0-9\.]+-[A-Z0-9\.]+/g)[0];
          usbsid.version = usbsid.fullversion.match(/\d+\.\d+\.\d+/g)[0];
          if (usbsid.version < webusb_min_version) {
            var message = `Warning, your firmware version ${usbsid.version} is lower then ${webusb_min_version}, playing results may vary! Please update your firmware!`;
            console.log(message);
            alert(message);
          } else {
            console.log(`Device firmware version ${usbsid.version} verified against minimal play version ${webusb_min_version}`);
          }
          webusbconnected = true;
          mute_SIDS();
          clear_BUS();
          unmute_SIDS();
          readLoop();
        });
    };

    webusb.Port.prototype.disconnect = function () {
      return this.device_.controlTransferOut({
        'requestType': 'class',
        'recipient': 'interface',
        'request': 0x22,
        'value': 0x00,
        'index': this.interfaceNumber
      }).then(() => {
        this.device_.reset();
      }).then(() => this.device_.close());
    };

    webusb.Port.prototype.send = function (data) {
      return this.device_.transferOut(this.endpointOut, data);
    };

    webusb.writeReg = function (array) {
      if (Mute_SID == 1) {
        array = 0;
      }
      port.send(
        new Uint8Array(array)
      );
      fillsidmemory(array);
      // if (array[1] >= 0x0 && array[1] < 0x20) {
      //   document.querySelector('#SID0col'+array[1]).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
      //   // document.querySelector('#SID0col'+array[1]).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(memory[0xD400 + array[1]]);
      // } else if (array[1] >= 0x20 && array[1] < 0x40) {
      //   document.querySelector('#SID1col'+(array[1] - 0x20)).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
      // } if (array[1] >= 0x40 && array[1] < 0x60) {
      //   document.querySelector('#SID2col'+(array[1] - 0x40)).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
      // } if (array[1] >= 0x60 && array[1] < 0x80) {
      //   document.querySelector('#SID3col'+(array[1] - 0x60)).textContent = '$' + dec2Hex(array[1]) + ':' + dec2Hex(array[2]);
      // }
    }

    webusb.readReg = function (array) {
      port.send(
        new Uint8Array(array)
      );
    }
  });

})();

async function mute_SIDS() {
  webusb.writeReg([((3 << 6) | 12), 0, 0]);
}

async function unmute_SIDS() {
  webusb.writeReg([((3 << 6) | 13), 0, 0]);
}

async function clear_BUS() {
  webusb.writeReg([((3 << 6) | 17), 0, 0]);
}

async function reset_SIDS() {
  webusb.writeReg([((3 << 6) | 14), 0, 0]);
}

async function set_ClockSpeed(clk_speed) {
  webusb.writeReg([((3 << 6) | 18), 0x50, clk_speed, 0, 0, 0]);
}

async function get_NoSIDs() {
  webusb.datainsize = 1;
  readnosids = true;
  await webusb.writeReg([((3 << 6) | 18), 0x39, 0, 0, 0, 0]);
}

async function get_FmOplSIDno() {
  webusb.datainsize = 1;
  readfmoplsid = true;
  await webusb.writeReg([((3 << 6) | 18), 0x3A, 0, 0, 0, 0]);
}

async function readVersion() {
  // appender.empty();
  readver = true;
  await webusb.readReg([((3 << 6) | 18), 0x80, 0, 0, 0, 0]);
}

async function readPCBVersion() {
  // appender.empty();
  readpcbver = true;
  await webusb.readReg([((3 << 6) | 18), 0x81, 0, 0, 0, 0]);
}

async function readConfig() {
  appender.empty();
  readcfg = true;
  await webusb.readReg([((3 << 6) | 18), 0x30, 0, 0, 0, 0]);
}

(function () {
  'use strict';

  document.addEventListener('DOMContentLoaded', event => {

    let connectButton = document.querySelector("#device-connect");
    let statusDisplay = document.querySelector('#status-text');
    let configButton = document.querySelector("#toggle-config");

    let cfgreads = 0;

    webusb.connect = function () {
      port.connect().then(() => {
        connectButton.textContent = 'Disconnect';
        statusDisplay.textContent = 'from USB device';
        configButton.classList.remove("d-none");

        if (checkPCBVersion(webusb.name)) {
          $('#webusb-audio-buttons').removeClass('d-none');
        } else {
          $('#webusb-audio-buttons').addClass('d-none');
        }

        if(version == null || version === 'undefined')
          setTimeout(function() { readVersion(); }, 100);
        if(pcbversion == null || pcbversion === 'undefined')
          setTimeout(function() { readPCBVersion(); }, 100);

        port.onReceive = data => {
          if (readcfg) {
            if (data.byteLength != 0) {
              appender.append(data.buffer);
              cfgreads++;
              if (appender.length() >= 64) {  /* BUG: TinyUSB is refusing to send more then 2x 64KB packets */
                presentConfig();
              }
            } else if (cfgreads > 2) {
              presentConfig(false);
              console.log("Received empty config buffer: " + data.byteLength);
            }
          } else if (readver) {
            if (data.byteLength != 0) {
              version = new Uint8Array(data.buffer);
              if (version[0] == 0x80) {
                presentVersion(version);
              }
            } else {
              presentVersion(false);
              console.log("Received empty version buffer: " + data.byteLength);
            }
          } else if (readpcbver) {
            if (data.byteLength != 0) {
              version = new Uint8Array(data.buffer);
              if (version[0] == 0x81) {
                let pcbversionLength = version[1];
                let versionText = version.slice(2, (2 + pcbversionLength));
                pcbversion = textDecoder.decode(versionText);
                readpcbver = false;
              }
            } else {
              console.log("Received empty PCB version buffer: " + data.byteLength);
            }
          } else if (readfmoplsid) {
            webusb.datareadresult = new Uint8Array(data.buffer);
            readfmoplsid = false;
            webusb.datainsize = 64;
            usbsid.fmoplsid = webusb.datareadresult[0];
          } else if (readnosids) {
            webusb.datareadresult = new Uint8Array(data.buffer);
            readnosids = false;
            webusb.datainsize = 64;
            usbsid.nosids = webusb.datareadresult[0];
          } else {
            webusb.datareadresult = new Uint8Array(data.buffer);
            webusb.datainsize = 64;
          }
        };
        port.onReceiveError = error => {
          statusDisplay.textContent = error;
          console.error(error);
        };
      }, error => {
        statusDisplay.textContent = error;
      }).then(_ => {

        get_NoSIDs();
        if (usbsid.nosids === undefined) {
          get_NoSIDs();
        }

        get_FmOplSIDno();
        if (usbsid.fmoplsid === undefined) {
          get_FmOplSIDno();
        }

        /* Empty buffer */
        port.send(new Uint8Array([0]));

        // port.device_.transferOut(port.endpointOut, new Uint8Array([0]));
        // port.device_.transferIn(port.endpointIn, new Uint8Array([0]));
      // }).then(_ => {
      //   if (enableConfigThings()) {
      //     setTimeout(readConfig, 200);
      //     setTimeout(readVersion, 400);
      //   };
      });
    }

    webusb.connectNow = function () {
      if (!webusbplaying) {
        if (port) {
          if (webusbconnected) {
            webusbconnected = false;
            port.disconnect();
            connectButton.textContent = 'Connect';
            statusDisplay.textContent = '';
            disableConfigThings();
            port = null;
          } else {
            statusDisplay.textContent = 'Not connected';
          }
        } else {
          webusb.getPorts().then(ports => {
            webusb.requestPort().then(selectedPort => {
              port = selectedPort;
              if (savedport != port) {
                localStorage.setItem("webusbport", port);
                savedport = localStorage.getItem("webusbport");
              }
              webusb.connect(port);
            }).catch(error => {
              statusDisplay.textContent = error;
            });
          })
        }
      } else {
        statusDisplay.textContent = 'Press stop playing first';
      }
    }

    /* Autoconnect sometimes fails on load: */
    /* SecurityError: Failed to execute 'claimInterface' on 'USBDevice': The requested interface implements a protected class. */
    webusb.autoConnect = function () {
      webusb.getPorts().then(ports => {
        if (ports.length === 0) {
          statusDisplay.textContent = 'No previous device to connect to';
        } else {
          statusDisplay.textContent = 'Connecting...';
          port = ports[0];
          webusb.connect();
        }
      });
    };

    connectButton.addEventListener('click', function () {
      webusb.connectNow();
    });

    // versionButton.addEventListener('click', function () {
    //   if (connectButton.textContent == 'Disconnect') {
    //     if (!webusbplaying) {
    //       readVersion();
    //     } else {
    //       statusDisplay.textContent = 'Press stop playing first';
    //     }
    //   };
    // })

    // configButton.addEventListener('click', function () {
    //   if (connectButton.textContent == 'Disconnect') {
    //     if (!webusbplaying) {
    //       setTimeout(readConfig, 100);
    //       setTimeout(readVersion, 300)
    //     } else {
    //       statusDisplay.textContent = 'Press stop playing first';
    //     }
    //   };
    // })

  });

})();

function playSID(sidurl, subtune) { //convenience function to create default-named jsSID object and play in one call, easily includable as inline JS function call in HTML
  if (typeof SIDplayer === 'undefined') SIDplayer = new jsSID(16384, 0.0005); //create the object if doesn't exist yet
  SIDplayer.loadstart(sidurl, subtune);
}
var jsSID_audioCtx, jsSID_scriptNode, clk_ratio, samplerate = 0, frame_sampleperiod, timermode, filedata, subtune;
var asid_enabled, webusb_enabled, bufferl;
var voiceMask, volume, maxsid, SIDamount;
function jsSID(bufferlen, background_noise, asid_enable = false, webusb_enable = false) {
  bufferl = bufferlen;
  this.author = 'Hermit'; this.sourcecode = 'http://hermit.uw.hu'; this.version = '0.9.1.7'; this.year = '2019';

  /* var */ maxsid = 0;

  // Initialize WebUSB
  if (webusb_enable) {
    bufferlen = 256;
    if (savedport != null) {
      webusb.autoConnect();
    } else {
      alert("Autoconnect not actived yet, please connect to a WebUSB device first.");
    }
    maxsid = 4;
  }
  /* var */ webusb_enabled = webusb_enable;

  // Initialize Web MIDI
  if (asid_enable) {
    bufferlen = 256;
    if (navigator.requestMIDIAccess) {
      navigator.requestMIDIAccess({ sysex: true })
        .then(onMIDISuccess, onMIDIFailure);
    } else {
      alert("Your browser does not support MIDI.");
    }
    maxsid = 4;
  }
  /* var */ asid_enabled = asid_enable;

  /* BUG: Somewhere there is still audio being played over the regular PC audio out speakers, this creates a very annoying clicking sound */
  // Create Web Audio context and scriptNode at jsSID object initialization (at the moment only mono output)
  // For ASID, we set a samplerate 200 times bigger than the buffer size - to be able to get a steady 200Hz clock (4x at 50). No audio is output so actual rate doesn't matter
  if (typeof AudioContext !== 'undefined') {
    // var jsSID_audioCtx = new AudioContext({});
    /* var */ jsSID_audioCtx = new AudioContext((asid_enabled || webusb_enabled)
      ? { sampleRate: bufferlen * 200 }
      : {  });
  } else {
    // var jsSID_audioCtx = new webkitAudioContext({});
    /* var */ jsSID_audioCtx = new webkitAudioContext((asid_enabled || webusb_enabled)
      ? { sampleRate: bufferlen * 200 }
      : { });
  }
  /* var */ samplerate = jsSID_audioCtx.sampleRate;
  if (typeof jsSID_audioCtx.createJavaScriptNode === 'function') {
    /* var */ jsSID_scriptNode = jsSID_audioCtx.createJavaScriptNode(bufferlen, 0, 1);
  } else {
    /* var */ jsSID_scriptNode = jsSID_audioCtx.createScriptProcessor(bufferlen, 0, 1);
  }

  jsSID_scriptNode.onaudioprocess = function (e) { //scriptNode will be replaced by AudioWorker in new browsers sooner or later
    var outBuffer = e.outputBuffer; var outData = outBuffer.getChannelData(0);
    for (var sample = 0; sample < outBuffer.length; sample++) { outData[sample] = play(); }
    if (typeof buffercallback !== "undefined") buffercallback(); // Added by JCH
  }

  this.opl = null;
  if (!webusb_enabled) {
    OPL.create(jsSID_audioCtx.sampleRate).then(opl1 => opl = opl1)
  }

  //user functions callable from outside
  this.loadstart = function (sidurl, subt) { this.loadinit(sidurl, subt); if (startcallback !== null) startcallback(); this.playcont();}

  this.loadinit = function (sidurl, subt) {
    loaded = 0; this.pause(); initSID(); subtune = subt; //stop playback before loading new tune
    var request = new XMLHttpRequest(); request.open('GET', sidurl, true); request.responseType = 'arraybuffer';

    request.onload = function () {  //request.onreadystatechange=function(){ if (this.readyState!==4) return; ... could be used too
      /* var */ filedata = new Uint8Array(request.response); //SID-file format information can be found at HVSC
      var i, strend, offs = filedata[7];
      loadaddr = filedata[8] + filedata[9] ? filedata[8] * 256 + filedata[9] : filedata[offs] + filedata[offs + 1] * 256;
      for (i = 0; i < 32; i++) timermode[31 - i] = filedata[0x12 + (i >> 3)] & Math.pow(2, 7 - i % 8);
      for (i = 0; i < memory.length; i++) memory[i] = 0;
      for (i = offs + 2; i < filedata.byteLength; i++) {
        if (loadaddr + i - (offs + 2) < memory.length) memory[loadaddr + i - (offs + 2)] = filedata[i];
      }
      strend = 1;
      for (i = 0; i < 32; i++) {
        if (strend != 0) strend = SIDtitle[i] = filedata[0x16 + i];
        else strend = SIDtitle[i] = 0;
      }
      strend = 1;
      for (i = 0; i < 32; i++) {
        if (strend != 0) strend = SIDauthor[i] = filedata[0x36 + i];
        else strend = SIDauthor[i] = 0;
      }
      strend = 1;
      for (i = 0; i < 32; i++) {
        if (strend != 0) strend = SIDinfo[i] = filedata[0x56 + i];
        else strend = SIDinfo[i] = 0;
      }
      initaddr = filedata[0xA] + filedata[0xB] ? filedata[0xA] * 256 + filedata[0xB] : loadaddr;
      playaddr = playaddf = filedata[0xC] * 256 + filedata[0xD];
      subtune_amount = filedata[0xF]; preferred_SID_model[0] = (filedata[0x77] & 0x30) >= 0x20 ? 8580 : 6581;
      preferred_SID_model[1] = (filedata[0x77] & 0xC0) >= 0x80 ? 8580 : 6581;
      preferred_SID_model[2] = (filedata[0x76] & 3) >= 3 ? 8580 : 6581;
      clockSpeed = ((((filedata[0x76]<<8|filedata[0x77]) & 0xC) >> 2) & 3);
      if (webusb_enabled) {
        set_ClockSpeed(clockSpeed);
      }
      // SID_address[1] = filedata[0x7A] >= 0x42 && (filedata[0x7A] < 0x80 || filedata[0x7A] >= 0xE0) ? 0xD000 + filedata[0x7A] * 16 : 0;
      // SID_address[2] = filedata[0x7B] >= 0x42 && (filedata[0x7B] < 0x80 || filedata[0x7B] >= 0xE0) ? 0xD000 + filedata[0x7B] * 16 : 0;
      SID_address[1] = filedata[0x7A] >= 0x42 && filedata[0x7A] < 0x80 ? (0xD << 12) | (filedata[0x7A] << 4) : 0;
      SID_address[2] = filedata[0x7C] >= 0x40 && filedata[0x7C] < 0x80 ? (0xD << 12) | (filedata[0x7C] << 4) : 0;
      SID_address[3] = filedata[0x7E] >= 0x42 && filedata[0x7E] < 0x80 ? (0xD << 12) | (filedata[0x7E] << 4) : 0;
      /* SID_address[1] = filedata[0x7A] >= 0x42 && filedata[0x7A] < 0x80 ? (0xD << 12) | (filedata[0x7A] << 4) : 0;
      SID_address[2] = filedata[0x7B] >= 0x40 && filedata[0x7B] < 0x80 ? (0xD << 12) | (filedata[0x7B] << 4) : 0;
      SID_address[3] = filedata[0x7C] >= 0x42 && filedata[0x7C] < 0x80 ? (0xD << 12) | (filedata[0x7C] << 4) : 0;
      SID_address[2] = (SID_address[2] == SID_address[0]) ? 0xD460 : SID_address[2]; */
      // 64 D400 54272
      // 66 D420 53314
      // 68 D440 53328
      // SID_address[3] = filedata[0x7C] >= 0x42 && (filedata[0x7C] <= 0xFE) ? 0xD000 + filedata[0x7C] * 16 : 0;
      // if (webusb_enabled) {  /* BUG: 4SID tunes workaround cause some sids to appear to have 4 sid addresses */
        // if (SID_address[1] > 0 && SID_address[2] > 0) {
          // SID_address[3] =
            // filedata[0x7C] >= 0x42 && (filedata[0x7C] < 0x80
            //   || filedata[0x7C] >= 0xE0)
            // filedata[0x7C] >= 0x42 && (filedata[0x7C] <= 0xFE)
              // ? 0xD000 + filedata[0x7C] * 16
              // : 0;  // Added for WebUSB with USBSID
        // } else if ((SID_address[1] > 0)
        //             && (SID_address[2] == 0)
        //             && (filedata[0x7C] >= 0x42
        //             && (filedata[0x7C] < 0x80
        //             || filedata[0x7C] >= 0xE0))) {
        // if ((SID_address[1] > 0) && (SID_address[3] > 0) && (SID_address[2] == 0)) {
          // SID_address[3] = filedata[0x7C] >= 0x42 && (filedata[0x7C] < 0x80 || filedata[0x7C] >= 0xE0) ? 0xD000 + filedata[0x7C] * 16 : 0;  // Added for WebUSB with USBSID
          // SID_address[2] = 0xD460;  /* WebUSB fix for Quad Core & Teen Spirit */
        // }
        SIDamount = 1 + (SID_address[1] > 0) + (SID_address[2] > 0) + (SID_address[3] > 0);
        // switch (SIDamount) {
        //   case 1:
        //     SID_address[0] = 0xD400;
        //     break
        //   case 2:
        //     SID_address[0] = 0xD400;
        //     SID_address[1] = 0xD420;
        //     break
        //   case 3:
        //     SID_address[0] = 0xD400;
        //     SID_address[1] = 0xD420;
        //     SID_address[2] = 0xD440;
        //     break
        //   case 4:
        //     SID_address[0] = 0xD400;
        //     SID_address[1] = 0xD420;
        //     SID_address[2] = 0xD440;
        //     SID_address[3] = 0xD460;
        //     break
        // }
        webusbplaying = true;
      // } else {
        // SIDamount = 1 + (SID_address[1] > 0) + (SID_address[2] > 0);
      // }
      loaded = 1; if (loadcallback !== null) loadcallback(); init(subtune);
      multi = 1; // Added by JCH
      if(webusb_enabled) {
        // nosidsintune = 1 + (SID_address[1] > 0 ? 1 : 0) + (SID_address[2] > 0 ? 1 : 0) + (SID_address[3] > 0 ? 1 : 0);
        for (var s = 0; s < 4; s++) {
          enablesidmemory(s, SIDamount);
        }
      }
    };   // ';' is needed here (and similar places) so that minimized/compacted jsSID.js generated by Makefile will work in the browser

    request.send(null);
  }

  this.start = function (subt) {
    if (webusb_enabled) {
      webusbplaying = true;
    }
    init(subt);
    if (startcallback !== null) startcallback();
    this.playcont();
  }
  this.playcont = function () {

    if (webusb_enabled) {
      webusbplaying = true;
      this.setvolume(0);
      // reset_SIDS();
      // Turn on volume (will later be sent upon first packet created)
      // for (var chip = 0; chip < maxsid; chip++) {
      //   webusb.writeReg([0x0, ((chip * 0x20) | 0x18), 0xf]);
      // }
    }

    if (asid_enabled) {
      // Turn on volume (will later be sent upon first packet created)
      for (var chip = 0; chip < maxsid; chip++) {
        asidRegisterBuffer[chip][21] |= 0x0f;
        asidRegisterUpdated[chip][21] = true;
      }
    }

    jsSID_scriptNode.connect(jsSID_audioCtx.destination); if (jsSID_audioCtx.state == "suspended") jsSID_audioCtx.resume(); /* Added by JCH */
  }
  this.pause = function () {

    if (webusb_enabled) {
      webusbplaying = false;
      this.setvolume(1);
      // Turn off volume and make sure it gets sent
      for (var chip = 0; chip < maxsid; chip++) {
        webusb.writeReg([0x0, ((chip * 0x20) | 0x18), 0x0]);
      }
    }

    if (asid_enabled) {
      // Turn off volume and make sure it gets sent
      for (var chip = 0; chip < maxsid; chip++) {
        asidRegisterBuffer[chip][21] &= 0xf0;
        asidRegisterUpdated[chip][21] = true;
        asidSend(chip);
      }
    }
    if (loaded && initialized) jsSID_scriptNode.disconnect(jsSID_audioCtx.destination);
  }
  //(Checking state before disconnecting is a workaround for Opera: gave error when code tried disconnecting what is not connected.
  //Checking inner state variables here, but maybe audioContext status info could be more reliable. I just didn't want to rely too many Audio API function.)
  this.stop = function () {
    if (webusb_enabled) webusbplaying = false;
    this.pause();
    init(subtune);
  }
  //using functions to get states instead of variables. this enables value conversions and gives easier/explicite scoping
  this.gettitle = function () { return String.fromCharCode.apply(null, SIDtitle); }
  this.getauthor = function () { return String.fromCharCode.apply(null, SIDauthor); }
  this.getinfo = function () { return String.fromCharCode.apply(null, SIDinfo); }
  this.getsubtunes = function () { return subtune_amount; }
  this.getprefmodel = function () { return preferred_SID_model[0]; }
  this.getclockspeed = function () { return SID_Clock_Speeds[clockSpeed]; }
  this.getmodel = function () { return SID_model; }
  this.getoutput = function () { return (output / OUTPUT_SCALEDOWN) * (memory[0xD418] & 0xF); }
  this.getplaytime = function () { return parseInt(playtime); }
  this.getcia = function () { return memory[0xDC04] + memory[0xDC05] * 256; } // Added by JCH
  this.getSIDAddress = function (chip) { return SID_address[chip]; } // Added by JCH
  this.readregister = function (register) { return memory[register]; } // Added by JCH
  this.enableVoices = function (mask) { voiceMask = mask; } // Added by JCH
  this.setmodel = function (model) { SID_model = model; }
  this.setvolume = function (vol) { volume = vol; }
  this.setloadcallback = function (fname) { loadcallback = fname; }
  this.setstartcallback = function (fname) { startcallback = fname; }
  this.setendcallback = function (fname, seconds) { endcallback = fname; playlength = seconds; }
  this.setbuffercallback = function (fname) { buffercallback = fname; } // Added by JCH
  this.setSpeedMultiplier = function (multiplier) { speed(multiplier); } // Added by JCH
  this.issuspended = function () { return jsSID_audioCtx.state == "suspended"; } // Added by JCH

  var //emulated machine constants
    C64_PAL_CPUCLK = 985248, //Hz
    PAL_FRAMERATE = 50, //NTSC_FRAMERATE = 60;
    SID_CHANNEL_AMOUNT = 3,
    OUTPUT_SCALEDOWN = 0x10000 * SID_CHANNEL_AMOUNT * 16;
  var SIDamount_vol = [0, 1, 0.6, 0.4]; //how much to attenuate with more 2SID/3SID to avoid master-output overflows

  //SID playback related arrays/variables - avoiding internal/automatic variables to retain speed
  var SIDtitle = new Uint8Array(0x20);
  var SIDauthor = new Uint8Array(0x20);
  var SIDinfo = new Uint8Array(0x20);
  /* var */ timermode = new Uint8Array(0x20);
  var loadaddr = 0x1000,
      initaddr = 0x1000,
      playaddf = 0x1003,
      playaddr = 0x1003,
      subtune_amount = 1,
      playlength = 0; //framespeed = 1;
  /* var */ subtune = 0;
  var SID_Clock_Speeds = [0, 1, 2, 3]; // 1MHz, PAL, NTSC, PAL&NTSC
  var clockSpeed;
  var preferred_SID_model = [8580.0, 8580.0, 8580.0];
  var SID_model = 8580.0;
  /* var */ memory = new Uint8Array(65536); //for(var i=0;i<memory.length;i++) memory[i]=0;
  var loaded = 0,
      initialized = 0,
      finished = 0,
      loadcallback = null,
      startcallback = null;
      endcallback = null,
      playtime = 0,
      ended = 0;
  /* var */ clk_ratio = C64_PAL_CPUCLK / samplerate;
  /* var */ frame_sampleperiod = samplerate / PAL_FRAMERATE; //samplerate/(PAL_FRAMERATE*framespeed);
  var framecnt = 1, CPUtime = 0, pPC;
  /* var */ volume = 1.0;
  /* var */ SIDamount = 1, mix = 0;
  /* var */ voiceMask = 0xFFF; // Added by JCH
  var oplFMbuf = new Int16Array(2).fill(0);
  var oplFMrefresh = true;

  function init(subt) {
    isOplFMwritten = false;

    if (webusb_enabled) {
      if (!webusbconnected) {
        alert("WebUSB not connected!");
      }
      // for (var chip = 0; chip < maxsid; chip++) {
      //   for (var addr = 0; addr <= 0x18; addr++) {
      //     webusb.writeReg([0x0, ((chip * 0x20) | addr), 0]);
      //   }
      // }
    }

    if (asid_enabled) {
      const select = document.getElementById('asid-midi-outputs');
      const outputs = Array.from(midiAccessObj.outputs.values());
      if (typeof outputs !== 'undefined' && outputs.length) {
        selectedMidiOutput = outputs[select.value];
      }
      else {
        alert("No MIDI devices found.");
        asid_enabled = false;
        return;
      }
    }

    if (loaded) {
      initialized = 0;
      subtune = subt;
      voiceMask = 0xFFF;
      /* Added by JCH*/
      initCPU(initaddr);
      initSID();
      A = subtune;
      memory[1] = 0x37;
      memory[0xDC05] = 0;
      for (var timeout = 100000; timeout >= 0; timeout--) {
        if (CPU()) {
          break;
        } else {
          // OPL FM-support
          if ((storadd == 0xDF40) || (storadd == 0xDF50)) {
            if (asid_enabled) {
              asidFMwriteReg(storadd == 0xDF50, memory[storadd]);
            } else if (webusb_enabled && usbsid.fmoplsid != 0) {
              let us_fmaddr = ((usbsid.fmoplsid - 1) << 5);
              webusb.writeReg([0x0, ((storadd - 0xDF40) | us_fmaddr), memory[storadd]]);
            } else {
              oplFMwriteReg(storadd == 0xDF50, memory[storadd], opl);
            }
          }

        }
      }
      if (timermode[subtune] || memory[0xDC05]) { //&& playaddf {   //CIA timing
        if (!memory[0xDC05]) { memory[0xDC04] = 0x24; memory[0xDC05] = 0x40; } frame_sampleperiod = (memory[0xDC04] + memory[0xDC05] * 256) / clk_ratio;
      }
      else frame_sampleperiod = samplerate / PAL_FRAMERATE; //Vsync timing
      //frame_sampleperiod = (memory[0xDC05]!=0 || (!timermode[subtune] && playaddf))? samplerate/PAL_FRAMERATE : (memory[0xDC04] + memory[0xDC05]*256) / clk_ratio;
      if (playaddf == 0) playaddr = ((memory[1] & 3) < 2) ? memory[0xFFFE] + memory[0xFFFF] * 256 : memory[0x314] + memory[0x315] * 256;
      else { playaddr = playaddf; if (playaddr >= 0xE000 && memory[1] == 0x37) memory[1] = 0x35; } //player under KERNAL (Crystal Kingdom Dizzy)
      initCPU(playaddr); framecnt = 1; finished = 0; CPUtime = 0; playtime = 0; ended = 0; initialized = 1;

      if (webusb_enabled) {
        // Setup all registers after Init
        for (var chip = 0; chip < maxsid; chip++) {
          for (var addr = 0; addr <= 0x18; addr++) {
            webusb.writeReg([0x0, ((chip * 0x20) | addr), memory[SID_address[chip] + addr]]);
          }
        }
      }
      if (asid_enabled) {
        asidSendEnv();
        // Setup all registers after Init
        for (var chip = 0; chip < maxsid; chip++) {
          for (var addr = 0; addr <= 0x18; addr++) {
            asidWriteReg(chip, addr, memory[SID_address[chip] + addr]);
          }
          asidRegisterUpdated[chip].fill(true);
          asidSend(chip);
        }
      }

    }
  }

  function play() { //called internally by the Web Audio API scriptNode callback; handles SID-register reading/processing and SID emulation
    if (loaded && initialized) {
      framecnt--; playtime += 1 / (samplerate / multi); // Added by JCH
      if (framecnt <= 0) { framecnt = frame_sampleperiod; finished = 0; PC = playaddr; SP = 0xFF; }
      if (finished == 0) {
        while (CPUtime <= clk_ratio) {
          pPC = PC;
          if (CPU() >= 0xFE) {
            finished = 1;
            if (asid_enabled) {
              asidSend(0); asidSend(1); asidSend(2); asidSend(3); asidFMSend()
            } else if (webusb_enabled) {
              // No need to do anything here for webusb, the line below cut off all sound
              /* webusb.writeReg([0x0, ((0x20 * chip) | addr), 0x0]); */
            };
            break;
          } else CPUtime += cycles;
          if ((memory[1] & 3) > 1 && pPC < 0xE000 && (PC == 0xEA31 || PC == 0xEA81)) { finished = 1; break; } //IRQ player ROM return handling
          if ((addr == 0xDC05 || addr == 0xDC04) && (memory[1] & 3) && timermode[subtune]) frame_sampleperiod = (memory[0xDC04] + memory[0xDC05] * 256) / clk_ratio; //Galway/Rubicon workaround
          if (storadd >= 0xD420 && storadd < 0xD800 && (memory[1] & 3)) {  //CJ in the USA workaround (writing above $d420, except SID2/SID3)
            if (!(SID_address[1] <= storadd && storadd < SID_address[1] + 0x1F)
              && !(SID_address[2] <= storadd && storadd < SID_address[2] + 0x1F)
              && (webusb_enabled ? !(SID_address[3] <= storadd && storadd < SID_address[3] + 0x1F) : true))  // NOTICE: THIS IS A TEST, MIGHT BE INCORRECTLY SUPPLIED
              memory[storadd & 0xD41F] = memory[storadd];
          }
          if (addr == 0xD404 && !(memory[0xD404] & 1)) ADSRstate[0] &= 0x3E; if (addr == 0xD40B && !(memory[0xD40B] & 1)) ADSRstate[1] &= 0x3E; if (addr == 0xD412 && !(memory[0xD412] & 1)) ADSRstate[2] &= 0x3E; //Whittaker player workaround
          if (asid_enabled) {
            // If a SID register on a chip was updated - add to ASID buffer
            for (var chip = 0; chip < maxsid; chip++) {
              if (SID_address[chip] && storadd >= SID_address[chip] && storadd <= (SID_address[chip] + 0x18)) {
                asidWriteReg(chip, storadd - SID_address[chip], memory[storadd]);
              }
            }
          } else if (webusb_enabled) {
            webusbplaying = true;
            for (var chip = 0; chip < maxsid; chip++) {
              if (SID_address[chip] && storadd >= SID_address[chip] && storadd <= (SID_address[chip] + 0x18)) {
                webusb.writeReg([0x80, ((chip * 0x20) | storadd - SID_address[chip]), memory[storadd], 0,11]);
              }
            }
          };

          // OPL FM-support
          if ((storadd == 0xDF40) || (storadd == 0xDF50)) {
            if (asid_enabled) {
              asidFMwriteReg(storadd == 0xDF50, memory[storadd]);
            } else if (webusb_enabled && usbsid.fmoplsid != 0) {
              let us_fmaddr = ((usbsid.fmoplsid - 1) << 5);
              webusb.writeReg([0x80, ((storadd - 0xDF40) | us_fmaddr), memory[storadd], 0,11]);
            } else {
              oplFMwriteReg(storadd == 0xDF50, memory[storadd], opl);
            }
          }
        }
        CPUtime -= clk_ratio;
      }
    }

    if (playlength > 0 && parseInt(playtime) == parseInt(playlength) && endcallback !== null && ended == 0) { ended = 1; endcallback(); }
    if (asid_enabled) {
      mix = 0;
    } else if (webusb_enabled) {
      // Do nothing
      mix = 0;
    } else {
      mix = SID(0, 0xD400);
      if (SID_address[1]) mix += SID(1, SID_address[1]);
      if (SID_address[2]) mix += SID(2, SID_address[2]);
      // Add OPL FM sound, if available
      if (isOplFMwritten && oplFMrefresh && opl !== undefined) {
        oplFMbuf = opl.generate(2, Int16Array);
        mix += oplFMbuf[0] / 32768.0;
        oplFMrefresh = false;
        SIDamount = 2; // Force mix balance
      } else {
        mix += oplFMbuf[1] / 32768.0;
        oplFMrefresh = true;
      }

    }

    /* return mix * volume * SIDamount_vol[SIDamount] + (Math.random() * background_noise - background_noise / 2); */
    return mix * volume * SIDamount_vol[SIDamount] + (background_noise * Math.random() - background_noise / 2);  // NOTICE: Testing if we can get the 4SID clicks to dissapear
  }

  function speed(multiplier) { // Added by JCH
    multi = multiplier;
    if (timermode[subtune] || memory[0xDC05]) { // CIA timing
      clk_ratio = (C64_PAL_CPUCLK * multi) / samplerate;
      frame_sampleperiod = (memory[0xDC04] + memory[0xDC05] * 256) / clk_ratio;
    } else
      frame_sampleperiod = samplerate / (PAL_FRAMERATE * multi);
  }

  var //CPU (and CIA/VIC-IRQ) emulation constants and variables - avoiding internal/automatic variables to retain speed
    flagsw = [0x01, 0x21, 0x04, 0x24, 0x00, 0x40, 0x08, 0x28], branchflag = [0x80, 0x40, 0x01, 0x02];
  var PC = 0, A = 0, T = 0, X = 0, Y = 0, SP = 0xFF, IR = 0, addr = 0, ST = 0x00, cycles = 0, storadd = 0; //STATUS-flags: N V - B D I Z C

  function initCPU(mempos) { PC = mempos; A = 0; X = 0; Y = 0; ST = 0; SP = 0xFF; }


  //My CPU implementation is based on the instruction table by Graham at codebase64.
  //After some examination of the table it was clearly seen that columns of the table (instructions' 2nd nybbles)
  // mainly correspond to addressing modes, and double-rows usually have the same instructions.
  //The code below is laid out like this, with some exceptions present.
  //Thanks to the hardware being in my mind when coding this, more of the illegal instructions can be added fairly easily...

  function CPU() //the CPU emulation for SID/PRG playback (ToDo: CIA/VIC-IRQ/NMI/RESET vectors, BCD-mode)
  { //'IR' is the instruction-register, naming after the hardware-equivalent
    IR = memory[PC]; cycles = 2; storadd = 0; //'cycle': ensure smallest 6510 runtime (for implied/register instructions)

    if (IR & 1) {  //nybble2:  1/5/9/D:accu.instructions, 3/7/B/F:illegal opcodes
      switch (IR & 0x1F) { //addressing modes (begin with more complex cases), PC wraparound not handled inside to save codespace
        case 1: case 3: addr = memory[memory[++PC] + X] + memory[memory[PC] + X + 1] * 256; cycles = 6; break; //(zp,x)
        case 0x11: case 0x13: addr = memory[memory[++PC]] + memory[memory[PC] + 1] * 256 + Y; cycles = 6; break; //(zp),y
        case 0x19: case 0x1F: addr = memory[++PC] + memory[++PC] * 256 + Y; cycles = 5; break; //abs,y
        case 0x1D: addr = memory[++PC] + memory[++PC] * 256 + X; cycles = 5; break; //abs,x
        case 0xD: case 0xF: addr = memory[++PC] + memory[++PC] * 256; cycles = 4; break; //abs
        case 0x15: addr = memory[++PC] + X; cycles = 4; break; //zp,x
        case 5: case 7: addr = memory[++PC]; cycles = 3; break; //zp
        case 0x17: addr = memory[++PC] + Y; cycles = 4; break; //zp,y for LAX/SAX illegal opcodes
        case 9: case 0xB: addr = ++PC; cycles = 2;  //immediate
      }
      addr &= 0xFFFF;
      switch (IR & 0xE0) {
        case 0x60: T = A; A += memory[addr] + (ST & 1); ST &= 20; ST |= (A & 128) | (A > 255); A &= 0xFF; ST |= (!A) << 1 | (!((T ^ memory[addr]) & 0x80) && ((T ^ A) & 0x80)) >> 1; break; //ADC
        case 0xE0: T = A; A -= memory[addr] + !(ST & 1); ST &= 20; ST |= (A & 128) | (A >= 0); A &= 0xFF; ST |= (!A) << 1 | (((T ^ memory[addr]) & 0x80) && ((T ^ A) & 0x80)) >> 1; break; //SBC
        case 0xC0: T = A - memory[addr]; ST &= 124; ST |= (!(T & 0xFF)) << 1 | (T & 128) | (T >= 0); break; //CMP
        case 0x00: A |= memory[addr]; ST &= 125; ST |= (!A) << 1 | (A & 128); break; //ORA
        case 0x20: A &= memory[addr]; ST &= 125; ST |= (!A) << 1 | (A & 128); break; //AND
        case 0x40: A ^= memory[addr]; ST &= 125; ST |= (!A) << 1 | (A & 128); break; //EOR
        case 0xA0: A = memory[addr]; ST &= 125; ST |= (!A) << 1 | (A & 128); if ((IR & 3) == 3) X = A; break; //LDA / LAX (illegal, used by my 1 rasterline player)
        case 0x80: memory[addr] = A & (((IR & 3) == 3) ? X : 0xFF); storadd = addr;   //STA / SAX (illegal)
      }
    }

    else if (IR & 2) {  //nybble2:  2:illegal/LDX, 6:A/X/INC/DEC, A:Accu-shift/reg.transfer/NOP, E:shift/X/INC/DEC
      switch (IR & 0x1F) { //addressing modes
        case 0x1E: addr = memory[++PC] + memory[++PC] * 256 + (((IR & 0xC0) != 0x80) ? X : Y); cycles = 5; break; //abs,x / abs,y
        case 0xE: addr = memory[++PC] + memory[++PC] * 256; cycles = 4; break; //abs
        case 0x16: addr = memory[++PC] + (((IR & 0xC0) != 0x80) ? X : Y); cycles = 4; break; //zp,x / zp,y
        case 6: addr = memory[++PC]; cycles = 3; break; //zp
        case 2: addr = ++PC; cycles = 2;  //imm.
      }
      addr &= 0xFFFF;
      switch (IR & 0xE0) {
        case 0x00: ST &= 0xFE; case 0x20: if ((IR & 0xF) == 0xA) { A = (A << 1) + (ST & 1); ST &= 60; ST |= (A & 128) | (A > 255); A &= 0xFF; ST |= (!A) << 1; } //ASL/ROL (Accu)
        else { T = (memory[addr] << 1) + (ST & 1); ST &= 60; ST |= (T & 128) | (T > 255); T &= 0xFF; ST |= (!T) << 1; memory[addr] = T; cycles += 2; } break; //RMW (Read-Write-Modify)
        case 0x40: ST &= 0xFE; case 0x60: if ((IR & 0xF) == 0xA) { T = A; A = (A >> 1) + (ST & 1) * 128; ST &= 60; ST |= (A & 128) | (T & 1); A &= 0xFF; ST |= (!A) << 1; } //LSR/ROR (Accu)
        else { T = (memory[addr] >> 1) + (ST & 1) * 128; ST &= 60; ST |= (T & 128) | (memory[addr] & 1); T &= 0xFF; ST |= (!T) << 1; memory[addr] = T; cycles += 2; } break; //RMW
        case 0xC0: if (IR & 4) { memory[addr]--; memory[addr] &= 0xFF; ST &= 125; ST |= (!memory[addr]) << 1 | (memory[addr] & 128); cycles += 2; } //DEC
        else { X--; X &= 0xFF; ST &= 125; ST |= (!X) << 1 | (X & 128); } break; //DEX
        case 0xA0: if ((IR & 0xF) != 0xA) X = memory[addr]; else if (IR & 0x10) { X = SP; break; } else X = A; ST &= 125; ST |= (!X) << 1 | (X & 128); break; //LDX/TSX/TAX
        case 0x80: if (IR & 4) { memory[addr] = X; storadd = addr; } else if (IR & 0x10) SP = X; else { A = X; ST &= 125; ST |= (!A) << 1 | (A & 128); } break; //STX/TXS/TXA
        case 0xE0: if (IR & 4) { memory[addr]++; memory[addr] &= 0xFF; ST &= 125; ST |= (!memory[addr]) << 1 | (memory[addr] & 128); cycles += 2; } //INC/NOP
      }
    }

    else if ((IR & 0xC) == 8) {  //nybble2:  8:register/status
      switch (IR & 0xF0) {
        case 0x60: SP++; SP &= 0xFF; A = memory[0x100 + SP]; ST &= 125; ST |= (!A) << 1 | (A & 128); cycles = 4; break; //PLA
        case 0xC0: Y++; Y &= 0xFF; ST &= 125; ST |= (!Y) << 1 | (Y & 128); break; //INY
        case 0xE0: X++; X &= 0xFF; ST &= 125; ST |= (!X) << 1 | (X & 128); break; //INX
        case 0x80: Y--; Y &= 0xFF; ST &= 125; ST |= (!Y) << 1 | (Y & 128); break; //DEY
        case 0x00: memory[0x100 + SP] = ST; SP--; SP &= 0xFF; cycles = 3; break; //PHP
        case 0x20: SP++; SP &= 0xFF; ST = memory[0x100 + SP]; cycles = 4; break; //PLP
        case 0x40: memory[0x100 + SP] = A; SP--; SP &= 0xFF; cycles = 3; break; //PHA
        case 0x90: A = Y; ST &= 125; ST |= (!A) << 1 | (A & 128); break; //TYA
        case 0xA0: Y = A; ST &= 125; ST |= (!Y) << 1 | (Y & 128); break; //TAY
        default: if (flagsw[IR >> 5] & 0x20) ST |= (flagsw[IR >> 5] & 0xDF); else ST &= 255 - (flagsw[IR >> 5] & 0xDF);  //CLC/SEC/CLI/SEI/CLV/CLD/SED
      }
    }

    else {  //nybble2:  0: control/branch/Y/compare  4: Y/compare  C:Y/compare/JMP
      if ((IR & 0x1F) == 0x10) {
        PC++; T = memory[PC]; if (T & 0x80) T -= 0x100; //BPL/BMI/BVC/BVS/BCC/BCS/BNE/BEQ  relative branch
        if (IR & 0x20) { if (ST & branchflag[IR >> 6]) { PC += T; cycles = 3; } } else { if (!(ST & branchflag[IR >> 6])) { PC += T; cycles = 3; } }
      }
      else {  //nybble2:  0:Y/control/Y/compare  4:Y/compare  C:Y/compare/JMP
        switch (IR & 0x1F) { //addressing modes
          case 0: addr = ++PC; cycles = 2; break; //imm. (or abs.low for JSR/BRK)
          case 0x1C: addr = memory[++PC] + memory[++PC] * 256 + X; cycles = 5; break; //abs,x
          case 0xC: addr = memory[++PC] + memory[++PC] * 256; cycles = 4; break; //abs
          case 0x14: addr = memory[++PC] + X; cycles = 4; break; //zp,x
          case 4: addr = memory[++PC]; cycles = 3;  //zp
        }
        addr &= 0xFFFF;
        switch (IR & 0xE0) {
          // case 0x00: memory[0x100 + SP] = PC % 256; SP--; SP &= 0xFF; memory[0x100 + SP] = PC / 256; SP--; SP &= 0xFF; memory[0x100 + SP] = ST; SP--; SP &= 0xFF;
          case 0x00: memory[0x100 + SP] = PC / 256; SP--; SP &= 0xFF; memory[0x100 + SP] = PC % 256; SP--; SP &= 0xFF; memory[0x100 + SP] = ST; SP--; SP &= 0xFF;
            PC = memory[0xFFFE] + memory[0xFFFF] * 256 - 1; cycles = 7; break; //BRK
          case 0x20: if (IR & 0xF) { ST &= 0x3D; ST |= (memory[addr] & 0xC0) | (!(A & memory[addr])) << 1; } //BIT
          // else { memory[0x100 + SP] = (PC + 2) % 256; SP--; SP &= 0xFF; memory[0x100 + SP] = (PC + 2) / 256; SP--; SP &= 0xFF; PC = memory[addr] + memory[addr + 1] * 256 - 1; cycles = 6; } break; //JSR
          else { memory[0x100 + SP] = (PC + 1) / 256; SP--; SP &= 0xFF; memory[0x100 + SP] = (PC + 1) % 256; SP--; SP &= 0xFF; PC = memory[addr] + memory[addr + 1] * 256 - 1; cycles = 6; } break; //JSR
          case 0x40: if (IR & 0xF) { PC = addr - 1; cycles = 3; } //JMP
          // else { if (SP >= 0xFF) return 0xFE; SP++; SP &= 0xFF; ST = memory[0x100 + SP]; SP++; SP &= 0xFF; T = memory[0x100 + SP]; SP++; SP &= 0xFF; PC = memory[0x100 + SP] + T * 256 - 1; cycles = 6; } break; //RTI
          else { if (SP >= 0xFF) return 0xFE; SP++; SP &= 0xFF; ST = memory[0x100 + SP]; SP++; SP &= 0xFF; T = memory[0x100 + SP]; SP++; SP &= 0xFF; PC = 256 * memory[0x100 + SP] + T - 1; cycles = 6; } break; //RTI
          case 0x60: if (IR & 0xF) { PC = memory[addr] + memory[addr + 1] * 256 - 1; cycles = 5; } //JMP() (indirect)
          // else { if (SP >= 0xFF) return 0xFF; SP++; SP &= 0xFF; T = memory[0x100 + SP]; SP++; SP &= 0xFF; PC = memory[0x100 + SP] + T * 256 - 1; cycles = 6; } break; //RTS
          else { if (SP >= 0xFF) return 0xFF; SP++; SP &= 0xFF; T = memory[0x100 + SP]; SP++; SP &= 0xFF; PC = 256 * memory[0x100 + SP] + T; cycles = 6; } break; //RTS
          case 0xC0: T = Y - memory[addr]; ST &= 124; ST |= (!(T & 0xFF)) << 1 | (T & 128) | (T >= 0); break; //CPY
          case 0xE0: T = X - memory[addr]; ST &= 124; ST |= (!(T & 0xFF)) << 1 | (T & 128) | (T >= 0); break; //CPX
          case 0xA0: Y = memory[addr]; ST &= 125; ST |= (!Y) << 1 | (Y & 128); break; //LDY
          case 0x80: memory[addr] = Y; storadd = addr;  //STY
        }
      }
    }

    PC++; PC &= 0xFFFF; return 0; //memory[addr]&=0xFF;
  }


  //My SID implementation is similar to what I worked out in a SwinSID variant during 3..4 months of development. (So jsSID only took 2 weeks armed with this experience.)
  //I learned the workings of ADSR/WAVE/filter operations mainly from the quite well documented resid and resid-fp codes.
  //(The SID reverse-engineering sites were also good sources.)
  //Note that I avoided internal/automatic variables from the SID function, assuming that JavaScript is better this way. (Not using stack as much, but I'm not sure and it may depend on platform...)
  //So I advise to keep them here. As they have 'var' in the declaration, they are in this scope and won't interfere with anything outside jsSID.
  //(The same is true for CPU emulation and player.)

  var //SID emulation constants and variables
    GATE_BITMASK = 0x01, SYNC_BITMASK = 0x02, RING_BITMASK = 0x04, TEST_BITMASK = 0x08, TRI_BITMASK = 0x10, SAW_BITMASK = 0x20, PULSE_BITMASK = 0x40, NOISE_BITMASK = 0x80,
    HOLDZERO_BITMASK = 0x10, DECAYSUSTAIN_BITMASK = 0x40, ATTACK_BITMASK = 0x80,
    FILTSW = [1, 2, 4, 1, 2, 4, 1, 2, 4], LOWPASS_BITMASK = 0x10, BANDPASS_BITMASK = 0x20, HIGHPASS_BITMASK = 0x40, OFF3_BITMASK = 0x80;
  var ADSRstate = [0, 0, 0, 0, 0, 0, 0, 0, 0], ratecnt = [0, 0, 0, 0, 0, 0, 0, 0, 0], envcnt = [0, 0, 0, 0, 0, 0, 0, 0, 0], expcnt = [0, 0, 0, 0, 0, 0, 0, 0, 0], prevSR = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  var phaseaccu = [0, 0, 0, 0, 0, 0, 0, 0, 0], prevaccu = [0, 0, 0, 0, 0, 0, 0, 0, 0], sourceMSBrise = [0, 0, 0], sourceMSB = [0, 0, 0];
  var noise_LFSR = [0x7FFFF8, 0x7FFFF8, 0x7FFFF8, 0x7FFFF8, 0x7FFFF8, 0x7FFFF8, 0x7FFFF8, 0x7FFFF8, 0x7FFFF8];
  var prevwfout = [0, 0, 0, 0, 0, 0, 0, 0, 0], prevwavdata = [0, 0, 0, 0, 0, 0, 0, 0, 0], combiwf;
  var prevlowpass = [0, 0, 0], prevbandpass = [0, 0, 0], cutoff_ratio_8580 = -2 * 3.14 * (12500 / 256) / samplerate, cutoff_ratio_6581 = -2 * 3.14 * (20000 / 256) / samplerate;
  var prevgate, chnadd, ctrl, wf, test, period, step, SR, accuadd, MSB, tmp, pw, lim, wfout, cutoff, resonance, filtin, output;
  //registers: 0:freql1  1:freqh1  2:pwml1  3:pwmh1  4:ctrl1  5:ad1   6:sr1  7:freql2  8:freqh2  9:pwml2 10:pwmh2 11:ctrl2 12:ad2  13:sr 14:freql3 15:freqh3 16:pwml3 17:pwmh3 18:ctrl3 19:ad3  20:sr3
  //           21:cutoffl 22:cutoffh 23:flsw_reso 24:vol_ftype 25:potX 26:potY 27:OSC3 28:ENV3

  function initSID() {
    for (var i = 0xD400; i <= 0xD7FF; i++) memory[i] = 0; for (var i = 0xDE00; i <= 0xDFFF; i++) memory[i] = 0;
    for (var i = 0; i < 9; i++) { ADSRstate[i] = HOLDZERO_BITMASK; ratecnt[i] = envcnt[i] = expcnt[i] = prevSR[i] = 0; }
  }


  function SID(num, SIDaddr) //the SID emulation itself ('num' is the number of SID to iterate (0..2)
  {
    filtin = 0; output = 0;

    //treating 2SID and 3SID channels uniformly (0..5 / 0..8), this probably avoids some extra code
    for (var channel = num * SID_CHANNEL_AMOUNT; channel < (num + 1) * SID_CHANNEL_AMOUNT; channel++) {
      prevgate = (ADSRstate[channel] & GATE_BITMASK); chnadd = SIDaddr + (channel - num * SID_CHANNEL_AMOUNT) * 7;
      ctrl = memory[chnadd + 4]; wf = ctrl & 0xF0; test = ctrl & TEST_BITMASK; SR = memory[chnadd + 6]; tmp = 0;

      //ADSR envelope generator:
      if (prevgate != (ctrl & GATE_BITMASK)) { //gatebit-change?
        if (prevgate) { ADSRstate[channel] &= 0xFF - (GATE_BITMASK | ATTACK_BITMASK | DECAYSUSTAIN_BITMASK); } //falling edge (with Whittaker workaround this never happens, but should be here)
        else {
          ADSRstate[channel] = (GATE_BITMASK | ATTACK_BITMASK | DECAYSUSTAIN_BITMASK); //rising edge, also sets hold_zero_bit=0
          if ((SR & 0xF) > (prevSR[channel] & 0xF)) tmp = 1; //assume SR->GATE write order: workaround to have crisp soundstarts by triggering delay-bug
        }                                               //(this is for the possible missed CTRL(GATE) vs SR register write order situations (1MHz CPU is cca 20 times faster than samplerate)
      }
      prevSR[channel] = SR;

      ratecnt[channel] += clk_ratio; if (ratecnt[channel] >= 0x8000) ratecnt[channel] -= 0x8000; //can wrap around (ADSR delay-bug: short 1st frame is usually achieved by utilizing this bug)

      //set ADSR period that should be checked against rate-counter (depending on ADSR state Attack/DecaySustain/Release)
      if (ADSRstate[channel] & ATTACK_BITMASK) { step = memory[chnadd + 5] >> 4; period = ADSRperiods[step]; }
      else if (ADSRstate[channel] & DECAYSUSTAIN_BITMASK) { step = memory[chnadd + 5] & 0xF; period = ADSRperiods[step]; }
      else { step = SR & 0xF; period = ADSRperiods[step]; }
      step = ADSRstep[step];

      if (ratecnt[channel] >= period && ratecnt[channel] < period + clk_ratio && tmp == 0) { //ratecounter shot (matches rateperiod) (in genuine SID ratecounter is LFSR)
        ratecnt[channel] -= period;  //compensation for timing instead of simply setting 0 on rate-counter overload
        if ((ADSRstate[channel] & ATTACK_BITMASK) || ++expcnt[channel] == ADSR_exptable[envcnt[channel]]) {
          if (!(ADSRstate[channel] & HOLDZERO_BITMASK)) {
            if (ADSRstate[channel] & ATTACK_BITMASK) { envcnt[channel] += step; if (envcnt[channel] >= 0xFF) { envcnt[channel] = 0xFF; ADSRstate[channel] &= 0xFF - ATTACK_BITMASK; } }
            else if (!(ADSRstate[channel] & DECAYSUSTAIN_BITMASK) || envcnt[channel] > (SR >> 4) + (SR & 0xF0)) { envcnt[channel] -= step; if (envcnt[channel] <= 0 && envcnt[channel] + step != 0) { envcnt[channel] = 0; ADSRstate[channel] |= HOLDZERO_BITMASK; } }
          }
          expcnt[channel] = 0;
        }
      }

      envcnt[channel] &= 0xFF; //'envcnt' may wrap around in some cases, mostly 0 -> FF (e.g.: Cloudless Rain, Boombox Alley)

      //WAVE generation codes (phase accumulator and waveform-selector):  (They are explained in resid source, I won't go in details, the code speaks for itself.)
      accuadd = (memory[chnadd] + memory[chnadd + 1] * 256) * clk_ratio;
      if (test || ((ctrl & SYNC_BITMASK) && sourceMSBrise[num])) { phaseaccu[channel] = 0; }
      else { phaseaccu[channel] += accuadd; if (phaseaccu[channel] > 0xFFFFFF) phaseaccu[channel] -= 0x1000000; }
      MSB = phaseaccu[channel] & 0x800000; sourceMSBrise[num] = (MSB > (prevaccu[channel] & 0x800000)) ? 1 : 0; //phaseaccu[channel] &= 0xFFFFFF;

      //waveform-selector:
      if (wf & NOISE_BITMASK) { //noise waveform
        tmp = noise_LFSR[channel];
        if (((phaseaccu[channel] & 0x100000) != (prevaccu[channel] & 0x100000)) || accuadd >= 0x100000) { //clock LFSR all time if clockrate exceeds observable at given samplerate
          step = (tmp & 0x400000) ^ ((tmp & 0x20000) << 5); tmp = ((tmp << 1) + (step > 0 || test)) & 0x7FFFFF; noise_LFSR[channel] = tmp;
        }
        //we simply zero output when other waveform is mixed with noise. On real SID LFSR continuously gets filled by zero and locks up. ($C1 waveform with pw<8 can keep it for a while...)
        wfout = (wf & 0x70) ? 0 : ((tmp & 0x100000) >> 5) + ((tmp & 0x40000) >> 4) + ((tmp & 0x4000) >> 1) + ((tmp & 0x800) << 1) + ((tmp & 0x200) << 2) + ((tmp & 0x20) << 5) + ((tmp & 0x04) << 7) + ((tmp & 0x01) << 8);
      }
      else if (wf & PULSE_BITMASK) { //simple pulse
        pw = (memory[chnadd + 2] + (memory[chnadd + 3] & 0xF) * 256) * 16; tmp = accuadd >> 9; if (0 < pw && pw < tmp) pw = tmp; tmp ^= 0xFFFF; if (pw > tmp) pw = tmp;
        tmp = phaseaccu[channel] >> 8;
        if (wf == PULSE_BITMASK) {
          step = 256 / (accuadd >> 16); //simple pulse, most often used waveform, make it sound as clean as possible without oversampling
          //One of my biggest success with the SwinSID-variant was that I could clean the high-pitched and thin sounds.
          //(You might have faced with the unpleasant sound quality of high-pitched sounds without oversampling. We need so-called 'band-limited' synthesis instead.
          // There are a lot of articles about this issue on the internet. In a nutshell, the harsh edges produce harmonics that exceed the
          // Nyquist frequency (samplerate/2) and they are folded back into hearable range, producing unvanted ringmodulation-like effect.)
          //After so many trials with dithering/filtering/oversampling/etc. it turned out I can't eliminate the fukkin aliasing in time-domain, as suggested at pages.
          //Oversampling (running the wave-generation 8 times more) was not a way at 32MHz SwinSID. It might be an option on PC but I don't prefer it in JavaScript.)
          //The only solution that worked for me in the end, what I came up eventually: The harsh rising and falling edges of the pulse are
          //elongated making it a bit trapezoid. But not in time-domain, but altering the transfer-characteristics. This had to be done
          //in a frequency-dependent way, proportionally to pitch, to keep the deep sounds crisp. The following code does this (my favourite testcase is Robocop3 intro):
          if (test) wfout = 0xFFFF;
          else if (tmp < pw) { lim = (0xFFFF - pw) * step; if (lim > 0xFFFF) lim = 0xFFFF; wfout = lim - (pw - tmp) * step; if (wfout < 0) wfout = 0; } //rising edge
          else { lim = pw * step; if (lim > 0xFFFF) lim = 0xFFFF; wfout = (0xFFFF - tmp) * step - lim; if (wfout >= 0) wfout = 0xFFFF; wfout &= 0xFFFF; }  //falling edge
        }
        else { //combined pulse
          wfout = (tmp >= pw || test) ? 0xFFFF : 0; //(this would be enough for simple but aliased-at-high-pitches pulse)
          if (wf & TRI_BITMASK) {
            if (wf & SAW_BITMASK) { wfout = (wfout) ? combinedWF(channel, PulseTriSaw_8580, tmp >> 4, 1) : 0; } //pulse+saw+triangle (waveform nearly identical to tri+saw)
            else { tmp = phaseaccu[channel] ^ (ctrl & RING_BITMASK ? sourceMSB[num] : 0); wfout = (wfout) ? combinedWF(channel, PulseSaw_8580, (tmp ^ (tmp & 0x800000 ? 0xFFFFFF : 0)) >> 11, 0) : 0; }
          } //pulse+triangle
          else if (wf & SAW_BITMASK) wfout = (wfout) ? combinedWF(channel, PulseSaw_8580, tmp >> 4, 1) : 0; //pulse+saw
        }
      }
      else if (wf & SAW_BITMASK) { //saw
        wfout = phaseaccu[channel] >> 8; //saw (this row would be enough for simple but aliased-at-high-pitch saw)
        //The anti-aliasing (cleaning) of high-pitched sawtooth wave works by the same principle as mentioned above for the pulse,
        //but the sawtooth has even harsher edge/transition, and as the falling edge gets longer, tha rising edge should became shorter,
        //and to keep the amplitude, it should be multiplied a little bit (with reciprocal of rising-edge steepness).
        //The waveform at the output essentially becomes an asymmetric triangle, more-and-more approaching symmetric shape towards high frequencies.
        //(If you check a recording from the real SID, you can see a similar shape, the high-pitch sawtooth waves are triangle-like...)
        //But for deep sounds the sawtooth is really close to a sawtooth, as there is no aliasing there, but deep sounds should be sharp...
        if (wf & TRI_BITMASK) wfout = combinedWF(channel, TriSaw_8580, wfout >> 4, 1); //saw+triangle
        else { step = accuadd / 0x1200000; wfout += wfout * step; if (wfout > 0xFFFF) wfout = 0xFFFF - (wfout - 0x10000) / step; }  //simple cleaned (bandlimited) saw
      }
      else if (wf & TRI_BITMASK) { //triangle (this waveform has no harsh edges, so it doesn't suffer from strong aliasing at high pitches)
        tmp = phaseaccu[channel] ^ (ctrl & RING_BITMASK ? sourceMSB[num] : 0); wfout = (tmp ^ (tmp & 0x800000 ? 0xFFFFFF : 0)) >> 7;
      }

      if (wf) prevwfout[channel] = wfout; else { wfout = prevwfout[channel]; } //emulate waveform 00 floating wave-DAC (on real SID waveform00 decays after 15s..50s depending on temperature?)
      prevaccu[channel] = phaseaccu[channel]; sourceMSB[num] = MSB;            //(So the decay is not an exact value. Anyway, we just simply keep the value to avoid clicks and support SounDemon digi later...)

      if (voiceMask & (1 << channel)) { // Added by JCH
        //routing the channel signal to either the filter or the unfiltered master output depending on filter-switch SID-registers
        if (memory[SIDaddr + 0x17] & FILTSW[channel]) filtin += (wfout - 0x8000) * (envcnt[channel] / 256);
        else if ((channel % SID_CHANNEL_AMOUNT) != 2 || !(memory[SIDaddr + 0x18] & OFF3_BITMASK)) output += (wfout - 0x8000) * (envcnt[channel] / 256);
      }
    }

    //update readable SID-registers (some SID tunes might use 3rd channel ENV3/OSC3 value as control)
    if (memory[1] & 3) memory[SIDaddr + 0x1B] = wfout >> 8; memory[SIDaddr + 0x1C] = envcnt[3]; //OSC3, ENV3 (some players rely on it)

    //FILTER: two integrator loop bi-quadratic filter, workings learned from resid code, but I kindof simplified the equations
    //The phases of lowpass and highpass outputs are inverted compared to the input, but bandpass IS in phase with the input signal.
    //The 8580 cutoff frequency control-curve is ideal, while the 6581 has a treshold, and below it it outputs a constant lowpass frequency.
    cutoff = (memory[SIDaddr + 0x15] & 7) / 8 + memory[SIDaddr + 0x16] + 0.2;
    if (SID_model == 8580.0) { cutoff = 1 - Math.exp(cutoff * cutoff_ratio_8580); resonance = Math.pow(2, ((4 - (memory[SIDaddr + 0x17] >> 4)) / 8)); }
    else { if (cutoff < 24) cutoff = 0.035; else cutoff = 1 - 1.263 * Math.exp(cutoff * cutoff_ratio_6581); resonance = (memory[SIDaddr + 0x17] > 0x5F) ? 8 / (memory[SIDaddr + 0x17] >> 4) : 1.41; }
    tmp = filtin + prevbandpass[num] * resonance + prevlowpass[num]; if (memory[SIDaddr + 0x18] & HIGHPASS_BITMASK) output -= tmp;
    tmp = prevbandpass[num] - tmp * cutoff; prevbandpass[num] = tmp; if (memory[SIDaddr + 0x18] & BANDPASS_BITMASK) output -= tmp;
    tmp = prevlowpass[num] + tmp * cutoff; prevlowpass[num] = tmp; if (memory[SIDaddr + 0x18] & LOWPASS_BITMASK) output += tmp;

    //when it comes to $D418 volume-register digi playback, I made an AC / DC separation for $D418 value in the SwinSID at low (20Hz or so) cutoff-frequency,
    //and sent the AC (highpass) value to a 4th 'digi' channel mixed to the master output, and set ONLY the DC (lowpass) value to the volume-control.
    //This solved 2 issues: Thanks to the lowpass filtering of the volume-control, SID tunes where digi is played together with normal SID channels,
    //won't sound distorted anymore, and the volume-clicks disappear when setting SID-volume. (This is useful for fade-in/out tunes like Hades Nebula, where clicking ruins the intro.)
    return (output / OUTPUT_SCALEDOWN) * (memory[SIDaddr + 0x18] & 0xF); // SID output
  }


  //And now, the combined waveforms. The resid source simply uses 4kbyte 8bit samples from wavetable arrays, says these waveforms are mystic due to the analog behaviour.
  //It's true, the analog things inside SID play a significant role in how the combined waveforms look like, but process variations are not so huge that cause much differences in SIDs.
  //After checking these waveforms by eyes, it turned out for me that these waveform are fractal-like, recursively approachable waveforms.
  //My 1st thought and trial was to store only a portion of the waveforms in table, and magnify them depending on phase-accumulator's state.
  //But I wanted to understand how these waveforms are produced. I felt from the waveform-diagrams that the bits of the waveforms affect each other,
  //hence the recursive look. A short C code proved by assumption, I could generate something like a pulse+saw combined waveform.
  //Recursive calculations were not feasible for MCU of SwinSID, but for jsSID I could utilize what I found out and code below generates the combined waveforms into wavetables.
  //To approach the combined waveforms as much as possible, I checked out the SID schematic that can be found at some reverse-engineering sites...
  //The SID's R-2R ladder WAVE DAC is driven by operation-amplifier like complementary FET output drivers, so that's not the place where I first thought the magic happens.
  //These 'opamps' (for all 12 wave-bits) have single FETs as inputs, and they switch on above a certain level of input-voltage, causing 0 or 1 bit as R-2R DAC input.
  //So the first keyword for the workings is TRESHOLD. These FET inputs are driven through serial switch FETs (wave-selector) that normally enables one waveform at a time.
  //The phase-accumulator's output is brought to 3 kinds of circuitries for the 3 basic waveforms. The pulse simply drives
  //all wave-selector inputs with a 0/1 depending on pulsewidth, the sawtooth has a XOR for triangle/ringmod generation, but what
  //is common for all waveforms, they have an open-drain driver before the wave-selector, which has FETs towards GND and 'FET resistor' towards the power-supply rail.
  //These outputs are clearly not designed to drive high loads, and normally they only have to drive the FETs input mentioned above.
  //But when more of these output drivers are switched together by the switch-FETs in the wave-selector, they affect each other by loading each other.
  //The pulse waveform, when selected, connects all of them together through a fairly strong connection, and its signal also affects the analog level (pulls below the treshold)...
  //The farther a specific DAC bit driver is from the other, the less it affects its output. It turned out it's not powers of 2 but something else,
  //that creates similar combined waveforms to that of real SID's...
  //The analog levels that get generated by the various bit drivers, that pull each other up/down depends on the resistances the components inside the SID have.
  //And finally, what is output on the DAC depends on whether these analog levels are below or above the FET gate's treshold-level,
  //That's how the combined waveform is generated. Maybe I couldn't explain well enough, but the code below is simple enough to understand the mechanism algoritmically.
  //This simplified schematic exapmle might make it easier to understand sawtooth+pulse combination (must be observed with monospace fonts):
  //                               _____            |-    .--------------.   /\/\--.
  // Vsupply                /  .----| |---------*---|-    /    Vsupply   !    R    !      As can be seen on this schematic,
  //  ------.       other   !  !   _____        !  TRES   \       \      !         /      the pulse wave-selector FETs
  //        !       saw bit *--!----| |---------'  HOLD   /       !     |-     2R  \      connect the neighbouring sawtooth
  //        /       output  !  !                          !      |------|-         /      outputs with a fairly strong
  //     Rd \              |-  !WAVEFORM-SELECTOR         *--*---|-      !    R    !      connection to each other through
  //        /              |-  !SWITCHING FETs            !  !    !      *---/\/\--*      their own wave-selector FETs.
  //        ! saw-bit          !    _____                |-  !   ---     !         !      So the adjacent sawtooth outputs
  //        *------------------!-----| |-----------*-----|-  !          |-         /      pull each other upper/lower
  //        ! (weak drive,so   !  saw switch       ! TRES-!  `----------|-     2R  \      depending on their low/high state and
  //       |- can be shifted   !                   ! HOLD !              !         /      distance from each other, causing
  //  -----|- by neighbours    !    _____          !      !              !     R   !      the resulting analog level that
  //        ! up or down)      *-----| |-----------'     ---            ---   /\/\-*      will either turn the output on or not.
  //   GND ---                 !  pulse switch                                     !      (Depending on their relation to treshold.)
  //
  //(As triangle waveform connects adjacent bits by default, the above explained effect becomes even stronger, that's why combined waveforms with thriangle are at 0 level most of the time.)

  function combinedWF(channel, wfarray, index, differ6581) { //on 6581 most combined waveforms are essentially halved 8580-like waves
    if (differ6581 && SID_model == 6581.0) index &= 0x7FF; combiwf = (wfarray[index] + prevwavdata[channel]) / 2; prevwavdata[channel] = wfarray[index]; return combiwf;
  }

  function createCombinedWF(wfarray, bitmul, bitstrength, treshold) { //I found out how the combined waveform works (neighboring bits affect each other recursively)
    for (var i = 0; i < 4096; i++) {
      wfarray[i] = 0; //neighbour-bit strength and DAC MOSFET treshold is approximately set by ears'n'trials
      for (var j = 0; j < 12; j++) {
        var bitlevel = 0;
        for (var k = 0; k < 12; k++) { bitlevel += (bitmul / Math.pow(bitstrength, Math.abs(k - j))) * (((i >> k) & 1) - 0.5); }
        wfarray[i] += (bitlevel >= treshold) ? Math.pow(2, j) : 0;
      }
      wfarray[i] *= 12;
    }
  }

  TriSaw_8580 = new Array(4096); createCombinedWF(TriSaw_8580, 0.8, 2.4, 0.64); //precalculate combined waveform
  PulseSaw_8580 = new Array(4096); createCombinedWF(PulseSaw_8580, 1.4, 1.9, 0.68);
  PulseTriSaw_8580 = new Array(4096); createCombinedWF(PulseTriSaw_8580, 0.8, 2.5, 0.64);

  var period0 = Math.max(clk_ratio, 9);
  var ADSRperiods = [period0, 32 * 1, 63 * 1, 95 * 1, 149 * 1, 220 * 1, 267 * 1, 313 * 1, 392 * 1, 977 * 1, 1954 * 1, 3126 * 1, 3907 * 1, 11720 * 1, 19532 * 1, 31251 * 1];
  var ADSRstep = [Math.ceil(period0 / 9), 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1];

  //prescaler values that slow down the envelope-counter as it decays and approaches zero level
  var ADSR_exptable = [1, 30, 30, 30, 30, 30, 30, 16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 4, 4, 4, 4, //pos0:1  pos6:30  pos14:16  pos26:8
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, //pos54:4 //pos93:2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1];

}
