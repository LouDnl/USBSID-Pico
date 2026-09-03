#!/usr/bin/env python3
"""USBSID-Pico MIDI patch editor.

A standalone GUI for creating and testing midi_patch_t patches over MIDI,
using the SysEx commands added to repo/src/sysex.c (manufacturer 0x50,
commands 0x20/0x21/0x22) and the CC map in repo/src/midi_defs.h.

Talks MIDI through `python-rtmidi`, not the `amidi` CLI this tool started
with. `amidi` opens the kernel rawmidi device (/dev/snd/midiC*D*) exclusively
on Linux, so it fails with "Device or resource busy" the instant anything
else - PipeWire, a patchbay, another app - already has that hardware port
bridged into the sequencer graph, which on a modern Linux desktop is
essentially always (PipeWire's ALSA MIDI bridge grabs it on its own). RtMidi
avoids this everywhere, not just on Linux: it always goes through each
platform's own shared, virtualised MIDI API rather than an exclusive device
handle - ALSA sequencer (snd_seq) on Linux, WinMM/MME on Windows, CoreMIDI on
macOS - the same kind of routing layer patchbay/aconnect/qpwgraph
(Linux), the Windows MIDI mapper, or Audio MIDI Setup (macOS) already use, so
any number of applications can connect to the same device at once. This tool
runs unmodified on Linux, Windows, and macOS - the same code path everywhere,
`pip`/`rtmidi` pick the right backend automatically per platform.

Setup (any OS): `pip install python-rtmidi` (a compiled extension with
prebuilt wheels for all three platforms - no compiler needed), then just run
`python3 patch_editor.py` (Windows: `python patch_editor.py`). `tkinter`
ships with the standard python.org installer on Windows and macOS; on Linux
it is sometimes a separate distro package (e.g. `python3-tk` on
Debian/Ubuntu, `python3-tkinter` on Fedora). This checkout also has a
ready-made Linux venv for convenience - `.venv/bin/python3 patch_editor.py`
(created with `python3 -m venv --system-site-packages .venv` so it still
sees the system `tkinter`, then `pip install python-rtmidi` inside it) - but
it is not required and is not portable as-is: on Windows or macOS, create
your own venv the platform-normal way (`python -m venv .venv` then
`.venv\\Scripts\\activate` on Windows, or `source .venv/bin/activate` on
macOS/Linux) if you want one at all, or just install python-rtmidi into your
regular Python.

The nibble pack/unpack scheme below is a literal port of sysex.c's
pack_patch()/unpack_patch() - see that file for the authoritative field
reference. Keep the two in sync by hand; there is no shared source of
truth between the C firmware and this Python script.
"""

import os
import re
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

import rtmidi

# --- MIDI transport (python-rtmidi, ALSA sequencer backend) ----------------

def list_ports():
    """Returns [(port_name, port_name), ...] of available MIDI OUT ports.
    Port name doubles as its own id - RtMidi/ALSA seq ports are addressed by
    name (or index into the same enumeration), there's no separate short id
    the way `amidi -l`'s "hw:3,0,0" was."""
    try:
        names = rtmidi.MidiOut().get_ports()
    except (RuntimeError, SystemError):
        return []
    return [(n, n) for n in names]


def find_usbsid_port():
    for name, _desc in list_ports():
        if "usbsid" in name.lower():
            return name
    return None


def _open_out(port_name):
    midiout = rtmidi.MidiOut()
    ports = midiout.get_ports()
    for i, p in enumerate(ports):
        if p == port_name or port_name in p or p in port_name:
            midiout.open_port(i)
            return midiout
    raise RuntimeError(f"MIDI output port not found: {port_name}")


def _open_in(port_name):
    midiin = rtmidi.MidiIn()
    ports = midiin.get_ports()
    for i, p in enumerate(ports):
        if p == port_name or port_name in p or p in port_name:
            midiin.open_port(i)
            # Sysex is ignored by default; we need it for patch dumps.
            midiin.ignore_types(sysex=False, timing=True, active_sense=True)
            return midiin
    raise RuntimeError(f"MIDI input port not found: {port_name}")


def _hex_to_bytes(hexstr):
    return [int(b, 16) for b in hexstr.split()]


def send_hex(port, hexstr):
    """Fire-and-forget send, matching midi_test.sh's send()."""
    try:
        midiout = _open_out(port)
    except RuntimeError:
        return
    try:
        midiout.send_message(_hex_to_bytes(hexstr))
    finally:
        midiout.close_port()


def send_hex_and_dump(port, hexstr, timeout_s=1.5):
    """Send, then listen for a reply. Returns the raw dump text (may be empty),
    formatted as space-separated hex byte pairs - same shape `amidi -d` used
    to produce, so the regex-based parsers elsewhere in this file don't care
    which transport actually gathered the bytes."""
    try:
        midiin = _open_in(port)
        midiout = _open_out(port)
    except RuntimeError:
        return ""
    try:
        midiout.send_message(_hex_to_bytes(hexstr))
        collected = []
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            msg = midiin.get_message()
            if msg is not None:
                data, _delta_time = msg
                collected.extend(data)
            else:
                time.sleep(0.01)
        return " ".join(f"{b:02x}" for b in collected)
    finally:
        midiin.close_port()
        midiout.close_port()


# --- Patch encoding (must match sysex.c's pack_patch()/unpack_patch()) -----

# (field, kind) - kind is 1 (one byte -> 2 nibbles) or 2 (u16 -> 4 nibbles).
# Order matters: this must match midi_patch_t's declaration order exactly.
PATCH_FIELDS = [
    ("tmpl_contr", 1), ("tmpl_attdec", 1), ("tmpl_susrel", 1),
    ("tmpl_pwmlo", 1), ("tmpl_pwmhi", 1),
    ("filter_cutoff", 2),
    ("resonance", 1), ("filter_routing", 1), ("filter_mode", 1),
    ("lfo_wave", 1), ("lfo_rate", 1), ("lfo_depth", 1), ("lfo_dest", 1),
    ("arp_mode", 1), ("arp_rate", 1), ("arp_octaves", 1),
    ("bend_range", 1),
    # LFO 2 and unison fields: appended, not interleaved - matches sysex.c's
    # pack_patch()/unpack_patch(), which appends these at the tail of the
    # existing 36 nibbles rather than reordering the frame, so an old
    # 36-nibble dump is refused by the firmware's size check instead of
    # being misread.
    ("lfo2_wave", 1), ("lfo2_rate", 1), ("lfo2_depth", 1), ("lfo2_dest", 1),
    ("unison_enabled", 1), ("unison_detune", 1),
]
PATCH_NIBBLE_COUNT = sum(2 if k == 1 else 4 for _, k in PATCH_FIELDS)  # 48


def encode_patch(values):
    """values: dict of field -> int. Returns a list of nibble ints (0-15)."""
    out = []
    for name, kind in PATCH_FIELDS:
        v = int(values[name]) & (0xFF if kind == 1 else 0xFFFF)
        if kind == 1:
            out += [(v >> 4) & 0xF, v & 0xF]
        else:
            out += [(v >> 12) & 0xF, (v >> 8) & 0xF, (v >> 4) & 0xF, v & 0xF]
    return out


def decode_patch(nibbles):
    """Inverse of encode_patch(). nibbles: list/iterable of PATCH_NIBBLE_COUNT ints (0-15)."""
    nibbles = list(nibbles)
    if len(nibbles) != PATCH_NIBBLE_COUNT:
        raise ValueError(f"expected {PATCH_NIBBLE_COUNT} nibbles, got {len(nibbles)}")
    out = {}
    i = 0
    for name, kind in PATCH_FIELDS:
        if kind == 1:
            out[name] = ((nibbles[i] & 0xF) << 4) | (nibbles[i + 1] & 0xF)
            i += 2
        else:
            out[name] = (
                ((nibbles[i] & 0xF) << 12) | ((nibbles[i + 1] & 0xF) << 8)
                | ((nibbles[i + 2] & 0xF) << 4) | (nibbles[i + 3] & 0xF)
            )
            i += 4
    return out


def build_load_sysex(patch_idx, values):
    nibbles = encode_patch(values)
    body = [0xF0, 0x50, 0x20, patch_idx] + nibbles + [0xF7]
    return " ".join(f"{b:02x}" for b in body)


def build_dump_sysex(patch_idx):
    return f"f0 50 21 {patch_idx:02x} f7"


def build_patch_save_sysex(channel, patch_idx):
    """SYSEX_MIDI_PATCH_SAVE (0x2A) - repo/src/sysex.c. Captures a channel's
    current live state into a patch slot on the device (the inverse of LOAD:
    no payload, just addressing - buffer[3]=channel, buffer[4]=patch index)."""
    return f"f0 50 2a {channel:02x} {patch_idx:02x} f7"


# --- FMOpl patch encoding (must match sysex.c's pack_opl_instrument()/
# unpack_opl_instrument() and midi_fmopl.h's opl_instrument_t) --------------
#
# 11 raw bytes (op_mult[2], op_ksl_tl[2], op_ar_dr[2], op_sl_rr[2],
# op_wave[2], feedback_algo), each as two nibbles same as the SID side - 22
# nibbles per patch. Field order matters, must match the struct exactly.
OPL_FIELDS = [
    "op_mult_0", "op_mult_1", "op_ksl_tl_0", "op_ksl_tl_1",
    "op_ar_dr_0", "op_ar_dr_1", "op_sl_rr_0", "op_sl_rr_1",
    "op_wave_0", "op_wave_1", "feedback_algo",
]
FMOPL_NIBBLE_COUNT = len(OPL_FIELDS) * 2  # 22


def encode_opl_instrument(values):
    """values: dict of OPL_FIELDS name -> int (0-255). Returns 22 nibbles."""
    out = []
    for name in OPL_FIELDS:
        v = int(values[name]) & 0xFF
        out += [(v >> 4) & 0xF, v & 0xF]
    return out


def decode_opl_instrument(nibbles):
    nibbles = list(nibbles)
    if len(nibbles) != FMOPL_NIBBLE_COUNT:
        raise ValueError(f"expected {FMOPL_NIBBLE_COUNT} nibbles, got {len(nibbles)}")
    out = {}
    for i, name in enumerate(OPL_FIELDS):
        out[name] = ((nibbles[2 * i] & 0xF) << 4) | (nibbles[2 * i + 1] & 0xF)
    return out


def build_opl_load_sysex(patch_idx, values):
    nibbles = encode_opl_instrument(values)
    body = [0xF0, 0x50, 0x23, patch_idx] + nibbles + [0xF7]
    return " ".join(f"{b:02x}" for b in body)


def build_opl_dump_sysex(patch_idx):
    return f"f0 50 24 {patch_idx:02x} f7"


def build_fmopl_patch_save_sysex(channel, patch_idx):
    """SYSEX_FMOPL_PATCH_SAVE (0x2B) - repo/src/sysex.c. Captures a channel's
    currently-selected FMOpl instrument into another patch slot (today a
    plain duplicate - there is no live per-operator editing yet to capture,
    see midi_fmopl.c's own doc comment on midi_fmopl_capture_patch())."""
    return f"f0 50 2b {channel:02x} {patch_idx:02x} f7"


def build_fmopl_clock_sysex(hz):
    """SYSEX_FMOPL_SET_CLOCK (0x26) - repo/src/sysex.c. `hz` is a debugging/
    tuning override for the OPL2 clock rate used to compute Fnum/block from
    a MIDI note; 0 resets to the firmware's compiled-in default (3579545,
    the standard OPL2 crystal). 24-bit value, big-endian, 3 bytes -> 6
    nibbles, no patch index (this is one global value, not per-patch)."""
    hz = int(hz) & 0xFFFFFF
    b0, b1, b2 = (hz >> 16) & 0xFF, (hz >> 8) & 0xFF, hz & 0xFF
    body = [0xF0, 0x50, 0x26,
            (b0 >> 4) & 0xF, b0 & 0xF, (b1 >> 4) & 0xF, b1 & 0xF, (b2 >> 4) & 0xF, b2 & 0xF,
            0xF7]
    return " ".join(f"{b:02x}" for b in body)


def parse_opl_dump_reply(raw_text, expected_idx=None):
    result = parse_patch_sysex(raw_text, commands=(0x25,), expected_idx=expected_idx,
                                nibble_count=FMOPL_NIBBLE_COUNT, decoder=decode_opl_instrument)
    if result is None:
        return None
    _cmd, idx, values = result
    return idx, values


# --- Arp table encoding (must match sysex.c's pack_arp_table()/
# unpack_arp_table() and midi_arp_table.h's midi_arp_table_t) ---------------
#
# 16 relative-semitone offsets (signed bytes, two's-complement over the wire
# same as encode_byte()/decode_byte() do in C - a Python int cast straight to
# an unsigned byte and back), plus loop_start and step_count, each 1 byte ->
# 2 nibbles - 36 nibbles per table. Entirely new commands (0x27/0x28/0x29),
# no shared frame with the SID/FMOpl patch commands above to keep in sync.
ARP_TABLE_STEPS = 16
ARPTABLE_NIBBLE_COUNT = (ARP_TABLE_STEPS + 2) * 2  # 36


def encode_arp_table(offsets, loop_start, step_count):
    """offsets: 16 ints, -128..127. Returns 36 nibbles."""
    out = []
    for v in offsets:
        b = int(v) & 0xFF  # two's complement, matches sysex.c's (uint8_t)t->offsets[i]
        out += [(b >> 4) & 0xF, b & 0xF]
    for v in (loop_start, step_count):
        b = int(v) & 0xFF
        out += [(b >> 4) & 0xF, b & 0xF]
    return out


def decode_arp_table(nibbles):
    """Inverse of encode_arp_table(). Returns {"offsets": [16 ints], "loop_start": int, "step_count": int}."""
    nibbles = list(nibbles)
    if len(nibbles) != ARPTABLE_NIBBLE_COUNT:
        raise ValueError(f"expected {ARPTABLE_NIBBLE_COUNT} nibbles, got {len(nibbles)}")
    offsets = []
    i = 0
    for _ in range(ARP_TABLE_STEPS):
        b = ((nibbles[i] & 0xF) << 4) | (nibbles[i + 1] & 0xF)
        if b >= 128:
            b -= 256  # back to signed
        offsets.append(b)
        i += 2
    loop_start = ((nibbles[i] & 0xF) << 4) | (nibbles[i + 1] & 0xF); i += 2
    step_count = ((nibbles[i] & 0xF) << 4) | (nibbles[i + 1] & 0xF)
    return {"offsets": offsets, "loop_start": loop_start, "step_count": step_count}


def build_arptable_load_sysex(table_idx, offsets, loop_start, step_count):
    nibbles = encode_arp_table(offsets, loop_start, step_count)
    body = [0xF0, 0x50, 0x27, table_idx] + nibbles + [0xF7]
    return " ".join(f"{b:02x}" for b in body)


def build_arptable_dump_sysex(table_idx):
    return f"f0 50 28 {table_idx:02x} f7"


def parse_arptable_dump_reply(raw_text, expected_idx=None):
    result = parse_patch_sysex(raw_text, commands=(0x29,), expected_idx=expected_idx,
                                nibble_count=ARPTABLE_NIBBLE_COUNT, decoder=decode_arp_table)
    if result is None:
        return None
    _cmd, idx, values = result
    return idx, values


# --- Bank/instrument file import (patches from disk) ------------------------
#
# Three source formats, all converted into the same OPL_FIELDS-shaped dict
# opl_instrument_t already uses everywhere else in this file:
#
#  - .s3i: a single Scream Tracker 3 instrument. Confirmed against the real
#    S3M format spec (TECH.DOC, github.com/reznet/S3MParser) and cross-checked
#    byte-for-byte against 444 real .s3i files - every one had type byte 2
#    (AdLib "Melody"), the "SCRI" magic, and
#    plausible register values. D00-D0B are the raw OPL register bytes
#    themselves, packed exactly the way opl_instrument_t already stores them -
#    a direct 1:1 field copy, no bit-remapping needed.
#  - .op2: a DMX "GENMIDI" bank (Doom/Heretic/Hexen), fixed 175 instruments.
#    Verified against a real file (dmxopl/GENMIDI.op2): magic, 11908-byte
#    total size, and the first 9 instrument names ("Acoustic Grand Piano",
#    "Bright Acoustic Piano", ...) all matched exactly. One correction found
#    versus the format research: KSL is NOT stored in the low 2 bits needing
#    a shift - the "scale" byte already carries it pre-shifted into bits 7:6,
#    same position as the real register, so it's a plain OR with the "level"
#    (Total Level) byte, not `(scale & 3) << 6`.
#  - .wopl: an OPL3BankEditor/libADLMIDI bank (variable instrument count,
#    optionally 4-operator). Verified against a real file
#    (dmxopl/GENMIDI.wopl, version 3): header field offsets, bank-meta
#    layout, and instrument name/operator byte positions all confirmed by
#    locating "Acoustic Grand Piano"/"Bright Acoustic Piano" at the exact
#    computed offsets. One correction found versus the format research: a
#    WOPL v3 instrument record is 66 bytes, not 62 - version 3 added two
#    trailing 16-bit fields (millisecond on/off sound-duration hints) after
#    the 4 operator blocks that the research summary's source didn't cover.
#    Only version 3 is handled; older WOPL versions (different record size)
#    raise a clear error rather than being silently misparsed.
#
# Both bank formats can carry more operators/voices than this firmware's
# 2-operator opl_instrument_t - OP2's second "voice" (a full second 2-op
# instrument layered with a pitch offset, for chorus/detune) and WOPL's
# 3rd/4th operators (4-op mode) are read but not converted, same "only the
# first oscillator survives" precedent already used for the MIDIbox SID
# preset conversion.

class S3IError(Exception):
    pass


def parse_s3i(data):
    """Parses one Scream Tracker 3 instrument file's bytes. Returns
    (name, opl_values_dict) for an AdLib/OPL instrument (type 2-7), or
    raises S3IError - including for a type-1 PCM sample, which this format
    can also contain and which has nothing to do with OPL at all."""
    if len(data) < 0x50:
        raise S3IError(f"too short for an S3I header ({len(data)} bytes, need at least 80)")
    itype = data[0]
    if itype == 0:
        raise S3IError("empty instrument slot (type 0)")
    if itype == 1:
        raise S3IError("this is a PCM sample (type 1), not an OPL/AdLib instrument")
    if itype > 7:
        raise S3IError(f"unknown instrument type {itype}")
    magic = data[0x4C:0x50]
    if magic not in (b"SCRI", b"SCRS"):
        raise S3IError(f"bad magic {magic!r}, expected SCRI/SCRS at offset 0x4C")
    d = data[0x10:0x1C]  # D00..D0B, 12 bytes
    values = {
        "op_mult_0": d[0], "op_mult_1": d[1],
        "op_ksl_tl_0": d[2], "op_ksl_tl_1": d[3],
        "op_ar_dr_0": d[4], "op_ar_dr_1": d[5],
        "op_sl_rr_0": d[6], "op_sl_rr_1": d[7],
        "op_wave_0": d[8] & 0x3, "op_wave_1": d[9] & 0x3,  # OPL2 only has 4 waveforms
        "feedback_algo": d[10],
    }
    name = data[0x30:0x4C].split(b"\x00", 1)[0].decode("latin-1").strip()
    return name, values


class OP2Error(Exception):
    pass


OP2_HEADER_SIZE = 8
OP2_ENTRY_SIZE = 36
OP2_ENTRY_COUNT = 175
OP2_NAME_SIZE = 32


def parse_op2_bank(data):
    """Parses a DMX GENMIDI (.op2) bank. Returns a list of
    (index, name, opl_values_dict), always exactly 175 entries in GM
    patch order (0-127 melodic, 128-174 percussion). Only "voice 0" of each
    instrument is converted - see the module-level comment above."""
    if data[0:8] != b"#OPL_II#":
        raise OP2Error(f"bad magic {data[0:8]!r}, expected b'#OPL_II#'")
    expected_size = OP2_HEADER_SIZE + OP2_ENTRY_COUNT * OP2_ENTRY_SIZE + OP2_ENTRY_COUNT * OP2_NAME_SIZE
    if len(data) < expected_size:
        raise OP2Error(f"file too short ({len(data)} bytes, need {expected_size})")

    names_off = OP2_HEADER_SIZE + OP2_ENTRY_COUNT * OP2_ENTRY_SIZE
    out = []
    for i in range(OP2_ENTRY_COUNT):
        entry_off = OP2_HEADER_SIZE + i * OP2_ENTRY_SIZE
        voice0 = data[entry_off + 4: entry_off + 20]  # 16 bytes, "voice 1" is unused here
        scale0, level0 = voice0[4], voice0[5]
        scale1, level1 = voice0[11], voice0[12]
        values = {
            "op_mult_0": voice0[0], "op_mult_1": voice0[7],
            "op_ksl_tl_0": (scale0 & 0xC0) | (level0 & 0x3F),
            "op_ksl_tl_1": (scale1 & 0xC0) | (level1 & 0x3F),
            "op_ar_dr_0": voice0[1], "op_ar_dr_1": voice0[8],
            "op_sl_rr_0": voice0[2], "op_sl_rr_1": voice0[9],
            "op_wave_0": voice0[3] & 0x3, "op_wave_1": voice0[10] & 0x3,  # OPL2 only has 4 waveforms
            "feedback_algo": voice0[6],
        }
        name_off = names_off + i * OP2_NAME_SIZE
        name = data[name_off:name_off + OP2_NAME_SIZE].split(b"\x00", 1)[0].decode("latin-1").strip()
        out.append((i, name, values))
    return out


class WOPLError(Exception):
    pass


WOPL_HEADER_SIZE = 19
WOPL_BANK_META_SIZE = 34
WOPL_INSTRUMENTS_PER_BANK = 128


def parse_wopl_bank(data):
    """Parses an OPL3BankEditor/libADLMIDI (.wopl) bank, version 3 only
    (other versions use a different instrument record size and would be
    silently misparsed - rejected with a clear error instead). Returns a
    list of (bank_name, is_percussion, index_in_bank, name, opl_values_dict).
    Only operators 1/2 (the first, "2-op" pair) are converted for a 4-op
    instrument - see the module-level comment above."""
    if data[0:11] != b"WOPL3-BANK\x00":
        raise WOPLError(f"bad magic {data[0:11]!r}, expected b'WOPL3-BANK\\x00'")
    version = struct.unpack_from("<H", data, 11)[0]
    if version != 3:
        raise WOPLError(f"WOPL version {version} is not supported (only version 3's 66-byte "
                         f"instrument records have been verified against a real file)")
    mel_banks = struct.unpack_from(">H", data, 13)[0]
    perc_banks = struct.unpack_from(">H", data, 15)[0]
    total_banks = mel_banks + perc_banks
    record_size = 66  # verified against a real file, see the module-level comment above

    expected_min = (WOPL_HEADER_SIZE + total_banks * WOPL_BANK_META_SIZE
                     + total_banks * WOPL_INSTRUMENTS_PER_BANK * record_size)
    if len(data) < expected_min:
        raise WOPLError(f"file too short ({len(data)} bytes, need at least {expected_min} "
                         f"for {mel_banks} melodic + {perc_banks} percussion bank(s))")

    bank_metas = []
    off = WOPL_HEADER_SIZE
    for b in range(total_banks):
        name = data[off:off + 32].split(b"\x00", 1)[0].decode("latin-1").strip()
        bank_metas.append(name)
        off += WOPL_BANK_META_SIZE

    out = []
    for b in range(total_banks):
        bank_name = bank_metas[b]
        is_percussion = b >= mel_banks
        for i in range(WOPL_INSTRUMENTS_PER_BANK):
            rec = data[off:off + record_size]
            off += record_size
            name = rec[0:32].split(b"\x00", 1)[0].decode("latin-1").strip()
            if not name:
                continue  # unused slot in a bank that doesn't fill all 128
            # Record alignment (name position, 66-byte stride) is verified
            # against a real file - see the module-level comment. Which of
            # these two 5-byte blocks is "modulator" vs "carrier" is NOT
            # independently confirmed the same way (no audio test possible,
            # see midi_fmopl.h's own hardware caveat) - OPL3's native 4-op
            # register layout is a linear operator chain, not necessarily a
            # mod/carrier label the way classic 2-op FM uses, so op0/op1
            # might come out swapped relative to what DMXOPL intended.
            op0 = rec[42:47]
            op1 = rec[47:52]
            values = {
                "op_mult_0": op0[0], "op_mult_1": op1[0],
                "op_ksl_tl_0": op0[1], "op_ksl_tl_1": op1[1],
                "op_ar_dr_0": op0[2], "op_ar_dr_1": op1[2],
                "op_sl_rr_0": op0[3], "op_sl_rr_1": op1[3],
                "op_wave_0": op0[4] & 0x3, "op_wave_1": op1[4] & 0x3,  # OPL2 only has 4 waveforms
                "feedback_algo": rec[40],
            }
            out.append((bank_name, is_percussion, i, name, values))
    return out


def parse_patch_sysex(raw_text, commands=(0x22,), expected_idx=None,
                       nibble_count=None, decoder=None):
    """Extracts the data nibbles from an F0 50 <cmd> <idx> ... F7 frame,
    <cmd> being any of `commands`. `nibble_count`/`decoder` default to the
    SID patch shape (36 nibbles, decode_patch) - pass FMOPL_NIBBLE_COUNT/
    decode_opl_instrument for an FMOpl patch frame instead (same F0 50 <cmd>
    <idx> ... F7 shape, sysex.c's SYSEX_FMOPL_PATCH_LOAD/DUMP/DATA, just a
    different, much smaller payload).
    Returns (cmd, patch_idx, decoded) or None if none was found."""
    if nibble_count is None:
        nibble_count = PATCH_NIBBLE_COUNT
    if decoder is None:
        decoder = decode_patch
    hexbytes = [int(b, 16) for b in re.findall(r"[0-9a-fA-F]{2}", raw_text)]
    # Scan anywhere in the text - amidi -d can print other traffic (clock,
    # etc.) interleaved, and a pasted preset may carry surrounding prose.
    for start in range(len(hexbytes)):
        if hexbytes[start] != 0xF0:
            continue
        if start + 3 >= len(hexbytes):
            continue
        if hexbytes[start + 1] != 0x50 or hexbytes[start + 2] not in commands:
            continue
        cmd = hexbytes[start + 2]
        idx = hexbytes[start + 3]
        data_start = start + 4
        data_end = data_start + nibble_count
        if data_end >= len(hexbytes):
            continue
        if hexbytes[data_end] != 0xF7:
            continue
        if expected_idx is not None and idx != expected_idx:
            continue
        nibbles = hexbytes[data_start:data_end]
        return cmd, idx, decoder(nibbles)
    return None


def parse_dump_reply(raw_text, expected_idx=None):
    """Extracts the 36 data nibbles from an 0x22 SYSEX_MIDI_PATCH_DATA reply.
    Returns (patch_idx, {field: value}) or None if no valid reply was found."""
    result = parse_patch_sysex(raw_text, commands=(0x22,), expected_idx=expected_idx)
    if result is None:
        return None
    _cmd, idx, values = result
    return idx, values


# --- Field definitions for the GUI (label, kind, range/choices) ------------

WAVEFORM_BITS = [("Triangle", 0x10), ("Sawtooth", 0x20), ("Pulse", 0x40), ("Noise", 0x80)]
CONTR_EXTRA_BITS = [("Sync", 0x02), ("Ring mod", 0x04), ("Test", 0x08)]
FILTER_ROUTE_BITS = [("Voice 1", 0x01), ("Voice 2", 0x02), ("Voice 3", 0x04), ("External", 0x08)]
FILTER_MODE_BITS = [("Low-pass", 0x10), ("Band-pass", 0x20), ("High-pass", 0x40)]
LFO_WAVES = ["Triangle", "Sawtooth", "Square", "Sample & Hold"]
LFO_DESTS = ["Pitch", "PWM", "Cutoff"]
ARP_MODES = ["Up", "Down", "Up/Down", "Random", "As played", "Table"]
ARP_TABLE_SLOT_COUNT = 16  # matches midi_arp_table.h's MIDI_ARP_TABLE_COUNT

# --- Live MIDI CC control map (repo/src/midi_defs.h MIDI_DEFAULT_CCVALUES_INIT) --
#
# Every CC number here is the firmware's *compiled-in default* - the board's
# own ccmap is itself user-remappable and persisted separately from
# anything this editor does, so if it's been changed these numbers won't
# match. "kind" picks the widget:
#   toggle       - Checkbutton, sends 127 when checked / 0 when unchecked
#   slider       - Scale 0-127, sends on release
#   slider_enum  - Scale 0-127 whose value maps to a small enum on the
#                  device; the enum names are shown next to the raw value
#   button       - momentary, sends a fixed value once per click
# Grouped to match midi_defs.h's own section comments.
CC_GROUPS = [
    ("Voice waveform (tmpl_contr bits - combinable, same field a Patch also sets)", [
        ("CC_NOIS", "Noise", "toggle", {}),
        ("CC_PULS", "Pulse", "toggle", {}),
        ("CC_SAWT", "Sawtooth", "toggle", {}),
        ("CC_TRIA", "Triangle", "toggle", {}),
        ("CC_TEST", "Test bit", "toggle", {}),
        ("CC_RMOD", "Ring modulator", "toggle", {}),
        ("CC_SYNC", "Sync", "toggle", {}),
        ("CC_GATE", "Gate (manual, held notes only)", "toggle", {}),
    ]),
    ("Voice envelope / pitch", [
        ("CC_ATT", "Attack", "slider", {}),
        ("CC_DEC", "Decay", "slider", {}),
        ("CC_SUS", "Sustain", "slider", {}),
        ("CC_REL", "Release", "slider", {}),
        ("CC_PWM", "Pulse width (mod wheel)", "slider", {}),
        ("CC_NOTE", "Note frequency (manual pitch nudge)", "slider", {}),
    ]),
    ("Chip filter / volume (per-SID, not per-voice)", [
        ("CC_FFC", "Filter cutoff", "slider", {}),
        ("CC_RES", "Filter resonance", "slider", {}),
        ("CC_FLT1", "Route voice 1 through filter", "toggle", {}),
        ("CC_FLT2", "Route voice 2 through filter", "toggle", {}),
        ("CC_FLT3", "Route voice 3 through filter", "toggle", {}),
        ("CC_FLTE", "Route external input through filter", "toggle", {}),
        ("CC_3OFF", "Voice 3 disconnect", "toggle", {}),
        ("CC_HPF", "High-pass", "toggle", {}),
        ("CC_BPF", "Band-pass", "toggle", {}),
        ("CC_LPF", "Low-pass", "toggle", {}),
        ("CC_VOL", "Channel volume", "slider", {}),
    ]),
    ("Modulation / timing (Phase 4 - same live fields a Patch also sets)", [
        ("CC_LFOW", "LFO waveform", "slider_enum", {"enum": LFO_WAVES}),
        ("CC_LFOR", "LFO rate", "slider", {}),
        ("CC_LFOD", "LFO depth", "slider", {}),
        ("CC_LFOT", "LFO destination", "slider_enum", {"enum": LFO_DESTS}),
        ("CC_PORT", "Portamento time", "slider", {}),
        ("CC_ARPM", "Arpeggiator mode", "slider_enum", {"enum": ARP_MODES}),
        ("CC_ARPR", "Arpeggiator rate", "slider", {}),
        ("CC_ARPO", "Arpeggiator octave range", "slider_enum", {"enum": ["1", "2", "3", "4"]}),
        ("CC_ARPE", "Arpeggiator enable", "toggle", {}),
        ("CC_ARPT", "Arpeggiator table select (used when mode = Table)", "slider", {}),
    ]),
    ("LFO 2 (stacks with LFO 1; same offset summed if both target the same destination)", [
        ("CC_LFO2W", "LFO 2 waveform", "slider_enum", {"enum": LFO_WAVES}),
        ("CC_LFO2R", "LFO 2 rate", "slider", {}),
        ("CC_LFO2D", "LFO 2 depth", "slider", {}),
        ("CC_LFO2T", "LFO 2 destination", "slider_enum", {"enum": LFO_DESTS}),
    ]),
    ("Unison (3 real oscillators per note; channel drops to 1 note/SID while on)", [
        ("CC_UNIS", "Unison mode on/off", "toggle", {}),
        ("CC_UDET", "Unison detune spread", "slider", {}),
    ]),
    ("Channel mode / custom commands", [
        ("CC_GTEN", "Auto gate (gate follows note-on/off)", "toggle", {}),
        ("CC_SPLY", "Polyphonic on current SID", "toggle", {}),
        ("CC_VELM", "Velocity mode (velocity scales decay)", "toggle", {}),
        ("CC_MONO", "Mono mode (silences channel)", "button", {"value": 127}),
        ("CC_POLY", "Poly mode (silences channel)", "button", {"value": 127}),
    ]),
    ("Panic / global (not scoped to one channel)", [
        ("CC_ASOF", "All Sound Off", "button", {"value": 127}),
        ("CC_RACT", "Reset All Controllers", "button", {"value": 127}),
        ("CC_ANOF", "All Notes Off", "button", {"value": 127}),
    ]),
    ("Cynthcart (only relevant with ONBOARD_EMULATOR=1)", [
        ("CC_CEN", "Enable Cynthcart", "button", {"value": 127}),
        ("CC_CDI", "Disable Cynthcart", "button", {"value": 127}),
        ("CC_CRE", "Restart Cynthcart", "button", {"value": 127}),
    ]),
]

# CC_SID1-4/CC_VCE1-3: momentary-select overrides, not simple toggles - value
# 0 on ANY of them clears whichever override is currently set (set_sid_override()/
# set_voice_override(), midi_handler.c), so these get their own widget group
# below rather than living in CC_GROUPS.
CC_SID_OVERRIDES = [("CC_SID1", "SID 1"), ("CC_SID2", "SID 2"), ("CC_SID3", "SID 3"), ("CC_SID4", "SID 4")]
CC_VCE_OVERRIDES = [("CC_VCE1", "Voice 1"), ("CC_VCE2", "Voice 2"), ("CC_VCE3", "Voice 3")]

# CC_BMSB/CC_BLSB/CC_MOD/CC_MODL are 0xFF ("unused") in MIDI_DEFAULT_CCVALUES_INIT
# and never appear in midi_handler.c's ASSIGN_CC table - no handler is bound to
# them at all, so a control here would silently do nothing. Listed, not wired.
CC_UNWIRED = ["CC_BMSB (Bank Select MSB)", "CC_BLSB (Bank Select LSB)",
              "CC_MOD (Modulation wheel)", "CC_MODL (Modulation wheel LSB)"]

# repo/src/midi_defs.h MIDI_DEFAULT_CCVALUES_INIT, compiled-in defaults.
CC_DEFAULT_NUMBERS = {
    "CC_NOTE": 0x1C, "CC_PWM": 0x01, "CC_NOIS": 0x14, "CC_PULS": 0x15, "CC_SAWT": 0x16,
    "CC_TRIA": 0x17, "CC_TEST": 0x18, "CC_RMOD": 0x19, "CC_SYNC": 0x1A, "CC_GATE": 0x1B,
    "CC_ATT": 0x11, "CC_DEC": 0x12, "CC_SUS": 0x13, "CC_REL": 0x1D,
    "CC_FFC": 0x20, "CC_RES": 0x21, "CC_FLT1": 0x22, "CC_FLT2": 0x23, "CC_FLT3": 0x24,
    "CC_FLTE": 0x25, "CC_3OFF": 0x30, "CC_HPF": 0x31, "CC_BPF": 0x32, "CC_LPF": 0x33, "CC_VOL": 0x07,
    "CC_SID1": 0x68, "CC_SID2": 0x69, "CC_SID3": 0x6A, "CC_SID4": 0x6B,
    "CC_VCE1": 0x6C, "CC_VCE2": 0x6D, "CC_VCE3": 0x6E,
    "CC_GTEN": 0x77, "CC_SPLY": 0x6F, "CC_VELM": 0x48,
    "CC_LFOW": 0x00, "CC_LFOR": 0x02, "CC_LFOD": 0x03, "CC_LFOT": 0x04, "CC_PORT": 0x05,
    "CC_ARPM": 0x06, "CC_ARPR": 0x0A, "CC_ARPO": 0x0B, "CC_ARPE": 0x0C, "CC_ARPT": 0x1E,
    "CC_LFO2W": 0x0D, "CC_LFO2R": 0x0E, "CC_LFO2D": 0x0F, "CC_LFO2T": 0x10,
    "CC_UNIS": 0x29, "CC_UDET": 0x2A,
    "CC_CEN": 0x55, "CC_CDI": 0x56, "CC_CRE": 0x57,
    "CC_ASOF": 0x78, "CC_RACT": 0x79, "CC_ANOF": 0x7B, "CC_MONO": 0x7E, "CC_POLY": 0x7F,
}


class PatchEditor:
    def __init__(self, root):
        self.root = root
        root.title("USBSID-Pico Patch Editor")

        self.port = tk.StringVar(value=find_usbsid_port() or "")
        self.channel = tk.IntVar(value=1)      # MIDI channel, 1-16 (display); nibble = value-1
        self.patch_idx = tk.IntVar(value=16)   # default to first bank-1/user slot

        # Waveform / control register bits, each a BooleanVar keyed by its bit value
        self.contr_bits = {bit: tk.BooleanVar(value=(name == "Triangle")) for name, bit in WAVEFORM_BITS}
        self.contr_extra = {bit: tk.BooleanVar(value=False) for name, bit in CONTR_EXTRA_BITS}

        self.attack = tk.IntVar(value=0)
        self.decay = tk.IntVar(value=0)
        self.sustain = tk.IntVar(value=15)
        self.release = tk.IntVar(value=0)
        self.pwm = tk.IntVar(value=2048)  # 12 bit, 0-4095

        self.cutoff = tk.IntVar(value=0)
        self.resonance = tk.IntVar(value=0)
        self.filter_route = {bit: tk.BooleanVar(value=False) for name, bit in FILTER_ROUTE_BITS}
        self.filter_mode = {bit: tk.BooleanVar(value=False) for name, bit in FILTER_MODE_BITS}

        self.lfo_wave = tk.StringVar(value=LFO_WAVES[0])
        self.lfo_rate = tk.IntVar(value=32)
        self.lfo_depth = tk.IntVar(value=0)
        self.lfo_dest = tk.StringVar(value=LFO_DESTS[0])

        self.arp_mode = tk.StringVar(value=ARP_MODES[0])
        self.arp_rate = tk.IntVar(value=64)
        self.arp_octaves = tk.IntVar(value=0)

        self.bend_range = tk.IntVar(value=2)

        # LFO 2, same shape as LFO 1 above, appended to the patch struct.
        self.lfo2_wave = tk.StringVar(value=LFO_WAVES[0])
        self.lfo2_rate = tk.IntVar(value=32)
        self.lfo2_depth = tk.IntVar(value=0)
        self.lfo2_dest = tk.StringVar(value=LFO_DESTS[0])

        # Unison on/off + detune spread, appended to the patch struct.
        self.unison_enabled = tk.BooleanVar(value=False)
        self.unison_detune = tk.IntVar(value=0)

        self.test_note = tk.IntVar(value=0x3C)  # middle-ish note, matches midi_test.sh
        self.status = tk.StringVar(value="Ready.")
        self.raw_sysex_var = tk.StringVar(value="")

        # --- FMOpl - one IntVar/BooleanVar per meaningful
        # sub-field of opl_instrument_t (same "decompose the register into
        # its named bits" approach the Oscillator/Filter tabs use for the
        # SID side), packed into bytes by _collect_opl_values() below.
        # index 0 = modulator (op1), index 1 = carrier (op2) throughout.
        # Defaults mirror midi_fmopl.c's factory patch 0 ("Organ").
        self.opl_patch_idx = tk.IntVar(value=6)  # first non-factory slot (0-5 are built-in)
        self.opl_am = [tk.BooleanVar(value=False), tk.BooleanVar(value=False)]
        self.opl_vib = [tk.BooleanVar(value=False), tk.BooleanVar(value=False)]
        self.opl_egt = [tk.BooleanVar(value=True), tk.BooleanVar(value=True)]  # bit5, EG type (sustained)
        self.opl_ksr = [tk.BooleanVar(value=False), tk.BooleanVar(value=False)]
        self.opl_multiple = [tk.IntVar(value=1), tk.IntVar(value=1)]  # bits3:0 of op_mult, 0-15
        self.opl_ksl = [tk.IntVar(value=0), tk.IntVar(value=0)]       # bits7:6 of op_ksl_tl, 0-3
        self.opl_tl = [tk.IntVar(value=0x1C), tk.IntVar(value=0x00)]  # bits5:0 of op_ksl_tl, 0-63 (0=loudest)
        self.opl_attack = [tk.IntVar(value=0xF), tk.IntVar(value=0xF)]   # bits7:4 of op_ar_dr, 0-15
        self.opl_decay = [tk.IntVar(value=0), tk.IntVar(value=0)]        # bits3:0 of op_ar_dr, 0-15
        self.opl_sustain = [tk.IntVar(value=0), tk.IntVar(value=0)]      # bits7:4 of op_sl_rr, 0-15
        self.opl_release = [tk.IntVar(value=0xF), tk.IntVar(value=0xF)]  # bits3:0 of op_sl_rr, 0-15
        self.opl_wave = [tk.IntVar(value=0), tk.IntVar(value=0)]  # 0-3, OPL2 only has 4 waveforms
        self.opl_feedback = tk.IntVar(value=0)  # 0-7
        self.opl_algorithm = tk.BooleanVar(value=False)  # False=FM(series), True=additive(parallel)
        self.opl_clock_hz = tk.IntVar(value=0)  # 0 = firmware default (3579545); debugging/tuning override

        # --- Arp table - one IntVar per step offset (signed,
        # -128..127), matching midi_arp_table_t exactly. Slot 0 is the
        # firmware's own factory default (repeating major triad), so this
        # editor opens on slot 1, the first user slot, same "default to the
        # first non-factory slot" convention the FMOpl tab uses.
        self.arptable_idx = tk.IntVar(value=1)
        self.arptable_offsets = [tk.IntVar(value=0) for _ in range(ARP_TABLE_SLOT_COUNT)]
        self.arptable_loop_start = tk.IntVar(value=0)
        self.arptable_step_count = tk.IntVar(value=0)

        self._build_ui()
        self._refresh_ports()

        # Size the window to what it actually needs (the FMOpl tab in
        # particular is tall - two full operator sections plus the clock
        # override block) rather than a fixed guess that clips buttons and
        # the status bar below the visible area with no way to reach them
        # (nothing here is inside a scrolling container except the Live CC
        # tab). Clamped to the screen size so this doesn't request a window
        # taller than the display on a small/laptop screen.
        root.update_idletasks()
        want_w = min(root.winfo_reqwidth() + 20, root.winfo_screenwidth() - 40)
        want_h = min(root.winfo_reqheight() + 20, root.winfo_screenheight() - 80)
        root.geometry(f"{want_w}x{want_h}")
        root.minsize(720, 600)

    # --- value <-> field helpers -----------------------------------------

    def _contr_value(self):
        v = 0
        for bit, var in self.contr_bits.items():
            if var.get():
                v |= bit
        for bit, var in self.contr_extra.items():
            if var.get():
                v |= bit
        return v

    def _set_contr_value(self, v):
        for bit, var in self.contr_bits.items():
            var.set(bool(v & bit))
        for bit, var in self.contr_extra.items():
            var.set(bool(v & bit))

    def _route_value(self, d):
        v = 0
        for bit, var in d.items():
            if var.get():
                v |= bit
        return v

    def _set_route_value(self, d, v):
        for bit, var in d.items():
            var.set(bool(v & bit))

    def _collect_values(self):
        return {
            "tmpl_contr": self._contr_value(),
            "tmpl_attdec": ((self.attack.get() & 0xF) << 4) | (self.decay.get() & 0xF),
            "tmpl_susrel": ((self.sustain.get() & 0xF) << 4) | (self.release.get() & 0xF),
            "tmpl_pwmlo": self.pwm.get() & 0xFF,
            "tmpl_pwmhi": (self.pwm.get() >> 8) & 0xF,
            "filter_cutoff": self.cutoff.get() & 0x7FF,
            "resonance": self.resonance.get() & 0xF,
            "filter_routing": self._route_value(self.filter_route),
            "filter_mode": self._route_value(self.filter_mode),
            "lfo_wave": LFO_WAVES.index(self.lfo_wave.get()),
            "lfo_rate": self.lfo_rate.get() & 0x7F,
            "lfo_depth": self.lfo_depth.get() & 0x7F,
            "lfo_dest": LFO_DESTS.index(self.lfo_dest.get()),
            "arp_mode": ARP_MODES.index(self.arp_mode.get()),
            "arp_rate": self.arp_rate.get() & 0x7F,
            "arp_octaves": self.arp_octaves.get() & 0x3,
            "bend_range": self.bend_range.get() & 0x7F,
            "lfo2_wave": LFO_WAVES.index(self.lfo2_wave.get()),
            "lfo2_rate": self.lfo2_rate.get() & 0x7F,
            "lfo2_depth": self.lfo2_depth.get() & 0x7F,
            "lfo2_dest": LFO_DESTS.index(self.lfo2_dest.get()),
            "unison_enabled": 1 if self.unison_enabled.get() else 0,
            "unison_detune": self.unison_detune.get() & 0xFF,
        }

    def _apply_values(self, v):
        self._set_contr_value(v["tmpl_contr"])
        self.attack.set((v["tmpl_attdec"] >> 4) & 0xF)
        self.decay.set(v["tmpl_attdec"] & 0xF)
        self.sustain.set((v["tmpl_susrel"] >> 4) & 0xF)
        self.release.set(v["tmpl_susrel"] & 0xF)
        self.pwm.set(((v["tmpl_pwmhi"] & 0xF) << 8) | (v["tmpl_pwmlo"] & 0xFF))
        self.cutoff.set(v["filter_cutoff"])
        self.resonance.set(v["resonance"])
        self._set_route_value(self.filter_route, v["filter_routing"])
        self._set_route_value(self.filter_mode, v["filter_mode"])
        self.lfo_wave.set(LFO_WAVES[v["lfo_wave"] % len(LFO_WAVES)])
        self.lfo_rate.set(v["lfo_rate"])
        self.lfo_depth.set(v["lfo_depth"])
        self.lfo_dest.set(LFO_DESTS[v["lfo_dest"] % len(LFO_DESTS)])
        self.arp_mode.set(ARP_MODES[v["arp_mode"] % len(ARP_MODES)])
        self.arp_rate.set(v["arp_rate"])
        self.arp_octaves.set(v["arp_octaves"])
        self.bend_range.set(v["bend_range"])
        self.lfo2_wave.set(LFO_WAVES[v["lfo2_wave"] % len(LFO_WAVES)])
        self.lfo2_rate.set(v["lfo2_rate"])
        self.lfo2_depth.set(v["lfo2_depth"])
        self.lfo2_dest.set(LFO_DESTS[v["lfo2_dest"] % len(LFO_DESTS)])
        self.unison_enabled.set(bool(v["unison_enabled"]))
        self.unison_detune.set(v["unison_detune"])

    def _channel_nibble(self):
        return max(0, min(15, self.channel.get() - 1))

    # --- MIDI actions -------------------------------------------------------

    def _require_port(self):
        p = self.port.get().strip()
        if not p:
            messagebox.showerror("No port", "Select a MIDI port first (Refresh if the list is empty).")
            return None
        return p

    def _set_status(self, text):
        self.status.set(text)
        self.root.update_idletasks()

    def _refresh_ports(self):
        ports = list_ports()
        names = [p[1] for p in ports]
        self.port_combo["values"] = names
        auto = find_usbsid_port()
        if auto:
            for pid, desc in ports:
                if pid == auto:
                    self.port.set(desc)
                    break
        elif names:
            self.port.set(names[0])
        self._set_status(f"Found {len(ports)} MIDI port(s).")

    def _selected_port_id(self):
        # With RtMidi the port "id" is just its name - no separate short id
        # the way amidi's "hw:3,0,0" was, so no parsing needed here.
        return self.port.get().strip()

    def _run_async(self, fn):
        threading.Thread(target=fn, daemon=True).start()

    def send_patch(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        idx = self.patch_idx.get() & 0x1F
        if idx < 16:
            if not messagebox.askyesno(
                "Overwrite factory patch",
                f"Slot {idx} is a factory patch (0-15), not a user slot. Sending overwrites it in "
                f"RAM (SysEx SAVE would persist that). Continue?",
            ):
                return
        hexstr = build_load_sysex(idx, self._collect_values())
        self._set_status(f"Sending patch to slot {idx} (0x{idx:02x})...")

        def work():
            send_hex(port, hexstr)
            self._set_status(f"Sent patch to slot {idx} (0x{idx:02x}). "
                              f"Not saved to flash - use 'Save to flash' to persist it.")
        self._run_async(work)

    def dump_patch(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        idx = self.patch_idx.get() & 0x1F
        self._set_status(f"Requesting slot {idx} (0x{idx:02x}) from device...")

        def work():
            reply = send_hex_and_dump(port, build_dump_sysex(idx))
            result = parse_dump_reply(reply, expected_idx=idx)
            if result is None:
                self._set_status("No reply received (timed out, or the device sent nothing back - "
                                  "check the port and that MIDI is enabled on the device).")
                return
            got_idx, values = result
            self.root.after(0, lambda: self._apply_values(values))
            self.root.after(0, lambda: self._set_status(f"Loaded slot {got_idx} (0x{got_idx:02x}) from device."))
        self._run_async(work)

    def _raw_sysex_text(self):
        return self.raw_sysex_var.get().strip()

    def send_raw_sysex(self):
        """Sends whatever is typed/pasted in the raw SysEx field verbatim -
        bypasses every GUI field entirely, so a preset copied from
        midibox_sid_presets.md (or anywhere else) can be sent as-is."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        text = self._raw_sysex_text()
        if not text:
            messagebox.showerror("Empty", "Paste a SysEx hex string into the raw field first.")
            return
        hexbytes = re.findall(r"[0-9a-fA-F]{2}", text)
        if not hexbytes:
            messagebox.showerror("Not hex", "Raw field doesn't look like hex bytes (e.g. 'f0 50 20 10 ... f7').")
            return
        hexstr = " ".join(hexbytes)
        self._set_status(f"Sending raw SysEx ({len(hexbytes)} bytes)...")
        self._run_async(lambda: (send_hex(port, hexstr),
                                  self._set_status(f"Sent raw SysEx ({len(hexbytes)} bytes) to device.")))

    def load_raw_sysex_into_editor(self):
        """Parses the raw SysEx field's patch data (0x20 LOAD or 0x22 DATA
        framing, either works) into the GUI fields, without sending anything
        to the device - lets you inspect/tweak a pasted preset before it's
        sent for real."""
        text = self._raw_sysex_text()
        if not text:
            messagebox.showerror("Empty", "Paste a SysEx hex string into the raw field first.")
            return
        result = parse_patch_sysex(text, commands=(0x20, 0x22))
        if result is None:
            messagebox.showerror(
                "No patch data found",
                f"Couldn't find a {PATCH_NIBBLE_COUNT}-nibble patch payload (F0 50 20/22 <slot> "
                f"<{PATCH_NIBBLE_COUNT} nibbles> F7) in the raw field.",
            )
            return
        _cmd, idx, values = result
        self._apply_values(values)
        self.patch_idx.set(idx & 0x1F)
        self._set_status(f"Loaded slot {idx} (0x{idx:02x}) from raw SysEx into the editor (not sent to device).")

    # --- FMOpl -----------------------------------------------------------

    def _collect_opl_values(self):
        def pack_mult(i):
            v = self.opl_multiple[i].get() & 0xF
            if self.opl_am[i].get(): v |= 0x80
            if self.opl_vib[i].get(): v |= 0x40
            if self.opl_egt[i].get(): v |= 0x20
            if self.opl_ksr[i].get(): v |= 0x10
            return v

        def pack_ksl_tl(i):
            return ((self.opl_ksl[i].get() & 0x3) << 6) | (self.opl_tl[i].get() & 0x3F)

        def pack_ar_dr(i):
            return ((self.opl_attack[i].get() & 0xF) << 4) | (self.opl_decay[i].get() & 0xF)

        def pack_sl_rr(i):
            return ((self.opl_sustain[i].get() & 0xF) << 4) | (self.opl_release[i].get() & 0xF)

        return {
            "op_mult_0": pack_mult(0), "op_mult_1": pack_mult(1),
            "op_ksl_tl_0": pack_ksl_tl(0), "op_ksl_tl_1": pack_ksl_tl(1),
            "op_ar_dr_0": pack_ar_dr(0), "op_ar_dr_1": pack_ar_dr(1),
            "op_sl_rr_0": pack_sl_rr(0), "op_sl_rr_1": pack_sl_rr(1),
            "op_wave_0": self.opl_wave[0].get() & 0x3, "op_wave_1": self.opl_wave[1].get() & 0x3,
            "feedback_algo": ((self.opl_feedback.get() & 0x7) << 1) | (1 if self.opl_algorithm.get() else 0),
        }

    def _apply_opl_values(self, v):
        for i, suffix in enumerate(("_0", "_1")):
            mult = v[f"op_mult{suffix}"]
            self.opl_am[i].set(bool(mult & 0x80))
            self.opl_vib[i].set(bool(mult & 0x40))
            self.opl_egt[i].set(bool(mult & 0x20))
            self.opl_ksr[i].set(bool(mult & 0x10))
            self.opl_multiple[i].set(mult & 0xF)

            ksl_tl = v[f"op_ksl_tl{suffix}"]
            self.opl_ksl[i].set((ksl_tl >> 6) & 0x3)
            self.opl_tl[i].set(ksl_tl & 0x3F)

            ar_dr = v[f"op_ar_dr{suffix}"]
            self.opl_attack[i].set((ar_dr >> 4) & 0xF)
            self.opl_decay[i].set(ar_dr & 0xF)

            sl_rr = v[f"op_sl_rr{suffix}"]
            self.opl_sustain[i].set((sl_rr >> 4) & 0xF)
            self.opl_release[i].set(sl_rr & 0xF)

            self.opl_wave[i].set(v[f"op_wave{suffix}"] & 0x3)

        fa = v["feedback_algo"]
        self.opl_feedback.set((fa >> 1) & 0x7)
        self.opl_algorithm.set(bool(fa & 0x1))

    def send_opl_patch(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        idx = self.opl_patch_idx.get() & 0x1F
        if idx < 6:
            if not messagebox.askyesno(
                "Overwrite factory instrument",
                f"Slot {idx} is one of the 6 built-in factory instruments (0-5). Sending overwrites "
                f"it in RAM. Continue?",
            ):
                return
        hexstr = build_opl_load_sysex(idx, self._collect_opl_values())
        self._set_status(f"Sending FMOpl patch to slot {idx} (0x{idx:02x})...")
        self._run_async(lambda: (send_hex(port, hexstr),
                                  self._set_status(f"Sent FMOpl patch to slot {idx} (0x{idx:02x}). "
                                                    f"Not saved to flash.")))

    def dump_opl_patch(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        idx = self.opl_patch_idx.get() & 0x1F
        self._set_status(f"Requesting FMOpl slot {idx} (0x{idx:02x}) from device...")

        def work():
            reply = send_hex_and_dump(port, build_opl_dump_sysex(idx))
            result = parse_opl_dump_reply(reply, expected_idx=idx)
            if result is None:
                self._set_status("No reply received (timed out, or the device sent nothing back - "
                                  "check the port, that MIDI is enabled, and that an FMOpl chip is "
                                  "actually configured on the device).")
                return
            got_idx, values = result
            self.root.after(0, lambda: self._apply_opl_values(values))
            self.root.after(0, lambda: self._set_status(f"Loaded FMOpl slot {got_idx} (0x{got_idx:02x}) from device."))
        self._run_async(work)

    def load_opl_raw_into_editor(self):
        """Same idea as load_raw_sysex_into_editor() but for the FMOpl patch
        frame shape (F0 50 23/25 <slot> <22 nibbles> F7) - shares the same
        raw SysEx text field, since both are just hex a user might paste."""
        text = self._raw_sysex_text()
        if not text:
            messagebox.showerror("Empty", "Paste a SysEx hex string into the raw field first.")
            return
        result = parse_patch_sysex(text, commands=(0x23, 0x25),
                                    nibble_count=FMOPL_NIBBLE_COUNT, decoder=decode_opl_instrument)
        if result is None:
            messagebox.showerror(
                "No FMOpl patch data found",
                "Couldn't find a 22-nibble FMOpl patch payload (F0 50 23/25 <slot> <22 nibbles> F7) "
                "in the raw field.",
            )
            return
        _cmd, idx, values = result
        self._apply_opl_values(values)
        self.opl_patch_idx.set(idx & 0x1F)
        self._set_status(f"Loaded FMOpl slot {idx} (0x{idx:02x}) from raw SysEx into the editor (not sent).")

    def select_opl_active(self):
        """Program Change on the current channel, selecting this FMOpl patch
        slot - only takes effect on a channel that has CC_FMEN (target
        FMOpl) enabled; use the button below to enable it first."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        idx = self.opl_patch_idx.get() & 0x1F
        hexstr = f"c{ch:x} {idx:02x}"
        self._set_status(f"Program Change: channel {self.channel.get()} -> FMOpl patch {idx} (0x{idx:02x})")
        self._run_async(lambda: send_hex(port, hexstr))

    def capture_opl_patch_from_channel(self):
        """SYSEX_FMOPL_PATCH_SAVE: captures the channel's currently-selected
        FMOpl instrument into the patch slot shown on this tab. Today this
        is a plain duplicate of that instrument, not a live-tweaks capture -
        there is no per-operator live editing yet, only Program Change
        instrument selection (see midi_fmopl.c's own doc comment on
        midi_fmopl_capture_patch()). Still useful for starting a new
        instrument from an existing one. RAM only; use 'Save to flash'
        afterward to persist it."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        idx = self.opl_patch_idx.get() & 0x1F
        if idx < 6:
            if not messagebox.askyesno(
                "Overwrite factory instrument",
                f"Slot {idx} is one of the 6 built-in factory instruments (0-5). Capturing overwrites "
                f"it in RAM. Continue?",
            ):
                return
        hexstr = build_fmopl_patch_save_sysex(ch, idx)
        self._set_status(f"Capturing channel {self.channel.get()}'s instrument into FMOpl patch {idx} (0x{idx:02x})...")
        self._run_async(lambda: (send_hex(port, hexstr),
                                  self._set_status(f"Captured channel {self.channel.get()}'s instrument into "
                                                    f"FMOpl patch {idx}. Not saved to flash.")))

    def _pick_from_list(self, title, items):
        """Modal picker: items is a list of display strings. Returns the
        chosen index or None if cancelled. Used for bank files (.op2/.wopl)
        which hold many instruments - a single-instrument file (.s3i) never
        needs this."""
        win = tk.Toplevel(self.root)
        win.title(title)
        win.transient(self.root)
        win.grab_set()
        win.geometry("500x400")

        search_var = tk.StringVar(value="")
        ttk.Entry(win, textvariable=search_var).pack(fill="x", padx=6, pady=(6, 2))

        listbox = tk.Listbox(win)
        listbox.pack(fill="both", expand=True, padx=6, pady=4)
        scrollbar = ttk.Scrollbar(win, orient="vertical", command=listbox.yview)
        listbox.configure(yscrollcommand=scrollbar.set)

        filtered = list(range(len(items)))

        def refresh(*_):
            q = search_var.get().lower()
            listbox.delete(0, tk.END)
            filtered.clear()
            for i, s in enumerate(items):
                if q in s.lower():
                    filtered.append(i)
                    listbox.insert(tk.END, s)
        search_var.trace_add("write", refresh)
        refresh()

        result = {"index": None}

        def choose(_event=None):
            sel = listbox.curselection()
            if sel:
                result["index"] = filtered[sel[0]]
            win.destroy()

        listbox.bind("<Double-Button-1>", choose)
        btn_row = ttk.Frame(win); btn_row.pack(fill="x", padx=6, pady=(0, 6))
        ttk.Button(btn_row, text="Select", command=choose).pack(side="left")
        ttk.Button(btn_row, text="Cancel", command=win.destroy).pack(side="left", padx=6)

        self.root.wait_window(win)
        return result["index"]

    def import_opl_from_disk(self):
        """Opens a file picker, parses an OPL instrument/bank file, and
        loads the chosen instrument into the editor fields - does not send
        anything to the device on its own, same as the raw-SysEx "load into
        editor" buttons. .s3i is a single instrument, applied directly;
        .op2/.wopl are banks, so a picker dialog lists every instrument name
        inside for you to choose from."""
        path = filedialog.askopenfilename(
            title="Import OPL instrument",
            filetypes=[
                ("All supported", "*.s3i *.op2 *.wopl"),
                ("Scream Tracker 3 instrument", "*.s3i"),
                ("DMX GENMIDI bank", "*.op2"),
                ("OPL3BankEditor/libADLMIDI bank", "*.wopl"),
                ("All files", "*.*"),
            ],
        )
        if not path:
            return
        try:
            data = open(path, "rb").read()
        except OSError as e:
            messagebox.showerror("Can't read file", str(e))
            return

        ext = os.path.splitext(path)[1].lower()
        basename = os.path.basename(path)
        try:
            if ext == ".s3i":
                name, values = parse_s3i(data)
            elif ext == ".op2":
                entries = parse_op2_bank(data)  # [(index, name, values), ...]
                labels = [f"{i:3d}  {n or '(unnamed)'}" for i, n, _v in entries]
                choice = self._pick_from_list(f"Choose an instrument from {basename}", labels)
                if choice is None:
                    return
                _idx, name, values = entries[choice]
            elif ext == ".wopl":
                wentries = parse_wopl_bank(data)  # [(bank_name, is_percussion, index, name, values), ...]
                labels = [f"{'PERC' if p else 'MEL '} {i:3d}  {n}" +
                          (f"  [{bn}]" if bn else "") for bn, p, i, n, _v in wentries]
                choice = self._pick_from_list(f"Choose an instrument from {basename}", labels)
                if choice is None:
                    return
                _bn, _p, _i, name, values = wentries[choice]
            else:
                raise S3IError(f"unrecognised extension '{ext}' - supported: .s3i, .op2, .wopl")
        except (S3IError, OP2Error, WOPLError) as e:
            messagebox.showerror("Import failed", f"{basename}: {e}")
            return

        self._apply_opl_values(values)
        label = f"'{name}'" if name else "(unnamed)"
        self._set_status(f"Imported {label} from {basename} into the FMOpl editor (not sent).")

    def set_fmopl_target(self, enable):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        value = 0x7F if enable else 0x00
        # CC_FMEN = 88 (repo/src/midi_defs.h default) - targets this channel's
        # notes at the FMOpl chip instead of a SID.
        hexstr = f"b{ch:x} 58 {value:02x}"
        self._set_status(f"{'Enabling' if enable else 'Disabling'} FMOpl target on channel {self.channel.get()}...")
        self._run_async(lambda: send_hex(port, hexstr))

    def send_fmopl_clock(self):
        """SYSEX_FMOPL_SET_CLOCK - a debugging/tuning override, not a real
        setting. Live-rewrites every currently held OPL voice on the device
        immediately, so this can be swept while a note is ringing (e.g. via
        play_test_note) to find the value that actually sounds in tune."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        try:
            hz = int(self.opl_clock_hz.get())
        except (tk.TclError, ValueError):
            messagebox.showerror("Not a number", "Clock override must be a whole number of Hz.")
            return
        if hz < 0 or hz > 0xFFFFFF:
            messagebox.showerror("Out of range", "Clock override must be 0-16777215 Hz (24-bit).")
            return
        hexstr = build_fmopl_clock_sysex(hz)
        self._set_status(f"Setting FMOpl clock override to {hz} Hz...")
        self._run_async(lambda: send_hex(port, hexstr))

    def reset_fmopl_clock(self):
        self.opl_clock_hz.set(0)
        self.send_fmopl_clock()

    # --- Arp table ---------------------------------------------------------

    def _collect_arptable_values(self):
        return {
            "offsets": [v.get() for v in self.arptable_offsets],
            "loop_start": self.arptable_loop_start.get() & 0xFF,
            "step_count": self.arptable_step_count.get() & 0xFF,
        }

    def _apply_arptable_values(self, v):
        offsets = v["offsets"]
        for i, var in enumerate(self.arptable_offsets):
            var.set(offsets[i] if i < len(offsets) else 0)
        self.arptable_loop_start.set(v["loop_start"])
        self.arptable_step_count.set(v["step_count"])

    def send_arptable(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        idx = self.arptable_idx.get() & 0x0F
        if idx == 0:
            if not messagebox.askyesno(
                "Overwrite factory table",
                "Slot 0 is the firmware's factory default (repeating major triad). Sending "
                "overwrites it in RAM. Continue?",
            ):
                return
        v = self._collect_arptable_values()
        if v["step_count"] > ARP_TABLE_SLOT_COUNT:
            messagebox.showerror("Out of range", f"Step count must be 0-{ARP_TABLE_SLOT_COUNT}.")
            return
        if v["step_count"] > 0 and v["loop_start"] >= v["step_count"]:
            messagebox.showerror("Out of range", "Loop start must be less than step count.")
            return
        hexstr = build_arptable_load_sysex(idx, v["offsets"], v["loop_start"], v["step_count"])
        self._set_status(f"Sending arp table to slot {idx}...")
        self._run_async(lambda: (send_hex(port, hexstr),
                                  self._set_status(f"Sent arp table to slot {idx}. Not saved to flash.")))

    def dump_arptable(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        idx = self.arptable_idx.get() & 0x0F
        self._set_status(f"Requesting arp table slot {idx} from device...")

        def work():
            reply = send_hex_and_dump(port, build_arptable_dump_sysex(idx))
            print(f"[arptable dump] raw reply ({len(reply.split()) if reply else 0} bytes): {reply!r}")
            result = parse_arptable_dump_reply(reply, expected_idx=idx)
            if result is None:
                nbytes = len(reply.split()) if reply else 0
                self._set_status(f"No valid reply parsed for slot {idx} ({nbytes} bytes received - "
                                  "see console for raw hex). Check the port and that MIDI is enabled "
                                  "on the device.")
                return
            got_idx, values = result
            self.root.after(0, lambda: self._apply_arptable_values(values))
            self.root.after(0, lambda: self._set_status(f"Loaded arp table slot {got_idx} from device."))
        self._run_async(work)

    def select_arptable_on_channel(self):
        """CC_ARPT (table select) followed by CC_ARPM = Table, on the current
        channel - the two-step "pick a table, switch mode to use it" this
        firmware's set_arp_table()/set_arp_mode() (midi_handler.c) split
        into two independent CCs, since selecting a table does not itself
        switch a channel into table mode."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        idx = self.arptable_idx.get() & 0x0F
        # CC_ARPT maps CC 0-127 onto 0..MIDI_ARP_TABLE_COUNT-1 (set_arp_table(),
        # midi_handler.c) - invert that MAP() to land exactly back on `idx`.
        cc_value = min(127, (idx * 127) // max(1, ARP_TABLE_SLOT_COUNT - 1))
        self._run_async(lambda: (
            send_hex(port, f"b{ch:x} 1e {cc_value:02x}"),   # CC_ARPT = idx
            send_hex(port, f"b{ch:x} 06 7f"),                # CC_ARPM = Table (top of its range)
        ))
        self._set_status(f"Channel {self.channel.get()}: arp table {idx}, mode -> Table. "
                          f"CC_ARPE must also be enabled for the arpeggiator to actually run.")

    def select_active_patch(self):
        """Program Change: makes the currently loaded slot the channel's active sound."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        idx = self.patch_idx.get() & 0x1F
        hexstr = f"c{ch:x} {idx:02x}"
        self._set_status(f"Program Change: channel {self.channel.get()} -> patch {idx} (0x{idx:02x})")
        self._run_async(lambda: send_hex(port, hexstr))

    def capture_patch_from_channel(self):
        """SYSEX_MIDI_PATCH_SAVE: captures the channel's current live state
        (timbre/filter/LFO/arp/bend/unison) into the patch slot shown at the
        top of the window - the inverse of Program Change. RAM only; use
        'Save to flash' afterward to persist it. Does not touch this
        editor's own fields - read the patch back afterward if you want to
        see what got captured."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        idx = self.patch_idx.get() & 0x1F
        if idx < 16:
            if not messagebox.askyesno(
                "Overwrite factory patch",
                f"Slot {idx} is a factory patch (0-15), not a user slot. Capturing overwrites it in "
                f"RAM. Continue?",
            ):
                return
        hexstr = build_patch_save_sysex(ch, idx)
        self._set_status(f"Capturing channel {self.channel.get()}'s live state into patch {idx} (0x{idx:02x})...")
        self._run_async(lambda: (send_hex(port, hexstr),
                                  self._set_status(f"Captured channel {self.channel.get()} into patch {idx}. "
                                                    f"Not saved to flash - use 'Save to flash' to persist it.")))

    def play_test_note(self, hold_s=1.0):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        note = self.test_note.get() & 0x7F

        def work():
            send_hex(port, f"9{ch:x} {note:02x} 60")
            self._set_status(f"Playing note 0x{note:02x} on channel {self.channel.get()}...")
            import time
            time.sleep(hold_s)
            send_hex(port, f"8{ch:x} {note:02x} 00")
            self._set_status("Note released.")
        self._run_async(work)

    def send_volume_on(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        # CC_VOL = 0x07 (repo/src/midi_defs.h). The board resets SIDs to a
        # clean startup state and expects a MIDI client to send its own
        # volume first - same convention midi_test.sh's t_volume_on uses.
        self._run_async(lambda: send_hex(port, f"b{ch:x} 07 7f"))
        self._set_status("Sent CC7 (volume) = 127.")

    def flash_action(self, cmd, label):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        if cmd == 0x12:
            if not messagebox.askyesno(
                "Confirm reset",
                "This erases ALL 16 MIDI flash save slots and resets every patch and channel to "
                "compiled defaults. This cannot be undone. Continue?",
            ):
                return
        self._set_status(f"{label}...")
        self._run_async(lambda: send_hex(port, f"f0 50 {cmd:02x} f7"))

    def send_midi_system_reset(self):
        """MIDI System Reset (0xFF, a real-time status byte - no manufacturer
        wrapper, no data bytes). repo/src/midi.c's 0xFF case calls
        midi_processor_init() - resets every channel to compiled defaults,
        then midi_config_load() re-applies the last-saved flash state if one
        exists. Live, not a flash write itself: does not touch what's saved,
        only what's currently running - a held note or in-progress
        performance will be cut."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        if not messagebox.askyesno(
            "Confirm MIDI System Reset",
            "This resets every channel's live state on the device (back to compiled defaults, then "
            "whatever was last saved to flash, if anything). Any held note or in-progress performance "
            "will be cut. Flash contents themselves are untouched. Continue?",
        ):
            return
        self._set_status("Sending MIDI System Reset (0xFF)...")
        self._run_async(lambda: (send_hex(port, "ff"),
                                  self._set_status("Sent MIDI System Reset (0xFF).")))

    # --- UI construction ------------------------------------------------

    def _build_ui(self):
        pad = {"padx": 6, "pady": 4}

        top = ttk.Frame(self.root)
        top.pack(fill="x", **pad)
        ttk.Label(top, text="MIDI port:").pack(side="left")
        self.port_combo = ttk.Combobox(top, textvariable=self.port, width=40, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        ttk.Button(top, text="Refresh", command=self._refresh_ports).pack(side="left")
        ttk.Label(top, text="  Channel:").pack(side="left")
        ttk.Spinbox(top, from_=1, to=16, textvariable=self.channel, width=4).pack(side="left")
        ttk.Label(top, text="  Patch slot (0-31, 16-31=user):").pack(side="left")
        ttk.Spinbox(top, from_=0, to=31, textvariable=self.patch_idx, width=4).pack(side="left")
        ttk.Label(top, text="  Factory (0-15):").pack(side="left")
        factory_combo = ttk.Combobox(top, values=[str(i) for i in range(16)], width=3, state="readonly")
        factory_combo.pack(side="left")

        def read_factory(_event=None):
            v = factory_combo.get()
            if v == "":
                return
            self.patch_idx.set(int(v))
            self.dump_patch()
        factory_combo.bind("<<ComboboxSelected>>", read_factory)

        nb = ttk.Notebook(self.root)
        nb.pack(fill="both", expand=True, **pad)

        self._build_osc_tab(nb)
        self._build_filter_tab(nb)
        self._build_mod_tab(nb)
        self._build_arptable_tab(nb)
        self._build_fmopl_tab(nb)
        self._build_cc_tab(nb)

        actions = ttk.LabelFrame(self.root, text="Actions")
        actions.pack(fill="x", **pad)
        row1 = ttk.Frame(actions); row1.pack(fill="x", pady=2)
        ttk.Button(row1, text="Send patch to device (RAM)", command=self.send_patch).pack(side="left", padx=3)
        ttk.Button(row1, text="Read patch from device", command=self.dump_patch).pack(side="left", padx=3)
        ttk.Button(row1, text="Select as active (Program Change)", command=self.select_active_patch).pack(side="left", padx=3)
        ttk.Button(row1, text="Capture channel's live state -> this patch (SAVE)", command=self.capture_patch_from_channel).pack(side="left", padx=3)

        row2 = ttk.Frame(actions); row2.pack(fill="x", pady=2)
        ttk.Label(row2, text="Test note (hex):").pack(side="left")
        ttk.Entry(row2, textvariable=self.test_note_hex(), width=5).pack(side="left")
        ttk.Button(row2, text="Play test note (1s)", command=self.play_test_note).pack(side="left", padx=3)
        ttk.Button(row2, text="Send volume on (CC7=127)", command=self.send_volume_on).pack(side="left", padx=3)

        row3 = ttk.Frame(actions); row3.pack(fill="x", pady=2)
        ttk.Label(row3, text="Flash:").pack(side="left")
        ttk.Button(row3, text="Save to flash", command=lambda: self.flash_action(0x10, "Saving to flash")).pack(side="left", padx=3)
        ttk.Button(row3, text="Load from flash", command=lambda: self.flash_action(0x11, "Loading from flash")).pack(side="left", padx=3)
        ttk.Button(row3, text="Factory reset (erases everything)", command=lambda: self.flash_action(0x12, "Resetting")).pack(side="left", padx=3)
        ttk.Button(row3, text="Send MIDI System Reset (0xFF)", command=self.send_midi_system_reset).pack(side="left", padx=3)

        raw = ttk.LabelFrame(self.root, text="Raw SysEx (paste a preset from midibox_sid_presets.md or anywhere else)")
        raw.pack(fill="x", **pad)
        raw_row = ttk.Frame(raw); raw_row.pack(fill="x", pady=2)
        ttk.Entry(raw_row, textvariable=self.raw_sysex_var).pack(side="left", fill="x", expand=True, padx=(0, 6))
        ttk.Button(raw_row, text="Send to device", command=self.send_raw_sysex).pack(side="left", padx=3)
        ttk.Button(raw_row, text="Load into Patch editor (no send)", command=self.load_raw_sysex_into_editor).pack(side="left", padx=3)
        ttk.Button(raw_row, text="Load into FMOpl editor (no send)", command=self.load_opl_raw_into_editor).pack(side="left", padx=3)

        status_bar = ttk.Frame(self.root)
        status_bar.pack(fill="x", **pad)
        ttk.Label(status_bar, textvariable=self.status, relief="sunken", anchor="w").pack(fill="x")

    def test_note_hex(self):
        # Bridges the hex-typing Entry to the underlying int IntVar.
        if not hasattr(self, "_test_note_hex_var"):
            self._test_note_hex_var = tk.StringVar(value=f"{self.test_note.get():02x}")

            def on_write(*_):
                try:
                    self.test_note.set(int(self._test_note_hex_var.get(), 16) & 0x7F)
                except ValueError:
                    pass
            self._test_note_hex_var.trace_add("write", on_write)
        return self._test_note_hex_var

    def _build_osc_tab(self, nb):
        f = ttk.Frame(nb)
        nb.add(f, text="Oscillator / Envelope")

        wf = ttk.LabelFrame(f, text="Waveform (combinable, matches real SID hardware)")
        wf.pack(fill="x", **{"padx": 6, "pady": 4})
        for name, bit in WAVEFORM_BITS:
            ttk.Checkbutton(wf, text=name, variable=self.contr_bits[bit]).pack(side="left", padx=6)
        for name, bit in CONTR_EXTRA_BITS:
            ttk.Checkbutton(wf, text=name, variable=self.contr_extra[bit]).pack(side="left", padx=6)

        pwm = ttk.LabelFrame(f, text="Pulse width (only matters if Pulse is checked above)")
        pwm.pack(fill="x", **{"padx": 6, "pady": 4})
        self._slider(pwm, "PWM", self.pwm, 0, 4095)

        env = ttk.LabelFrame(f, text="Envelope (ADSR, each 0-15)")
        env.pack(fill="x", **{"padx": 6, "pady": 4})
        self._slider(env, "Attack", self.attack, 0, 15)
        self._slider(env, "Decay", self.decay, 0, 15)
        self._slider(env, "Sustain", self.sustain, 0, 15)
        self._slider(env, "Release", self.release, 0, 15)

        bend = ttk.LabelFrame(f, text="Pitch bend")
        bend.pack(fill="x", **{"padx": 6, "pady": 4})
        self._slider(bend, "Bend range (semitones)", self.bend_range, 0, 24)

    def _build_filter_tab(self, nb):
        f = ttk.Frame(nb)
        nb.add(f, text="Filter")

        warn = ttk.Label(
            f, foreground="#a00",
            text="A voice routed into the filter with no mode selected below is SILENT - real SID\n"
                 "hardware behaviour, not a bug. If you check any routing box, check a mode too.",
            justify="left",
        )
        warn.pack(fill="x", padx=6, pady=(6, 0))

        cf = ttk.LabelFrame(f, text="Cutoff / Resonance")
        cf.pack(fill="x", **{"padx": 6, "pady": 4})
        self._slider(cf, "Cutoff", self.cutoff, 0, 2047)
        self._slider(cf, "Resonance", self.resonance, 0, 15)

        rt = ttk.LabelFrame(f, text="Routing (which voice position feeds the filter - "
                                     "position, not this specific note; check all three for reliability)")
        rt.pack(fill="x", **{"padx": 6, "pady": 4})
        for name, bit in FILTER_ROUTE_BITS:
            ttk.Checkbutton(rt, text=name, variable=self.filter_route[bit]).pack(side="left", padx=6)

        md = ttk.LabelFrame(f, text="Mode (required if any routing box above is checked)")
        md.pack(fill="x", **{"padx": 6, "pady": 4})
        for name, bit in FILTER_MODE_BITS:
            ttk.Checkbutton(md, text=name, variable=self.filter_mode[bit]).pack(side="left", padx=6)

    def _build_mod_tab(self, nb):
        f = ttk.Frame(nb)
        nb.add(f, text="LFO / Arpeggiator")

        lfo = ttk.LabelFrame(f, text="LFO (depth 0 = off)")
        lfo.pack(fill="x", **{"padx": 6, "pady": 4})
        row = ttk.Frame(lfo); row.pack(fill="x", pady=2)
        ttk.Label(row, text="Waveform:").pack(side="left")
        ttk.Combobox(row, textvariable=self.lfo_wave, values=LFO_WAVES, state="readonly", width=14).pack(side="left", padx=4)
        ttk.Label(row, text="  Destination:").pack(side="left")
        ttk.Combobox(row, textvariable=self.lfo_dest, values=LFO_DESTS, state="readonly", width=10).pack(side="left", padx=4)
        self._slider(lfo, "Rate", self.lfo_rate, 0, 127)
        self._slider(lfo, "Depth", self.lfo_depth, 0, 127)

        arp = ttk.LabelFrame(f, text="Arpeggiator (only runs once the channel's CC_ARPE separately "
                                       "enables it - a patch alone cannot turn arp on; 'Table' mode "
                                       "reads whichever slot the Arp Table tab's editor authors)")
        arp.pack(fill="x", **{"padx": 6, "pady": 4})
        row = ttk.Frame(arp); row.pack(fill="x", pady=2)
        ttk.Label(row, text="Mode:").pack(side="left")
        ttk.Combobox(row, textvariable=self.arp_mode, values=ARP_MODES, state="readonly", width=12).pack(side="left", padx=4)
        self._slider(arp, "Rate", self.arp_rate, 0, 127)
        self._slider(arp, "Octaves", self.arp_octaves, 0, 3)

        lfo2 = ttk.LabelFrame(f, text="LFO 2 (depth 0 = off; stacks with LFO 1 above if "
                                        "both target the same destination)")
        lfo2.pack(fill="x", **{"padx": 6, "pady": 4})
        row = ttk.Frame(lfo2); row.pack(fill="x", pady=2)
        ttk.Label(row, text="Waveform:").pack(side="left")
        ttk.Combobox(row, textvariable=self.lfo2_wave, values=LFO_WAVES, state="readonly", width=14).pack(side="left", padx=4)
        ttk.Label(row, text="  Destination:").pack(side="left")
        ttk.Combobox(row, textvariable=self.lfo2_dest, values=LFO_DESTS, state="readonly", width=10).pack(side="left", padx=4)
        self._slider(lfo2, "Rate", self.lfo2_rate, 0, 127)
        self._slider(lfo2, "Depth", self.lfo2_depth, 0, 127)

        unison = ttk.LabelFrame(f, text="Unison (3 real oscillators per note; the SID this "
                                          "channel lands on drops to 1 note of polyphony while active)")
        unison.pack(fill="x", **{"padx": 6, "pady": 4})
        ttk.Checkbutton(unison, text="Unison mode on", variable=self.unison_enabled).pack(side="left", padx=6, pady=4)
        self._slider(unison, "Detune spread", self.unison_detune, 0, 255)

    def _build_arptable_tab(self, nb):
        f = ttk.Frame(nb)
        nb.add(f, text="Arp Table")

        intro = ttk.Label(
            f, justify="left", wraplength=680,
            text="GoatTracker/SidWizard-style arp table: a sequence of relative semitone "
                 "offsets from the arpeggiator's held root note, instead of one of the 5 fixed shapes. "
                 "A channel only reads this once its CC_ARPM is set to 'Table' (see the LFO / "
                 "Arpeggiator tab, or the button below) and CC_ARPE separately enables the arpeggiator.",
        )
        intro.pack(fill="x", padx=6, pady=(6, 2))

        slot_row = ttk.Frame(f); slot_row.pack(fill="x", padx=6, pady=2)
        ttk.Label(slot_row, text=f"Table slot (0-{ARP_TABLE_SLOT_COUNT - 1}, 0 = factory default):").pack(side="left")
        ttk.Spinbox(slot_row, from_=0, to=ARP_TABLE_SLOT_COUNT - 1, textvariable=self.arptable_idx, width=4).pack(side="left", padx=4)

        steps = ttk.LabelFrame(f, text="Step offsets (relative semitones from the root, -128..127)")
        steps.pack(fill="x", padx=6, pady=4)
        for i, var in enumerate(self.arptable_offsets):
            cell = ttk.Frame(steps); cell.grid(row=i // 8, column=i % 8, padx=4, pady=2)
            ttk.Label(cell, text=str(i), width=2, anchor="e").pack(side="left")
            ttk.Spinbox(cell, from_=-128, to=127, textvariable=var, width=5).pack(side="left")

        shape = ttk.LabelFrame(f, text="Shape")
        shape.pack(fill="x", padx=6, pady=4)
        self._slider(shape, "Step count (0 = unauthored/inert)", self.arptable_step_count, 0, ARP_TABLE_SLOT_COUNT)
        self._slider(shape, "Loop start (returns here after the last step)", self.arptable_loop_start, 0, ARP_TABLE_SLOT_COUNT - 1)

        actions = ttk.Frame(f); actions.pack(fill="x", padx=6, pady=6)
        ttk.Button(actions, text="Send table to device (RAM)", command=self.send_arptable).pack(side="left", padx=3)
        ttk.Button(actions, text="Read table from device", command=self.dump_arptable).pack(side="left", padx=3)
        ttk.Button(actions, text="Select on channel (CC_ARPT + mode=Table)", command=self.select_arptable_on_channel).pack(side="left", padx=3)

    def _build_fmopl_tab(self, nb):
        f = ttk.Frame(nb)
        nb.add(f, text="FMOpl")

        intro = ttk.Label(
            f, justify="left", wraplength=680,
            text="A channel must have FMOpl targeting enabled (below) before Program Change or "
                 "notes on it reach the OPL chip at all - see repo/src/midi_fmopl.c.",
        )
        intro.pack(fill="x", padx=6, pady=(6, 2))

        target = ttk.LabelFrame(f, text="Channel targeting (CC_FMEN)")
        target.pack(fill="x", padx=6, pady=4)
        ttk.Button(target, text="Enable FMOpl on this channel",
                   command=lambda: self.set_fmopl_target(True)).pack(side="left", padx=6, pady=4)
        ttk.Button(target, text="Disable (back to SID)",
                   command=lambda: self.set_fmopl_target(False)).pack(side="left", padx=6, pady=4)

        slot_row = ttk.Frame(f); slot_row.pack(fill="x", padx=6, pady=2)
        ttk.Label(slot_row, text="FMOpl patch slot (0-31, 0-5 = built-in factory):").pack(side="left")
        ttk.Spinbox(slot_row, from_=0, to=31, textvariable=self.opl_patch_idx, width=4).pack(side="left", padx=4)

        for i, opname in ((0, "Operator 1 (modulator)"), (1, "Operator 2 (carrier)")):
            op = ttk.LabelFrame(f, text=opname)
            op.pack(fill="x", padx=6, pady=4)
            flags = ttk.Frame(op); flags.pack(fill="x", pady=2, padx=4)
            ttk.Checkbutton(flags, text="AM", variable=self.opl_am[i]).pack(side="left", padx=4)
            ttk.Checkbutton(flags, text="Vibrato", variable=self.opl_vib[i]).pack(side="left", padx=4)
            ttk.Checkbutton(flags, text="Sustained envelope (EGT)", variable=self.opl_egt[i]).pack(side="left", padx=4)
            ttk.Checkbutton(flags, text="Key scaling rate (KSR)", variable=self.opl_ksr[i]).pack(side="left", padx=4)
            self._slider(op, "Multiple", self.opl_multiple[i], 0, 15)
            self._slider(op, "Key scale level", self.opl_ksl[i], 0, 3)
            self._slider(op, "Total level (0=loud)", self.opl_tl[i], 0, 63)
            self._slider(op, "Attack", self.opl_attack[i], 0, 15)
            self._slider(op, "Decay", self.opl_decay[i], 0, 15)
            self._slider(op, "Sustain", self.opl_sustain[i], 0, 15)
            self._slider(op, "Release", self.opl_release[i], 0, 15)
            wave_row = ttk.Frame(op); wave_row.pack(fill="x", pady=2, padx=4)
            ttk.Label(wave_row, text="Waveform", width=16, anchor="w").pack(side="left")
            ttk.Combobox(wave_row, textvariable=self.opl_wave[i], state="readonly", width=14,
                         values=[0, 1, 2, 3]).pack(side="left")
            ttk.Label(wave_row, text="0=sine 1=half-sine 2=abs-sine 3=quarter-sine",
                      foreground="#888").pack(side="left", padx=6)

        conn = ttk.LabelFrame(f, text="Connection")
        conn.pack(fill="x", padx=6, pady=4)
        self._slider(conn, "Feedback", self.opl_feedback, 0, 7)
        ttk.Checkbutton(conn, text="Additive (parallel) - off means FM (series)",
                         variable=self.opl_algorithm).pack(side="left", padx=6, pady=4)

        clk = ttk.LabelFrame(f, text="Clock override (debugging/tuning, not a patch field - "
                                      "see repo/src/midi_fmopl.c's fmopl_clock_override comment)")
        clk.pack(fill="x", padx=6, pady=4)
        clk_row = ttk.Frame(clk); clk_row.pack(fill="x", padx=4, pady=4)
        ttk.Label(clk_row, text="Clock Hz (0 = firmware default, 3579545):").pack(side="left")
        ttk.Entry(clk_row, textvariable=self.opl_clock_hz, width=10).pack(side="left", padx=4)
        ttk.Button(clk_row, text="Send", command=self.send_fmopl_clock).pack(side="left", padx=3)
        ttk.Button(clk_row, text="Reset to default", command=self.reset_fmopl_clock).pack(side="left", padx=3)
        ttk.Button(clk_row, text="Hold test note (3s)", command=lambda: self.play_test_note(hold_s=3.0)).pack(side="left", padx=3)
        ttk.Label(clk, text="Live - takes effect on already-held notes immediately. Hold the test note "
                             "for a few seconds and send different clock values while it rings to find "
                             "what actually sounds in tune.",
                  wraplength=680, justify="left").pack(fill="x", padx=4, pady=(0, 4))

        actions = ttk.Frame(f); actions.pack(fill="x", padx=6, pady=6)
        ttk.Button(actions, text="Import from disk (.s3i)...", command=self.import_opl_from_disk).pack(side="left", padx=3)
        ttk.Button(actions, text="Send patch to device (RAM)", command=self.send_opl_patch).pack(side="left", padx=3)
        ttk.Button(actions, text="Read patch from device", command=self.dump_opl_patch).pack(side="left", padx=3)
        ttk.Button(actions, text="Select as active (Program Change)", command=self.select_opl_active).pack(side="left", padx=3)
        ttk.Button(actions, text="Capture channel's instrument -> this patch (SAVE)", command=self.capture_opl_patch_from_channel).pack(side="left", padx=3)

    # --- Live CC tab ------------------------------------------------------

    def send_cc(self, field, value):
        """Sends one Control Change on the current channel. Fire-and-forget,
        same as every other live action in this app - independent of the
        Patch/Raw SysEx tools above: this does not read from or write to any
        patch slot. It does drive some of the *same live per-channel state*
        a Patch's Program Change also sets (waveform, ADSR, filter routing,
        LFO, arp) - repo/src/midi_handler.c's CC handlers and
        apply_patch_to_channel() write those exact same fields - so loading
        a patch after touching these overwrites them, and vice versa."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        cc_num = CC_DEFAULT_NUMBERS[field]
        ch = self._channel_nibble()
        value = max(0, min(127, int(value)))
        hexstr = f"b{ch:x} {cc_num:02x} {value:02x}"
        self._run_async(lambda: send_hex(port, hexstr))
        self._set_status(f"Sent {field} (CC {cc_num}) = {value} on channel {self.channel.get()}.")

    def _build_cc_tab(self, nb):
        outer = ttk.Frame(nb)
        nb.add(outer, text="Live CC Controls")

        intro = ttk.Label(
            outer, justify="left", wraplength=680,
            text="These send raw MIDI Control Change messages straight to the device in real "
                 "time, on the Channel selected at the top of the window - a different mechanism "
                 "entirely from the Patch and Raw SysEx tools elsewhere in this app: nothing here "
                 "reads or writes a patch slot, and none of it is saved unless you separately send "
                 "SysEx Save (0x10). CC numbers shown are this firmware's compiled-in defaults "
                 "(repo/src/midi_defs.h) - if the board's CC map has been reconfigured, these won't "
                 "match. Note some controls below act on the same live channel state a Patch's "
                 "Program Change also sets (waveform, ADSR, filter, LFO, arp) - the two can "
                 "overwrite each other.",
        )
        intro.pack(fill="x", padx=8, pady=(8, 4))

        canvas = tk.Canvas(outer, highlightthickness=0)
        scrollbar = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        body = ttk.Frame(canvas)
        body.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=body, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True, padx=(8, 0), pady=4)
        scrollbar.pack(side="right", fill="y", pady=4)

        def on_wheel(event):
            canvas.yview_scroll(-1 * (event.delta // 120 or (1 if event.delta > 0 else -1)), "units")
        canvas.bind_all("<MouseWheel>", on_wheel)
        canvas.bind_all("<Button-4>", lambda e: canvas.yview_scroll(-3, "units"))
        canvas.bind_all("<Button-5>", lambda e: canvas.yview_scroll(3, "units"))

        for title, entries in CC_GROUPS:
            grp = ttk.LabelFrame(body, text=title)
            grp.pack(fill="x", padx=4, pady=4)
            for field, label, kind, extra in entries:
                self._build_cc_row(grp, field, label, kind, extra)

        override = ttk.LabelFrame(body, text="SID / Voice selection overrides (momentary select; "
                                              "value 0 on any of a group clears it)")
        override.pack(fill="x", padx=4, pady=4)
        self._build_override_row(override, "Narrow to one SID:", CC_SID_OVERRIDES)
        self._build_override_row(override, "Narrow to one voice:", CC_VCE_OVERRIDES)

        unwired = ttk.LabelFrame(body, text="Not wired in this firmware build")
        unwired.pack(fill="x", padx=4, pady=4)
        ttk.Label(unwired, text=", ".join(CC_UNWIRED) + " - no handler is bound to these CC "
                  "numbers (see midi_handler.c's ASSIGN_CC table), sending them does nothing.",
                  wraplength=680, justify="left").pack(fill="x", padx=6, pady=4)

    def _build_cc_row(self, parent, field, label, kind, extra):
        cc_num = CC_DEFAULT_NUMBERS[field]
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=2, padx=4)
        ttk.Label(row, text=f"{label} ({field}, CC {cc_num})", width=42, anchor="w").pack(side="left")

        if kind == "toggle":
            var = tk.BooleanVar(value=False)
            ttk.Checkbutton(row, variable=var,
                             command=lambda: self.send_cc(field, 127 if var.get() else 0)).pack(side="left")
            return

        if kind == "button":
            value = extra.get("value", 127)
            ttk.Button(row, text="Send", width=8,
                       command=lambda: self.send_cc(field, value)).pack(side="left")
            return

        # slider / slider_enum
        var = tk.IntVar(value=0)
        s = ttk.Scale(row, from_=0, to=127, orient="horizontal",
                       command=lambda v: var.set(round(float(v))))
        s.pack(side="left", fill="x", expand=True, padx=6)
        val_lbl = ttk.Label(row, width=16, anchor="w")
        val_lbl.pack(side="left")

        enum_names = extra.get("enum")

        def sync_label(*_):
            if enum_names:
                idx = min(len(enum_names) - 1, (var.get() * len(enum_names)) // 128)
                val_lbl.config(text=f"{var.get()}  ({enum_names[idx]})")
            else:
                val_lbl.config(text=str(var.get()))
        var.trace_add("write", sync_label)
        sync_label()

        s.bind("<ButtonRelease-1>", lambda e: self.send_cc(field, var.get()))

    def _build_override_row(self, parent, label, options):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=2, padx=4)
        ttk.Label(row, text=label, width=20, anchor="w").pack(side="left")
        selected = tk.StringVar(value="")
        for field, name in options:
            ttk.Radiobutton(row, text=name, variable=selected, value=field,
                             command=lambda f=field: self.send_cc(f, 127)).pack(side="left", padx=4)

        def clear():
            selected.set("")
            self.send_cc(options[0][0], 0)
        ttk.Button(row, text="Clear", command=clear).pack(side="left", padx=8)

    @staticmethod
    def _slider(parent, label, var, lo, hi):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=2, padx=4)
        ttk.Label(row, text=label, width=16, anchor="w").pack(side="left")
        s = ttk.Scale(row, from_=lo, to=hi, orient="horizontal",
                       command=lambda v: var.set(round(float(v))))
        s.set(var.get())
        s.pack(side="left", fill="x", expand=True, padx=6)
        val_lbl = ttk.Label(row, width=5, anchor="e")
        val_lbl.pack(side="left")

        def sync_label(*_):
            val_lbl.config(text=str(var.get()))
        var.trace_add("write", sync_label)
        sync_label()
        return row


def main():
    root = tk.Tk()
    PatchEditor(root)
    root.mainloop()


if __name__ == "__main__":
    main()
