/*
 * USBSID-Player: a cycle exact C64 SID player for USBSID-Pico, for command
 * line playback, for embedding on RP2350 (Pico2), and in a browser.
 *
 * web/usbsid-transport.js
 * Which way to talk to the board, and what a transport has to provide.
 *
 * There are two, and they are not interchangeable per browser:
 *
 *   usbsid-webusb.js     Chromium only. The vendor interface, endpoints
 *                        0x04/0x84. Measured, tuned, and what every board that
 *                        has played from a browser has played from.
 *   usbsid-webserial.js  Firefox 151 and later, and Chromium too. The CDC ACM
 *                        interface, endpoints 0x02/0x82, which is the one the
 *                        command line player has always used. Measured on a
 *                        board 2026-08-10: equal to WebUSB on byte rate, better
 *                        on queue depth and worst drain gap, never worse.
 *
 * WebUSB still wins where both exist, and after the measurement the reason has
 * changed. It is no longer that it is the faster or the better known path: on
 * the numbers there is nothing to choose. It is that **WebUSB claims the vendor
 * interface and leaves the CDC one alone**, so the command line player can still
 * open the board while a browser has it. Web Serial takes the CDC interface,
 * which is the one the CLI wants, and getting it back costs a replug. Preferring
 * WebUSB in Chromium keeps that out of the way of anybody switching between the
 * two players, and it is the only difference the measurement did not erase.
 *
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Player)
 * File author: LouD
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

import { USBSIDWebUSBTransport }    from './usbsid-webusb.js';
import { USBSIDWebSerialTransport } from './usbsid-webserial.js';

export { USBSIDWebUSBTransport, USBSIDWebSerialTransport };

/**
 * What the player calls on a transport. Neither class inherits this: the call
 * sites duck type, which is what JavaScript wants, and the value of writing it
 * down is that a second implementation has something to be checked against.
 *
 * Required:
 *   get isOpen        boolean
 *   get productName   string, may be empty
 *   connect()         async, needs a user gesture, shows a picker
 *   connectGranted()  async, no picker, false when nothing is granted yet.
 *                     This is what lets the worker re-acquire what the page
 *                     was granted, which is how the emulation and the writes
 *                     live on the same thread.
 *   disconnect()      async
 *   writeCycled(reg, val, cycles)   the hot path. reg is physical, chip folded
 *                     in: $00-$1f chip one, $20-$3f chip two, and so on.
 *                     cycles is the gap before this write, never over $ffff.
 *   flush()           frame boundary, push whatever is held back
 *   reset()           drop anything half assembled
 *   resetSID()        drop the backlog and silence the chip
 *   mute() / unmute()
 *   setClock(rateId)  see CLOCK
 *   readBoardConfig() async, { sidsSocketOne, sidsSocketTwo, fmoplSid } or null
 *   get queueDepth / get queueLimit   the player's backpressure
 *   resetUsbStats() / usbStats()
 *   onWrite           settable, called (reg, value) per write, or null
 *
 * Optional, and guarded at every call site:
 *   probe()               async, is this really the board's SID port
 *   nosids                settable chip count
 *   selectOutputByName()  the MIDI transports only
 *   playbackStart()       the MIDI transports only
 */

/** Clock ids, the same order the firmware's own table uses. */
export const CLOCK = { DEFAULT: 0, PAL: 1, NTSC: 2, DREAN: 3, NTSC2: 4 };

/** @returns {'webusb'|'webserial'|null} what this browser can do. */
export function availableTransport() {
  if (typeof navigator === 'undefined') return null;
  if (navigator.usb) return 'webusb';
  if (navigator.serial) return 'webserial';
  return null;
}

/**
 * What to call the transport in the interface.
 *
 * Worth naming rather than leaving as "USB", because the two are not
 * interchangeable from the user's side: WebUSB takes the vendor interface and
 * leaves the CDC one alone, so the command line player can still open the board.
 * Web Serial takes the CDC interface, which is the one the command line player
 * wants, so the two cannot both have it and using the CLI costs a replug before
 * the browser can have it back.
 *
 * @param {'webusb'|'webserial'|null} kind  defaults to this browser's
 * @param {boolean} withInterface  append which USB interface it uses
 */
export function transportLabel(kind = availableTransport(), withInterface = false) {
  if (kind === 'webusb')    return withInterface ? 'WebUSB (vendor itf)'   : 'WebUSB';
  if (kind === 'webserial') return withInterface ? 'Web Serial (CDC itf)'  : 'Web Serial';
  return 'unsupported';
}

/**
 * Build the transport for this browser.
 *
 * @param {object} opts  passed to the chosen constructor. `opts.device` forces
 *   WebUSB, because it is an already open WebUSB device to share. `opts.prefer`
 *   ('webusb' or 'webserial') overrides the choice, which is how bench.html
 *   compares the two in one browser.
 * @returns a transport, or null when the browser can do neither.
 */
export function createTransport(opts = {}) {
  const { prefer, ...rest } = opts;

  /* A shared device is a WebUSB device by definition. */
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

/* Which one this is, on the instance. A caller across a worker boundary cannot
 * read the constructor name back, and a bench that guesses would report the
 * wrong transport the moment `prefer` and the browser default disagree. */
function tag(t, kind) { t.kind = kind; return t; }

/** A sentence for the user when neither API is there. */
export function unsupportedMessage() {
  return 'This browser cannot reach USB devices. Use Chrome or Edge 89 and ' +
         'later, or Firefox 151 and later.';
}
