# MIDIbox SID V2 preset conversion (v2: LFO2 + Unison)

Every Lead- and Bassline-engine patch from the four [MIDIbox SID V2](https://github.com/midibox/mios8/tree/master/apps/synthesizers/midibox_sid_v2) preset banks, converted into this firmware's current `midi_patch_t` format (`repo/src/midi_patch.h`/`.c`) with a ready-to-send SysEx line for each one. This is the v2 conversion: same 499 patches as [`midibox_sid_presets.md`](midibox_sid_presets.md), rebuilt against the firmware state that added a second LFO and 3-voice unison to `midi_patch_t` (`sysex.c`'s `PATCH_SYSEX_NIBBLES` went from 36 to 48 nibbles per patch). Every field this firmware already had in v1 (waveform/ADSR/PWM, filter, LFO1, arp, bend range) is carried over byte-for-byte unchanged from that first conversion; only the two new fields below are freshly parsed from the source `.syx` dumps. Parsed directly from the real `.syx` dump files (`bank1__v2_vintage_bank.syx`, `bank2__tk2_soundbank.syx`, `bank3__127_arps.syx`, `bank4__Midibox_SammichSiD_Patches-chiptraxxx.syx`, all four in [`midibox_sid_v2/presets`](https://github.com/midibox/mios8/tree/master/apps/synthesizers/midibox_sid_v2/presets)), not retyped by hand, using the field layout in [`mbsidv2_sysex_implementation.txt`](https://github.com/midibox/mios8/blob/master/apps/synthesizers/midibox_sid_v2/doc/mbsidv2_sysex_implementation.txt).

**Why this can only ever be an approximation.** MIDIbox SID V2 is a far bigger synth engine than this firmware's `midi_patch_t`: 3 detunable oscillators per voice (this firmware doubles OSC1 across up to 3 SIDs for unison instead of true additive multi-osc), 2 envelopes plus an 8-slot modulation matrix that can route any of 6 LFOs to almost any target (this firmware has 2 fixed-destination LFOs as of this v2 conversion, still a long way from MBSID's matrix), a wavetable sequencer, and separate Bassline/Drum/Multi engines with their own structures. What follows is each patch's *closest reachable equivalent* on real SID hardware through this firmware's simpler model, not a byte-exact recreation - MIDIbox's own patch dump is not directly compatible with this firmware's SysEx protocol (different manufacturer ID, different command set, different patch layout) and was never meant to be loaded as-is.

Per-patch conversion choices, applied uniformly across every entry below:

- **Waveform/ADSR/PWM** come straight from the patch's Voice #1 (OSC1) - MIDIbox's own SID register bytes for these are already nibble-packed exactly the way this firmware's `tmpl_attdec`/`tmpl_susrel` are, so no scaling happens here. If OSC2/OSC3 were also active in the source (common on "3OSC detuned" patches), it's noted per patch - only OSC1's timbre survives the conversion; the detuned unison thickness does not (see the new **Unison** bullet below for what does carry over from OSC2/OSC3).
- **Filter cutoff** is rescaled from MIDIbox's 12-bit range to this firmware's 11-bit `CUTOFF_MAX` (divide by 2); **resonance** from 0-255 to 0-15 (its top 4 bits, which is all the real SID resonance register ever reads anyway); **routing** and **filter mode** bits map 1:1, both firmwares mirror the real RESFLT/MODVOL hardware bit layout.
- **LFO1** is the hard part: MIDIbox routes any of 6 LFOs to any target through its modulation matrix, so "LFO1" is frequently not the one doing anything audible. Each patch below was scanned for whichever LFO actually has a live modulation path into OSC1's pitch, PWM or the filter cutoff (matching the largest routed depth, via either the mod matrix's direct-assignment bits or its indexed target fields), and that LFO's own wave/rate was used; if none of the 6 are actually routed anywhere, LFO is reported off even if stale leftover values sit in the source's LFO1 slot. Sine and Positive-Sine waveforms become Triangle - the real SID has no sine oscillator, and neither does this firmware's LFO.
- **LFO2 (new in v2).** This firmware's second LFO is filled from whichever *other* MBSID LFO also has a live routed path into OSC1's pitch, PWM or cutoff, distinct from whatever was picked for LFO1 above - same liveness rule (the source LFO must be enabled and have a nonzero rate; a routed-but-frozen LFO produces no audible modulation and is skipped) and same largest-routed-depth tiebreak. For the Bassline engine, which has its own always-present LFO1/LFO2 pair rather than a 6-LFO matrix, this firmware's LFO2 is simply MBSID's own native LFO2, whichever of its three fixed depths (pitch/PW/cutoff) is largest. Rate is MIDIbox's own 0-255 LFO Rate halved to fit this firmware's range (matching how LFO1's rate is already carried over); depth is the absolute value of the routed depth, capped at 127. If no second live route exists, LFO2 is off. A small handful of Bank 4's more elaborately-patched entries route their modulation through velocity/key-tracking/knob-fed operator chains this scan doesn't unwind - those are left with LFO2 off rather than guessing.
- **Unison (new in v2).** This firmware's 3-voice unison (one detuned copy of the same patch per claimed SID, in `midi_config.h`) is filled straight from MIDIbox's own per-patch Oscillators Detune byte (`mbsidv2_sysex_implementation.txt` addr `0x051`, 0-255) - the same 0-255 range this firmware's `unison_detune` uses (`ch->unison_detune * 256 / 255` in `midi_handler.c` for the actual semitone spread), so no rescale is needed. Unison is reported on whenever the source patch has OSC2 and/or OSC3 actually enabled (not the disabled-oscillator bit), off otherwise, regardless of whether the detune byte happens to be 0 - MIDIbox's own multi-oscillator thickness is still lost in the conversion (this firmware doubles one oscillator's timbre across extra SIDs, not three independently-configured ones), but the *spread amount* the source patch used for it now carries over instead of being discarded.
- **Arpeggiator** mode/rate/octave-range come from the source patch's own arp settings regardless of whether the source patch had its arp switched on - this firmware's patch format has no "arp enabled" bit of its own (that's channel CC_ARPE, sent separately), so an arp pattern is always carried over as data even when it was dormant in the original; each entry notes whether the source had it enabled.
- **Bend range** is copied from the source's own pitch-range field. On percussive/one-shot patches (drum-ish Lead patches, e.g. "Bassdrum") this field is often just leftover/unused data in the source and not a deliberate bend range - don't read anything into a large or odd bend value on those.
- **Drum engine and Multi engine** patches are listed by name only, with no converted values: Drum is a 16-instrument kit triggered by note number, and Multi is up to 6 simultaneous layered instruments - neither has a meaningful single-voice equivalent in this firmware's one-`midi_patch_t`-per-Program-Change model. Worth revisiting by hand if one of those names is wanted.

**FMOPL.** None of the four MIDIbox SID V2 banks reference FM/OPL synthesis anywhere in the SysEx implementation - MBSID is SID-only, it has no OPL/FM chip concept at all (the closest thing, cross-oscillator FM, isn't present here either). So nothing below carries an FMOPL mark. The convention, if a future preset source does need it: mark that entry with **`[FMOPL]`** right after its name, meaning "needs FM/OPL synthesis this firmware doesn't implement yet", and skip generating values/SysEx for it, same as the Drum/Multi entries below.

**SysEx format.** Each line is `SYSEX_MIDI_PATCH_LOAD` (`repo/src/sysex.c`): `F0 50 20 <slot> <48 nibble bytes> F7` - 48 nibbles (24 raw bytes, 2 nibbles per 8-bit field, 4 for the 16-bit `filter_cutoff`), not 36; every field is sent as a value 0x00-0x0F to stay 7-bit MIDI safe, see `pack_patch()`/`unpack_patch()` in `sysex.c`. The first 36 nibbles are exactly [`midibox_sid_presets.md`](midibox_sid_presets.md)'s own line for the same patch, unchanged; the 12 new nibbles appended at the tail are `lfo2_wave`, `lfo2_rate`, `lfo2_depth`, `lfo2_dest`, `unison_enabled`, `unison_detune`, in that order, matching `midi_patch_t`'s own field order. Every line below targets slot **0x10 (16)**, the first user slot - edit that byte (4th, right after `f0 50 20`) to target a different slot, 0x10-0x1f (16-31) for your own patches or 0x00-0x0f (0-15) to overwrite a factory one (`tools/patch_editor.py` will ask for confirmation if you send there). Loading only updates RAM - send `f0 50 10 f7` (`SYSEX_MIDI_SAVE`) afterwards to persist it to flash, or use `tools/patch_editor.py`'s "Save to flash" button. **These 48-nibble lines only work against firmware that already has the LFO2/unison fields in `midi_patch_t`** - sending one to older firmware will be rejected by the `PATCH_SYSEX_MIN_SIZE` check as a malformed (too-long) message; use [`midibox_sid_presets.md`](midibox_sid_presets.md)'s 36-nibble lines against that firmware instead. You can send any of these lines directly with `amidi -p <port> -S "..."`, or by pasting the 48 nibble values into `tools/patch_editor.py`'s fields by hand.

## Contents

- [Bank 1 - Vintage Bank](#bank-1-vintage-bank)
- [Bank 2 - TK2 Soundbank](#bank-2-tk2-soundbank)
- [Bank 3 - 127 Arps](#bank-3-127-arps)
- [Bank 4 - SammichSiD Chiptraxxx](#bank-4-sammichsid-chiptraxxx)

## Bank 1 - Vintage Bank

128 patches converted from MBSID V1's 8580 preset bank, refined by Thorsten Klose for V2. The oldest and broadest bank: leads, basses, pads, drum hits, arps and sequences.

### 001 Lead Patch (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 002 Techno PWM (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1312/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=18 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 02 00 00 05 00 00 07 00 00 0f 00 00 01 00 00 00 01 02 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 003 Techno Saw (Lead engine)

Sawtooth &middot; A0 D0 S11 R5 &middot; Filter off &middot; LFO Triangle -> Pitch rate=75 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=24/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=24/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0b 05 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 04 0b 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 08 f7
```

### 004 Techno 5th (Lead engine)

Pulse &middot; A0 D8 S6 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=38 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=4/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=4/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 08 06 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 02 06 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 04 f7
```

### 005 Cool Brass (Lead engine)

Sawtooth &middot; A0 D14 S3 R0 &middot; Filter off &middot; LFO Triangle -> Pitch rate=73 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=16/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=16/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 0e 03 00 00 00 00 0a 00 07 0f 00 00 00 00 00 01 00 00 00 04 09 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 00 f7
```

### 006 Simple Saw (Lead engine)

Sawtooth &middot; A2 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 02 00 0f 00 00 00 00 08 00 07 0f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 007 Simple Pulse (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=2 depth=43 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 06 0b 00 00 00 00 00 00 00 00 00 00 02 02 0b 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 008 Pulse w/o Body (Lead engine)

Pulse &middot; A0 D4 S7 R8 &middot; PWM 32/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 04 07 08 02 00 00 00 00 02 09 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 009 Popcorn (Lead engine)

Triangle &middot; A0 D6 S0 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 01 00 00 06 00 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 010 WT Flute (Lead engine)

Triangle &middot; A7 D0 S13 R0 &middot; Filter off &middot; LFO Triangle -> Pitch rate=61 depth=120 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 01 00 07 00 0d 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 03 0d 07 08 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 011 Synth Plug (Lead engine)

Pulse &middot; A0 D6 S0 R8 &middot; PWM 2048/4095 &middot; Filter LP cutoff=1168/2047 res=7/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=86 depth=64 &middot; LFO2 Triangle -> PWM rate=58 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=28/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=58, depth=64, -> PWM); Unison on, detune=28/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 06 00 08 00 00 00 08 00 04 09 00 00 07 00 07 01 00 00 00 05 06 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 03 0a 04 00 00 01 00 01 01 0c f7
```

### 012 WT Synth (Lead engine)

Pulse &middot; A0 D3 S13 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=3/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=3/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 0d 00 00 00 00 08 00 04 01 00 00 0f 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 03 f7
```

### 013 WT HardcoreSynth (Lead engine)

Pulse [ring] &middot; A0 D0 S11 R5 &middot; PWM 1856/4095 &middot; Filter off &middot; LFO Square -> Pitch rate=108 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 04 00 00 0b 05 04 00 00 07 00 01 00 00 00 0f 00 00 01 00 00 02 06 0c 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 014 Sync Sound (Lead engine)

Triangle [sync] &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO Triangle -> Pitch rate=9 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 106 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 02 00 00 0f 00 04 00 00 03 00 07 0f 00 00 00 00 00 00 00 00 00 00 09 04 00 00 00 00 00 03 0e 00 00 06 0a 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 015 Sync Pad (Lead engine)

Pulse [sync,ring] &middot; A15 D4 S13 R0 &middot; PWM 1600/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=6 depth=64 &middot; LFO2 Triangle -> PWM rate=49 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=49/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=49, depth=64, -> PWM); Unison on, detune=49/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 06 0f 04 0d 00 04 00 00 06 00 04 00 00 00 00 00 00 01 00 00 00 00 06 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 03 01 04 00 00 01 00 01 03 01 f7
```

### 016 Filtered Poly (Multi engine) - not converted

### 017 Filt. Mono Pad (Lead engine)

Pulse [ring] &middot; A0 D0 S10 R11 &middot; PWM 2048/4095 &middot; Filter BP cutoff=112/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=53 depth=64 &middot; LFO2 Triangle -> Cutoff rate=17 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=24/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=17, depth=64, -> Cutoff); Unison on, detune=24/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 04 00 00 0a 0b 00 00 00 08 00 00 07 00 00 0f 00 07 02 00 00 00 03 05 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 01 01 04 00 00 02 00 01 01 08 f7
```

### 018 Arpeggio (Lead engine)

Pulse &middot; A0 D0 S15 R5 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=5 depth=64 &middot; LFO2 off &middot; Arp Up rate=30 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 05 00 00 00 08 00 06 0b 00 00 00 00 00 00 00 00 00 00 05 04 00 00 01 00 00 01 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 019 Arpeggio 2 (Lead engine)

Pulse &middot; A5 D6 S7 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=34 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 05 06 07 00 00 00 00 08 00 03 05 00 00 00 00 00 01 00 00 00 02 02 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 020 WT Arp Fun (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=85 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 05 05 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 021 Ringmodulation (Lead engine)

Triangle [ring] &middot; A0 D0 S15 R5 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 37 &middot; Unison on detune=2/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 00 00 0f 05 02 00 00 07 00 06 0b 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 02 05 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 022 Filtered B. 6581 (Lead engine)

Pulse &middot; A0 D0 S11 R0 &middot; PWM 1312/4095 &middot; Filter LP cutoff=176/2047 res=0/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=18 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO3 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0b 00 02 00 00 05 00 00 0b 00 00 00 00 07 01 00 00 00 01 02 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 023 Filtered B. 8580 (Lead engine)

Pulse &middot; A0 D0 S11 R0 &middot; PWM 1312/4095 &middot; Filter LP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=18 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO3 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0b 00 02 00 00 05 00 00 00 00 00 0f 00 07 01 00 00 00 01 02 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 024 Filtered Bass 2 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=17/2047 res=15/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 00 01 01 00 0f 00 01 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 025 C64 Bass (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2112/4095 &middot; Filter LP cutoff=507/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=26 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 08 00 01 0f 0b 00 0f 00 01 01 00 00 00 01 0a 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 026 Autobahn (Lead engine)

Sawtooth &middot; A0 D0 S11 R7 &middot; Filter LP cutoff=142/2047 res=5/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 00 0b 07 00 00 00 08 00 00 08 0e 00 05 00 03 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 027 Bassdrum (Lead engine)

Triangle &middot; A0 D10 S0 R5 &middot; Filter LP cutoff=16/2047 res=0/15 route=v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 55 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 0a 00 05 02 00 00 07 00 00 01 00 00 00 00 02 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 03 07 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 028 Bassdrum2 (Lead engine)

Triangle &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO Triangle -> PWM rate=20 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 45 &middot; Unison on detune=0/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 00 0f 00 04 00 00 08 00 07 0f 00 00 00 00 00 01 00 00 00 01 04 04 00 00 01 00 00 03 0e 00 00 02 0d 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 029 Cymbal (Lead engine)

Noise &middot; A0 D9 S0 R9 &middot; Filter off &middot; LFO Triangle -> Pitch rate=87 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 37 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 00 09 00 09 04 00 00 08 00 07 0f 00 00 00 00 00 01 00 00 00 05 07 04 00 00 00 00 00 03 0e 00 00 02 05 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 030 Klick (Lead engine)

Noise &middot; A0 D2 S0 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 37 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 00 02 00 00 04 00 00 08 00 07 0f 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 02 05 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 031 Metal (Lead engine)

Triangle [ring] &middot; A0 D7 S0 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 37 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 00 07 00 00 04 00 00 08 00 07 0f 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 02 05 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 032 Deep Bass 9 (Lead engine)

Sawtooth &middot; A0 D0 S15 R10 &middot; Filter LP cutoff=65/2047 res=14/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=84 depth=111 &middot; LFO2 Triangle -> PWM rate=58 depth=64 &middot; Arp Up rate=30 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=19/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=58, depth=64, -> PWM); Unison on, detune=19/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 0a 08 04 00 03 00 00 04 01 00 0e 00 07 01 00 00 00 05 04 06 0f 00 00 00 00 01 0e 00 01 00 02 00 00 03 0a 04 00 00 01 00 01 01 03 f7
```

### 033 Drum Kit 1 (Drum engine) - not converted

### 034 Drum Kit 2 (Drum engine) - not converted

### 035 Drum Kit 3 (Drum engine) - not converted

### 036 Drum Kit 4 (Drum engine) - not converted

### 037 Some Triggs (Lead engine)

Triangle [ring] &middot; A0 D3 S0 R10 &middot; Filter LP cutoff=675/2047 res=15/15 route=v1+v2+v3 &middot; LFO Sawtooth -> Cutoff rate=10 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 00 03 00 0a 07 0a 00 04 00 02 0a 03 00 0f 00 07 01 00 00 01 00 0a 04 00 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 038 More Triggs (Lead engine)

Sawtooth [sync] &middot; A0 D5 S0 R11 &middot; Filter HP cutoff=1151/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=0 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO4 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 02 00 05 00 0b 00 00 00 08 00 04 07 0f 00 0f 00 07 04 00 00 00 00 00 04 00 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 039 RingModMod (Lead engine)

Triangle [ring] &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO Triangle -> PWM rate=12 depth=64 &middot; LFO2 Triangle -> Pitch rate=4 depth=17 &middot; Arp Random rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=4, depth=17, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 00 00 0f 00 00 00 00 08 00 00 00 00 00 0f 00 00 01 00 00 00 00 0c 04 00 00 01 00 03 03 0e 00 02 00 02 00 00 00 04 01 01 00 00 00 01 00 00 f7
```

### 040 RampUp (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1433/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=16 depth=64 &middot; LFO2 Sawtooth -> Pitch rate=19 depth=1 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=12/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Sawtooth, rate=19, depth=1, -> Pitch); Unison on, detune=12/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 09 09 00 05 00 01 00 00 00 00 00 00 01 00 00 00 01 00 04 00 00 01 00 00 03 0e 00 00 00 02 00 01 01 03 00 01 00 00 00 01 00 0c f7
```

### 041 Math Game (Lead engine)

Sawtooth [sync] &middot; A0 D0 S15 R0 &middot; Filter LP cutoff=148/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=55 depth=124 &middot; LFO2 Triangle -> Cutoff rate=21 depth=105 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=33/255

Notes: LFO4 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=21, depth=105, -> Cutoff); Unison on, detune=33/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 02 00 00 0f 00 00 00 00 08 00 00 09 04 00 0f 00 07 01 00 00 00 03 07 07 0c 00 00 00 00 03 0e 00 00 00 02 00 00 01 05 06 09 00 02 00 01 02 01 f7
```

### 042 WT Runner (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=455/2047 res=15/15 route=v1+v2+v3 &middot; LFO Sample&Hold -> PWM rate=126 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 0c 07 00 0f 00 07 01 00 00 03 07 0e 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 043 WT Stereo Echo (Lead engine)

Triangle &middot; A0 D7 S15 R6 &middot; Filter off &middot; LFO Sample&Hold -> PWM rate=126 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 07 0f 06 00 00 00 08 00 01 0c 07 00 0f 00 00 01 00 00 03 07 0e 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 044 Turntable (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 668/4095 &middot; Filter LP cutoff=273/2047 res=15/15 route=v1+v2+v3 &middot; LFO Sawtooth -> PWM rate=73 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=28/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=28/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 09 0c 00 02 00 01 01 01 00 0f 00 07 01 00 00 01 04 09 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 0c f7
```

### 045 Driving a Car (Lead engine)

Triangle+Pulse &middot; A0 D0 S13 R9 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=4 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 40 &middot; Unison on detune=8/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=8/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 00 00 0d 09 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 04 04 00 00 00 00 00 03 0e 00 00 02 08 00 00 00 00 00 00 00 00 00 01 00 08 f7
```

### 046 Ufo Reverse (Lead engine)

Triangle [ring] &middot; A13 D0 S0 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 25 &middot; Unison on detune=40/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=40/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 0d 00 00 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 01 09 00 00 00 00 00 00 00 00 00 01 02 08 f7
```

### 047 WT Falling (Lead engine)

Pulse &middot; A0 D0 S13 R11 &middot; PWM 2080/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0d 0b 02 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 048 WT Helicopter (Lead engine)

Pulse [sync] &middot; A0 D0 S15 R0 &middot; PWM 1856/4095 &middot; Filter LP cutoff=448/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=9 depth=64 &middot; LFO2 Triangle -> Cutoff rate=7 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=7, depth=64, -> Cutoff); Unison off (source uses only OSC1).

```
f0 50 20 10 04 02 00 00 0f 00 04 00 00 07 00 01 0c 00 00 0f 00 07 01 00 00 00 00 09 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 07 04 00 00 02 00 00 00 00 f7
```

### 049 WT Neutron (Lead engine)

Pulse &middot; A0 D2 S0 R10 &middot; PWM 2080/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=4 depth=64 &middot; LFO2 Triangle -> Pitch rate=16 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 8 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=16, depth=64, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 02 00 0a 02 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 04 04 00 00 00 00 00 03 0e 00 00 00 08 00 00 01 00 04 00 00 00 00 00 00 00 f7
```

### 050 WT Filtered Seq. (Lead engine)

Pulse &middot; A0 D0 S15 R8 &middot; PWM 2112/4095 &middot; Filter BP cutoff=157/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=43 depth=64 &middot; LFO2 Triangle -> Cutoff rate=5 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=32/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=5, depth=64, -> Cutoff); Unison on, detune=32/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 08 04 00 00 08 00 00 09 0d 00 0f 00 07 02 00 00 00 02 0b 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 05 04 00 00 02 00 01 02 00 f7
```

### 051 Alien Groove (Lead engine)

Pulse &middot; A0 D0 S12 R10 &middot; PWM 1632/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=22 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=21/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=21/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0c 0a 06 00 00 06 00 05 00 00 00 00 00 00 00 00 00 00 01 06 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 05 f7
```

### 052 Nice Lead (Lead engine)

Pulse &middot; A0 D3 S12 R9 &middot; PWM 1037/4095 &middot; Filter LP cutoff=1170/2047 res=0/15 route=v1 &middot; LFO Triangle -> Pitch rate=61 depth=125 &middot; LFO2 Triangle -> Pitch rate=57 depth=125 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=14/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=57, depth=125, -> Pitch); Unison on, detune=14/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 0c 09 00 0d 00 04 00 04 09 02 00 00 00 01 01 00 00 00 03 0d 07 0d 00 00 00 00 03 0e 00 00 00 02 00 00 03 09 07 0d 00 00 00 01 00 0e f7
```

### 053 NT Bass (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2848/4095 &middot; Filter LP cutoff=213/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=5/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=5/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 02 00 00 0b 00 00 0d 05 00 0f 00 07 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 05 f7
```

### 054 RingKabinett (Lead engine)

Pulse [ring] &middot; A0 D0 S15 R0 &middot; PWM 1344/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=33/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; OSC1 'Disable Oscillator' flag was set in source; kept waveform bits anyway; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=33/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 04 00 00 0f 00 04 00 00 05 00 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 02 01 f7
```

### 055 Random Fun (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Sample&Hold -> Pitch rate=15 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=16/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=16/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 03 00 0f 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 00 f7
```

### 056 Don't cry baby! (Lead engine)

Triangle [ring] &middot; A7 D3 S10 R0 &middot; Filter off &middot; LFO Triangle -> Pitch rate=26 depth=99 &middot; LFO2 Triangle -> Pitch rate=13 depth=76 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=13, depth=76, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 01 04 07 03 0a 00 00 00 00 08 00 00 00 00 00 0f 00 00 02 00 00 00 01 0a 06 03 00 00 00 00 03 0e 00 00 00 02 00 00 00 0d 04 0c 00 00 00 00 00 00 f7
```

### 057 Tweak the LFOs (Lead engine)

Triangle [ring] &middot; A7 D3 S10 R6 &middot; Filter LP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO Square -> Pitch rate=24 depth=64 &middot; LFO2 Triangle -> Pitch rate=32 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=32, depth=64, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 07 03 0a 06 00 00 00 08 00 00 00 00 00 0f 00 07 01 00 00 02 01 08 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 02 00 04 00 00 00 00 01 00 00 f7
```

### 058 Random Sync (Lead engine)

Triangle [ring] &middot; A7 D3 S10 R0 &middot; Filter off &middot; LFO Sample&Hold -> Pitch rate=11 depth=27 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 07 03 0a 00 0f 08 00 07 00 03 05 00 00 0f 00 00 02 00 00 03 00 0b 01 0b 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 059 A stormy Day (Lead engine)

Noise [sync] &middot; A0 D0 S15 R10 &middot; Filter LP cutoff=64/2047 res=15/15 route=v1+v2+v3 &middot; LFO Sample&Hold -> Pitch rate=5 depth=64 &middot; LFO2 S&H -> Pitch rate=97 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=60/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 sourced from a second live-routed MBSID LFO (S&H, rate=97, depth=64, -> Pitch); Unison on, detune=60/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 02 00 00 0f 0a 00 00 00 08 00 00 04 00 00 0f 00 07 01 00 00 03 00 05 04 00 00 00 00 00 03 0e 00 00 00 02 00 03 06 01 04 00 00 00 00 01 03 0c f7
```

### 060 6 Octave Plug (Lead engine)

Pulse &middot; A0 D10 S0 R8 &middot; PWM 2048/4095 &middot; Filter LP+BP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=70 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 0a 00 08 00 00 00 08 00 00 00 00 00 0f 00 07 03 00 00 00 04 06 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 061 Poly Saw (Multi engine) - not converted

### 062 Step by Step (Lead engine)

Pulse &middot; A7 D7 S10 R4 &middot; PWM 2080/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=11 depth=64 &middot; LFO2 Triangle -> Pitch rate=70 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=70, depth=64, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 07 07 0a 04 02 00 00 08 00 04 00 00 00 00 00 00 00 00 00 00 00 0b 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 04 06 04 00 00 00 00 01 00 00 f7
```

### 063 Slow Intro (Lead engine)

Pulse &middot; A0 D0 S12 R12 &middot; PWM 0/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=14/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=14/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0c 0c 00 00 00 00 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0e f7
```

### 064 ARPSEQ One A (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1856/4095 &middot; Filter BP cutoff=514/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=61 depth=64 &middot; LFO2 Triangle -> Cutoff rate=6 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=10/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=6, depth=64, -> Cutoff); Unison on, detune=10/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 07 00 02 00 02 00 0f 00 01 02 00 00 00 03 0d 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 06 04 00 00 02 00 01 00 0a f7
```

### 065 ARPSEQ One B (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1856/4095 &middot; Filter BP cutoff=515/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=61 depth=64 &middot; LFO2 Triangle -> Cutoff rate=6 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=10/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=6, depth=64, -> Cutoff); Unison on, detune=10/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 07 00 02 00 03 00 0f 00 01 02 00 00 00 03 0d 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 06 04 00 00 02 00 01 00 0a f7
```

### 066 ARPSEQ One C (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1856/4095 &middot; Filter BP cutoff=467/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=61 depth=64 &middot; LFO2 Triangle -> Cutoff rate=6 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=10/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=6, depth=64, -> Cutoff); Unison on, detune=10/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 07 00 01 0d 03 00 0f 00 01 02 00 00 00 03 0d 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 06 04 00 00 02 00 01 00 0a f7
```

### 067 SEQ TranceBass (Lead engine)

Pulse &middot; A0 D0 S15 R10 &middot; PWM 2080/4095 &middot; Filter LP+BP cutoff=91/2047 res=15/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 0a 02 00 00 08 00 00 05 0b 00 0f 00 03 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 068 SEQ Vintage A (Lead engine)

Sawtooth &middot; A0 D1 S13 R11 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 01 0d 0b 00 00 00 08 00 00 07 00 00 0f 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 069 SEQ Vintage B (Lead engine)

Sawtooth &middot; A0 D2 S13 R11 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=10/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=10/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 02 0d 0b 00 00 00 08 00 00 07 00 00 0f 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0a f7
```

### 070 SEQ Vintage C (Lead engine)

Sawtooth &middot; A0 D2 S12 R11 &middot; Filter BP+HP cutoff=720/2047 res=15/15 route=v2+v3 &middot; LFO Triangle -> Cutoff rate=3 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=10/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=10/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 02 0c 0b 00 00 00 08 00 02 0d 00 00 0f 00 06 06 00 00 00 00 03 04 00 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0a f7
```

### 071 ARPSEQ Two A (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1344/4095 &middot; Filter LP cutoff=80/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=38 depth=64 &middot; LFO2 Triangle -> Cutoff rate=39 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=39, depth=64, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 05 00 00 05 00 00 0f 00 01 01 00 00 00 02 06 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 02 07 04 00 00 02 00 01 00 00 f7
```

### 072 ARPSEQ Two B (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1344/4095 &middot; Filter LP cutoff=80/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=38 depth=64 &middot; LFO2 Triangle -> Cutoff rate=39 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=39, depth=64, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 05 00 00 05 00 00 0f 00 01 01 00 00 00 02 06 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 02 07 04 00 00 02 00 01 00 00 f7
```

### 073 ARPSEQ Two C (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1344/4095 &middot; Filter LP cutoff=80/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=38 depth=64 &middot; LFO2 Triangle -> Cutoff rate=39 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=39, depth=64, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 05 00 00 05 00 00 0f 00 01 01 00 00 00 02 06 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 02 07 04 00 00 02 00 01 00 00 f7
```

### 074 ARPSEQ Three (Lead engine)

Pulse &middot; A0 D0 S15 R2 &middot; PWM 1344/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=38 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=18/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=18/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 02 04 00 00 05 00 01 06 00 00 0f 00 00 00 00 00 00 02 06 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 02 f7
```

### 075 ARPSEQ Four (Lead engine)

Sawtooth &middot; A0 D8 S9 R4 &middot; Filter LP+BP+HP cutoff=488/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=124 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=2/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 08 09 04 00 00 00 08 00 01 0e 08 00 0f 00 07 07 00 00 00 07 0c 04 00 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 076 SEQ Mighty Bass (Lead engine)

Sawtooth &middot; A0 D2 S12 R11 &middot; Filter LP+BP cutoff=234/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 02 0c 0b 04 00 00 07 00 00 0e 0a 00 0f 00 07 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 077 Analog Dream1 (Lead engine)

(no waveform) &middot; A1 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=5/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; OSC1 'Disable Oscillator' flag was set in source; kept waveform bits anyway; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=5/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 00 01 00 0f 00 02 00 00 02 00 00 00 00 00 0f 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 05 f7
```

### 078 Analog Dream2 (Lead engine)

Pulse &middot; A0 D0 S15 R8 &middot; PWM 1312/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=2/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 08 02 00 00 05 00 07 0f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 079 Analog Dream3 (Lead engine)

Pulse &middot; A2 D15 S4 R6 &middot; PWM 800/4095 &middot; Filter LP cutoff=1184/2047 res=0/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=52 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 02 0f 04 06 02 00 00 03 00 04 0a 00 00 00 00 07 01 00 00 00 03 04 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 080 Analog Dream4 (Lead engine)

Pulse [ring] &middot; A0 D0 S15 R9 &middot; PWM 32/4095 &middot; Filter LP cutoff=992/2047 res=7/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=5 depth=64 &middot; LFO2 Triangle -> Cutoff rate=11 depth=64 &middot; Arp Up rate=30 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=64, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 04 00 00 0f 09 02 00 00 00 00 03 0e 00 00 07 00 07 01 00 00 00 00 05 04 00 00 02 00 00 01 0e 00 00 00 02 00 00 00 0b 04 00 00 02 00 01 00 00 f7
```

### 081 Analog Dream5 (Lead engine)

Pulse &middot; A2 D15 S7 R6 &middot; PWM 800/4095 &middot; Filter LP cutoff=1421/2047 res=6/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=52 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 02 0f 07 06 02 00 00 03 00 05 08 0d 00 06 00 07 01 00 00 00 03 04 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 082 PWM Bass1 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=36 depth=64 &middot; LFO2 Triangle -> PWM rate=22 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=16/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=22, depth=64, -> PWM); Unison on, detune=16/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 00 01 00 00 0f 00 00 00 00 00 00 02 04 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 01 06 04 00 00 01 00 01 01 00 f7
```

### 083 PWM Bass2 (Lead engine)

Sawtooth &middot; A0 D11 S4 R11 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 0b 04 0b 02 00 00 05 00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 084 PWM Bass3 (Lead engine)

Pulse &middot; A0 D11 S5 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=176/2047 res=2/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=35 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 0b 05 00 00 00 00 08 00 00 0b 00 00 02 00 07 01 00 00 00 02 03 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 085 PWM Bass4 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1312/4095 &middot; Filter LP cutoff=1331/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=4 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=15/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=15/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 02 00 00 05 00 05 03 03 00 0f 00 07 01 00 00 00 00 04 04 00 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0f f7
```

### 086 Seq Bass1 (Lead engine)

Sawtooth &middot; A1 D0 S15 R0 &middot; Filter LP cutoff=124/2047 res=6/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=71 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 01 00 0f 00 02 00 00 02 00 00 07 0c 00 06 00 07 01 00 00 00 04 07 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 087 Seq Bass2 (Lead engine)

Pulse &middot; A0 D11 S5 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=0/2047 res=2/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=20 depth=127 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 0b 05 00 00 00 00 08 00 00 00 00 00 02 00 07 01 00 00 00 01 04 07 0f 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 088 Seq Bass3 (Lead engine)

Sawtooth &middot; A0 D12 S4 R11 &middot; Filter LP cutoff=992/2047 res=15/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 0c 04 0b 06 00 00 05 00 03 0e 00 00 0f 00 01 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 089 Seq Bass4 (Lead engine)

Triangle+Pulse &middot; A0 D0 S10 R0 &middot; PWM 2048/4095 &middot; Filter LP+BP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 00 00 0a 00 00 00 00 08 00 00 00 00 00 0f 00 07 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 090 Seq Bass5 (Lead engine)

Triangle+Pulse &middot; A0 D0 S10 R0 &middot; PWM 2048/4095 &middot; Filter LP+BP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=9/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=9/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 00 00 0a 00 00 00 00 08 00 00 00 00 00 0f 00 07 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 09 f7
```

### 091 Seq Bass6 (Lead engine)

Triangle+Pulse &middot; A0 D0 S10 R0 &middot; PWM 2048/4095 &middot; Filter LP+BP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 00 00 0a 00 00 00 00 08 00 00 00 00 00 0f 00 07 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 092 Monty Bass1 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 00 0f 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 093 Monty Bass2 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2400/4095 &middot; Filter LP cutoff=400/2047 res=10/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=33 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=5/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=5/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 06 00 00 09 00 01 09 00 00 0a 00 07 01 00 00 00 02 01 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 05 f7
```

### 094 Monty Bass3 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1344/4095 &middot; Filter LP cutoff=336/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=68 depth=64 &middot; LFO2 Triangle -> Cutoff rate=2 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=3/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=2, depth=64, -> Cutoff); Unison on, detune=3/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 05 00 01 05 00 00 0f 00 07 01 00 00 00 04 04 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 02 04 00 00 02 00 01 00 03 f7
```

### 095 Monty Lead1 (Lead engine)

Pulse &middot; A2 D15 S4 R6 &middot; PWM 256/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=52 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 02 0f 04 06 00 00 00 01 00 01 0a 00 00 0f 00 00 00 00 00 00 03 04 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 096 Monty Lead2 (Lead engine)

Pulse &middot; A2 D15 S4 R6 &middot; PWM 256/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=74 depth=64 &middot; LFO2 Triangle -> Pitch rate=78 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=3/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=78, depth=64, -> Pitch); Unison on, detune=3/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 02 0f 04 06 00 00 00 01 00 01 0a 00 00 0f 00 00 00 00 00 00 04 0a 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 04 0e 04 00 00 00 00 01 00 03 f7
```

### 097 Monty Lead3 (Lead engine)

Pulse [sync] &middot; A2 D15 S4 R6 &middot; PWM 512/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=47 depth=100 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 02 02 0f 04 06 00 00 00 02 00 01 0a 00 00 0f 00 00 00 00 00 00 02 0f 06 04 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 098 Monty Lead4 (Lead engine)

Pulse &middot; A2 D15 S4 R6 &middot; PWM 1568/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=78 depth=97 &middot; LFO2 Triangle -> PWM rate=74 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=9/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=74, depth=64, -> PWM); Unison on, detune=9/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 02 0f 04 06 02 00 00 06 00 01 0a 00 00 0f 00 00 00 00 00 00 04 0e 06 01 00 00 00 00 03 0e 00 00 00 02 00 00 04 0a 04 00 00 01 00 01 00 09 f7
```

### 099 Bassline Demo1 (Bassline engine)

Pulse &middot; A0 D2 S0 R1 &middot; PWM 2304/4095 &middot; Filter LP cutoff=192/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=54 depth=127 &middot; LFO2 Triangle -> Pitch rate=26 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=26, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 02 00 01 00 00 00 09 00 00 0c 00 00 0f 00 07 01 00 00 00 03 06 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 01 0a 07 0f 00 00 00 00 00 00 f7
```

### 100 Bassline Demo2 (Bassline engine)

Pulse &middot; A0 D3 S0 R1 &middot; PWM 2546/4095 &middot; Filter LP+BP cutoff=8/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=55 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 12 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 03 00 01 0f 02 00 09 00 00 00 08 00 0f 00 07 03 00 00 00 03 07 07 0f 00 00 00 00 03 0e 00 00 00 0c 00 00 00 0b 07 0f 00 00 00 00 00 00 f7
```

### 101 Vib Synth (Lead engine)

Sawtooth &middot; A0 D0 S15 R8 &middot; Filter LP cutoff=1296/2047 res=15/15 route=v1 &middot; LFO Triangle -> Pitch rate=64 depth=118 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 00 0f 08 06 00 00 02 00 05 01 00 00 0f 00 01 01 00 00 00 04 00 07 06 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 102 Whats That? (Lead engine)

Triangle [ring] &middot; A0 D0 S15 R0 &middot; Filter LP cutoff=736/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=25 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 00 00 0f 00 00 00 00 08 00 02 0e 00 00 0f 00 07 01 00 00 00 01 09 04 00 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 103 Sample & Hold 1 (Lead engine)

Sawtooth &middot; A0 D0 S15 R10 &middot; Filter off &middot; LFO Sawtooth -> Pitch rate=22 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 00 0f 0a 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 01 01 06 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 104 Sample & Hold 2 (Lead engine)

Triangle [ring] &middot; A0 D0 S15 R10 &middot; Filter off &middot; LFO Sample&Hold -> Pitch rate=127 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 00 00 0f 0a 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 03 07 0f 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 105 Curve Filter D (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=352/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=16 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 06 00 00 0f 00 07 01 00 00 00 01 00 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 106 Curve total (Lead engine)

Triangle &middot; A0 D0 S15 R0 &middot; Filter LP cutoff=0/2047 res=0/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 00 0f 00 00 00 00 08 00 00 00 00 00 00 00 07 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 107 Poly Trancegate (Multi engine) - not converted

### 108 Zak Bass (Lead engine)

Pulse &middot; A0 D0 S13 R0 &middot; PWM 0/4095 &middot; Filter LP cutoff=960/2047 res=9/15 route=v2 &middot; LFO Triangle -> PWM rate=11 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0d 00 00 00 00 00 00 03 0c 00 00 09 00 02 01 00 00 00 00 0b 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 109 Zak Bass 2 (Lead engine)

Pulse &middot; A0 D0 S13 R0 &middot; PWM 0/4095 &middot; Filter LP cutoff=960/2047 res=9/15 route=v2+v3 &middot; LFO Triangle -> PWM rate=11 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0d 00 00 00 00 00 00 03 0c 00 00 09 00 06 01 00 00 00 00 0b 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 110 Kik A (Lead engine)

Triangle &middot; A0 D10 S0 R5 &middot; Filter LP cutoff=256/2047 res=5/15 route=v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 55 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 0a 00 05 02 00 00 07 00 01 00 00 00 05 00 06 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 03 07 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 111 Cymbal   A (Lead engine)

Noise &middot; A2 D6 S0 R9 &middot; Filter BP cutoff=968/2047 res=3/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=78 depth=64 &middot; LFO2 Triangle -> Cutoff rate=11 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 37 &middot; Unison on detune=22/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=64, -> Cutoff); Unison on, detune=22/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 02 06 00 09 04 00 00 08 00 03 0c 08 00 03 00 07 02 00 00 00 04 0e 04 00 00 00 00 00 03 0e 00 00 02 05 00 00 00 0b 04 00 00 02 00 01 01 06 f7
```

### 112 Cymbal   B (Lead engine)

Noise &middot; A2 D6 S0 R9 &middot; Filter BP cutoff=752/2047 res=8/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=78 depth=64 &middot; LFO2 Triangle -> Cutoff rate=7 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 37 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=7, depth=64, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 02 06 00 09 04 00 00 08 00 02 0f 00 00 08 00 07 02 00 00 00 04 0e 04 00 00 00 00 00 03 0e 00 00 02 05 00 00 00 07 04 00 00 02 00 01 00 00 f7
```

### 113 Hat A (Lead engine)

Pulse &middot; A0 D0 S13 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0d 00 00 00 00 08 00 03 04 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 114 Snare A (Lead engine)

Pulse &middot; A0 D0 S14 R5 &middot; PWM 1792/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0e 05 00 00 00 07 00 00 09 00 00 0f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 115 Accomp A (Lead engine)

Sawtooth &middot; A0 D3 S15 R6 &middot; Filter LP cutoff=1312/2047 res=9/15 route=v1 &middot; LFO Triangle -> Cutoff rate=24 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 0 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 03 0f 06 04 00 00 07 00 05 02 00 00 09 00 01 01 00 00 00 01 08 04 00 00 02 00 00 03 0e 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 116 Accomp B (Lead engine)

Sawtooth [sync] &middot; A3 D3 S15 R10 &middot; Filter BP cutoff=944/2047 res=9/15 route=v2+v3 &middot; LFO Triangle -> PWM rate=53 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 02 03 03 0f 0a 02 00 00 01 00 03 0b 00 00 09 00 06 02 00 00 00 03 05 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 117 Lead Stacco1 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1088/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=5 depth=64 &middot; LFO2 off &middot; Arp Up rate=30 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=13/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=13/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 04 00 06 0b 00 00 00 00 00 00 00 00 00 00 05 04 00 00 01 00 00 01 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0d f7
```

### 118 Lead Stacco2 (Lead engine)

Sawtooth &middot; A0 D0 S15 R5 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=5/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=5/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 05 04 00 00 08 00 06 0b 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 05 f7
```

### 119 Lead Melodia (Lead engine)

Sawtooth &middot; A0 D0 S11 R5 &middot; Filter off &middot; LFO Triangle -> Pitch rate=63 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=17/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=17/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0b 05 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 03 0f 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 01 f7
```

### 120 Stacco4 (Lead engine)

Pulse &middot; A0 D0 S12 R1 &middot; PWM 1632/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=22 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0c 01 06 00 00 06 00 05 00 00 00 00 00 00 00 00 00 00 01 06 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 121 String Ponte 1 (Lead engine)

(no waveform) &middot; A6 D3 S14 R3 &middot; Filter BP cutoff=0/2047 res=15/15 route=v3 &middot; LFO Triangle -> Pitch rate=3 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 00 06 03 0e 03 00 00 00 08 00 00 00 00 00 0f 00 04 02 00 00 00 00 03 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 122 String Ponte 2 (Lead engine)

Triangle &middot; A6 D3 S14 R3 &middot; Filter BP cutoff=0/2047 res=15/15 route=v3 &middot; LFO Triangle -> Pitch rate=11 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 06 03 0e 03 04 00 00 01 00 00 00 00 00 0f 00 04 02 00 00 00 00 0b 04 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 123 Crescendo (Lead engine)

Pulse &middot; A0 D0 S15 R9 &middot; PWM 2592/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=78 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 0 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 09 02 00 00 0a 00 00 0e 00 00 0f 00 00 00 00 00 00 04 0e 04 00 00 00 00 00 03 0e 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 124 Crazy Lead (Lead engine)

Sawtooth &middot; A0 D0 S11 R5 &middot; Filter off &middot; LFO Triangle -> Pitch rate=66 depth=112 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 6 &middot; Unison on detune=15/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=15/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0b 05 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 04 02 07 00 00 00 00 00 03 0e 00 00 00 06 00 00 00 00 00 00 00 00 00 01 00 0f f7
```

### 125 Accomp1 (Lead engine)

Triangle &middot; A0 D0 S15 R9 &middot; Filter LP cutoff=1024/2047 res=0/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 00 0f 09 00 00 00 08 00 04 00 00 00 00 00 01 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 126 Casio Drums (Lead engine)

Pulse &middot; A0 D4 S0 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 04 00 00 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 127 Classic Zelda (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=2032/2047 res=0/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 07 0f 00 00 00 00 01 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 128 Neo Zelda (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=1744/2047 res=6/15 route=v1+v2+v3 &middot; LFO Square -> PWM rate=100 depth=64 &middot; LFO2 Triangle -> Cutoff rate=45 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=45, depth=64, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 06 0d 00 00 06 00 07 01 00 00 02 06 04 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 02 0d 04 00 00 02 00 01 00 00 f7
```

## Bank 2 - TK2 Soundbank

A collection of bassline sequences by Thorsten Klose (2010-2017). http://midibox.org/forums/topic/15119-midibox-sid-v2-patches/

### 001 Lead Patch (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 002 Bassline 3_1 (Bassline engine)

Pulse &middot; A0 D3 S1 R1 &middot; PWM 2394/4095 &middot; Filter LP+BP cutoff=783/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=63 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 01 01 05 0a 00 09 00 03 00 0f 00 0f 00 0f 03 00 00 00 03 0f 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 003 Bassline 3_2 (Bassline engine)

Pulse &middot; A0 D3 S1 R1 &middot; PWM 2394/4095 &middot; Filter LP+BP cutoff=664/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=63 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 01 01 05 0a 00 09 00 02 09 08 00 0f 00 0f 03 00 00 00 03 0f 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 004 Bassline 4_1 (Bassline engine)

Sawtooth &middot; A0 D3 S1 R1 &middot; Filter LP cutoff=1351/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=27 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 03 01 01 01 08 00 08 00 05 04 07 00 0f 00 07 01 00 00 00 01 0b 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 00 00 00 f7
```

### 005 Bassline 4_2 (Bassline engine)

Sawtooth &middot; A0 D3 S1 R1 &middot; Filter LP cutoff=1351/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=27 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 03 01 01 01 08 00 08 00 05 04 07 00 0f 00 07 01 00 00 00 01 0b 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 006 Bassline 4_3 (Bassline engine)

Sawtooth &middot; A0 D3 S1 R1 &middot; Filter LP cutoff=1351/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=27 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 03 01 01 01 08 00 08 00 05 04 07 00 0f 00 07 01 00 00 00 01 0b 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 007 Bassline 5 (Bassline engine)

Pulse &middot; A0 D2 S2 R1 &middot; PWM 2117/4095 &middot; Filter LP+BP cutoff=198/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 02 02 01 04 05 00 08 00 00 0c 06 00 0f 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 008 Bassline 6 (Bassline engine)

Pulse &middot; A0 D10 S2 R1 &middot; PWM 1447/4095 &middot; Filter LP+BP cutoff=171/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=34/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=34/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 0a 02 01 0a 07 00 05 00 00 0a 0b 00 0f 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 02 02 f7
```

### 009 Bassline 7 (Bassline engine)

Sawtooth &middot; A0 D10 S2 R1 &middot; Filter LP+BP cutoff=161/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=4 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=4/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=4/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 0a 02 01 0a 07 00 05 00 00 0a 01 00 0f 00 0f 03 00 00 00 00 04 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 04 f7
```

### 010 Bassline 8 (Bassline engine)

Sawtooth &middot; A0 D0 S2 R1 &middot; Filter LP+BP cutoff=616/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 02 01 00 00 00 09 00 02 06 08 00 0f 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 011 Bassline 9 (Bassline engine)

Sawtooth &middot; A0 D1 S2 R1 &middot; Filter LP+BP cutoff=542/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=1/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=1/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 01 02 01 01 00 00 09 00 02 01 0e 00 0f 00 07 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 01 f7
```

### 012 Bassline 10 (Bassline engine)

Pulse &middot; A0 D8 S2 R1 &middot; PWM 3766/4095 &middot; Filter LP+BP cutoff=587/2047 res=0/15 route=v1+v2+v3+ext &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 08 02 01 0b 06 00 0e 00 02 04 0b 00 00 00 0f 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 013 Bassline 11 (Bassline engine)

Pulse &middot; A0 D2 S2 R1 &middot; PWM 2304/4095 &middot; Filter LP+BP cutoff=223/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 02 02 01 00 00 00 09 00 00 0d 0f 00 0f 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 014 Bassline 12 (Bassline engine)

Pulse &middot; A0 D2 S2 R1 &middot; PWM 2117/4095 &middot; Filter LP+BP cutoff=168/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=33/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=33/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 02 02 01 04 05 00 08 00 00 0a 08 00 0f 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 02 01 f7
```

### 015 Bassline 13 (Bassline engine)

Pulse &middot; A0 D1 S1 R3 &middot; PWM 2304/4095 &middot; Filter LP+BP cutoff=1727/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 01 01 03 00 00 00 09 00 06 0b 0f 00 0f 00 0f 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 016 Bassline 14 (Bassline engine)

Pulse &middot; A0 D3 S2 R1 &middot; PWM 2304/4095 &middot; Filter LP+BP cutoff=707/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 03 02 01 00 00 00 09 00 02 0c 03 00 0f 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 00 00 00 f7
```

### 017 Bassline 15 (Bassline engine)

Pulse &middot; A0 D3 S2 R0 &middot; PWM 2304/4095 &middot; Filter LP+BP cutoff=641/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=8 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 03 02 00 00 00 00 09 00 02 08 01 00 0f 00 0f 03 00 00 00 00 08 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 00 00 00 f7
```

### 018 Bassline 16 (Bassline engine)

Pulse &middot; A0 D3 S2 R0 &middot; PWM 2304/4095 &middot; Filter LP+BP cutoff=0/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=8 depth=127 &middot; LFO2 Triangle -> Pitch rate=116 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=116, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 03 02 00 00 00 00 09 00 00 00 00 00 0f 00 0f 03 00 00 00 00 08 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 07 04 07 0f 00 00 00 00 00 00 f7
```

### 019 Bassline 17 (Bassline engine)

Pulse &middot; A0 D3 S2 R0 &middot; PWM 2215/4095 &middot; Filter LP+BP cutoff=64/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=9 depth=127 &middot; LFO2 Triangle -> Pitch rate=116 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=116, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 03 02 00 0a 07 00 08 00 00 04 00 00 0f 00 0f 03 00 00 00 00 09 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 07 04 07 0f 00 00 00 00 00 00 f7
```

### 020 Bassline 18 (Bassline engine)

Sawtooth &middot; A0 D3 S2 R0 &middot; Filter LP+BP cutoff=418/2047 res=0/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=9 depth=127 &middot; LFO2 Triangle -> Pitch rate=116 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=116, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 03 02 00 0a 07 00 08 00 01 0a 02 00 00 00 0f 03 00 00 00 00 09 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 07 04 07 0f 00 00 00 00 00 00 f7
```

### 021 Bassline 19 (Bassline engine)

Pulse &middot; A0 D3 S2 R0 &middot; PWM 2215/4095 &middot; Filter LP+BP cutoff=494/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=9 depth=127 &middot; LFO2 Triangle -> Pitch rate=116 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=116, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 03 02 00 0a 07 00 08 00 01 0e 0e 00 0f 00 0f 03 00 00 00 00 09 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 07 04 07 0f 00 00 00 00 00 00 f7
```

### 022 Bassline 20 (Bassline engine)

Sawtooth &middot; A0 D3 S2 R0 &middot; Filter LP+BP cutoff=679/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=6 depth=127 &middot; LFO2 Triangle -> Pitch rate=116 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=3/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=116, depth=127, -> Pitch); Unison on, detune=3/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 03 02 00 06 0d 00 04 00 02 0a 07 00 0f 00 07 03 00 00 00 00 06 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 07 04 07 0f 00 00 00 01 00 03 f7
```

### 023 Bassline 21 (Bassline engine)

Pulse &middot; A0 D3 S2 R0 &middot; PWM 1133/4095 &middot; Filter LP+BP cutoff=955/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=6 depth=127 &middot; LFO2 Triangle -> Pitch rate=116 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=116, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 03 02 00 06 0d 00 04 00 03 0b 0b 00 0f 00 0f 03 00 00 00 00 06 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 07 04 07 0f 00 00 00 00 00 00 f7
```

### 024 Bassline 22 (Bassline engine)

Sawtooth &middot; A0 D7 S1 R2 &middot; Filter LP+BP cutoff=129/2047 res=7/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 07 01 02 00 00 00 09 00 00 08 01 00 07 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 025 Bassline 23 (Bassline engine)

Sawtooth &middot; A0 D7 S1 R2 &middot; Filter LP+BP cutoff=129/2047 res=7/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 07 01 02 00 00 00 09 00 00 08 01 00 07 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 026 Bassline 24 (Bassline engine)

Pulse &middot; A0 D7 S1 R2 &middot; PWM 1518/4095 &middot; Filter LP+BP cutoff=115/2047 res=7/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=17 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 07 01 02 0e 0e 00 05 00 00 07 03 00 07 00 0f 03 00 00 00 01 01 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 027 Bassline 24_2 (Bassline engine)

Pulse &middot; A0 D4 S1 R2 &middot; PWM 1518/4095 &middot; Filter LP+BP cutoff=335/2047 res=7/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=17 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 04 01 02 0e 0e 00 05 00 01 04 0f 00 07 00 0f 03 00 00 00 01 01 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 028 Bassline 25 (Bassline engine)

Sawtooth &middot; A0 D1 S0 R2 &middot; Filter LP+BP cutoff=0/2047 res=7/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=23 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 01 00 02 0e 0e 00 05 00 00 00 00 00 07 00 0f 03 00 00 00 01 07 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 029 Bassline 26 (Bassline engine)

Sawtooth &middot; A0 D1 S0 R2 &middot; Filter LP+BP cutoff=0/2047 res=7/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=23 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 01 00 02 0e 0e 00 05 00 00 00 00 00 07 00 0f 03 00 00 00 01 07 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 030 Bassline 27 (Bassline engine)

Sawtooth+Pulse &middot; A0 D2 S1 R1 &middot; PWM 2107/4095 &middot; Filter LP+BP cutoff=721/2047 res=11/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 00 00 02 01 01 03 0b 00 08 00 02 0d 01 00 0b 00 0f 03 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 031 Bassline 28 (Bassline engine)

Pulse &middot; A0 D2 S1 R1 &middot; PWM 1550/4095 &middot; Filter LP+BP cutoff=936/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=36 depth=127 &middot; LFO2 Triangle -> Pitch rate=19 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=19, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 02 01 01 00 0e 00 06 00 03 0a 08 00 0f 00 0f 03 00 00 00 02 04 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 01 03 07 0f 00 00 00 01 00 00 f7
```

### 032 Bassline 29 (Bassline engine)

Sawtooth &middot; A0 D3 S2 R1 &middot; Filter LP cutoff=667/2047 res=15/15 route=v1+v2+v3+ext &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=108/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=108/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 03 02 01 00 00 00 09 00 02 09 0b 00 0f 00 0f 01 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 06 0c f7
```

### 033  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 034  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 035  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 036  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 037  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 038  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 039  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 040  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 041  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 042  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 043  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 044  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 045  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 046  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 047  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 048  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 049  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 050  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 051  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 052  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 053  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 054  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 055  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 056  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 057  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 058  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 059  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 060  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 061  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 062  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 063  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 064  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 065  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 066  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 067  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 068  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 069  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 070  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 071  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 072  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 073  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 074  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 075  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 076  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 077  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 078  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 079  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 080  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 081  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 082  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 083  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 084  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 085  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 086  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 087  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 088  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 089  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 090  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 091  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 092  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 093  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 094  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 095  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 096  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 097  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 098  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 099  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 100  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 101  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 102  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 103  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 104  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 105  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 106  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 107  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 108  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 109  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 110  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 111  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 112  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 113  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 114  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 115  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 116  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 117  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 118  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 119  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 120  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 121  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 122  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 123  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 124  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 125  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 126  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 127  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 128  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

## Bank 3 - 127 Arps

A collection of arpeggiator patterns. http://midibox.org/forums/topic/13345-127-arps/

### 001 Surface 1 (Lead engine)

Triangle+Pulse &middot; A11 D10 S11 R10 &middot; PWM 2048/4095 &middot; Filter BP cutoff=1170/2047 res=10/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=127 depth=64 &middot; LFO2 Sawtooth -> PWM rate=126 depth=64 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Sawtooth, rate=126, depth=64, -> PWM); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 0b 0a 0b 0a 00 00 00 08 00 04 09 02 00 0a 00 07 02 00 00 00 07 0f 04 00 00 00 00 00 03 0e 00 00 00 02 00 01 07 0e 04 00 00 01 00 01 00 00 f7
```

### 002 Arp 2 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 003 Arp 3 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 01 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 004 Arp 4 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 02 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 005 Arp 5 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 006 Arp 6 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 007 Arp 7 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=9/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=9/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 09 f7
```

### 008 Arp 8 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=9/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=9/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 09 f7
```

### 009 Arp 9 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=9/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=9/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 09 f7
```

### 010 Arp 10 (Lead engine)

Pulse [sync] &middot; A0 D0 S15 R0 &middot; PWM 1853/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=126 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=9/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=9/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 02 00 00 0f 00 03 0d 00 07 00 01 03 0e 00 00 00 00 01 00 00 00 07 0e 04 00 00 01 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 09 f7
```

### 011 Arp 11 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1080/4095 &middot; Filter LP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 03 08 00 04 00 00 00 00 00 0f 00 07 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 012 Arp 12 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1080/4095 &middot; Filter LP+BP cutoff=854/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=124 depth=27 &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 03 08 00 04 00 03 05 06 00 0f 00 07 03 00 00 00 07 0c 01 0b 00 02 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 013 Arp 13 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1080/4095 &middot; Filter LP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=124 depth=27 &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=18/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=18/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 03 08 00 04 00 00 00 00 00 0f 00 07 01 00 00 00 07 0c 01 0b 00 02 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 01 02 f7
```

### 014 Arp 14 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1080/4095 &middot; Filter LP cutoff=425/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=124 depth=27 &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=18/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=18/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 03 08 00 04 00 01 0a 09 00 0f 00 07 01 00 00 00 07 0c 01 0b 00 02 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 01 02 f7
```

### 015 Arp 15 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1506/4095 &middot; Filter HP cutoff=447/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=126 depth=27 &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=3/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=3/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 0e 02 00 05 00 01 0b 0f 00 0f 00 07 04 00 00 00 07 0e 01 0b 00 02 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 03 f7
```

### 016 Arp 16 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1156/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=127 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 07 0f 04 00 00 01 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 017 Arp 17 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 018 Arp 18 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 01 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 019 Arp 19 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Random rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 03 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 020 Arp 20 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Random rate=62 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 03 03 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 021 Arp 21 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Random rate=30 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 03 01 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 022 Arp 22 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Random rate=90 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 03 05 0a 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 023 Arp 23 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=30 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 01 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 024 Arp 24 (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 08 04 00 04 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 025 Arp 25 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1156/4095 &middot; Filter LP+HP cutoff=1762/2047 res=15/15 route=v1 &middot; LFO Triangle -> PWM rate=124 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 08 04 00 04 00 06 0e 02 00 0f 00 01 05 00 00 00 07 0c 04 00 00 01 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 026 Arp 26 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1156/4095 &middot; Filter LP+HP cutoff=1762/2047 res=15/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=30 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 08 04 00 04 00 06 0e 02 00 0f 00 01 05 00 00 00 00 00 00 00 00 00 00 00 01 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 027 Arp 27 (Lead engine)

Triangle &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=30 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 00 0f 00 08 04 00 04 00 06 0e 02 00 0f 00 00 00 00 00 00 00 00 00 00 00 00 00 01 01 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 028 Arp 28 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> PWM rate=36 depth=64 &middot; LFO2 Triangle -> PWM rate=22 depth=64 &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=16/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=22, depth=64, -> PWM); Unison on, detune=16/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 00 01 00 00 0f 00 00 00 00 00 00 02 04 04 00 00 01 00 00 03 0e 00 01 00 02 00 00 01 06 04 00 00 01 00 01 01 00 f7
```

### 029 Arp 29 (Lead engine)

Sawtooth &middot; A5 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 05 00 0f 00 0c 02 00 0d 00 01 08 05 00 0a 00 00 06 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 030 Arp 30 (Lead engine)

Triangle+Pulse &middot; A4 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=62 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 04 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 01 03 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 031 Arp 31 (Lead engine)

Pulse &middot; A4 D11 S10 R3 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 04 0b 0a 03 00 00 00 08 00 06 04 01 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 032 Arp 32 (Lead engine)

Sawtooth &middot; A4 D8 S15 R2 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 04 08 0f 02 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 02 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 033 Arp 33 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1080/4095 &middot; Filter LP+BP cutoff=854/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=124 depth=27 &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 03 08 00 04 00 03 05 06 00 0f 00 07 03 00 00 00 07 0c 01 0b 00 02 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 034 Arp 34 (Lead engine)

Sawtooth &middot; A7 D11 S10 R8 &middot; Filter LP+HP cutoff=1678/2047 res=0/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 85 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 07 0b 0a 08 0a 0d 00 0c 00 06 08 0e 00 00 00 03 05 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 05 05 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 035 Arp 35 (Lead engine)

Sawtooth+Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP+BP cutoff=1883/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=126 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 00 00 00 0f 00 00 00 00 08 00 07 05 0b 00 0f 00 07 03 00 00 00 00 00 00 00 00 00 00 02 07 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 036 Arp 36 (Lead engine)

Pulse &middot; A2 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=2047/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=30 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=17/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=17/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 02 00 0f 00 00 00 00 08 00 07 0f 0f 00 0f 00 07 01 00 00 00 00 00 00 00 00 00 00 02 01 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 01 01 f7
```

### 037 Arp 37 (Lead engine)

Triangle &middot; A0 D4 S2 R5 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 01 00 00 04 02 05 00 00 00 08 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 01 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 038 Arp 38 (Lead engine)

Pulse &middot; A0 D3 S13 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=2/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 0d 00 00 00 00 08 00 04 01 00 00 0f 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 039 Arp 39 (Lead engine)

Sawtooth &middot; A7 D0 S15 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 07 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 02 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 040 Arp 40 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter BP+HP cutoff=107/2047 res=13/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 00 06 0b 00 0d 00 01 06 00 00 00 00 00 00 00 00 00 00 02 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 041 Arp 41 (Lead engine)

Triangle+Pulse &middot; A2 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter HP cutoff=148/2047 res=15/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=30 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 05 00 02 00 0f 00 00 00 00 08 00 00 09 04 00 0f 00 01 04 00 00 00 00 00 00 00 00 00 00 02 01 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 042 Arp 42 (Lead engine)

Sawtooth &middot; A4 D0 S15 R8 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=9/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=9/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 04 00 0f 08 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 09 f7
```

### 043 Arp 43 (Lead engine)

Pulse &middot; A2 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter BP+HP cutoff=577/2047 res=7/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=22/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=22/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 02 00 0f 00 00 00 00 08 00 02 04 01 00 07 00 03 06 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 01 06 f7
```

### 044 Arp 44 (Lead engine)

Triangle &middot; A2 D0 S15 R0 &middot; Filter BP+HP cutoff=577/2047 res=7/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=22/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=22/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 02 00 0f 00 00 00 00 08 00 02 04 01 00 07 00 03 06 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 01 06 f7
```

### 045 Arp 45 (Lead engine)

Sawtooth &middot; A2 D0 S15 R0 &middot; Filter BP+HP cutoff=577/2047 res=7/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=22/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=22/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 02 00 0f 00 00 00 00 08 00 02 04 01 00 07 00 03 06 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 01 06 f7
```

### 046 Arp 46 (Lead engine)

Triangle+Pulse &middot; A2 D0 S15 R3 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=13/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=13/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 02 00 0f 03 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 0d f7
```

### 047 Arp 47 (Lead engine)

Triangle+Pulse &middot; A2 D0 S15 R3 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=13/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=13/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 02 00 0f 03 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 0d f7
```

### 048 Arp 48 (Lead engine)

Triangle+Pulse &middot; A2 D0 S15 R3 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=13/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=13/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 02 00 0f 03 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 01 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 0d f7
```

### 049 Arp 49 (Lead engine)

Triangle+Pulse &middot; A2 D0 S15 R3 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=62 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=13/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=13/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 02 00 0f 03 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 01 03 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 0d f7
```

### 050 Arp 50 (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP+BP cutoff=1868/2047 res=0/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 07 04 0c 00 00 00 07 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 051  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 052  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 053  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 054  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 055  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 056  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 057  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 058  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 059  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 060  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 061  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 062  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 063  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 064  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 065  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 066  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 067  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 068  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 069  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 070  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 071  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 072  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 073  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 074  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 075  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 076  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 077  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 078  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 079  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 080  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 081  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 082  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 083  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 084  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 085  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 086  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 087  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 088  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 089  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 090  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 091  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 092  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 093  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 094  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 095  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 096  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 097  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 098  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 099  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 100  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 101  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 102  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 103  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 104  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 105  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 106  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 107  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 108  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 109  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 110  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 111  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 112  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 113  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 114  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 115  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 116  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 117  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 118  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 119  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 120  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 121  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 122  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 123  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 124  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 125  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 126  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 127  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 128  (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

## Bank 4 - SammichSiD Chiptraxxx

A full bank of patches by Chiptraxxx. http://chiptraxxx.blogspot.de/search?q=midibox

### 001 Two Finger ARP (Multi engine) - not converted

### 002 Red Flute (Lead engine)

Pulse &middot; A5 D10 S13 R11 &middot; PWM 2334/4095 &middot; Filter LP+BP+HP cutoff=574/2047 res=11/15 route=v2+v3 &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=94 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=2/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 05 0a 0d 0b 01 0e 00 09 00 02 03 0e 00 0b 00 06 07 00 00 00 03 0b 07 0e 00 00 00 00 05 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 003 Oldschool (Lead engine)

Triangle+Pulse [ring] &middot; A2 D6 S13 R2 &middot; PWM 308/4095 &middot; Filter LP+BP+HP cutoff=564/2047 res=12/15 route=v2 &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=102 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=2/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 02 06 0d 02 03 04 00 01 00 02 03 04 00 0c 00 02 07 00 00 00 03 0b 07 0e 00 00 00 00 06 06 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 004 Bad Basshead (Lead engine)

Triangle+Sawtooth+Pulse &middot; A2 D15 S7 R12 &middot; PWM 3292/4095 &middot; Filter LP+BP cutoff=999/2047 res=12/15 route=v1+v2 &middot; LFO Square -> PWM rate=119 depth=64 &middot; LFO2 off &middot; Arp Up rate=32 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 02 0f 07 0c 0d 0c 00 0c 00 03 0e 07 00 0c 00 03 03 00 00 02 07 07 04 00 00 01 00 00 02 00 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 005 Unstable Horror (Lead engine)

Triangle+Pulse [ring] &middot; A14 D10 S5 R12 &middot; PWM 3601/4095 &middot; Filter BP cutoff=576/2047 res=9/15 route=v1+v2 &middot; LFO Triangle -> Cutoff rate=116 depth=71 &middot; LFO2 off &middot; Arp Up rate=118 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 0e 0a 05 0c 01 01 00 0e 00 02 04 00 00 09 00 03 02 00 00 00 07 04 04 07 00 02 00 00 07 06 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 006 Sandflute 69 (Lead engine)

Triangle [sync,ring] &middot; A3 D9 S2 R9 &middot; Filter HP cutoff=570/2047 res=12/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=48 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 06 03 09 02 09 08 0e 00 0e 00 02 03 0a 00 0c 00 04 04 00 00 00 00 00 00 00 00 00 00 00 03 00 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 007 Telephone ARP (Lead engine)

Pulse &middot; A0 D0 S15 R7 &middot; PWM 1705/4095 &middot; Filter HP cutoff=592/2047 res=6/15 route=v1+v3 &middot; LFO Sawtooth -> Cutoff rate=10 depth=10 &middot; LFO2 off &middot; Arp Up/Down rate=30 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=1/255

Notes: source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=1/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 07 0a 09 00 06 00 02 05 00 00 06 00 05 04 00 00 01 00 0a 00 0a 00 02 00 02 01 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 01 f7
```

### 008 Saddam is Back (Lead engine)

Noise &middot; A3 D4 S2 R7 &middot; Filter LP+BP cutoff=597/2047 res=12/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=86 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 03 04 02 07 03 0b 00 0b 00 02 05 05 00 0c 00 07 03 00 00 00 00 00 00 00 00 00 00 00 05 06 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 009 Gumpa in Space (Lead engine)

Triangle+Sawtooth+Pulse [sync,ring] &middot; A1 D0 S12 R0 &middot; PWM 1719/4095 &middot; Filter LP+HP cutoff=635/2047 res=4/15 route=v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=18 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 0 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 06 01 00 0c 00 0b 07 00 06 00 02 07 0b 00 04 00 06 05 00 00 00 00 00 00 00 00 00 00 00 01 02 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 010 Apoteket (Lead engine)

(no waveform) &middot; A15 D14 S13 R3 &middot; Filter NO MODE SELECTED (silent!) cutoff=560/2047 res=12/15 route=v1+v2 &middot; LFO Triangle -> Pitch rate=15 depth=64 &middot; LFO2 Pulse -> Cutoff rate=28 depth=64 &middot; Arp Up rate=110 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; OSC1 'Disable Oscillator' flag was set in source; kept waveform bits anyway; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Pulse, rate=28, depth=64, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 00 0f 0e 0d 03 04 08 00 09 00 02 03 00 00 0c 00 03 00 00 00 00 00 0f 04 00 00 00 00 00 06 0e 00 03 00 02 00 02 01 0c 04 00 00 02 00 01 00 00 f7
```

### 011 The Alko 88 (Lead engine)

Triangle [ring] &middot; A11 D1 S5 R13 &middot; Filter LP+HP cutoff=580/2047 res=7/15 route=v3 &middot; LFO Triangle -> Cutoff rate=105 depth=73 &middot; LFO2 off &middot; Arp Up rate=96 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=14/255

Notes: source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=14/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 0b 01 05 0d 02 04 00 0d 00 02 04 04 00 07 00 04 05 00 00 00 06 09 04 09 00 02 00 00 06 00 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 0e f7
```

### 012 Dansa ditt Svin! (Lead engine)

Sawtooth &middot; A0 D0 S12 R5 &middot; Filter LP cutoff=579/2047 res=12/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=84 depth=111 &middot; LFO2 Triangle -> PWM rate=58 depth=64 &middot; Arp Up rate=30 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 0 &middot; Unison on detune=41/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=58, depth=64, -> PWM); Unison on, detune=41/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0c 05 09 0e 00 07 00 02 04 03 00 0c 00 07 01 00 00 00 05 04 06 0f 00 00 00 00 01 0e 00 01 00 00 00 00 03 0a 04 00 00 01 00 01 02 09 f7
```

### 013 Rinkeby Centrum (Lead engine)

Sawtooth+Pulse &middot; A0 D0 S6 R0 &middot; PWM 3855/4095 &middot; Filter LP cutoff=516/2047 res=14/15 route=v1+v2+v3 &middot; LFO Sawtooth -> Pitch rate=79 depth=126 &middot; LFO2 off &middot; Arp Up rate=40 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 00 00 00 06 00 00 0f 00 0f 00 02 00 04 00 0e 00 07 01 00 00 01 04 0f 07 0e 00 00 00 00 02 08 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 014 Shemale (Lead engine)

Sawtooth [ring] &middot; A2 D8 S12 R4 &middot; Filter LP+HP cutoff=603/2047 res=13/15 route=v2+v3 &middot; LFO Triangle -> PWM rate=89 depth=91 &middot; LFO2 Triangle -> Cutoff rate=91 depth=90 &middot; Arp Up rate=104 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Pos.Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=91, depth=90, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 04 02 08 0c 04 00 00 00 0a 00 02 05 0b 00 0d 00 06 05 00 00 00 05 09 05 0b 00 01 00 00 06 08 00 03 00 02 00 00 05 0b 05 0a 00 02 00 01 00 00 f7
```

### 015 TV Piraterna (Lead engine)

Noise &middot; A14 D15 S1 R7 &middot; Filter off &middot; LFO Sawtooth -> PWM rate=72 depth=64 &middot; LFO2 off &middot; Arp Up rate=70 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 0e 0f 01 07 07 0e 00 0b 00 02 07 0e 00 05 00 00 01 00 00 01 04 08 04 00 00 01 00 00 04 06 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 016 Tensta Marknad (Lead engine)

Sawtooth+Pulse [ring] &middot; A5 D0 S9 R12 &middot; PWM 328/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=94 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 04 05 00 09 0c 04 08 00 01 00 01 00 00 00 0b 00 00 01 00 00 00 03 0b 07 0e 00 00 00 00 05 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 017 Stress (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 964/4095 &middot; Filter LP+BP cutoff=1948/2047 res=12/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=16 depth=64 &middot; LFO2 off &middot; Arp Up rate=28 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 0c 04 00 03 00 07 09 0c 00 0c 00 07 03 00 00 00 01 00 04 00 00 01 00 00 01 0c 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 018 Aids Alien (Lead engine)

Noise &middot; A3 D0 S8 R0 &middot; Filter LP cutoff=537/2047 res=8/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 0 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 03 00 08 00 0c 0e 00 02 00 02 01 09 00 08 00 03 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 019 Nipple Twister (Lead engine)

Triangle [sync] &middot; A14 D2 S10 R12 &middot; Filter off &middot; LFO Triangle -> Pitch rate=57 depth=64 &middot; LFO2 off &middot; Arp Up rate=100 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=15/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=15/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 02 0e 02 0a 0c 0c 01 00 0b 00 01 00 00 00 00 00 00 01 00 00 00 03 09 04 00 00 00 00 00 06 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 0f f7
```

### 020 Old Drummer (Drum engine) - not converted

### 021 Cafe Rissne (Lead engine)

(no waveform) [sync,ring] &middot; A0 D4 S15 R11 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=112 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 06 00 04 0f 0b 03 0e 00 08 00 02 04 0c 00 05 00 00 01 00 00 00 00 00 00 00 00 00 00 00 07 00 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 022 Razorbladed (Lead engine)

Pulse &middot; A0 D0 S13 R0 &middot; PWM 0/4095 &middot; Filter LP cutoff=960/2047 res=9/15 route=v2+v3 &middot; LFO Triangle -> PWM rate=11 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0d 00 00 00 00 00 00 03 0c 00 00 09 00 06 01 00 00 00 00 0b 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 023 Mooning (Lead engine)

Triangle+Sawtooth [sync] &middot; A11 D0 S3 R7 &middot; Filter HP cutoff=540/2047 res=8/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=12 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 03 02 0b 00 03 07 04 03 00 05 00 02 01 0c 00 08 00 04 04 00 00 00 00 00 00 00 00 00 00 00 00 0c 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 024 Frogger DRM (Drum engine) - not converted

### 025 Pantyshitter (Lead engine)

Pulse [ring] &middot; A2 D8 S12 R4 &middot; PWM 2560/4095 &middot; Filter LP+HP cutoff=640/2047 res=7/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=89 depth=91 &middot; LFO2 Triangle -> Cutoff rate=91 depth=90 &middot; Arp Up rate=104 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Pos.Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=91, depth=90, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 04 02 08 0c 04 00 00 00 0a 00 02 08 00 00 07 00 07 05 00 00 00 05 09 05 0b 00 01 00 00 06 08 00 03 00 02 00 00 05 0b 05 0a 00 02 00 01 00 00 f7
```

### 026 Abucted (Lead engine)

Triangle [ring] &middot; A13 D14 S5 R11 &middot; Filter LP+HP cutoff=608/2047 res=13/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=102 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 0d 0e 05 0b 04 0d 00 07 00 02 06 00 00 0d 00 07 05 00 00 00 00 00 00 00 00 00 00 00 06 06 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 027 Radioskugga 45 (Lead engine)

Noise &middot; A0 D13 S11 R11 &middot; Filter LP cutoff=2032/2047 res=0/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=76 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 00 0d 0b 0b 09 0a 00 05 00 07 0f 00 00 00 00 01 01 00 00 00 00 00 00 00 00 00 00 00 04 0c 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 028 Flugsvamparna (Lead engine)

Pulse &middot; A11 D5 S13 R10 &middot; PWM 1806/4095 &middot; Filter BP+HP cutoff=535/2047 res=7/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=44 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 0b 05 0d 0a 00 0e 00 07 00 02 01 07 00 07 00 04 06 00 00 00 00 00 00 00 00 00 00 00 02 0c 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 029 Cannon XX (Lead engine)

Triangle+Sawtooth+Pulse &middot; A0 D4 S10 R4 &middot; PWM 537/4095 &middot; Filter LP cutoff=1181/2047 res=8/15 route=v1 &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=32 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 7 &middot; Unison on detune=3/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=3/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 00 04 0a 04 01 09 00 02 00 04 09 0d 00 08 00 01 01 00 00 00 03 0b 07 0e 00 00 00 00 02 00 00 00 00 07 00 00 00 00 00 00 00 00 00 01 00 03 f7
```

### 030 Violin in Ass (Lead engine)

Triangle+Pulse [ring] &middot; A8 D0 S12 R12 &middot; PWM 1299/4095 &middot; Filter HP cutoff=1207/2047 res=0/15 route=v3 &middot; LFO Triangle -> Pitch rate=67 depth=126 &middot; LFO2 off &middot; Arp Up rate=98 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 08 00 0c 0c 01 03 00 05 00 04 0b 07 00 00 00 04 04 00 00 00 04 03 07 0e 00 00 00 00 06 02 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 031 The Lead (Lead engine)

Pulse [sync] &middot; A4 D0 S15 R2 &middot; PWM 703/4095 &middot; Filter NO MODE SELECTED (silent!) cutoff=1311/2047 res=9/15 route=v3 &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=46 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 02 04 00 0f 02 0b 0f 00 02 00 05 01 0f 00 09 00 04 00 00 00 00 03 0b 07 0e 00 00 00 00 02 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 032 Eaten Alive (Lead engine)

Sawtooth [sync,ring] &middot; A10 D0 S5 R11 &middot; Filter BP+HP cutoff=626/2047 res=15/15 route=v2 &middot; LFO Square -> Pitch rate=54 depth=48 &middot; LFO2 off &middot; Arp Up rate=110 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 06 0a 00 05 0b 00 08 00 0f 00 02 07 02 00 0f 00 02 06 00 00 02 03 06 03 00 00 00 00 00 06 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 033 Drop your Pants (Lead engine)

Sawtooth &middot; A0 D0 S15 R0 &middot; Filter off &middot; LFO Triangle -> Pitch rate=53 depth=126 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 00 00 0f 00 06 0d 00 0a 00 06 0f 0f 00 06 00 00 00 00 00 00 03 05 07 0e 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 034 Sopnedkastet (Lead engine)

Noise &middot; A5 D11 S7 R11 &middot; Filter LP+BP+HP cutoff=383/2047 res=1/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=100 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 05 0b 07 0b 0e 0b 00 0f 00 01 07 0f 00 01 00 03 07 00 00 00 00 00 00 00 00 00 00 00 06 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 035 Dystopia (Lead engine)

Triangle+Pulse [sync] &middot; A14 D4 S10 R11 &middot; PWM 3371/4095 &middot; Filter BP+HP cutoff=638/2047 res=5/15 route=v1 &middot; LFO Sawtooth -> Pitch rate=115 depth=90 &middot; LFO2 off &middot; Arp Up rate=98 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 02 0e 04 0a 0b 02 0b 00 0d 00 02 07 0e 00 05 00 01 06 00 00 01 07 03 05 0a 00 00 00 00 06 02 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 036 2 Finger ARP (Lead engine)

Sawtooth &middot; A15 D0 S15 R3 &middot; Filter BP+HP cutoff=547/2047 res=9/15 route=v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 0f 00 0f 03 00 00 00 08 00 02 02 03 00 09 00 02 06 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 037 NESmod (Lead engine)

Triangle &middot; A5 D0 S15 R9 &middot; Filter off &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 05 00 0f 09 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 03 0b 07 0e 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 038 Ballad ARP (Lead engine)

Pulse &middot; A0 D0 S15 R8 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=16 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 08 00 00 00 08 00 06 02 0a 00 00 00 00 01 00 00 00 03 0b 07 0e 00 00 00 00 01 00 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 039 Baise (Lead engine)

Noise &middot; A0 D9 S3 R2 &middot; Filter off &middot; LFO Triangle -> Pitch rate=22 depth=126 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 00 09 03 02 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 01 06 07 0e 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 040 Slowpreggio ARP (Lead engine)

Sawtooth &middot; A0 D0 S15 R9 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=10 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 0 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 02 00 00 00 0f 09 0e 0d 00 09 00 01 03 06 00 05 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 0a 00 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 041 The Facesucker 4 (Lead engine)

Triangle &middot; A0 D15 S10 R3 &middot; Filter off &middot; LFO Triangle -> Pitch rate=98 depth=126 &middot; LFO2 off &middot; Arp Up rate=20 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 0f 0a 03 01 03 00 02 00 00 00 00 00 00 00 00 01 00 00 00 06 02 07 0e 00 00 00 00 01 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 042 Headriller 18 (Lead engine)

Noise &middot; A0 D8 S4 R2 &middot; Filter off &middot; LFO Square -> Pitch rate=27 depth=126 &middot; LFO2 off &middot; Arp Up rate=124 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 00 08 04 02 0d 03 00 07 00 06 02 07 00 09 00 00 00 00 00 02 01 0b 07 0e 00 00 00 00 07 0c 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 043 Pac Preggo ARP (Lead engine)

Triangle &middot; A0 D0 S15 R0 &middot; Filter LP cutoff=1414/2047 res=15/15 route=v1 &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=8 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO1 Sine -> Triangle (no sine on real SID); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 01 00 00 00 0f 00 00 00 00 08 00 05 08 06 00 0f 00 01 01 00 00 00 03 0b 07 0e 00 00 00 00 00 08 00 01 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 044 Ready Banana (Lead engine)

Triangle+Sawtooth+Pulse &middot; A15 D12 S0 R9 &middot; PWM 3079/4095 &middot; Filter HP cutoff=578/2047 res=2/15 route=v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 0f 0c 00 09 00 07 00 0c 00 02 04 02 00 02 00 06 04 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 045 The Loader (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=59 depth=126 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 01 00 00 00 00 00 00 01 00 00 00 03 0b 07 0e 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 046 HC Dancer (Lead engine)

Pulse &middot; A5 D0 S15 R3 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=57 depth=126 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 05 00 0f 03 00 00 00 08 00 06 02 0e 00 00 00 00 01 00 00 00 03 09 07 0e 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 047 Pulsator (Lead engine)

Sawtooth+Pulse &middot; A15 D0 S15 R9 &middot; PWM 1312/4095 &middot; Filter LP cutoff=166/2047 res=13/15 route=v1 &middot; LFO Triangle -> PWM rate=18 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 00 0f 00 0f 09 02 00 00 05 00 00 0a 06 00 0d 00 01 01 00 00 00 01 02 04 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 048 Auto ARP (Lead engine)

Pulse &middot; A0 D15 S1 R4 &middot; PWM 2560/4095 &middot; Filter LP cutoff=1598/2047 res=13/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=26 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 0f 01 04 00 00 00 0a 00 06 03 0e 00 0d 00 04 01 00 00 00 00 00 00 00 00 00 00 00 01 0a 00 02 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 049 1 Finger ARP (Lead engine)

Pulse &middot; A0 D3 S11 R4 &middot; PWM 2048/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=14 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 0b 04 00 00 00 08 00 07 0a 03 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 02 00 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 050 Show it Now! (Lead engine)

Pulse &middot; A7 D0 S13 R0 &middot; PWM 2609/4095 &middot; Filter LP cutoff=521/2047 res=10/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=22 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 07 00 0d 00 03 01 00 0a 00 02 00 09 00 0a 00 04 01 00 00 00 00 00 00 00 00 00 00 00 01 06 00 03 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 051 Melodifestival (Lead engine)

Pulse &middot; A0 D6 S0 R8 &middot; PWM 2048/4095 &middot; Filter BP cutoff=636/2047 res=10/15 route=v3 &middot; LFO Triangle -> PWM rate=86 depth=80 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=28/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=28/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 06 00 08 00 00 00 08 00 02 07 0c 00 0a 00 04 02 00 00 00 05 06 05 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 01 0c f7
```

### 052 Fartlighter 69 (Lead engine)

(no waveform) &middot; A0 D3 S13 R0 &middot; Filter LP+BP cutoff=229/2047 res=9/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; OSC1 'Disable Oscillator' flag was set in source; kept waveform bits anyway; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 00 00 03 0d 00 09 04 00 01 00 00 0e 05 00 09 00 01 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 053 Out of Gas (Lead engine)

(no waveform) [ring] &middot; A0 D4 S13 R11 &middot; Filter BP cutoff=551/2047 res=3/15 route=v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=34 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 04 00 04 0d 0b 00 05 00 06 00 02 02 07 00 03 00 02 02 00 00 00 00 00 00 00 00 00 00 00 02 02 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 054 Birdface 2 (Lead engine)

Triangle+Pulse [sync] &middot; A4 D3 S0 R11 &middot; PWM 225/4095 &middot; Filter off &middot; LFO Triangle -> Pitch rate=9 depth=64 &middot; LFO2 off &middot; Arp Up rate=60 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 02 04 03 00 0b 0e 01 00 00 00 07 0f 00 00 00 00 00 00 00 00 00 00 09 04 00 00 00 00 00 03 0c 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 055 Wormified Ass (Lead engine)

(no waveform) [sync,ring] &middot; A13 D6 S2 R8 &middot; Filter LP+BP+HP cutoff=539/2047 res=4/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=42 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=21/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; OSC1 'Disable Oscillator' flag was set in source; kept waveform bits anyway; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=21/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 06 0d 06 02 08 0d 0c 00 04 00 02 01 0b 00 04 00 01 07 00 00 00 00 00 00 00 00 00 00 00 02 0a 00 02 00 02 00 00 00 00 00 00 00 00 00 01 01 05 f7
```

### 056 Call from the E (Lead engine)

Pulse [sync] &middot; A7 D8 S10 R6 &middot; PWM 2012/4095 &middot; Filter LP+BP+HP cutoff=535/2047 res=12/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=85 depth=64 &middot; LFO2 off &middot; Arp Up rate=90 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=4/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=4/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 02 07 08 0a 06 0d 0c 00 07 00 02 01 07 00 0c 00 07 07 00 00 00 05 05 04 00 00 01 00 00 05 0a 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 04 f7
```

### 057 German Fartlover (Lead engine)

(no waveform) &middot; A0 D0 S11 R0 &middot; Filter LP+BP cutoff=169/2047 res=4/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; OSC1 'Disable Oscillator' flag was set in source; kept waveform bits anyway; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 00 00 00 0b 00 02 00 00 05 00 00 0a 09 00 04 00 04 03 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 058 He is Back! (Lead engine)

Triangle+Pulse [ring] &middot; A7 D2 S9 R7 &middot; PWM 2236/4095 &middot; Filter LP cutoff=17/2047 res=15/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=110 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 07 02 09 07 0b 0c 00 08 00 00 01 01 00 0f 00 01 01 00 00 00 00 00 00 00 00 00 00 00 06 0e 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 059 Slussen (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2112/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 08 00 02 02 03 00 0e 00 00 04 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 060 Intel Inside (Lead engine)

Triangle &middot; A15 D0 S11 R7 &middot; Filter BP+HP cutoff=578/2047 res=9/15 route=v3 &middot; LFO Triangle -> Cutoff rate=32 depth=78 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO2 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 0f 00 0b 07 00 00 00 08 00 02 04 02 00 09 00 04 06 00 00 00 02 00 04 0e 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 061 Roaches (Lead engine)

Noise &middot; A0 D2 S0 R0 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 37 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 00 02 00 00 04 00 00 08 00 07 0f 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 02 05 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 062 Sopbil by Night (Lead engine)

Sawtooth [ring] &middot; A10 D13 S0 R6 &middot; Filter LP+BP cutoff=806/2047 res=7/15 route=v1+v3 &middot; LFO Sample&Hold -> Cutoff rate=70 depth=65 &middot; LFO2 off &middot; Arp Up rate=4 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 04 0a 0d 00 06 07 0a 00 01 00 03 02 06 00 07 00 05 03 00 00 03 04 06 04 01 00 02 00 00 00 04 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 063 UK Handjob 7 (Lead engine)

Triangle+Pulse [ring] &middot; A0 D11 S4 R6 &middot; PWM 2349/4095 &middot; Filter LP+HP cutoff=637/2047 res=0/15 route=v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=6 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=2/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 00 0b 04 06 02 0d 00 09 00 02 07 0d 00 00 00 06 05 00 00 00 00 00 00 00 00 00 00 00 00 06 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 064 Kackelackornass (Lead engine)

(no waveform) [ring] &middot; A0 D3 S0 R10 &middot; Filter LP cutoff=675/2047 res=15/15 route=v1+v2+v3 &middot; LFO Square -> Cutoff rate=90 depth=64 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; OSC1 'Disable Oscillator' flag was set in source; kept waveform bits anyway; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 04 00 03 00 0a 07 0a 00 04 00 02 0a 03 00 0f 00 07 01 00 00 02 05 0a 04 00 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 065 Myror i Brallan (Lead engine)

Sawtooth [ring] &middot; A1 D13 S13 R5 &middot; Filter HP cutoff=584/2047 res=11/15 route=v2+v3 &middot; LFO Triangle -> Cutoff rate=0 depth=64 &middot; LFO2 off &middot; Arp Up rate=108 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO4 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 04 01 0d 0d 05 0e 04 00 02 00 02 04 08 00 0b 00 06 04 00 00 00 00 00 04 00 00 02 00 00 06 0c 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 066 Leathergayed 5 (Lead engine)

Triangle &middot; A0 D15 S10 R11 &middot; Filter BP cutoff=2047/2047 res=2/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=38 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 0f 0a 0b 08 02 00 07 00 07 0f 0f 00 02 00 04 02 00 00 00 00 00 00 00 00 00 00 00 02 06 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 067 C-Plus 4 (Lead engine)

Sawtooth &middot; A2 D12 S9 R4 &middot; Filter LP+BP cutoff=1421/2047 res=6/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=82 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=12/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=12/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 00 02 0c 09 04 00 00 00 0f 00 05 08 0d 00 06 00 01 03 00 00 00 00 00 00 00 00 00 00 00 05 02 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 0c f7
```

### 068 Arcade Lover (Lead engine)

Sawtooth [sync] &middot; A0 D0 S15 R0 &middot; Filter LP cutoff=148/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=55 depth=95 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=33/255

Notes: LFO4 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=33/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 02 00 00 0f 00 00 00 00 08 00 00 09 04 00 0f 00 07 01 00 00 00 03 07 05 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 02 01 f7
```

### 069 Diarre Lover (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 2048/4095 &middot; Filter LP cutoff=1758/2047 res=6/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 00 00 00 08 00 06 0d 0e 00 06 00 07 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 070 Silent Urbex (Lead engine)

Triangle &middot; A0 D7 S15 R6 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 07 0f 06 00 00 00 08 00 01 0c 07 00 0f 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 071 The Cave Rapist (Lead engine)

Triangle [sync] &middot; A5 D13 S9 R11 &middot; Filter BP cutoff=585/2047 res=15/15 route=v2 &middot; LFO Square -> Pitch rate=75 depth=62 &middot; LFO2 off &middot; Arp Up rate=66 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 02 05 0d 09 0b 05 09 00 01 00 02 04 09 00 0f 00 02 02 00 00 02 04 0b 03 0e 00 00 00 00 04 02 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 072 TDK Drummer (Drum engine) - not converted

### 073 Sergels Torg (Bassline engine)

Pulse &middot; A0 D3 S2 R1 &middot; PWM 2304/4095 &middot; Filter LP cutoff=4/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 02 01 00 00 00 09 00 00 00 04 00 0f 00 07 01 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 074 Kungsholmen (Bassline engine)

Sawtooth [sync,ring] &middot; A0 D3 S2 R1 &middot; Filter LP cutoff=4/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=12 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 06 00 03 02 01 06 0e 00 0e 00 00 00 04 00 0f 00 07 01 00 00 00 00 03 07 0f 00 00 00 00 00 0c 00 03 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 075 Ta ditt Hasch! (Bassline engine)

Pulse &middot; A0 D3 S2 R1 &middot; PWM 2304/4095 &middot; Filter LP cutoff=4/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=3 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 03 02 01 00 00 00 09 00 00 00 04 00 0f 00 07 01 00 00 00 00 03 07 0f 00 00 00 00 03 0e 00 00 00 02 00 00 00 0b 07 0f 00 00 00 01 00 00 f7
```

### 076 Gul Fanta (Lead engine)

Triangle+Sawtooth+Pulse &middot; A0 D15 S0 R1 &middot; PWM 1905/4095 &middot; Filter LP+BP cutoff=1075/2047 res=12/15 route=v3 &middot; LFO Sawtooth -> PWM rate=65 depth=108 &middot; LFO2 off &middot; Arp Up rate=84 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=14/255

Notes: source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=14/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 00 0f 00 01 07 01 00 07 00 04 03 03 00 0c 00 04 03 00 00 01 04 01 06 0c 00 01 00 00 05 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 0e f7
```

### 077 Laserdome Lover (Lead engine)

Pulse &middot; A0 D0 S15 R6 &middot; PWM 791/4095 &middot; Filter LP+BP cutoff=797/2047 res=12/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=24 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 06 01 07 00 03 00 03 01 0d 00 0c 00 01 03 00 00 00 00 00 00 00 00 00 00 02 01 08 00 03 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 078 8-bit Starpig (Lead engine)

Triangle &middot; A0 D0 S15 R8 &middot; Filter LP+BP cutoff=651/2047 res=15/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Down rate=6 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 01 00 00 00 0f 08 09 0f 00 04 00 02 08 0b 00 0f 00 01 03 00 00 00 00 00 00 00 00 00 00 01 00 06 00 01 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 079 Moped to Work (Lead engine)

Triangle+Pulse [sync] &middot; A3 D3 S5 R4 &middot; PWM 475/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=126 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=21/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=21/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 02 03 03 05 04 0d 0b 00 01 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 07 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 01 05 f7
```

### 080 Smaka Bajset! (Lead engine)

Pulse &middot; A4 D14 S10 R7 &middot; PWM 1107/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=16/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=16/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 04 0e 0a 07 05 03 00 04 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 01 00 02 00 00 00 00 00 00 00 00 00 01 01 00 f7
```

### 081 Akayism (Lead engine)

Pulse [sync,ring] &middot; A15 D15 S6 R3 &middot; PWM 2121/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=112 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=1/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=1/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 06 0f 0f 06 03 04 09 00 08 00 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 07 00 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 01 f7
```

### 082 Daytime Raver (Lead engine)

Triangle [ring] &middot; A1 D6 S5 R8 &middot; Filter LP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO Sample&Hold -> PWM rate=99 depth=93 &middot; LFO2 Sawtooth -> Cutoff rate=107 depth=34 &middot; Arp Up rate=100 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Sawtooth, rate=107, depth=34, -> Cutoff); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 04 01 06 05 08 08 01 00 01 00 00 00 00 00 0f 00 07 01 00 00 03 06 03 05 0d 00 01 00 00 06 04 00 03 00 02 00 01 06 0b 02 02 00 02 00 01 00 00 f7
```

### 083 Bodymelter (Lead engine)

Sawtooth [ring] &middot; A6 D11 S15 R9 &middot; Filter LP+BP cutoff=0/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=70 depth=92 &middot; LFO2 off &middot; Arp Up rate=122 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 04 06 0b 0f 09 09 0e 00 03 00 00 00 00 00 0f 00 07 03 00 00 00 04 06 05 0c 00 02 00 00 07 0a 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 084 Helsinki Fight (Bassline engine)

Sawtooth [ring] &middot; A0 D2 S0 R1 &middot; Filter LP+BP+HP cutoff=1558/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=52 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 04 00 02 00 01 03 04 00 0d 00 06 01 06 00 0f 00 07 07 00 00 02 05 04 00 00 00 00 00 00 03 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 085 The UE King (Bassline engine)

Triangle [ring] &middot; A0 D3 S0 R1 &middot; Filter LP+BP cutoff=8/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> Pitch rate=55 depth=127 &middot; LFO2 Triangle -> Pitch rate=11 depth=127 &middot; Arp Up rate=60 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=11, depth=127, -> Pitch); Unison off (source uses only OSC1).

```
f0 50 20 10 01 04 00 03 00 01 06 08 00 0d 00 00 00 08 00 0f 00 07 03 00 00 00 03 07 07 0f 00 00 00 00 03 0c 00 03 00 02 00 00 00 0b 07 0f 00 00 00 00 00 00 f7
```

### 086 Lords of Brown (Lead engine)

Triangle+Pulse &middot; A1 D6 S10 R15 &middot; PWM 564/4095 &middot; Filter LP+BP+HP cutoff=736/2047 res=15/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=122 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 00 01 06 0a 0f 03 04 00 02 00 02 0e 00 00 0f 00 07 07 00 00 00 00 00 00 00 00 00 00 00 07 0a 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 087 Kirk in Prag (Lead engine)

Triangle+Pulse [sync,ring] &middot; A0 D15 S9 R7 &middot; PWM 666/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=66 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 06 00 0f 09 07 09 0a 00 02 00 01 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 04 02 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 088 Gotland (Lead engine)

Pulse [sync,ring] &middot; A13 D1 S6 R3 &middot; PWM 2533/4095 &middot; Filter BP cutoff=0/2047 res=7/15 route=v2 &middot; LFO Sample&Hold -> Cutoff rate=127 depth=113 &middot; LFO2 off &middot; Arp Up rate=120 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 06 0d 01 06 03 0e 05 00 09 00 00 00 00 00 07 00 02 02 00 00 03 07 0f 07 01 00 02 00 00 07 08 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 089 Jordstrom Inside (Lead engine)

Noise &middot; A15 D12 S1 R0 &middot; Filter LP cutoff=1547/2047 res=3/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=68 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 00 0f 0c 01 00 05 0c 00 01 00 06 00 0b 00 03 00 03 01 00 00 00 00 00 00 00 00 00 00 00 04 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 090 Forgotten Arpz (Lead engine)

Pulse &middot; A1 D0 S15 R12 &middot; PWM 3808/4095 &middot; Filter LP+BP cutoff=401/2047 res=9/15 route=v1 &middot; LFO Square -> PWM rate=72 depth=72 &middot; LFO2 off &middot; Arp Up rate=16 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 4 &middot; Unison off

Notes: source modulation actually comes from LFO5, not LFO1 (this firmware has only one LFO per channel); source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 01 00 0f 0c 0e 00 00 0e 00 01 09 01 00 09 00 01 03 00 00 02 04 08 04 08 00 01 00 00 01 00 00 03 00 04 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 091 Buttpirate 89 (Lead engine)

Sawtooth [ring] &middot; A7 D10 S11 R6 &middot; Filter off &middot; LFO Triangle -> Cutoff rate=80 depth=100 &middot; LFO2 off &middot; Arp Up rate=38 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=17/255

Notes: LFO5 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO5, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=17/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 02 04 07 0a 0b 06 03 01 00 0e 00 04 00 00 00 00 00 00 01 00 00 00 05 00 06 04 00 02 00 00 02 06 00 01 00 02 00 00 00 00 00 00 00 00 00 01 01 01 f7
```

### 092 Broken Ham (Lead engine)

Pulse &middot; A0 D0 S15 R8 &middot; PWM 3231/4095 &middot; Filter LP+BP+HP cutoff=407/2047 res=14/15 route=v1 &middot; LFO Triangle -> PWM rate=6 depth=51 &middot; LFO2 off &middot; Arp Up rate=18 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: LFO4 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel). New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 08 09 0f 00 0c 00 01 09 07 00 0e 00 01 07 00 00 00 00 06 03 03 00 01 00 00 01 02 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 093 Powerup ARP (Lead engine)

Pulse &middot; A0 D0 S15 R0 &middot; PWM 1344/4095 &middot; Filter LP cutoff=336/2047 res=15/15 route=v1+v2+v3 &middot; LFO Triangle -> PWM rate=96 depth=96 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=3/255

Notes: LFO6 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO6, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=3/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 00 04 00 00 05 00 01 05 00 00 0f 00 07 01 00 00 00 06 00 06 00 00 01 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 03 f7
```

### 094 The Future ARP (Lead engine)

Pulse &middot; A0 D0 S15 R8 &middot; PWM 1815/4095 &middot; Filter LP+BP cutoff=528/2047 res=15/15 route=v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=50 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 08 01 07 00 07 00 02 01 00 00 0f 00 02 03 00 00 00 00 00 00 00 00 00 00 02 03 02 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 095 Time to Die! (Lead engine)

Triangle+Sawtooth &middot; A0 D11 S4 R10 &middot; Filter BP+HP cutoff=614/2047 res=11/15 route=v2 &middot; LFO Triangle -> Cutoff rate=35 depth=123 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=11/255

Notes: LFO4 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO4, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=11/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 03 00 00 0b 04 0a 06 0c 00 05 00 02 06 06 00 0b 00 02 06 00 00 00 02 03 07 0b 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 0b f7
```

### 096 Mustachen Rakad (Lead engine)

Pulse &middot; A0 D0 S15 R8 &middot; PWM 1312/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=2/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=2/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 00 00 0f 08 02 00 00 05 00 07 0f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 02 f7
```

### 097 Carrot Whip DR (Drum engine) - not converted

### 098 The Guy (Lead engine)

Triangle+Sawtooth [sync] &middot; A0 D13 S9 R2 &middot; Filter off &middot; LFO Triangle -> PWM rate=11 depth=64 &middot; LFO2 Triangle -> Pitch rate=70 depth=64 &middot; Arp Up rate=52 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=70, depth=64, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 03 02 00 0d 09 02 05 04 00 0d 00 04 00 00 00 00 00 00 00 00 00 00 00 0b 04 00 00 01 00 00 03 04 00 03 00 02 00 00 04 06 04 00 00 00 00 01 00 00 f7
```

### 099 Cleaning Carpets (Lead engine)

Pulse [sync] &middot; A0 D0 S15 R10 &middot; PWM 2048/4095 &middot; Filter LP+BP+HP cutoff=611/2047 res=11/15 route=v1 &middot; LFO Triangle -> Cutoff rate=54 depth=14 &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=60/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=60/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 02 00 00 0f 0a 00 00 00 08 00 02 06 03 00 0b 00 01 07 00 00 00 03 06 00 0e 00 02 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 03 0c f7
```

### 100 The Sucker (Lead engine)

Triangle [sync,ring] &middot; A12 D13 S5 R10 &middot; Filter off &middot; LFO Triangle -> PWM rate=80 depth=84 &middot; LFO2 Triangle -> Pitch rate=64 depth=14 &middot; Arp Up rate=32 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=1/255

Notes: LFO5 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO5, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=64, depth=14, -> Pitch); Unison on, detune=1/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 06 0c 0d 05 0a 01 09 00 08 00 04 00 00 00 00 00 00 00 00 00 00 05 00 05 04 00 01 00 00 02 00 00 03 00 02 00 00 04 00 00 0e 00 00 00 01 00 01 f7
```

### 101 Proud Mary (Lead engine)

Sawtooth+Pulse [sync] &middot; A8 D9 S15 R5 &middot; PWM 780/4095 &middot; Filter LP+BP cutoff=1246/2047 res=13/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=107 depth=9 &middot; LFO2 off &middot; Arp Up rate=32 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=14/255

Notes: source modulation actually comes from LFO2, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=14/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 02 08 09 0f 05 00 0c 00 03 00 04 0d 0e 00 0d 00 07 03 00 00 00 06 0b 00 09 00 02 00 00 02 00 00 01 00 02 00 00 00 00 00 00 00 00 00 01 00 0e f7
```

### 102 Diarre Lunch (Lead engine)

Triangle+Pulse [ring] &middot; A14 D10 S11 R11 &middot; PWM 1080/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=114 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=14/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=14/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 0e 0a 0b 0b 03 08 00 04 00 04 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 07 02 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 0e f7
```

### 103 Nine inch Sails (Lead engine)

Triangle+Pulse [ring] &middot; A1 D15 S6 R0 &middot; PWM 2385/4095 &middot; Filter LP+BP+HP cutoff=585/2047 res=12/15 route=v1 &middot; LFO Sawtooth -> PWM rate=44 depth=67 &middot; LFO2 off &middot; Arp Up rate=112 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 01 0f 06 00 05 01 00 09 00 02 04 09 00 0c 00 01 07 00 00 01 02 0c 04 03 00 01 00 00 07 00 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 104 Tranny Supprice (Lead engine)

Triangle &middot; A0 D0 S15 R8 &middot; Filter LP+BP cutoff=1269/2047 res=11/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=10 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 00 00 0f 08 00 00 00 08 00 04 0f 05 00 0b 00 01 03 00 00 00 00 00 00 00 00 00 00 00 00 0a 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 105 Inside (Lead engine)

Triangle+Pulse [ring] &middot; A9 D14 S0 R5 &middot; PWM 1620/4095 &middot; Filter LP cutoff=573/2047 res=10/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=4 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 09 0e 00 05 05 04 00 06 00 02 03 0d 00 0a 00 03 01 00 00 00 00 00 00 00 00 00 00 00 00 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 106 Arpeggio 123 (Lead engine)

Pulse &middot; A0 D0 S15 R4 &middot; PWM 1500/4095 &middot; Filter LP+BP cutoff=446/2047 res=12/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=8 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 24 &middot; Unison off

Notes: source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 00 0f 04 0d 0c 00 05 00 01 0b 0e 00 0c 00 01 03 00 00 00 00 00 00 00 00 00 00 00 00 08 00 02 01 08 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 107 Nosetransplant 3 (Lead engine)

Pulse [ring] &middot; A5 D5 S14 R9 &middot; PWM 3981/4095 &middot; Filter LP+BP cutoff=543/2047 res=5/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=104 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=7/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=7/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 04 05 05 0e 09 08 0d 00 0f 00 02 01 0f 00 05 00 04 03 00 00 00 00 00 00 00 00 00 00 00 06 08 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 07 f7
```

### 108 Arpeggio 321 (Lead engine)

Sawtooth+Pulse &middot; A0 D0 S15 R0 &middot; PWM 1156/4095 &middot; Filter LP+BP cutoff=1087/2047 res=11/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=32 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 06 00 00 00 0f 00 08 04 00 04 00 04 03 0f 00 0b 00 07 03 00 00 00 00 00 00 00 00 00 00 02 02 00 00 01 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 109 Dansband (Lead engine)

Sawtooth+Pulse &middot; A0 D0 S15 R0 &middot; PWM 1080/4095 &middot; Filter BP cutoff=553/2047 res=12/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=20 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 00 00 00 0f 00 03 08 00 04 00 02 02 09 00 0c 00 04 02 00 00 00 00 00 00 00 00 00 00 00 01 04 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 110 Das Proteindrink (Lead engine)

Triangle &middot; A6 D14 S7 R7 &middot; Filter LP+BP+HP cutoff=541/2047 res=12/15 route=v1+v2 &middot; LFO Triangle -> Cutoff rate=14 depth=64 &middot; LFO2 off &middot; Arp Up rate=94 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO6 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO6, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 06 0e 07 07 0d 04 00 09 00 02 01 0d 00 0c 00 03 07 00 00 00 00 0e 04 00 00 02 00 00 05 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 111 Electro Poop ARP (Lead engine)

Pulse &middot; A0 D7 S5 R0 &middot; PWM 2622/4095 &middot; Filter LP+BP cutoff=1029/2047 res=14/15 route=v1 &middot; LFO off &middot; LFO2 off &middot; Arp Up/Down rate=18 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison off

Notes: New in v2: LFO2 off (no second live-routed LFO found in the source); Unison off (source uses only OSC1).

```
f0 50 20 10 04 00 00 07 05 00 03 0e 00 0a 00 04 00 05 00 0e 00 01 03 00 00 00 00 00 00 00 00 00 00 02 01 02 00 03 00 02 00 00 00 00 00 00 00 00 00 00 00 00 f7
```

### 112 Dancing Arpigs 4 (Multi engine) - not converted

### 113 Auto Zone (Lead engine)

Triangle+Sawtooth [sync,ring] &middot; A0 D4 S3 R11 &middot; Filter LP+BP cutoff=292/2047 res=8/15 route=v3 &middot; LFO Triangle -> Cutoff rate=1 depth=64 &middot; LFO2 off &middot; Arp Up rate=80 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: LFO1 Sine -> Triangle (no sine on real SID); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 03 06 00 04 03 0b 05 0f 00 09 00 01 02 04 00 08 00 04 03 00 00 00 00 01 04 00 00 02 00 00 05 00 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 114 Birdbath (Bassline engine)

Triangle+Sawtooth+Pulse [sync] &middot; A5 D7 S13 R12 &middot; PWM 2877/4095 &middot; Filter BP+HP cutoff=636/2047 res=9/15 route=v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=84 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 02 05 07 0d 0c 03 0d 00 0b 00 02 07 0c 00 09 00 02 06 00 00 03 07 0e 00 00 00 00 00 00 05 04 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 115 The Skinflute (Lead engine)

Pulse [sync,ring] &middot; A1 D1 S15 R1 &middot; PWM 3151/4095 &middot; Filter BP+HP cutoff=1722/2047 res=1/15 route=v1+v3 &middot; LFO Square -> Cutoff rate=58 depth=35 &middot; LFO2 off &middot; Arp Up rate=32 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 06 01 01 0f 01 04 0f 00 0c 00 06 0b 0a 00 01 00 05 06 00 00 02 03 0a 02 03 00 02 00 00 02 00 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 116 Boner Delux (Lead engine)

Sawtooth+Pulse [ring] &middot; A12 D10 S13 R12 &middot; PWM 2317/4095 &middot; Filter LP+HP cutoff=558/2047 res=12/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=4 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 06 04 0c 0a 0d 0c 00 0d 00 09 00 02 02 0e 00 0c 00 07 05 00 00 00 00 00 00 00 00 00 00 00 00 04 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 117 Zombiefied (Lead engine)

Pulse [sync] &middot; A9 D3 S10 R8 &middot; PWM 3408/4095 &middot; Filter off &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=32 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 02 09 03 0a 08 05 00 00 0d 00 00 0e 00 00 0f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02 00 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 118 Eat Your Supper (Lead engine)

Pulse [ring] &middot; A5 D1 S11 R3 &middot; PWM 2575/4095 &middot; Filter LP cutoff=247/2047 res=7/15 route=v1+v2+v3 &middot; LFO Triangle -> Cutoff rate=118 depth=83 &middot; LFO2 off &middot; Arp Up rate=88 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=17/255

Notes: LFO3 Sine -> Triangle (no sine on real SID); source modulation actually comes from LFO3, not LFO1 (this firmware has only one LFO per channel); Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=17/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 04 05 01 0b 03 00 0f 00 0a 00 00 0f 07 00 07 00 07 01 00 00 00 07 06 05 03 00 02 00 00 05 08 00 03 00 02 00 00 00 00 00 00 00 00 00 01 01 01 f7
```

### 119 Arise 242 (Lead engine)

Triangle+Sawtooth+Pulse &middot; A13 D14 S11 R12 &middot; PWM 3417/4095 &middot; Filter BP+HP cutoff=638/2047 res=9/15 route=v1+v2 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=96 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 0d 0e 0b 0c 05 09 00 0d 00 02 07 0e 00 09 00 03 06 00 00 00 00 00 00 00 00 00 00 00 06 00 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 120 Allo A110 (Lead engine)

Triangle+Sawtooth [sync,ring] &middot; A6 D3 S8 R11 &middot; Filter LP+BP cutoff=561/2047 res=15/15 route=v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=90 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 03 06 06 03 08 0b 0b 05 00 03 00 02 03 01 00 0f 00 06 03 00 00 00 00 00 00 00 00 00 00 00 05 0a 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 121 Burning Meat (Lead engine)

Noise [sync,ring] &middot; A2 D8 S5 R5 &middot; Filter LP+BP+HP cutoff=536/2047 res=5/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=22 range=3oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 08 06 02 08 05 05 07 02 00 04 00 02 01 08 00 05 00 04 07 00 00 00 00 00 00 00 00 00 00 00 01 06 00 02 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 122 Ballsmash Bass (Lead engine)

(no waveform) [sync,ring] &middot; A0 D9 S6 R8 &middot; Filter off &middot; LFO Square -> Pitch rate=7 depth=64 &middot; LFO2 off &middot; Arp Up rate=94 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 00 06 00 09 06 08 01 0e 00 09 00 02 0a 0c 00 0f 00 00 00 00 00 02 00 07 04 00 00 00 00 00 05 0e 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 123 USSR (Lead engine)

Triangle+Sawtooth+Pulse &middot; A13 D9 S10 R15 &middot; PWM 1402/4095 &middot; Filter BP cutoff=578/2047 res=8/15 route=v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=42 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; source uses OSC2/OSC3 too (multi-osc); only OSC1's timbre captured here. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 0d 09 0a 0f 07 0a 00 05 00 02 04 02 00 08 00 04 02 00 00 00 00 00 00 00 00 00 00 00 02 0a 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 124 Trip to Beckis (Bassline engine)

Triangle+Sawtooth+Pulse &middot; A4 D4 S5 R10 &middot; PWM 400/4095 &middot; Filter LP+BP cutoff=547/2047 res=5/15 route=v1+v3 &middot; LFO Sawtooth -> PWM rate=118 depth=74 &middot; LFO2 Pulse -> Pitch rate=20 depth=103 &middot; Arp Up rate=54 range=2oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Pulse, rate=20, depth=103, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 04 04 05 0a 09 00 00 01 00 02 02 03 00 05 00 05 03 00 00 01 07 06 04 0a 00 01 00 00 03 06 00 01 00 02 00 02 01 04 06 07 00 00 00 01 00 00 f7
```

### 125 ChiPTR4XXX (Bassline engine)

Triangle+Pulse [ring] &middot; A11 D8 S3 R14 &middot; PWM 383/4095 &middot; Filter HP cutoff=628/2047 res=3/15 route=v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=12 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 05 04 0b 08 03 0e 07 0f 00 01 00 02 07 04 00 03 00 06 04 00 00 00 07 0b 00 00 00 01 00 00 00 0c 00 03 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 126 Acid Vibrator (Bassline engine)

Pulse &middot; A7 D12 S1 R13 &middot; PWM 2304/4095 &middot; Filter LP cutoff=466/2047 res=5/15 route=v1+v2+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 04 00 07 0c 01 0d 00 00 00 09 00 01 0d 02 00 05 00 07 01 00 00 00 04 03 00 00 00 00 00 00 03 0e 00 00 00 02 00 00 00 00 00 00 00 00 00 01 00 00 f7
```

### 127 Washing Brains (Bassline engine)

Triangle &middot; A3 D1 S0 R15 &middot; Filter LP cutoff=606/2047 res=1/15 route=v2+v3 &middot; LFO Sawtooth -> Pitch rate=113 depth=58 &middot; LFO2 Triangle -> Pitch rate=8 depth=74 &middot; Arp Up rate=54 range=4oct (needs CC_ARPE on to hear) &middot; Bend range 2 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 sourced from a second live-routed MBSID LFO (Triangle, rate=8, depth=74, -> Pitch); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 01 00 03 01 00 0f 04 08 00 0a 00 02 05 0e 00 01 00 06 01 00 00 01 07 01 03 0a 00 00 00 00 03 06 00 03 00 02 00 00 00 08 04 0a 00 00 00 01 00 00 f7
```

### 128 Bajamaja Raver (Bassline engine)

Triangle+Sawtooth+Pulse &middot; A15 D11 S7 R2 &middot; PWM 1720/4095 &middot; Filter LP cutoff=639/2047 res=15/15 route=v1+v3 &middot; LFO off &middot; LFO2 off &middot; Arp Up rate=62 range=1oct (needs CC_ARPE on to hear) &middot; Bend range 7 &middot; Unison on detune=0/255

Notes: Arp defined but disabled in the source patch; CC_ARPE off by default; Bassline engine's envelope is separate from OSC1's voice ADSR; using the engine envelope (scaled 8bit->4bit), not v1's own ADSR bytes. New in v2: LFO2 off (no second live-routed LFO found in the source); Unison on, detune=0/255 (from the source's OSC2/OSC3 detune spread, now usable as 3-voice unison instead of being discarded).

```
f0 50 20 10 07 00 0f 0b 07 02 0b 08 00 06 00 02 07 0f 00 0f 00 05 01 00 00 00 03 0d 00 00 00 00 00 00 03 0e 00 00 00 07 00 00 00 00 00 00 00 00 00 01 00 00 f7
```
