/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * usbsid_adapter.js
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

// globalThis.spp_usbsid_state_SIDBackendAdapter= {
// 	locateFile: function(path, scriptDirectory) { return (typeof globalThis.WASM_SEARCH_PATH == 'undefined') ? path : globalThis.WASM_SEARCH_PATH + path; },
// 	notReady: true,
// 	adapterCallback: function(){}	// overwritten later
// };
// globalThis.spp_usbsid_state_SIDBackendAdapter["onRuntimeInitialized"] = function() {	// emscripten callback needed in case async init is used (e.g. for WASM)
// 	this.notReady= false;
// 	this.adapterCallback();
// }.bind(globalThis.spp_usbsid_state_SIDBackendAdapter);

// var usbsid_SIDBackendAdapter = (function(SIDBackendAdapter) {
// 	return {
// 		SIDBackendAdapter: SIDBackendAdapter,
// 	};
// })(globalThis.spp_usbsid_state_SIDBackendAdapter);

/**
 * @document WEBSID Adapter for USBSID-Pico
 */

class USBSIDBackendAdapter/*  extends usbsid_SIDBackendAdapter.SIDBackendAdapter */ {

  static Port = class {
    constructor(device)
    {
      this._device = device;
      this._name = device.productName;
      this._manufacturer = device.manufacturerName;
      this._serial = device.serialNumber;
      localStorage.setItem("usbsid_device", this);
    }
  }

  constructor(backend) {
    this._backend = ((backend != null)
      ? backend
      : (globalThis.player != null)
      ? globalThis.player._backendAdapter
      : undefined);
    this.Module = (globalThis.backend_SID != undefined)
      ? globalThis.backend_SID.Module : undefined;
    this._savedport = localStorage.getItem("usbsid_device");
    this._port = null;
    this._connected = false;
    this._filter = {
      filters: [
        {
          vendorId: 0xcafe,
          productId: 0x4011,
        },
      ],
    }

    globalThis.usbsid = this;

    if (this._backend != undefined) {
      this._backend._silence_loop = 1;
      this._backend.setProcessorBufSize(4096);
    }

    this.AutoConnect();

    if (this._backend != undefined) {
      this._backend.play = function() {
        globalThis.usbsid.UnMuteUSBSID();
      };
      this._backend.pause = function() {
        globalThis.usbsid.MuteUSBSID();
      };
      this._backend.resume = function() {
        globalThis.usbsid.UnMuteUSBSID();
      };
    }
  }

  /**
   * To be called _during_ or _after_ ScriptNodePlayer.initialize()
   * @workaround
   */
  extend_loadMusicFromURL() {
    (function() {
      var _ctx = globalThis.ScriptNodePlayer.getInstance();
      var _old_loadMusicFromURL = _ctx.loadMusicFromURL;
      _ctx.loadMusicFromURL = extended_loadMusicFromURL;
      async function extended_loadMusicFromURL(url, options, onFail, onProgress) {
        if (globalThis.usbsid._connected) {
          await globalThis.usbsid.SetFlushUSBSID();
          await globalThis.usbsid.ResetUSBSID();
        }
        _old_loadMusicFromURL.call(_ctx, url, options, onFail, onProgress);
      }
    })();
  }

  /**
   * @deprecated
   * this.promiseState(this.Device()).then(state => {
   * 	 if(state != 'rejected') { do it }
   * });
    */
  promiseState(p) {
    const t = {};
    return Promise.race([p, t])
      .then(v => (v === t)? "pending" : "fulfilled", () => "rejected");
  }

  #hideButton()
  {
    var btn = document.getElementById("connect");
    if (btn === null) btn = document.getElementById("websiddevice-connect");
    if (btn !== null) btn.style.display = "none";
  }

  /**
   * @info opens connect dialog to choose device
   * and inits device after conncetion
   */
  async findDevice() {
    await navigator.usb.requestDevice(this._filter)
      .then(device => this._port = new USBSIDBackendAdapter.Port(device))
      .then(_ => console.log("connected"))
      .then(_ => this.#hideButton());
      // .then(_ => this.Init());
  }

  /**
   * @internal do not use
   * @returns a connected device
   */
  async #GetPorts()
  {
    return await navigator.usb.getDevices(this._filter).then(devices => {
      return devices.map(device => new USBSIDBackendAdapter.Port(device));
    });
  }

  /**
   * @info Autoconnects to USB device if saved previouslt
   */
  async AutoConnect() {
    if (this._savedport !== null) {
      await this.#GetPorts().then(ports => {
        if (ports.length === 0) {
          console.log("[USBSID Adapter] No previoussly saved USBSID-Pico to connect to");
        } else {
          console.log("[USBSID Adapter] Automatically connecting to USBSID-Pico");
          this._port = ports[0];
          this.#hideButton();
          console.log(`[USBSID Adapter] Connected to '${this._port._name}, ${this._port._manufacturer}`);
        }
      });
    } else {
      console.log("[USBSID Adapter] No previoussly saved USBSID-Pico to connect to");
    }
    if (this._port !== null) {
      if (this._backend != undefined) {
        await this.Init();
      }
    }
  }

  /**
   * @internal do not use
   * @returns an existing device
   */
  #Device() {
    if (this._port != null) return this._port._device;
  }

  /**
   * @info Creates USBSIDClass
   */
  async CreateUSBSID()
  {
    return await this.Module.ccall('emu_create_USBSID','',[],[],{async: true});
  }

  /**
   * @info Closes the connection to USBSID-Pico
   */
  async CloseUSBSID()
  {
    return await this.Module.ccall('emu_close_USBSID','',[],[],{async: true});
  }

  /**
   * @info Connects to USBSID-Pico
   */
  async ConnectUSBSID()
  {
    return await this.Module.ccall('emu_connect_USBSID','',[],[],{async: true});
  }

  /**
   * @info Set flag for the write thread to flush the
   * current data in the writebuffer to USBSID-Pico
   */
  async SetFlushUSBSID()
  {
    return await this.Module.ccall('emu_setflush_USBSID',[],[],{async: true});
  }

  /**
   * @info Initiate a flush that will write all
   * current data in the writebuffer to USBSID-Pico
   */
  async FlushUSBSID()
  {
    return await this.Module.ccall('emu_flush_USBSID',[],[],{async: true});
  }

  /**
   * @info Set a flag for the write thread to mute USBSID-Pico
   */
  async MuteUSBSID()
  {
    return await this.Module.ccall('emu_mute_USBSID',[],[],{async: true});
  }

  /**
   * @info Set a flag for the write thread to unmute USBSID-Pico
   */
  async UnMuteUSBSID()
  {
    return await this.Module.ccall('emu_unmute_USBSID',[],[],{async: true});
  }

  /**
   * @info Set a flag for the write thread to reset
   * the SID's on USBSID-Pico
   */
  async ResetUSBSID()
  {
    return await this.Module.ccall('emu_reset_USBSID',[],[],{async: true});
  }

  /**
   * @info Set a flag for the write thread to clear
   * all SID registers on USBSID-Pico
   */
  async ClearUSBSID()
  {
    return await this.Module.ccall('emu_clear_USBSID',[],[],{async: true});
  }

  /**
   * @info Initializes the connection for you after the connection dialog has been used
   */
  async Init() {
    console.log("[USBSID Adapter] Initializing play!");
    if (this.#Device() != null) {
      console.log("[USBSID Adapter] Device found!");
      if (this._connected == false) {

        /* USBSIDAdapter.cpp This way freaks out on async shizzle */
        // this._usbsid = new backend_SID.Module.USBSID_Class;
        // if (this._usbsid) {
        // 	if(this.this.USBSID_Init(true,true)) {
        // 		this.this.USBSID_Flush();
        // 	}
        // }
        // this._connected = 1;

        /* Adapter.cpp This one does work */
        await this.CreateUSBSID();
        console.log("[USBSID Adapter] Driver initialized!");
        if(await this.ConnectUSBSID()) {
          console.log("[USBSID Adapter] Driver connected!");
          await this.FlushUSBSID();
          this._connected = true;
        }
      } else {
        await this.SetFlushUSBSID();
        await this.ClearUSBSID();
      }
    } else {
      console.log("[USBSID Adapter] Not using USBSID-Pico, did you click 'CONNECT'?")
    }
  }

}
