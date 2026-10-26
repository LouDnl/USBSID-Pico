/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico,
 * command line playback, websites and for embedding on RP2040/RP2350.
 *
 * usbsid-transport.js
 * Transport selection and transport interface
 *
 *   usbsid-webusb.js     Chromium only, vendor interface, endpoints 0x04/0x84
 *   usbsid-webserial.js  Firefox 151+ and Chromium, CDC ACM interface, endpoints 0x02/0x82
 *
 * Byte rate is equal on both, Web Serial has better queue depth and drain gap
 * WebUSB is preferred, it leaves the CDC interface free for the command line player
 * Web Serial takes the CDC interface and getting it back costs a replug
 *
 * This file is part of USBSID-Player (https://github.com/LouDnl/USBSID-Player)
 * File author: LouD
 *
 * Copyright (c) 2026 LouD
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

import { USBSIDWebUSBTransport }    from './usbsid-webusb.js';
import { USBSIDWebSerialTransport } from './usbsid-webserial.js';

export { USBSIDWebUSBTransport, USBSIDWebSerialTransport };

/**
 * Transport interface the player calls (duck typed, no base class)
 *
 * Required:
 *   get isOpen        boolean
 *   get productName   string, may be empty
 *   connect()         async, needs a user gesture, shows a picker
 *   connectGranted()  async, no picker, false when nothing is granted yet
 *                     lets the worker re-acquire what the page was granted
 *   disconnect()      async
 *   writeCycled(reg, val, cycles)   hot path, reg has the chip folded in
 *                     ($00-$1f chip one, $20-$3f chip two, etc)
 *                     cycles is the gap before this write, max $ffff
 *   flush()           frame boundary, push held back writes
 *   reset()           drop anything half assembled
 *   resetSID()        drop the backlog and silence the chip
 *   mute() / unmute()
 *   setClock(rateId)  see CLOCK
 *   readBoardConfig() async, { sidsSocketOne, sidsSocketTwo, fmoplSid } or null
 *   get queueDepth / get queueLimit   the player's backpressure
 *   resetUsbStats() / usbStats()
 *   onWrite           settable, called (reg, value) per write, or null
 *
 * Optional, guarded at the call site:
 *   probe()               async, check this is the board's SID port
 *   nosids                settable chip count
 *   selectOutputByName()  the MIDI transports only
 *   playbackStart()       the MIDI transports only
 */

/* Clock ids, same order as the firmware table */
export const CLOCK = { DEFAULT: 0, PAL: 1, NTSC: 2, DREAN: 3, NTSC2: 4 };

/* Returns 'webusb', 'webserial' or null for this browser */
export function availableTransport() {
  if (typeof navigator === 'undefined') return null;
  if (navigator.usb) return 'webusb';
  if (navigator.serial) return 'webserial';
  return null;
}

/**
 * Label for the transport in the UI
 *
 * @param {'webusb'|'webserial'|null} kind  defaults to this browser
 * @param {boolean} withInterface  append the USB interface
 */
export function transportLabel(kind = availableTransport(), withInterface = false) {
  if (kind === 'webusb')    return withInterface ? 'WebUSB (vendor itf)'   : 'WebUSB';
  if (kind === 'webserial') return withInterface ? 'Web Serial (CDC itf)'  : 'Web Serial';
  return 'unsupported';
}

/**
 * Build the transport for this browser, null when neither API exists
 *
 * @param {object} opts  passed to the constructor, opts.device forces WebUSB
 *   (shared open device), opts.prefer ('webusb' or 'webserial') overrides the choice
 */
export function createTransport(opts = {}) {
  const { prefer, ...rest } = opts;

  /* A shared device is a WebUSB device */
  if (rest.device) return tag(new USBSIDWebUSBTransport(rest), 'webusb');

  const want = prefer || availableTransport();
  if (want === 'webusb') {
    if (typeof navigator === 'undefined' || !navigator.usb) return null;
    return tag(new USBSIDWebUSBTransport(rest), 'webusb');
  }
  if (want === 'webserial') {
    if (typeof navigator === 'undefined' || !navigator.serial) return null;
    return tag(new USBSIDWebSerialTransport(rest), 'webserial');
  }
  return null;
}

/* Store the kind on the instance, the constructor name is lost across a worker boundary */
function tag(t, kind) { t.kind = kind; return t; }

/* Message for when neither API is there */
export function unsupportedMessage() {
  return 'This browser cannot reach USB devices. Use Chrome or Edge 89 and ' +
         'later, or Firefox 151 and later.';
}
