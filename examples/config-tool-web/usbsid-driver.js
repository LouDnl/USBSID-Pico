/**
 * USBSID-Pico WebUSB Driver
 * Direct WebUSB implementation (no worker) for config + playback in same page.
 * Updated to match config.h command set.
 */

'use strict';

/* WebUSB constants */
const CDC_CLASS     = 0x0A;
const DEVICE_CLASS  = 0xFF;
const CTRL_TRANSFER = 0x22;
const CTRL_ENABLE   = 0x01;
const CTRL_DISABLE  = 0x00;

/* Device identity */
const USBSID_VID = 0xcafe;
const USBSID_PID = 0x4011;

/* Buffer / packet constants */
const BUFFER_SIZE       = 64;
const MAX_PACKET_SIZE   = 64;

/**
 * How long to wait for the board-information reads at connect.
 *
 * The socket configuration, the SID count and the FM/OPL slot were all failing on
 * the 500 ms the other reads use, while the same commands answered every time from
 * the console with no limit at all. The firmware receives them and replies either
 * way, which the UART shows (`READ_SOCKETCFG` then `[VDR] TX 12`), so the reply was
 * simply arriving after the race had given up.
 *
 * Only these three, and not the 500 ms default: they run once at connect, nothing
 * waits on them, and a board whose firmware predates them still has to be allowed
 * to say nothing rather than hang.
 */
/* No longer used for the board reads: see configReadNoRace(). Kept because
 * configCmdRead() still takes a timeout for anything that genuinely may go
 * unanswered.
 *
 * Anything still using the raced read carries a hazard worth knowing: one timeout
 * abandons a transferIn that cannot be cancelled, and every read after it in the
 * session gets the previous question's answer. Only GET_CLOCK and GET_AUDIO still
 * do, both from the config panel rather than the connect path. If either ever
 * misbehaves, unrace it too rather than lengthening its timeout. */
const BOARD_READ_TIMEOUT_MS = 3000;
const MAX_WRITE_BYTES   =  3;   /* 1 cmd, 1 reg, 1 val */
const MAX_CYCLED_BYTES  =  5;   /* 1 cmd, 1 reg, 1 val, cycles_hi, cycles_lo */
const MAX_WRITE_BUFFER  = 63;   /* 1 cmd byte, 62 / 2 writes */
const MAX_CYCLED_BUFFER = 61;   /* 1 cmd byte, 60 / 4 cycled writes */

/* Command byte type (top 2 bits of byte 0) */
const WRITE        = 0;   /* 0b00 << 6 = 0x00 */
const READ         = 1;   /* 0b01 << 6 = 0x40 */
const CYCLED_WRITE = 2;   /* 0b10 << 6 = 0x80 */
const COMMAND      = 3;   /* 0b11 << 6 = 0xC0 */

/* Command IDs (lower 6 bits when type=COMMAND) */
const PAUSE        = 10;  /* 0x0A */
const UNPAUSE      = 11;  /* 0x0B */
const MUTE         = 12;  /* 0x0C */
const UNMUTE       = 13;  /* 0x0D */
const RESET_SID    = 14;  /* 0x0E */
const DISABLE_SID  = 15;  /* 0x0F */
const ENABLE_SID   = 16;  /* 0x10 */
const CLEAR_BUS    = 17;  /* 0x11 */
const CONFIG       = 18;  /* 0x12 - used as sub-command prefix */
const RESET_MCU    = 19;  /* 0x13 */
const BOOTLOADER   = 20;  /* 0x14 */

/* WebUSB interface commands */
const WEBUSB_COMMAND  = 0xFF;
const WEBUSB_RESET    = 0x15;
const WEBUSB_CONTINUE = 0x16;

/* Config sub-commands (byte 1 when byte0 = COMMAND|CONFIG) */
const RESET_USBSID    = 0x20;

const READ_CONFIG     = 0x30;
const APPLY_CONFIG    = 0x31;
const SET_CONFIG      = 0x32;  /* (was STORE_CONFIG in old driver) */
const SAVE_CONFIG     = 0x33;
const SAVE_NORESET    = 0x34;
const RESET_CONFIG    = 0x35;
const WRITE_CONFIG    = 0x36;
const READ_SOCKETCFG  = 0x37;
const RELOAD_CONFIG   = 0x38;
const READ_NUMSIDS    = 0x39;
const READ_FMOPLSID   = 0x3A;
const READ_CONFIGACK  = 0x3F;

const SINGLE_SID      = 0x40;
const DUAL_SID        = 0x41;
const QUAD_SID        = 0x42;
const TRIPLE_SID      = 0x43;
const TRIPLE_SID_TWO  = 0x44;
const MIRRORED_SID    = 0x45;
const DUAL_SOCKET1    = 0x46;
const DUAL_SOCKET2    = 0x47;
const DUAL_FLIPPED    = 0x48;
const QUAD_FLIPPED    = 0x49;
const QUAD_MIXED      = 0x4A;
const QUAD_FLIPMIX    = 0x4B;
const HOTFLIP_SOCKETS = 0x4F;

const SET_CLOCK       = 0x50;
const DETECT_SIDS     = 0x51;
const TEST_ALLSIDS    = 0x52;
const TEST_SID1       = 0x53;
const TEST_SID2       = 0x54;
const TEST_SID3       = 0x55;
const TEST_SID4       = 0x56;
const GET_CLOCK       = 0x57;
const LOCK_CLOCK      = 0x58;
const STOP_TESTS      = 0x59;
const DETECT_CLONES   = 0x5A;
const AUTO_DETECT     = 0x5B;

const LOAD_MIDI_STATE  = 0x60;
const SAVE_MIDI_STATE  = 0x61;
const RESET_MIDI_STATE = 0x63;

const USBSID_VERSION  = 0x80;
const US_PCB_VERSION  = 0x81;
const RESTART_BUS     = 0x85;
const RESTART_BUS_CLK = 0x86;
const SYNC_PIOS       = 0x87;
const TOGGLE_AUDIO    = 0x88;
const SET_AUDIO       = 0x89;
const LOCK_AUDIO      = 0x90;
const GET_AUDIO       = 0x91;

const FPGASID         = 0xA0;
const SKPICO          = 0xA1;
const ARMSID          = 0xA2;
const PDSID           = 0xA3;

const UPLOAD_SID_START = 0xD0;
const UPLOAD_SID_DATA  = 0xD1;
const UPLOAD_SID_END   = 0xD2;
const UPLOAD_SID_SIZE  = 0xD3;
/* Max playtime for the tune just uploaded, in milliseconds. Without it the
   onboard player runs for five minutes. Send it after UPLOAD_SID_END. */
const UPLOAD_SID_PLAYTIME = 0xD4;
const SID_PLAYER_TUNE  = 0xE0;
const SID_PLAYER_START = 0xE1;
const SID_PLAYER_STOP  = 0xE2;
const SID_PLAYER_PAUSE = 0xE3;
const SID_PLAYER_NEXT  = 0xE4;
const SID_PLAYER_PREV  = 0xE5;
const SID_PLAYER_TWO   = 0xE6;
const SID_PLAYER_FFWD  = 0xE7;   /* non functional, see playerFfwd() */
const SID_PLAYER_MUTE  = 0xE9;
const SID_PLAYER_MUTED = 0xEA;
const SID_PLAYER_TIME  = 0xEB;

const CONFIG_ACK       = 0xFA;  /* Acknowledge the current configuration and switch on regulators (v1.5+ boards only) */
const SOCKET_DETECT    = 0xFD;  /* Disable/enable automatic socket change detection on boot (v1.5+ boards only) */

/* Config write sub-types */
const FULL_CONFIG   = 0x00;
const SOCKET_CONFIG = 0x10;
const MIDI_CONFIG   = 0x20;
const MIDI_CCVALUES = 0x30;

/* Clock rate identifiers */
const clock_rates = {
  DEFAULT: 0,  /* 1000000 Hz */
  PAL:     1,  /* 985248 Hz */
  NTSC:    2,  /* 1022727 Hz */
  DREAN:   3,  /* 1023440 Hz */
};

/* Delay helper */
const us_delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/**
 * USBSIDDevice - direct WebUSB, no worker
 *
 */
class USBSIDDevice {
  constructor() {
    this._device     = null;
    this._epOut      = null;
    this._epIn       = null;
    this._ifaceNum   = null;
    this._isOpen     = false;
    this._debug      = false;
    /* Best-effort close on page hide/refresh.
     * Chrome does not cancel HCI-queued bulk OUT transfers on same-origin
     * page reload - old SID write packets from unawaited write() calls
     * keep arriving at the device for seconds after reconnect.
     * Calling device.close() from pagehide cancels Chrome's pending queue
     * before the page unloads, so the new session starts with a clean pipe. */
    if (typeof window !== 'undefined') {
      window.addEventListener('pagehide', () => this._syncClose());
      window.addEventListener('beforeunload', () => this._syncClose());
    }
  }

  /** Synchronous best-effort close called from pagehide (cannot await). */
  _syncClose() {
    if (!this._device) return;
    try {
      if (this._ifaceNum !== null) this._device.releaseInterface(this._ifaceNum).catch(() => {});
      this._device.close().catch(() => {});
    } catch (_) {}
    this._isOpen   = false;
    this._device   = null;
    this._epOut    = null;
    this._epIn     = null;
    this._ifaceNum = null;
  }

  /* Connection */

  /** Request device via browser USB picker */
  async connect() {
    if (this._isOpen) return true;
    try {
      this._device = await navigator.usb.requestDevice({
        filters: [{ vendorId: USBSID_VID, productId: USBSID_PID }]
      });
      return await this._open();
    } catch (e) {
      this._log('connect failed:', e);
      return false;
    }
  }

  /** Close and immediately reopen - cancels all pending transfers, then
   *  reopens with a fresh endpoint state. Used to recover from a leaked
   *  transferIn after a 2 timeout. */
  async _reopen() {
    const dev = this._device;    /* save ref before close() nulls it */
    await this.close();
    if (!dev) return false;
    this._device = dev;          /* restore: device is closed but reusable */
    return await this._open();
  }

  /** Re-open a previously-permitted device (no picker).
   *  Hard refresh (CTRL+F5) can leave Chrome's WebUSB session in a stale
   *  "permitted, attached, URB queue dead" limbo: open() resolves OK,
   *  transferOut() resolves OK, but no URB ever hits the wire.  Force a
   *  release/re-acquire cycle by opening then closing the device once
   *  before the real _open() so Chrome discards the stale handle. */
  async reconnect() {
    if (this._isOpen) return true;
    try {
      const devices = await navigator.usb.getDevices();
      const dev = devices.find(d => d.vendorId === USBSID_VID && d.productId === USBSID_PID);
      if (!dev) return false;
      this._device = dev;
      try { await dev.open(); await dev.close(); } catch (_) {}
      return await this._open();
    } catch (e) {
      this._log('reconnect failed:', e);
      return false;
    }
  }

  /** Internal: open, claim interface, enable */
  async _open() {
    try {
      await this._device.open();
      if (this._device.configuration === null) {
        await this._device.selectConfiguration(1);
      }
      /* Find DEVICE_CLASS (0xFF) interface */
      this._ifaceNum = null;
      this._epOut    = null;
      this._epIn     = null;
      for (const iface of this._device.configuration.interfaces) {
        for (const alt of iface.alternates) {
          if (alt.interfaceClass === DEVICE_CLASS) {
            this._ifaceNum = iface.interfaceNumber;
            for (const ep of alt.endpoints) {
              if (ep.direction === 'out') this._epOut = ep.endpointNumber;
              if (ep.direction === 'in')  this._epIn  = ep.endpointNumber;
            }
          }
        }
      }
      if (this._ifaceNum === null) {
        this._log('DEVICE_CLASS interface not found');
        await this._device.close();
        return false;
      }
      await this._device.claimInterface(this._ifaceNum);
      await this._device.selectAlternateInterface(this._ifaceNum, 0);
      try { await this._device.clearHalt('out', this._epOut); } catch (_) {}
      try { await this._device.clearHalt('in',  this._epIn);  } catch (_) {}
      await this._device.controlTransferOut({
        requestType: 'class',
        recipient:   'interface',
        request:     CTRL_TRANSFER,
        value:       CTRL_ENABLE,
        index:       this._ifaceNum,
      });
      /* Short settle delay: selectAlternateInterface sends SET_INTERFACE which
       * causes TinyUSB to reset the bulk endpoints. On fast reconnects (page
       * refresh, emulator switch) the first OUT packet can arrive while the
       * device is still processing SET_INTERFACE and gets silently dropped.
       * 100ms is imperceptible to the user but sufficient for the device to
       * finish endpoint reset before the first bulk transfer. */
      await us_delay(100);
      this._isOpen = true;
      this._log('opened, ifaceNum', this._ifaceNum, 'epOut', this._epOut, 'epIn', this._epIn);
      return true;
    } catch (e) {
      this._log('_open failed:', e);
      return false;
    }
  }

  async close() {
    /* Always attempt full teardown even if _isOpen is false (e.g. partial open) */
    if (this._device) {
      try {
        if (this._ifaceNum !== null) await this._device.releaseInterface(this._ifaceNum);
        await this._device.close();
      } catch (_) {}
    }
    this._isOpen   = false;
    this._device   = null;
    this._epOut    = null;
    this._epIn     = null;
    this._ifaceNum = null;
    this._log('closed');
  }

  get isOpen()          { return this._isOpen; }
  get productName()     { return this._device ? (this._device.productName     || '') : ''; }
  get manufacturerName(){ return this._device ? (this._device.manufacturerName || '') : ''; }

  /* Raw I/O */

  /** Write a Uint8Array or plain Array to the OUT endpoint */
  async write(data) {
    if (!this._isOpen) return;
    const buf = data instanceof Uint8Array ? data : new Uint8Array(data);
    try {
      /* await */ this._device.transferOut(this._epOut, buf);
    } catch (e) {
      this._log('write error:', e);
    }
  }

  /** Write then read one packet from the IN endpoint */
  async writeAndRead(data, readLen = MAX_PACKET_SIZE) {
    if (!this._isOpen) return null;
    const buf = data instanceof Uint8Array ? data : new Uint8Array(data);
    try {
      await this._device.transferOut(this._epOut, buf);
      const result = await this._device.transferIn(this._epIn, readLen);
      return new Uint8Array(result.data.buffer);
    } catch (e) {
      this._log('writeAndRead error:', e);
      return null;
    }
  }

  /** Read one packet from the IN endpoint (after write was already done) */
  async read(readLen = MAX_PACKET_SIZE) {
    if (!this._isOpen) return null;
    try {
      const result = await this._device.transferIn(this._epIn, readLen);
      return new Uint8Array(result.data.buffer);
    } catch (e) {
      this._log('read error:', e);
      return null;
    }
  }

  /** Clear endpoint halt/toggle state (e.g. after switching away and back) **/
  async clearHaltEndpoints() {
    if (!this._isOpen) return;
    try { await this._device.clearHalt('out', this._epOut); } catch (_) {}
    try { await this._device.clearHalt('in',  this._epIn);  } catch (_) {}
  }

  /** Send a ZLP to the OUT endpoint to flush any buffered data */
  async clearEndpoints() {
    if (!this._isOpen) return;
    try { await this._device.transferOut(this._epOut, new Uint8Array(0)); } catch (_) {}
  }

  /* High-level helpers */

  /** Build command byte: type (2-bit) | payload (6-bit) */
  cmd(type, payload) {
    return ((type & 0x3) << 6) | (payload & 0x3F);
  }

  /** Send a raw 6-byte config command */
  async configCmd(sub, b2 = 0, b3 = 0, b4 = 0, b5 = 0) {
    await this.write([this.cmd(COMMAND, CONFIG), sub, b2, b3, b4, b5]);
  }

  /** Send config command and read response packets.
   *  On timeout a transferIn is leaked (WebUSB has no cancel API). We recover
   *  by closing and reopening the device (which aborts all pending transfers),
   *  then retrying the command once on a clean connection. */
  /**
   * @param timeoutMs how long to wait for the reply. 500 is enough for a board
   *   doing nothing; the board-information reads at connect need more, see
   *   BOARD_READ_TIMEOUT_MS.
   */
  async configCmdRead(sub, b2 = 0, b3 = 0, b4 = 0, b5 = 0, numBytes = MAX_PACKET_SIZE,
                      timeoutMs = 500) {
    /* One at a time, whoever asks.
     *
     * A config read is a write followed by a read, and the endpoint has no way of
     * saying which reply belongs to which request. Two of these overlapping means
     * either can take the other's answer. Reading the socket configuration, the
     * SID count and the FM/OPL slot one after another produced three zeros and
     * then a fourth read that returned one of *their* replies, which is how this
     * was found. Serialised, so the pairing holds. */
    const mine = this._cfgReadChain = (this._cfgReadChain || Promise.resolve())
      .then(() => this._configCmdReadOnce(sub, b2, b3, b4, b5, numBytes, timeoutMs),
            () => this._configCmdReadOnce(sub, b2, b3, b4, b5, numBytes, timeoutMs));
    return await mine;
  }

  async _configCmdReadOnce(sub, b2, b3, b4, b5, numBytes, timeoutMs) {
    if (!this._isOpen) return [];
    const cmdBuf = new Uint8Array([this.cmd(COMMAND, CONFIG), sub, b2, b3, b4, b5]);
    const packets = [];

    /* Clear a skew left by an earlier timeout before asking anything.
     *
     * A raced read that times out abandons its transferIn rather than cancelling
     * it, because WebUSB has no cancel. The board's reply still arrives and is
     * handed to the next read, so from that moment every read is one reply behind
     * and they all time out. It is not hypothetical and it is not even ours to
     * begin with: readConfigAck() says its first attempt may time out on a freshly
     * opened endpoint, and the log shows exactly that, followed by the socket
     * configuration, the SID count and the FM/OPL slot all failing in turn.
     *
     * The late reply is genuinely in flight, so one read with nothing sent first
     * consumes it. Raced, because the alternative is waiting for ever when the
     * timeout was a command the firmware does not implement, and given a long
     * enough limit that a board which is going to answer has answered. */
    if (this._cfgReadSkew) {
      this._cfgReadSkew = false;
      try {
        const stale = await Promise.race([
          this._device.transferIn(this._epIn, MAX_PACKET_SIZE),
          new Promise((res) => setTimeout(() => res(null), 1500)),
        ]);
        if (stale) {
          usbsidLog('configCmdRead: resynchronised, discarded a late reply');
        } else {
          /* Nothing came, so the earlier timeout was a question this board does
           * not answer. Say so once: the reads are in step, there is simply no
           * answer to that one. */
          usbsidLog('configCmdRead: no late reply, the board does not answer that command');
        }
      } catch (_) { /* closed under us; the next read will report it */ }
    }

    try {
      await this._device.transferOut(this._epOut, cmdBuf);
    } catch (e) {
      usbsidLog('configCmdRead write error:', e.message || e);
      return packets;
    }
    try {
      const r = await Promise.race([
        this._device.transferIn(this._epIn, numBytes),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error('configCmdRead timeout')), timeoutMs)
        ),
      ]);
      packets.push(new Uint8Array(r.data.buffer));
    } catch (e) {
      /* Remember it: the next read clears up after this one. The abandoned
       * transferIn cannot be cancelled, so its reply is still coming. */
      this._cfgReadSkew = true;
      usbsidLog('configCmdRead error:', e.message || e);
    }
    return packets;
  }

  /**
   * A config read that cannot leak, for commands the board is known to answer.
   *
   * `configCmdRead()` races the read against a timeout, which is unavoidable for
   * a question some firmware does not implement: an unraced read would wait for
   * ever. The cost is that a timeout **abandons** the transferIn rather than
   * cancelling it, because WebUSB has no cancel, so the reply still arrives and is
   * handed to the next read. One timeout therefore puts every later config read
   * one reply behind, for the rest of the session.
   *
   * That is what broke the onboard player's clock. The firmware was answering
   * every request (the UART showed five `[VDR] TX 4`, which TinyUSB logs after the
   * bytes have gone to the host) while the browser timed out on all five, because
   * each reply was going to the transfer abandoned by the one before it. The first
   * abandonment is not even ours: readConfigAck() notes that its first attempt may
   * time out on a freshly opened endpoint.
   *
   * So: no race here. Only call it for a command this board certainly implements,
   * which for the onboard player's reads means only when the board has reported an
   * onboard player. Then a reply is certain and nothing is ever abandoned.
   *
   * If a skew was inherited from an earlier raced read, the first call returns
   * that stale packet and every call after it is aligned again: one wrong reading
   * rather than a session that never recovers.
   */
  async configReadNoRace(sub, len = MAX_PACKET_SIZE, b2 = 0, b3 = 0, b4 = 0, b5 = 0) {
    const mine = this._cfgReadChain = (this._cfgReadChain || Promise.resolve())
      .then(() => this._configReadNoRaceOnce(sub, len, b2, b3, b4, b5),
            () => this._configReadNoRaceOnce(sub, len, b2, b3, b4, b5));
    return await mine;
  }

  async _configReadNoRaceOnce(sub, len, b2, b3, b4, b5) {
    if (!this._isOpen) return null;
    /* Says so if a read never comes back, without abandoning it.
     *
     * Abandoning is what broke this in the first place: WebUSB cannot cancel a
     * transferIn, so every timed out read leaves a queued transfer behind and the
     * next reply feeds the oldest one. The backlog only grows, which is why reads
     * that work in isolation come back fifteen seconds late once a few have been
     * given up on. So this reports and waits: the read is serialised, so a stuck
     * one stops the others rather than burying them. */
    const stuck = setTimeout(() => {
      usbsidLog('config read 0x' + (sub & 0xff).toString(16) +
                ' has not answered after 10s, still waiting (reads are serialised,'
                + ' so nothing else will run until it does)');
    }, 10000);
    try {
      await this._device.transferOut(this._epOut,
        new Uint8Array([this.cmd(COMMAND, CONFIG), sub, b2, b3, b4, b5]));
      const r = await this._device.transferIn(this._epIn, len);
      return new Uint8Array(r.data.buffer);
    } catch (e) {
      usbsidLog('configReadNoRace error:', e.message || e);
      return null;
    } finally {
      clearTimeout(stuck);
    }
  }

  /* Config reading - returns flat Uint8Array of CONFIG_SIZE (64) bytes.
   * The firmware queues 4 transfers of 64 bytes each; TinyUSB only sends the
   * first (real config, bytes 0-63). Stale packets from prior commands may sit
   * in the endpoint buffer and be returned ahead of the real response.
   * We skip leading stale packets (zero-length, all-zero, or wrong magic) inline
   * without using Promise.race - that approach leaks pending transferIn calls
   * which then consume the real response on the next read, yielding 0 bytes. */
  async readConfig() {
    /* Through the same chain as every other config read. It has its own
     * transferOut/transferIn loop rather than going via configCmdRead(), so
     * without this it can run at the same time as one of those and the two take
     * each other's replies: one reader at a time is the only thing that keeps a
     * reply paired with its question. */
    const mine = this._cfgReadChain = (this._cfgReadChain || Promise.resolve())
      .then(() => this._readConfigOnce(), () => this._readConfigOnce());
    return await mine;
  }

  async _readConfigOnce() {
    const CONFIG_SIZE = 64;
    const CC = this.cmd(COMMAND, CONFIG);
    const cmdBuf = new Uint8Array([CC, READ_CONFIG, 0, 0, 0, 0]);
    const all = [];
    try {
      /* Send READ_CONFIG and collect response without Promise.race (which leaks
       * pending transferIn calls and causes the next read to consume the wrong packet).
       * Skip stale packets inline instead:
       *   - zero-length packets -> skip (ZLP residue)
       *   - all-zero packets   -> skip (stale empty response)
       *   - wrong magic bytes  -> skip (stale response from another command)
       * fw >= 0.7.0: full config fits in one 64-byte packet (terminator at [62..63]).
       * fw < 0.7.0:  may have sent additional packets; we break at CONFIG_SIZE so
       *              only the first valid packet is consumed - any extra packets left
       *              in the buffer are stale and skipped by magic checks in later reads.
       * WebUSB rejects all pending transferIn on disconnect, so no infinite hang. */
      await this._device.transferOut(this._epOut, cmdBuf);
      for (let i = 0; i < 8; i++) {
        const r = await this._device.transferIn(this._epIn, MAX_PACKET_SIZE);
        const chunk = new Uint8Array(r.data.buffer);
        if (all.length === 0) {
          if (chunk.length === 0) { this._log('readConfig: skipping zero-length packet'); continue; }
          if (chunk.every(b => b === 0)) { this._log('readConfig: skipping stale zero packet'); continue; }
          if (chunk[0] !== 0x30 || chunk[1] !== 127) {
            this._log('readConfig: skipping stale packet (magic', chunk[0], chunk[1] + ')');
            continue;
          }
        }
        if (chunk.length === 0) break;
        all.push(...chunk);
        if (all.length >= CONFIG_SIZE) break;
      }
    } catch (e) {
      usbsidLog('readConfig error:', e.message || e);
    }
    return new Uint8Array(all.slice(0, CONFIG_SIZE));
  }

  /* Read firmware version string */
  async readVersion() {
    usbsidLog("Reading USBSID-Pico Firmware version");
    /* Unraced: this runs at connect, and one abandoned transfer there puts
     * every later read one reply behind. See readConfigAck(). */
    const r = await this.configReadNoRace(USBSID_VERSION, MAX_PACKET_SIZE);
    if (!r || !r.length) return '';
    /* configReadNoRace() answers with the bytes themselves, not the array of
     * packets configCmdRead() returns. */
    const bytes = r;
    let s = '';
    for (let i = 0; i < bytes.length; i++) {
      if (bytes[i] === 0) break;
      s += String.fromCharCode(bytes[i]);
    }
    return s.trim();
  }

  /* Read PCB version */
  async readPCBVersion() {
    usbsidLog("Reading USBSID-Pico PCB version");
    /* Unraced: this runs at connect, and one abandoned transfer there puts
     * every later read one reply behind. See readConfigAck(). */
    const r = await this.configReadNoRace(US_PCB_VERSION, MAX_PACKET_SIZE);
    if (!r || !r.length) return '';
    /* configReadNoRace() answers with the bytes themselves, not the array of
     * packets configCmdRead() returns. */
    const bytes = r;
    let s = '';
    for (let i = 0; i < bytes.length; i++) {
      if (bytes[i] === 0) break;
      s += String.fromCharCode(bytes[i]);
    }
    return s.trim();
  }

  /* Set a single config item: group=nameval, item, value */
  async setConfigItem(group, item, value) {
    await this.configCmd(SET_CONFIG, group, item, value, 0);
  }

  /* Save config and reboot */
  async saveConfig() {
    await this.configCmd(SAVE_CONFIG);
  }

  /* Save config without reboot */
  async saveNoReset() {
    await this.configCmd(SAVE_NORESET);
  }

  /* Reset config to defaults */
  async resetConfig() {
    await this.configCmd(RESET_CONFIG);
  }

  /* Apply config from memory */
  async applyConfig() {
    await this.configCmd(APPLY_CONFIG);
  }

  /* Reload stored config from flash */
  async reloadConfig() {
    await this.configCmd(RELOAD_CONFIG);
  }

  /* Reboot MCU */
  async resetMCU() {
    await this.configCmd(RESET_USBSID);
  }

  /* Reset SID chips */
  async resetSID() {
    await this.write([this.cmd(COMMAND, RESET_SID), 0, 0, 0, 0, 0]);
  }

  /* Set clock rate (0=DEFAULT, 1=PAL, 2=NTSC, 3=DREAN) */
  async setClock(rateId) {
    await this.configCmd(SET_CLOCK, rateId);
  }

  /* Get current clock (returns packet) */
  async getClock() {
    usbsidLog("Reading clock speed");
    const r = await this.configCmdRead(GET_CLOCK, 0, 0, 0, 0, MAX_PACKET_SIZE);
    return r.length ? r[0][0] : -1;
  }

  /* Lock clock */
  async lockClock() {
    await this.configCmd(LOCK_CLOCK);
  }

  /* Preset commands */
  async setSingleSID()     { await this.configCmd(SINGLE_SID); }
  async setDualSID()       { await this.configCmd(DUAL_SID); }
  async setTripleSID()     { await this.configCmd(TRIPLE_SID); }
  async setTripleSIDTwo()  { await this.configCmd(TRIPLE_SID_TWO); }
  async setQuadSID()       { await this.configCmd(QUAD_SID); }
  async setMirroredSID()   { await this.configCmd(MIRRORED_SID); }
  async setDualSocket1()   { await this.configCmd(DUAL_SOCKET1); }
  async setDualSocket2()   { await this.configCmd(DUAL_SOCKET2); }
  async setDualFlipped()   { await this.configCmd(DUAL_FLIPPED); }
  async setQuadFlipped()   { await this.configCmd(QUAD_FLIPPED); }
  async setQuadMixed()     { await this.configCmd(QUAD_MIXED); }
  async setQuadFlipMix()   { await this.configCmd(QUAD_FLIPMIX); }
  async hotFlipSockets()   { await this.configCmd(HOTFLIP_SOCKETS); }

  /* Detection */
  async detectSIDs()       { await this.configCmd(DETECT_SIDS); }
  async detectClones()     { await this.configCmd(DETECT_CLONES); }
  async autoDetect()       { await this.configCmd(AUTO_DETECT); }
  async stopTests()        { await this.configCmd(STOP_TESTS); }

  /* Audio switch (PCB v1.3+) */
  async toggleAudio()      { await this.configCmd(TOGGLE_AUDIO); }
  async setAudio(val)      { await this.configCmd(SET_AUDIO, val); }
  async lockAudio()        { await this.configCmd(LOCK_AUDIO); }
  async getAudio() { /* NOT FINISHED IN FIRMWARE */
    usbsidLog("Reading current audio switch setting");
    const r = await this.configCmdRead(GET_AUDIO, 0, 0, 0, 0, MAX_PACKET_SIZE);
    return r.length ? r[0][0] : -1;
  }

  /* Bus control */
  async restartBus()       { await this.configCmd(RESTART_BUS); }
  async restartBusClock()  { await this.configCmd(RESTART_BUS_CLK); }
  async syncPIOs()         { await this.configCmd(SYNC_PIOS); }

  /* Read number of SIDs */
  async readNumSIDs() {
    usbsidLog("Reading number of available SID's");
    const r = await this.configReadNoRace(READ_NUMSIDS, MAX_PACKET_SIZE);
    return (r && r.length) ? r[0] : 0;
  }

  /**
   * What is actually in the two sockets.
   *
   * The board reports one byte per socket: the high nibble says whether the
   * socket is enabled, the low nibble whether it holds two chips. So a socket can
   * hold 0, 1 or 2. Byte 2 is socket one, byte 5 is socket two, which is the same
   * layout USBSID-Player's transports parse.
   *
   * @returns {{one:number, two:number}|null} null when the board will not say
   */
  async readSocketConfig() {
    const b = await this.configReadNoRace(READ_SOCKETCFG, MAX_PACKET_SIZE);
    if (!b || b.length < 6) return null;
    const chips = (byte) =>
      (((byte & 0xf0) >> 4) === 1) ? (((byte & 0x0f) === 1) ? 2 : 1) : 0;
    return { one: b.length > 2 ? chips(b[2]) : 0,
             two: b.length > 5 ? chips(b[5]) : 0 };
  }

  /* Read FMOpl SID number */
  async readFMOplSID() {
    usbsidLog("Reading FMOpl SID number");
    const r = await this.configReadNoRace(READ_FMOPLSID, MAX_PACKET_SIZE);
    return (r && r.length) ? r[0] : 0;
  }

  /* Clone config commands */
  async configFPGASID(b2, b3, b4) { await this.configCmd(FPGASID, b2, b3, b4); }
  async configSKPico(b2, b3, b4)  { await this.configCmd(SKPICO, b2, b3, b4); }
  async configARMSID(b2, b3, b4)  { await this.configCmd(ARMSID, b2, b3, b4); }
  async configPDSID()             { await this.configCmd(PDSID); }

  /* v1.5+ board features */

  /* Read config acknowledgment status - returns 1 if confirmation needed, 0 otherwise.
   * Uses transferIn(MAX_PACKET_SIZE) not transferIn(1): empirically, Chrome's WebUSB on
   * Linux does not properly complete bulk-IN URBs of < wMaxPacketSize even when the
   * firmware sends exactly 1 byte as a short packet.  transferIn(64) terminates correctly.
   * First attempt may time out if the vendor IN endpoint is not yet ready on first
   * connect; reopen cancels the leaked transferIn and the retry succeeds once the
   * endpoint is fully initialised. */
  async readConfigAck() {
    if (!this._isOpen) return 0;
    /* Unraced, and this one matters more than the others.
     *
     * It is the first read after opening, and its own note says the first attempt
     * may time out on an endpoint that has just been reset. A timeout abandons a
     * transferIn that WebUSB cannot cancel, so the reply to the *next* question
     * goes to it instead, and everything after that is one reply behind for the
     * rest of the session. That is what left READ_SOCKETCFG waiting for ever while
     * the UART showed its 12 bytes going out.
     *
     * Safe to wait: the firmware answers this on every board, write_back_data(1)
     * sits outside the PCB version guard, so a v1.0 board says "not supported" by
     * replying with a zero rather than by staying silent. */
    const r = await this.configReadNoRace(READ_CONFIGACK, MAX_PACKET_SIZE);
    return (r && r.length) ? r[0] : 0;
  }

  /* Acknowledge current config - enables socket power regulators (v1.5+).
   * Uses direct awaited transferOut (not write()) so caller can await completion
   * before issuing subsequent commands like READ_CONFIG. */
  async confirmConfig() {
    if (!this._isOpen) return;
    const cmdBuf = new Uint8Array([this.cmd(COMMAND, CONFIG), CONFIG_ACK, 0, 0, 0, 0]);
    await this._device.transferOut(this._epOut, cmdBuf);
  }

  /* Enable/disable socket change detection on boot (v1.5+).
   * val: 0 = detection enabled (default), 1 = detection disabled */
  async setSocketDetect(val) {
    await this.configCmd(SOCKET_DETECT, val & 1);
  }

  /* SID register write (plain, 3 bytes: cmd, reg, val) */
  async writeReg(regAddr, value) {
    await this.write([(WRITE << 6), regAddr, value]);
  }

  /* SID register write (with cycles, 5 bytes: cmd, reg, val, cycles hi, cycles lo) */
  async writeRegC(regAddr, value, cycles_hi, cycles_lo) {
    await this.write([(CYCLED_WRITE << 6), regAddr, value, cycles_hi, cycles_lo]);
  }

  /* Raw array write - used by player integration */
  async writeArray(arr) {
    await this.write(arr);
  }

  /* Raw array write that AWAITS transfer completion (device accepted the
   * packet). Unlike write()/writeArray() (fire-and-forget), the returned promise
   * resolves only once transferOut settles, giving the caller real USB
   * backpressure. Used by the USBSID-Player transport to serialize cycled
   * packets so a whole-frame burst cannot overflow the device buffer. */
  async writeArrayAwait(arr) {
    if (!this._isOpen) return;
    const buf = arr instanceof Uint8Array ? arr : new Uint8Array(arr);
    try {
      await this._device.transferOut(this._epOut, buf);
    } catch (e) {
      this._log('writeArrayAwait error:', e);
    }
  }

  /* Pause / unpause SID output */
  /**
   * Silence the board outright, volume register included.
   *
   * Not the same thing as muting every voice. The player's voice mute writes the
   * sustain/release and control registers of the voices it is given and never
   * touches $18, so a tune playing samples through the volume register carries on
   * regardless: muting all three voices leaves a digi audible.
   *
   * This is the firmware's own MUTE, which writes the volume nibble of $d418 to
   * zero on every chip and remembers what was there, so UNMUTE puts it back.
   */
  async muteAll()   { await this.write([this.cmd(COMMAND, MUTE),   0, 0, 0, 0, 0]); }
  async unmuteAll() { await this.write([this.cmd(COMMAND, UNMUTE), 0, 0, 0, 0, 0]); }

  async pause()   { await this.write([this.cmd(COMMAND, PAUSE),   0, 0, 0, 0, 0]); }
  async unpause() { await this.write([this.cmd(COMMAND, UNPAUSE), 0, 0, 0, 0, 0]); }

  /* Onboard player (requires ONBOARD_SIDPLAYER firmware build) */
  async playerUploadStart(fileType = 0x01) {
    await this.configCmd(UPLOAD_SID_START, fileType);
  }
  async playerUploadData(chunk) {
    const buf = new Uint8Array(MAX_PACKET_SIZE);
    buf[0] = this.cmd(COMMAND, CONFIG);
    buf[1] = UPLOAD_SID_DATA;
    buf.set(chunk.slice(0, MAX_PACKET_SIZE - 2), 2);
    await this.write(buf);
  }
  async playerUploadSize(size) {
    const hi = (size >> 8) & 0xFF;
    const lo = size & 0xFF;
    await this.configCmd(UPLOAD_SID_SIZE, hi, lo);
  }
  async playerUploadEnd() {
    await this.configCmd(UPLOAD_SID_END);
  }
  async playerLoadTune(subtune = 0) {
    /* byte[2]=0 (file ID 0 = uploaded file), byte[3]=subtune index (0-based) */
    await this.configCmd(SID_PLAYER_TUNE, 0, subtune);
  }
  async playerStart()     { await this.configCmd(SID_PLAYER_START); }
  async playerStop()      { await this.configCmd(SID_PLAYER_STOP); }
  async playerPause()     { await this.configCmd(SID_PLAYER_PAUSE); }
  async playerNext()      { await this.configCmd(SID_PLAYER_NEXT); }
  async playerPrev()      { await this.configCmd(SID_PLAYER_PREV); }
  async playerSocketTwo() { await this.configCmd(SID_PLAYER_TWO); }

  /**
   * How long the tune just uploaded should run, in milliseconds.
   *
   * Send after playerUploadEnd(). Without it the onboard player stops after five
   * minutes, which is its own default and not the tune's length. A 32 bit count,
   * most significant byte first, matching set_maxplaytime() in config.c.
   *
   * SID_PLAYER_STOP resets it back to five minutes, so it has to be sent again
   * for every tune rather than once per session.
   */
  async playerSetPlaytime(ms) {
    const v = Math.max(0, Math.round(ms)) >>> 0;
    await this.configCmd(UPLOAD_SID_PLAYTIME,
                         (v >>> 24) & 0xFF, (v >>> 16) & 0xFF,
                         (v >>> 8) & 0xFF, v & 0xFF);
  }

  /**
   * The onboard player's position, in milliseconds, or null.
   *
   * get_playtime() only samples the player while it is actually playing, so the
   * value **freezes at the last live reading** once playback stops rather than
   * dropping to zero. That is usually what you want, since a stopped tune keeps
   * showing where it stopped, but it means a reading is not by itself evidence
   * that anything is playing. UPLOAD_SID_START resets it to zero, so it cannot
   * carry a stale position from the previous tune into a new one.
   */
  async playerTime() {
    /* configReadNoRace(), not configCmdRead(): the board always answers this one,
     * and a raced read that gives up abandons its transferIn, whose reply is then
     * handed to whoever reads next.
     *
     * A whole packet, not the 4 bytes get_playtime() sends. The buffer has to be
     * big enough for the largest reply that could land in it, not the one that
     * should: with a 4 byte buffer a stray 12 byte socket configuration reply
     * comes back as `babble` and zero bytes. Only the first four are read. */
    const d = await this.configReadNoRace(SID_PLAYER_TIME, MAX_PACKET_SIZE);
    if (!d || d.length < 4) return null;
    return ((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]) >>> 0;
  }

  /**
   * Mute or unmute one voice, one whole chip, or everything.
   *
   *   chip 1..4, voice 1..3   that voice, masked on the way out
   *   chip 1..4, voice 0      that whole chip, by dropping its writes
   *   chip 0,    voice 0      every chip and every voice
   *
   * @param mute true to silence
   *
   * The two are different mechanisms and not degrees of the same one. A voice mute
   * masks the gate and the sustain and lets every other write through, so the tune
   * carries on driving the voice. A chip mute drops the chip's writes, which is the
   * only one of the two that reaches $18: a tune playing samples through the volume
   * register keeps sounding with all three of its voices muted.
   */
  async playerMute(chip, voice, mute) {
    await this.configCmd(SID_PLAYER_MUTE, chip & 0xFF, voice & 0xFF, mute ? 1 : 0);
  }

  /** Every voice of every chip. */
  async playerMuteAll(mute) {
    await this.playerMute(0, 0, mute);
  }

  /**
   * One whole chip, in one command.
   *
   * Sent as voice 0, which set_mutestate() routes to usplayer_set_chip_mute().
   * This used to send the three voices instead, because the firmware had no chip
   * form; it has one now, and unlike three voice mutes it silences the volume
   * register as well.
   */
  async playerMuteChip(chip, mute) {
    return await this.playerMute(chip, 0, mute);
  }

  /**
   * The mute state, as one bitmask per chip, bit 0 to bit 2 for voices 1 to 3.
   *
   * Reads back zeros for every chip while nothing is playing: get_mutestate()
   * only asks the player when it is running.
   */
  /**
   * The board's mute state.
   *
   * Five bytes since chip mute arrived, not four: byte 0 is the chip mask, bit 0
   * for chip one, and the per chip voice masks moved along to bytes 1 to 4. Reads
   * zeros throughout while nothing is playing, because the firmware only asks the
   * player when it is running.
   *
   * @returns {{chips:number, voices:number[]}|null}
   */
  async playerMuteState() {
    const d = await this.configReadNoRace(SID_PLAYER_MUTED, MAX_PACKET_SIZE);
    if (!d || d.length < 5) return null;
    return { chips: d[0], voices: [d[1], d[2], d[3], d[4]] };
  }

  /**
   * Fast forward on or off.
   *
   * **Does nothing.** The firmware marks the case `/* Non functional *\/` and the
   * `emu_ffwd()` call is commented out, because emu_ffwd() was itself an empty
   * stub: on the device the SID writes are the pacing, so there is nothing to
   * skip until the embedded build has a pacer. Kept so the wiring is ready, and
   * deliberately absent from the UI: a control that cannot work invites being
   * pressed repeatedly.
   */
  async playerFfwd(on) {
    await this.configCmd(SID_PLAYER_FFWD, on ? 1 : 0);
  }

  /**
   * Upload a SID/PRG file to the onboard player and start playback.
   * Matches the protocol in send_sid.c exactly - all upload packets are 64 bytes.
   * @param {Uint8Array} bytes   - raw file bytes
   * @param {number}     subtune - 1-based subtune number (default 1)
   * @param {number}     fileType - 0x01=SID, 0x02=PRG (default 0x01)
   * @param {function}   onProgress - optional callback(sent, total)
  */
  async uploadSIDFile(bytes, subtune = 1, fileType = 0x01, onProgress = null,
                      playtimeMs = 0) {
    const CMD        = this.cmd(COMMAND, CONFIG);  /* 0xD2 */
    const CHUNK      = MAX_PACKET_SIZE - 2;         /* 62 bytes of file data per packet */
    const total      = bytes.length;
    let   pkt        = new Uint8Array(MAX_PACKET_SIZE);

    /* 1. Stop any current playback */
    await this.playerStop();

    /* 2. START packet: [CMD, UPLOAD_SID_START, fileType, 0...] */
    pkt.fill(0);
    pkt[0] = CMD;  pkt[1] = UPLOAD_SID_START;  pkt[2] = fileType;
    await this._device.transferOut(this._epOut, pkt);

    /* 3. DATA packets: [CMD, UPLOAD_SID_DATA, b0..b61] - 62 bytes per packet */
    let sent = 0;
    while (sent < total) {
      pkt.fill(0);
      pkt[0] = CMD;  pkt[1] = UPLOAD_SID_DATA;
      const end = Math.min(sent + CHUNK, total);
      pkt.set(bytes.subarray(sent, end), 2);
      await this._device.transferOut(this._epOut, pkt);
      sent = end;
      if (onProgress) onProgress(sent, total);
    }

    /* 4. END packet */
    pkt.fill(0);
    pkt[0] = CMD;  pkt[1] = UPLOAD_SID_END;
    await this._device.transferOut(this._epOut, pkt);

    /* 5. SIZE packet: [CMD, UPLOAD_SID_SIZE, size_hi, size_lo, 0...] */
    pkt.fill(0);
    pkt[0] = CMD;  pkt[1] = UPLOAD_SID_SIZE;
    pkt[2] = (total >> 8) & 0xFF;
    pkt[3] =  total       & 0xFF;
    await this._device.transferOut(this._epOut, pkt);

    /* 6. PLAYTIME, before anything starts.
     *
     * send_sid.c sends it here, between SIZE and the load, and the order matters:
     * sent after playback has begun the tune is already running against the
     * firmware's own five minute default, and the board only picks the real
     * length up part way in. */
    if (playtimeMs > 0) await this.playerSetPlaytime(playtimeMs);

    /* 7. Load tune (file ID 0 = uploaded file, subtune is 0-based in firmware) */
    await this.playerLoadTune(subtune > 0 ? subtune - 1 : 0);

    /* 8. Start playback */
    await this.playerStart();
  }

  _log(...args) {
    if (this._debug) console.info('%cUSBSID-DRV:', 'background:#4040b4;color:#9090f0;font-weight:bold;', ...args);
  }
}

/* Helper: calculate chip address byte */
function calculate_chip_address(chip, addr) {
  return ((chip & 0x3) << 5) | (addr & 0x1F);
}

/* Buffered write queue (for player) */
class USBSID_queue {
  constructor() { this._q = []; }
  enqueue(item)     { this._q.push(item); }
  dequeue()         { return this._q.shift(); }
  isNotEmpty()      { return this._q.length > 0; }
  get length()      { return this._q.length; }
  clear()           { this._q = []; }
}

/* Singleton device instance */
const usbsidDevice = new USBSIDDevice();
