/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * player.js
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
class USPlayer
{
  #_path; #_filename; #_sidpath;
  #_worker; #_channel;
  #_url;
  #_URL = globalThis.location.origin;
  #_PATH = window.location.pathname.replace(window.location.pathname.match().input, "/SID/");
  constructor(path, filename) {
    this.#_path = path;
    this.#_filename = filename;
    this.#_sidpath = (this.#_path + this.#_filename);
    this.#_worker = undefined;
    this.#_channel = undefined;
    this.#_url = (this.#_URL.endsWith('websid') ? this.#_URL : (this.#_URL + '/websid'));
    this.usbsid = new USBSIDBackendAdapter();
    this._indebug = false;
    this.#listenFor();
  }

  _debug_player(...message)
  {
    if (this._indebug) {
      console.info('%cPLAYER: ', 'background: #00aeffff; color: #000000ff; font-weight: bold;', ...message);
    }
  }

  #listenFor()
  {
    const el = document.querySelector("#websiddevice-connect");
    el.addEventListener('click', this.Connect.bind(this), false);
  }

  #promiseState(p) {
    const t = {};
    return Promise.race([p, t])
      .then(v => (v === t)? "pending" : "fulfilled", () => "rejected");
  }

  setTrack(track)
  {
    this.track = track;
  }

  getTrack()
  {
    return this.track;
  }

  getPathFilename()
  {
    this.this._debug_player(`Path: ${this.#_path} Filename: ${this.#_filename}`);
    return [this.#_path, this.#_filename];
  }
  setPath(path)
  {
    this.#_path = (path.startsWith('http') ? path : (this.#_URL + path));
  }
  setFilename(filename)
  {
    this.#_filename = filename;
  }
  setSIDPath()
  {
    if(this.#_path == "undefined"
      || this.#_filename == "undefined"
    ) {
      console.error(`path ${this.#_path} or filename ${this.#_filename} not set!`);
      return;
    }
    this.#_sidpath = (this.#_path + this.#_filename);
  }
  setPathFilename(path, filename)
  {
    this.setPath(path);
    this.setFilename(filename);
    this.setSIDPath();
  }

  StartWorker()
  {
    if (typeof(w) == "undefined") {
      this._debug_player("Start worker...");
      this.#_worker = new Worker(this.#_url + "/worker.js");
      this.#_worker.onmessage = function(event) {
        // document.getElementById("result").innerHTML = event.data;
        this._debug_player("W: ", event.data);
      };
    } else {
      this._debug_player("worker already started...");
    }
  }

  StopWorker()
  {
    if (typeof(this.#_worker) != "undefined") {
      this.#_worker.terminate();
      this.#_worker = undefined;
      this._debug_player("worker stopped...");
    } else {
      this._debug_player("worker not started...");
    }
  }

  Connect()
  {
    this._debug_player("connecting...");
    this.usbsid.findDevice();
  //   navigator.usb.requestDevice(
  //     {filters:
  //       [
  //         {
  //           vendorId: 0xcafe,
  //           productId: 0x4011,
  //         },
  //       ],
  //     }
  //   );
  }

  async #Command(command)
  {
    this._debug_player("Command: ", command);
    if (this.#_channel == undefined) {
      this.#_channel = new BroadcastChannel('player');
      this.#_channel.onmessage = function (m) {
        this._debug_player('Received from worker ', m.data);
        if (m.data.song) {
          this._debug_player("song: ", m.data.song)
          this.songInfo = m.data.song;
        }
      }.bind(this);
    }
    switch (command) {
      case "init":
        await this.#_channel.postMessage({
          cmd: command,
          url: this.#_sidpath
        });
        break;
      case "start":
        await this.#_channel.postMessage({
          cmd: command,
          filename: this.#_filename,
          track: (this.track != undefined ? this.track : 0),
          pbs: 2048
        });
        break;
      default:
        await this.#_channel.postMessage({
          cmd: command
        });
        break;
    }
  }

  async Cmd(cmd)
  {
    await this.#Command(cmd);
  }

  AutoPlay(){
    // setTimeout(() => {connect();}, 200);
    setTimeout(() => {this.StartWorker();}, 400);
    setTimeout(() => {this.#Command(1);}, 500);
    setTimeout(() => {this.#Command(2);}, 1000);
    setTimeout(() => {this.#Command(3);}, 1500);
    setTimeout(() => {this.#Command(4);}, 2000);
  }

}

/*
  var em = new USPLayer()
  em.setPathFilename(em.#_URL+em.#_PATH+'digitunes/','Look_Sharp.sid')
  em.getPathFilename()
  em.StartWorker()
  em.Cmd('init')
  em.Cmd('usb')
  em.Cmd('start')
  em.Cmd('run')
  em.StopWorker()
*/

/*
navigator.usb.requestDevice({filters: [
{
  vendorId: 0xcafe,
  productId: 0x4011,
},
],})
*/

/*
loadSID(SIDPATH + filename);
filedata
inittest();
setsongoptions();
updateSongInfo();

*/
