/**
 * USBSID-Pico WebUSB Driver
 * Direct WebUSB implementation (no worker) for config + playback in same page.
 * Updated to match config.h command set.
 */

'use strict';

/* ─── WebUSB constants ───────────────────────────────── */
const CDC_CLASS     = 0x0A;
const DEVICE_CLASS  = 0xFF;
const CTRL_TRANSFER = 0x22;
const CTRL_ENABLE   = 0x01;
const CTRL_DISABLE  = 0x00;

/* ─── Device identity ────────────────────────────────── */
const USBSID_VID = 0xcafe;
const USBSID_PID = 0x4011;

/* ─── Buffer / packet constants ──────────────────────── */
const BUFFER_SIZE       = 64;
const MAX_PACKET_SIZE   = 64;
const MAX_WRITE_BYTES   =  3;   /* 1 cmd, 1 reg, 1 val */
const MAX_CYCLED_BYTES  =  5;   /* 1 cmd, 1 reg, 1 val, cycles_hi, cycles_lo */
const MAX_WRITE_BUFFER  = 63;   /* 1 cmd byte, 62 / 2 writes */
const MAX_CYCLED_BUFFER = 61;   /* 1 cmd byte, 60 / 4 cycled writes */

/* ─── Command byte type (top 2 bits of byte 0) ───────── */
const WRITE        = 0;   /* 0b00 << 6 = 0x00 */
const READ         = 1;   /* 0b01 << 6 = 0x40 */
const CYCLED_WRITE = 2;   /* 0b10 << 6 = 0x80 */
const COMMAND      = 3;   /* 0b11 << 6 = 0xC0 */

/* ─── Command IDs (lower 6 bits when type=COMMAND) ───── */
const PAUSE        = 10;  /* 0x0A */
const UNPAUSE      = 11;  /* 0x0B */
const MUTE         = 12;  /* 0x0C */
const UNMUTE       = 13;  /* 0x0D */
const RESET_SID    = 14;  /* 0x0E */
const DISABLE_SID  = 15;  /* 0x0F */
const ENABLE_SID   = 16;  /* 0x10 */
const CLEAR_BUS    = 17;  /* 0x11 */
const CONFIG       = 18;  /* 0x12 – used as sub-command prefix */
const RESET_MCU    = 19;  /* 0x13 */
const BOOTLOADER   = 20;  /* 0x14 */

/* ─── WebUSB interface commands ──────────────────────── */
const WEBUSB_COMMAND  = 0xFF;
const WEBUSB_RESET    = 0x15;
const WEBUSB_CONTINUE = 0x16;

/* ─── Config sub-commands (byte 1 when byte0 = COMMAND|CONFIG) ── */
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
const SID_PLAYER_TUNE  = 0xE0;
const SID_PLAYER_START = 0xE1;
const SID_PLAYER_STOP  = 0xE2;
const SID_PLAYER_PAUSE = 0xE3;
const SID_PLAYER_NEXT  = 0xE4;
const SID_PLAYER_PREV  = 0xE5;
const SID_PLAYER_TWO   = 0xE6;

/* Config write sub-types */
const FULL_CONFIG   = 0x00;
const SOCKET_CONFIG = 0x10;
const MIDI_CONFIG   = 0x20;
const MIDI_CCVALUES = 0x30;

/* ─── Clock rate identifiers ─────────────────────────── */
const clock_rates = {
  DEFAULT: 0,  /* 1000000 Hz */
  PAL:     1,  /*  985248 Hz */
  NTSC:    2,  /* 1022727 Hz */
  DREAN:   3,  /* 1023440 Hz */
};

/* Delay helper */
const us_delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/* ─────────────────────────────────────────────────────── *
 *  USBSIDDevice — direct WebUSB, no worker                *
 * ─────────────────────────────────────────────────────── */
class USBSIDDevice {
  constructor() {
    this._device     = null;
    this._epOut      = null;
    this._epIn       = null;
    this._ifaceNum   = null;
    this._isOpen     = false;
    this._debug      = false;
  }

  /* ── Connection ── */

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

  /** Re-open a previously-permitted device (no picker) */
  async reconnect() {
    if (this._isOpen) return true;
    try {
      const devices = await navigator.usb.getDevices();
      const dev = devices.find(d => d.vendorId === USBSID_VID && d.productId === USBSID_PID);
      if (!dev) return false;
      this._device = dev;
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
      await this._device.controlTransferOut({
        requestType: 'class',
        recipient:   'interface',
        request:     CTRL_TRANSFER,
        value:       CTRL_ENABLE,
        index:       this._ifaceNum,
      });
      this._isOpen = true;
      this._log('opened, ifaceNum', this._ifaceNum, 'epOut', this._epOut, 'epIn', this._epIn);
      return true;
    } catch (e) {
      this._log('_open failed:', e);
      return false;
    }
  }

  async close() {
    if (!this._isOpen) return;
    try {
      await this._device.releaseInterface(this._ifaceNum);
      await this._device.close();
    } catch (_) {}
    this._isOpen = false;
    this._log('closed');
  }

  get isOpen()       { return this._isOpen; }
  get productName()  { return this._device ? (this._device.productName || '') : ''; }

  /* ── Raw I/O ── */

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

  /* ── High-level helpers ── */

  /** Build command byte: type (2-bit) | payload (6-bit) */
  cmd(type, payload) {
    return ((type & 0x3) << 6) | (payload & 0x3F);
  }

  /** Send a raw 6-byte config command */
  async configCmd(sub, b2 = 0, b3 = 0, b4 = 0, b5 = 0) {
    await this.write([this.cmd(COMMAND, CONFIG), sub, b2, b3, b4, b5]);
  }

  /** Send config command and read response packets */
  // async configCmdRead(sub, b2 = 0, b3 = 0, b4 = 0, b5 = 0, numPackets = 1) {
  async configCmdRead(sub, b2 = 0, b3 = 0, b4 = 0, b5 = 0, numBytes = 1) {
    const cmdBuf = new Uint8Array([this.cmd(COMMAND, CONFIG), sub, b2, b3, b4, b5]);
    const packets = [];
    try {
      usbsidLog("configCmdRead ==> Write: ", cmdBuf);
      await this._device.transferOut(this._epOut, cmdBuf);
      usbsidLog("configCmdRead <== Read start");
      const r = await this._device.transferIn(this._epIn, numBytes);
      usbsidLog("configCmdRead <== Read done");
      packets.push(new Uint8Array(r.data.buffer));
      usbsidLog("configCmdRead <== data pushed");
      /* .then(r => { */
        // this.onReceive(r.data);
      /* }, error => {
        this.onReceiveError(error);
        console.error("error: " + error);
      }); */
      // await this._device.transferOut(this._epOut, cmdBuf);
      // for (let i = 0; i < numPackets; i++) {
      //   const r = await this._device.transferIn(this._epIn, MAX_PACKET_SIZE);
      //   packets.push(new Uint8Array(r.data.buffer));
      // }
    } catch (e) {
      this._log('configCmdRead error:', e);
    }
    return packets;
  }

  /* Config reading — returns flat Uint8Array of CONFIG_SIZE (64) bytes.
   * The firmware queues 4 transfers of 64 bytes each; TinyUSB only sends the
   * first (real config, bytes 0-63). Stale packets from prior commands may sit
   * in the endpoint buffer and be returned ahead of the real response.
   * We skip leading stale packets (zero-length, all-zero, or wrong magic) inline
   * without using Promise.race — that approach leaks pending transferIn calls
   * which then consume the real response on the next read, yielding 0 bytes. */
  async readConfig() {
    const CONFIG_SIZE = 64;
    const CC = this.cmd(COMMAND, CONFIG);
    const cmdBuf = new Uint8Array([CC, READ_CONFIG, 0, 0, 0, 0]);
    const all = [];
    try {
      /* Send READ_CONFIG and collect response.
       * We do NOT drain first using Promise.race — that approach leaks pending
       * transferIn calls (when the timeout fires the transferIn stays live and
       * later consumes the real firmware response, yielding 0 bytes).
       * Instead we use direct await and skip any stale packets inline:
       *   - zero-length packets → skip (ZLP residue)
       *   - all-zero packets   → skip (stale empty response)
       *   - wrong magic bytes  → skip (stale response from another command)
       * WebUSB rejects all pending transferIn calls on disconnect, so hanging
       * on an empty buffer is not a concern. */
      await this._device.transferOut(this._epOut, cmdBuf);
      // await this._device.transferIn(this._epIn, MAX_PACKET_SIZE).then(result => {
        //   this.onReceive(result.data);
        // }
      // this._device.transferOut(this._epOut, cmdBuf); /* No await here needed */
      for (let i = 0; i < 1; i++) {
      // for (let i = 0; i < 8; i++) {
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
        // await this._device.transferIn(this._epIn, MAX_PACKET_SIZE).then(r => {
        // const chunk = new Uint8Array(r.data.buffer);
        console.log(i + ": " + chunk);
        // if (all.length === 0) {
        //   // if (chunk.length === 0) { this._log('readConfig: skipping zero-length packet'); continue; }
        //   // if (chunk.every(b => b === 0)) { this._log('readConfig: skipping stale zero packet'); continue; }
        //   if (chunk[0] !== 0x30 || chunk[1] !== 127) {
        //     this._log('readConfig: skipping stale packet (magic', chunk[0], chunk[1] + ')');
        //     // continue;
        //   }
        // }
        // if (chunk.length === 0) break;
        all.push(...chunk);
        // if (all.length >= CONFIG_SIZE) break;
        // })
      }
    } catch (e) {
      this._log('readConfig error:', e.message || e);
    }
    return new Uint8Array(all.slice(0, CONFIG_SIZE));
  }

  /* Read firmware version string */
  async readVersion() {
    usbsidLog("Reading USBSID-Pico Firmware version");
    const r = await this.configCmdRead(USBSID_VERSION, 0, 0, 0, 0, MAX_PACKET_SIZE);
    if (!r.length) return '';
    const bytes = r[0];
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
    const r = await this.configCmdRead(US_PCB_VERSION, 0, 0, 0, 0, MAX_PACKET_SIZE);
    if (!r.length) return '';
    const bytes = r[0];
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
    const r = await this.configCmdRead(GET_CLOCK, 0, 0, 0, 0, 1);
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
  async getAudio() {
    usbsidLog("Reading current audio switch setting");
    const r = await this.configCmdRead(GET_AUDIO, 0, 0, 0, 0, 1);
    return r.length ? r[0][0] : -1;
  }

  /* Bus control */
  async restartBus()       { await this.configCmd(RESTART_BUS); }
  async restartBusClock()  { await this.configCmd(RESTART_BUS_CLK); }
  async syncPIOs()         { await this.configCmd(SYNC_PIOS); }

  /* Read number of SIDs */
  async readNumSIDs() {
    usbsidLog("Reading number of available SID's");
    const r = await this.configCmdRead(READ_NUMSIDS, 0, 0, 0, 0, 1);
    return r.length ? r[0][0] : 0;
  }

  /* Read FMOpl SID number */
  async readFMOplSID() {
    usbsidLog("Reading FMOpl SID number");
    const r = await this.configCmdRead(READ_FMOPLSID, 0, 0, 0, 0, 1);
    return r.length ? r[0][0] : 0;
  }

  /* Clone config commands */
  async configFPGASID(b2, b3, b4) { await this.configCmd(FPGASID, b2, b3, b4); }
  async configSKPico(b2, b3, b4)  { await this.configCmd(SKPICO, b2, b3, b4); }
  async configARMSID(b2, b3, b4)  { await this.configCmd(ARMSID, b2, b3, b4); }
  async configPDSID()             { await this.configCmd(PDSID); }

  /* SID register write (plain, 3 bytes: cmd, reg, val) */
  async writeReg(regAddr, value) {
    await this.write([(WRITE << 6), regAddr, value]);
  }

  /* SID register write (with cycles, 5 bytes: cmd, reg, val, cycles hi, cycles lo) */
  async writeRegC(regAddr, value, cycles_hi, cycles_lo) {
    await this.write([(CYCLED_WRITE << 6), regAddr, value, cycles_hi, cycles_lo]);
  }

  /* Raw array write — used by player integration */
  async writeArray(arr) {
    await this.write(arr);
  }

  /* Pause / unpause SID output */
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
   * Upload a SID/PRG file to the onboard player and start playback.
   * Matches the protocol in send_sid.c exactly — all upload packets are 64 bytes.
   * @param {Uint8Array} bytes   - raw file bytes
   * @param {number}     subtune - 1-based subtune number (default 1)
   * @param {number}     fileType - 0x01=SID, 0x02=PRG (default 0x01)
   * @param {function}   onProgress - optional callback(sent, total)
   */
  async uploadSIDFile(bytes, subtune = 1, fileType = 0x01, onProgress = null) {
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

    /* 3. DATA packets: [CMD, UPLOAD_SID_DATA, b0..b61] — 62 bytes per packet */
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

    /* 6. Load tune (file ID 0 = uploaded file, subtune is 0-based in firmware) */
    await this.playerLoadTune(subtune > 0 ? subtune - 1 : 0);

    /* 7. Start playback */
    await this.playerStart();
  }

  _log(...args) {
    if (this._debug) console.info('%cUSBSID-DRV:', 'background:#4040b4;color:#9090f0;font-weight:bold;', ...args);
  }
}

/* ─── Helper: calculate chip address byte ────────────── */
function calculate_chip_address(chip, addr) {
  return ((chip & 0x3) << 5) | (addr & 0x1F);
}

/* ─── Buffered write queue (for player) ──────────────── */
class USBSID_queue {
  constructor() { this._q = []; }
  enqueue(item)     { this._q.push(item); }
  dequeue()         { return this._q.shift(); }
  isNotEmpty()      { return this._q.length > 0; }
  get length()      { return this._q.length; }
  clear()           { this._q = []; }
}

/* ─── Singleton device instance ──────────────────────── */
const usbsidDevice = new USBSIDDevice();
