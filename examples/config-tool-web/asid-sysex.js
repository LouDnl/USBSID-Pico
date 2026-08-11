/*
 * asid-sysex.js
 * The MIDI sysex and shared globals that outlived jsSID.
 *
 * WHY THIS FILE EXISTS
 *
 * Hermit's jsSID engine was replaced by USBSID-Player (ResidFp) as the app's
 * software playback mode, and jsSID-webusb.js went with it. But that file was
 * never only an engine: it also carried the sysex helper the config tool sends
 * to a board over MIDI, and the handful of globals usbsid-app.js assigns to.
 * Deleting it outright would have taken those with it, and because the call
 * site is `typeof` guarded, the sysex button would simply have stopped doing
 * anything rather than failing loudly.
 *
 * So this is the part that was still in use, lifted out unchanged, and nothing
 * else. The ASID register buffers, the OPL/FM register packing, the MIDI port
 * enumeration and the emulator itself all went: the ASID playback path is now
 * usplayer/asid-midi.js, and the MIDI output picker is populated by
 * usbsid-app.js from its own requestMIDIAccess().
 *
 * ATTRIBUTION. The sysex command below comes from jsSID-webusb.js, whose
 * lineage is:
 *   jsSID by Hermit (Mihaly Horvath), 2016, http://hermit.sidrip.com
 *   modified by JCH for DeepSID
 *   ASID support added by Thomas Jansson for DeepSID
 *   WebUSB support added by LouD
 *
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 *
 * Copyright (c) 2026 LouD
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
 */

/* The MIDI output the sysex goes to, and the access object it came from.
 *
 * Assigned by usbsid-app.js, which owns the picker and the MIDIAccess. Declared
 * here rather than there because both of the app's MIDI paths read them and
 * because they were declared here before, so nothing about the order in which
 * the scripts load has to change. */
var selectedMidiOutput = null;
var midiAccessObj = null;

/* Sysex */

const SYSEX_BUFFER_SIZE = 8; /* 0xF0, 0x19, 0, 0, 0 ,0, 0, 0xF7 */
var sysexOutBuffer = new Uint8Array(SYSEX_BUFFER_SIZE);

/**
 * Send a config command to the board over MIDI system exclusive.
 *
 * This is how ASID mode reaches settings that have no ASID message of their
 * own, the mono/stereo audio switch among them: over WebUSB and Web Serial the
 * player has a command for it, over MIDI it goes through here.
 *
 * @param {number} command config command
 * @param {number} a       struct setting, e.g. socketOne, clock_rate, or an
 *                         additional command
 * @param {number} b       setting entry, e.g. dualsid
 * @param {number} c       new value
 * @param {number} d       reserved
 */
function sysexCommand(command, a = 0, b = 0, c = 0, d = 0) {
  if (!selectedMidiOutput) return;   /* no output picked: nowhere to send it */
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

/* Globals usbsid-app.js assigns to.
 *
 * It runs under 'use strict', where assigning to an undeclared name throws
 * rather than creating a global, so these have to exist before it does. They
 * were jsSID's, which is why they read like an engine's state and not like an
 * app's: `webusb` in particular is the write shim the app overrides in
 * onDeviceConnected(). Nothing calls that shim any more now that the engine is
 * gone, but the override still runs, and it must not throw. */
let port,
    savedport;
let webusbconnected = false,
    webusbplaying = false;
let configavailable = false;
var webusb = {};
