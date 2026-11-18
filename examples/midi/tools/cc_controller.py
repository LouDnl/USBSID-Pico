#!/usr/bin/env python3
"""USBSID-Pico live MIDI CC controller.

Standalone GUI: every CC (repo/src/midi_defs.h MIDI_DEFAULT_CCVALUES_INIT),
Note On/Off, and Program Change as direct sliders/toggles/buttons - nothing
else. Deliberately separate from patch_editor.py (that tool edits/dumps/
loads midi_patch_t slots over SysEx; this one just fires raw MIDI at the
device in real time) so it can be used for A/B hardware timing tests -
e.g. Program-Change-switching between patches and holding a note to compare
how long the SID takes to become audible - without any patch-slot state
getting in the way.

Same transport as patch_editor.py and for the same reason: python-rtmidi,
not `amidi` (which opens the ALSA rawmidi device exclusively and fails with
"Device or resource busy" the instant PipeWire's ALSA-MIDI bridge - or
anything else - already has the port open, which on a modern Linux desktop
is essentially always). Setup: `pip install python-rtmidi`, then
`python3 cc_controller.py` (or `.venv/bin/python3 cc_controller.py` using
the venv already set up alongside patch_editor.py in this directory).

The CC map below (CC_GROUPS/CC_DEFAULT_NUMBERS/CC_SID_OVERRIDES/
CC_VCE_OVERRIDES/CC_UNWIRED) is a straight copy of patch_editor.py's - kept
in sync by hand, no shared import, so this file stays a true standalone
drop-in with no dependency on the other script.
"""

import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

import rtmidi

# --- MIDI transport (python-rtmidi, ALSA sequencer backend) ----------------
# Identical to patch_editor.py's - see that file's module docstring for why
# RtMidi is used instead of `amidi`.

def list_ports():
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


def send_hex(port, hexstr):
    """Fire-and-forget send."""
    try:
        midiout = _open_out(port)
    except RuntimeError:
        return
    try:
        midiout.send_message([int(b, 16) for b in hexstr.split()])
    finally:
        midiout.close_port()


def _open_in(port_name):
    midiin = rtmidi.MidiIn()
    ports = midiin.get_ports()
    for i, p in enumerate(ports):
        if p == port_name or port_name in p or p in port_name:
            midiin.open_port(i)
            midiin.ignore_types(sysex=False, timing=True, active_sense=True)
            return midiin
    raise RuntimeError(f"MIDI input port not found: {port_name}")


def send_hex_and_dump(port, hexstr, timeout_s=1.5):
    """Send, then collect replies for timeout_s. Returns space-separated hex
    byte text (may be empty)."""
    try:
        midiin = _open_in(port)
        midiout = _open_out(port)
    except RuntimeError:
        return ""
    try:
        midiout.send_message([int(b, 16) for b in hexstr.split()])
        collected = []
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            msg = midiin.get_message()
            if msg is not None:
                collected.extend(msg[0])
            else:
                time.sleep(0.01)
        return " ".join(f"{b:02x}" for b in collected)
    finally:
        midiin.close_port()
        midiout.close_port()


# --- Live MIDI CC control map (repo/src/midi_defs.h MIDI_DEFAULT_CCVALUES_INIT) --
# Copied from patch_editor.py - see its own comment above CC_GROUPS for the
# widget-kind legend (toggle/slider/slider_enum/button).

LFO_WAVES = ["Triangle", "Sawtooth", "Square", "Sample & Hold"]
LFO_DESTS = ["Pitch", "PWM", "Cutoff"]
ARP_MODES = ["Up", "Down", "Up/Down", "Random", "As played", "Table"]

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
    ("Modulation / timing", [
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
    ("Embedded application: Cynthcart, SID SYNTH (only relevant with ONBOARD_EMULATOR=1)", [
        ("CC_CEN", "Enable embedded application", "button", {"value": 127}),
        ("CC_CDI", "Disable embedded application", "button", {"value": 127}),
        ("CC_CRE", "Reset embedded application", "button", {"value": 127}),
    ]),
]

CC_SID_OVERRIDES = [("CC_SID1", "SID 1"), ("CC_SID2", "SID 2"), ("CC_SID3", "SID 3"), ("CC_SID4", "SID 4")]
CC_VCE_OVERRIDES = [("CC_VCE1", "Voice 1"), ("CC_VCE2", "Voice 2"), ("CC_VCE3", "Voice 3")]

CC_UNWIRED = ["CC_BMSB (Bank Select MSB)", "CC_BLSB (Bank Select LSB)",
              "CC_MOD (Modulation wheel)", "CC_MODL (Modulation wheel LSB)"]

CC_DEFAULT_NUMBERS = {
    "CC_NOTE": 0x1C, "CC_PWM": 0x01, "CC_NOIS": 0x14, "CC_PULS": 0x15, "CC_SAWT": 0x16,
    "CC_TRIA": 0x17, "CC_TEST": 0x18, "CC_RMOD": 0x19, "CC_SYNC": 0x1A, "CC_GATE": 0x1B,
    "CC_ATT": 0x49, "CC_DEC": 0x4B, "CC_SUS": 0x46, "CC_REL": 0x48,
    "CC_FFC": 0x4A, "CC_RES": 0x47, "CC_FLT1": 0x10, "CC_FLT2": 0x11, "CC_FLT3": 0x12,
    "CC_FLTE": 0x13, "CC_3OFF": 0x53, "CC_HPF": 0x52, "CC_BPF": 0x51, "CC_LPF": 0x50, "CC_VOL": 0x07,
    "CC_SID1": 0x68, "CC_SID2": 0x69, "CC_SID3": 0x6A, "CC_SID4": 0x6B,
    "CC_VCE1": 0x6C, "CC_VCE2": 0x6D, "CC_VCE3": 0x6E,
    "CC_GTEN": 0x1D, "CC_SPLY": 0x6F, "CC_VELM": 0x1E, "CC_FMEN": 0x1F,
    "CC_LFOW": 0x66, "CC_LFOR": 0x4C, "CC_LFOD": 0x4D, "CC_LFOT": 0x67, "CC_PORT": 0x05,
    "CC_ARPM": 0x71, "CC_ARPR": 0x72, "CC_ARPO": 0x73, "CC_ARPE": 0x70, "CC_ARPT": 0x74,
    "CC_LFO2W": 0x59, "CC_LFO2R": 0x0E, "CC_LFO2D": 0x0F, "CC_LFO2T": 0x5A,
    "CC_UNIS": 0x5D, "CC_UDET": 0x5E,
    "CC_CEN": 0x75, "CC_CDI": 0x76, "CC_CRE": 0x77,
    "CC_ASOF": 0x78, "CC_RACT": 0x79, "CC_ANOF": 0x7B, "CC_MONO": 0x7E, "CC_POLY": 0x7F,
}

# --- Live CC map (SysEx 0x2C-0x2F, repo/src/sysex.c) ------------------------
# Function id -> short name, index = id. Ids 0-51 remappable, 52-63 fixed or
# unwired (device refuses SET on them), 64+ report 0xFF in a DUMP.
CC_ID_NAMES = (
    "NOTE PWM NOIS PULS SAWT TRIA TEST RMOD SYNC GATE ATT DEC SUS REL FFC RES "
    "FLT1 FLT2 FLT3 FLTE 3OFF HPF BPF LPF VOL SID1 SID2 SID3 SID4 VCE1 VCE2 VCE3 "
    "GTEN SPLY VELM LFOW LFOR LFOD LFOT PORT ARPM ARPR ARPO ARPE ARPT LFO2W LFO2R "
    "LFO2D LFO2T UNIS UDET FMEN CEN CDI CRE BMSB BLSB MOD MODL ASOF RACT ANOF MONO POLY"
).split()
CC_REMAP_COUNT = 52
CC_ID_BY_FIELD = {f"CC_{n}": i for i, n in enumerate(CC_ID_NAMES)}
# Standard MIDI controllers: bank select, mod wheel, data entry, sustain, NRPN/RPN.
CC_WARN_NUMBERS = frozenset((0, 1, 6, 32, 38, 64, 98, 99, 100, 101))
CC_UNASSIGNED = 0xFF


def cc_number_legal(number):
    """Legal CC numbers: 0-119 except 117-119."""
    return 0 <= number <= 119 and number not in (117, 118, 119)


def build_ccmap_set_sysex(cc_id, number):
    """SET frame, value as two nibbles (high first), None = unassign (0xFF)."""
    v = CC_UNASSIGNED if number is None else number
    return f"f0 50 2c {cc_id:02x} {(v >> 4) & 0xF:02x} {v & 0xF:02x} f7"


def build_ccmap_dump_sysex(page):
    return f"f0 50 2d {page:02x} f7"


def build_ccmap_reset_sysex(mode):
    """mode 0 = compiled defaults, 1 = all remappable ids unassigned. RAM only."""
    return f"f0 50 2f {mode:02x} f7"


def parse_ccmap_data(raw_text, expected_page=None):
    """Extract the 24 map entries from a 0x2E DATA frame in raw_text
    (space-separated hex bytes). Returns (page, [24 ints]) or None."""
    try:
        toks = [int(t, 16) for t in raw_text.split()]
    except ValueError:
        return None
    i = 0
    while i + 3 < len(toks):
        if toks[i] == 0xF0 and toks[i + 1] == 0x50 and toks[i + 2] == 0x2E:
            page = toks[i + 3]
            try:
                end = toks.index(0xF7, i)
            except ValueError:
                return None
            nib = toks[i + 4:end]
            if len(nib) == 48 and all(n <= 0xF for n in nib) and \
                    (expected_page is None or page == expected_page):
                return page, [(nib[k] << 4) | nib[k + 1] for k in range(0, 48, 2)]
            i = end
        i += 1
    return None


def read_cc_map(port, timeout_s=0.6):
    """DUMP pages 0-3. Returns a list of 64 entries (0xFF = unassigned),
    or None when any page gets no valid reply."""
    out = []
    for page in range(4):
        reply = send_hex_and_dump(port, build_ccmap_dump_sysex(page), timeout_s=timeout_s)
        parsed = parse_ccmap_data(reply, expected_page=page)
        if parsed is None:
            return None
        out += parsed[1]
    return out


def cc_numbers_from_map(ccmap):
    """field -> CC number (None = unassigned), compiled defaults as base."""
    nums = dict(CC_DEFAULT_NUMBERS)
    for field, cc_id in CC_ID_BY_FIELD.items():
        v = ccmap[cc_id]
        nums[field] = None if v == CC_UNASSIGNED else v
    return nums


class CCController:
    def __init__(self, root):
        self.root = root
        root.title("USBSID-Pico Live CC Controller")

        self.port = tk.StringVar(value=find_usbsid_port() or "")
        self.channel = tk.IntVar(value=1)       # MIDI channel, 1-16 (display); nibble = value-1
        self.note = tk.IntVar(value=0x3C)
        self.velocity = tk.IntVar(value=100)
        self.program = tk.IntVar(value=0)       # patch slot for Program Change, 0-31
        self.note_held = False                  # tracks whether Note On has fired without a matching Note Off

        self.status = tk.StringVar(value="Ready.")
        self.cc_numbers = dict(CC_DEFAULT_NUMBERS)  # replaced by the live map when the board replies
        self.cc_map = None
        self.cc_row_labels = {}

        self._build_ui()
        self._refresh_ports()

        root.update_idletasks()
        want_w = min(root.winfo_reqwidth() + 20, root.winfo_screenwidth() - 40)
        want_h = min(root.winfo_reqheight() + 20, root.winfo_screenheight() - 80)
        root.geometry(f"{want_w}x{want_h}")
        root.minsize(640, 480)

    # --- helpers -------------------------------------------------------

    def _channel_nibble(self):
        return max(0, min(15, self.channel.get() - 1))

    def _require_port(self):
        p = self.port.get().strip()
        if not p:
            messagebox.showerror("No port", "Select a MIDI port first (Refresh if the list is empty).")
            return None
        return p

    def _selected_port_id(self):
        return self.port.get().strip()

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
        self.refresh_cc_map()

    def _run_async(self, fn):
        threading.Thread(target=fn, daemon=True).start()

    # --- live CC map -----------------------------------------------------

    def _cc_row_text(self, field, label):
        n = self.cc_numbers.get(field)
        return f"{label} ({field}, " + ("unassigned" if n is None else f"CC {n}") + ")"

    def _apply_cc_map(self, ccmap):
        self.cc_map = list(ccmap)
        self.cc_numbers = cc_numbers_from_map(ccmap)
        for field, (lbl, label) in self.cc_row_labels.items():
            lbl.config(text=self._cc_row_text(field, label))

    def refresh_cc_map(self):
        """Read the live map via DUMP pages 0-3, keep defaults when no reply."""
        port = self._selected_port_id()
        if not port:
            return

        def work():
            ccmap = read_cc_map(port)
            if ccmap is None:
                self._set_status("CC map: no reply, using compiled-in defaults.")
                return
            self.root.after(0, lambda: self._apply_cc_map(ccmap))
            self.root.after(0, lambda: self._set_status("CC map read from device."))
        self._run_async(work)

    # --- MIDI actions ----------------------------------------------------

    def send_cc(self, field, value):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        cc_num = self.cc_numbers.get(field)
        if cc_num is None:
            self._set_status(f"{field} is unassigned on the device, nothing sent.")
            return
        ch = self._channel_nibble()
        value = max(0, min(127, int(value)))
        hexstr = f"b{ch:x} {cc_num:02x} {value:02x}"
        self._run_async(lambda: send_hex(port, hexstr))
        self._set_status(f"Sent {field} (CC {cc_num}) = {value} on channel {self.channel.get()}.")

    def program_change(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        prog = self.program.get() & 0x1F
        hexstr = f"c{ch:x} {prog:02x}"
        self._set_status(f"Program Change: channel {self.channel.get()} -> patch {prog} (0x{prog:02x})")
        self._run_async(lambda: send_hex(port, hexstr))

    def note_on(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        note = self.note.get() & 0x7F
        vel = self.velocity.get() & 0x7F
        self.note_held = True
        self._set_status(f"Note On 0x{note:02x} vel {vel} on channel {self.channel.get()} - "
                          f"held until Note Off.")
        self._run_async(lambda: send_hex(port, f"9{ch:x} {note:02x} {vel:02x}"))

    def note_off(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        note = self.note.get() & 0x7F
        self.note_held = False
        self._set_status(f"Note Off 0x{note:02x} on channel {self.channel.get()}.")
        self._run_async(lambda: send_hex(port, f"8{ch:x} {note:02x} 00"))

    def send_volume_on(self):
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        self._run_async(lambda: send_hex(port, f"b{ch:x} 07 7f"))
        self._set_status("Sent CC7 (volume) = 127.")

    def capture_patch_from_channel(self):
        """SYSEX_MIDI_PATCH_SAVE (0x2A, repo/src/sysex.c): captures the
        channel's current live state (timbre/filter/LFO/arp/bend/unison)
        into the patch slot shown above - the inverse of Program Change.
        RAM only, does not touch flash on its own."""
        port = self._selected_port_id()
        if not port:
            self._require_port()
            return
        ch = self._channel_nibble()
        prog = self.program.get() & 0x1F
        if prog < 16:
            if not messagebox.askyesno(
                "Overwrite factory patch",
                f"Slot {prog} is a factory patch (0-15), not a user slot. Capturing overwrites it in "
                f"RAM. Continue?",
            ):
                return
        hexstr = f"f0 50 2a {ch:02x} {prog:02x} f7"
        self._set_status(f"Capturing channel {self.channel.get()}'s live state into patch {prog} (0x{prog:02x})...")
        self._run_async(lambda: (send_hex(port, hexstr),
                                  self._set_status(f"Captured channel {self.channel.get()} into patch {prog}. "
                                                    f"Not saved to flash.")))

    def send_midi_system_reset(self):
        """MIDI System Reset (0xFF, a real-time status byte - no
        manufacturer wrapper, no data bytes). repo/src/midi.c's 0xFF case
        calls midi_processor_init() - resets every channel to compiled
        defaults, then re-applies the last-saved flash state if one exists.
        Live only: does not touch what's saved, only what's currently
        running - a held note or in-progress performance will be cut."""
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

    # --- UI construction ---------------------------------------------------

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
        ttk.Button(top, text="Send volume on (CC7=127)", command=self.send_volume_on).pack(side="left", padx=(12, 0))
        ttk.Button(top, text="Send MIDI System Reset (0xFF)", command=self.send_midi_system_reset).pack(side="left", padx=(6, 0))

        transport = ttk.LabelFrame(
            self.root,
            text="Note / Program Change (for A/B timing tests - e.g. Program Change repeatedly to "
                 "the same or different slots, then Note On/Off, to compare delay before sound)",
        )
        transport.pack(fill="x", **pad)

        prog_row = ttk.Frame(transport)
        prog_row.pack(fill="x", pady=2, padx=4)
        ttk.Label(prog_row, text="Patch slot (0-31):", width=20, anchor="w").pack(side="left")
        ttk.Spinbox(prog_row, from_=0, to=31, textvariable=self.program, width=4).pack(side="left")
        ttk.Button(prog_row, text="Program Change", command=self.program_change).pack(side="left", padx=6)
        ttk.Button(prog_row, text="Capture channel's live state -> this patch (SAVE)", command=self.capture_patch_from_channel).pack(side="left", padx=3)

        note_row = ttk.Frame(transport)
        note_row.pack(fill="x", pady=2, padx=4)
        ttk.Label(note_row, text="Note (0-127):", width=20, anchor="w").pack(side="left")
        ttk.Spinbox(note_row, from_=0, to=127, textvariable=self.note, width=4).pack(side="left")
        ttk.Label(note_row, text="  Velocity:").pack(side="left")
        ttk.Spinbox(note_row, from_=1, to=127, textvariable=self.velocity, width=4).pack(side="left")
        ttk.Button(note_row, text="Note On (press and hold)", command=self.note_on).pack(side="left", padx=6)
        ttk.Button(note_row, text="Note Off", command=self.note_off).pack(side="left", padx=3)

        outer = ttk.Frame(self.root)
        outer.pack(fill="both", expand=True, **pad)

        intro = ttk.Label(
            outer, justify="left", wraplength=680,
            text="These send raw MIDI Control Change messages straight to the device in real time, "
                 "on the channel selected above. CC numbers shown are this firmware's compiled-in "
                 "defaults (repo/src/midi_defs.h) - if the board's CC map has been reconfigured, "
                 "these won't match.",
        )
        intro.pack(fill="x", pady=(0, 4))

        canvas = tk.Canvas(outer, highlightthickness=0)
        scrollbar = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        body = ttk.Frame(canvas)
        body.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=body, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

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

        status_bar = ttk.Frame(self.root)
        status_bar.pack(fill="x", **pad)
        ttk.Label(status_bar, textvariable=self.status, relief="sunken", anchor="w").pack(fill="x")

    def _build_cc_row(self, parent, field, label, kind, extra):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=2, padx=4)
        name_lbl = ttk.Label(row, text=self._cc_row_text(field, label), width=42, anchor="w")
        name_lbl.pack(side="left")
        self.cc_row_labels[field] = (name_lbl, label)

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


def main():
    root = tk.Tk()
    CCController(root)
    root.mainloop()


if __name__ == "__main__":
    main()
