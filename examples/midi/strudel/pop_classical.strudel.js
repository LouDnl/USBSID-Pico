// USBSID-Pico Strudel demo 1/2: "Sunday Drive" - I-V-vi-IV pop/classical loop
// 3 SIDs, 3 channels, 3 factory patches (0-15), no config tool needed.
//
// Channel map: this firmware's stock default gives channel 1 all 12 pool
// slots (every SID, full polyphony) and channels 2/3/4 one SID each - but
// this board's config has been changed from that default, so here channels
// 1/2/3 map one-SID-each instead. Check your own board's channel config
// before assuming either mapping.
//
// SIDs reset to silent registers on boot/reset and expect an explicit
// volume CC before anything is audible - that's what the ccn(7).ccv(1)
// lines below are for, sent once per channel before the tune starts.
//
// Patch choices (repo/src/midi_patch.c factory bank):
//   3  Bass            - sawtooth, punchy decay, already low-passed
//   5  Organ           - triangle, slight attack, full sustain (good pad)
//   1  Sawtooth lead    - plain, clear, good for a singable melody line

let init = register('init', (pat) => pat.filterWhen((begin) => begin<1))

// CC_ANOF (All Notes Off, CC 123) is pool-wide, not channel-scoped - one
// send silences every held SID voice AND every held FMOpl voice
// (all_notes_off() in midi_handler.c calls midi_fmopl_all_notes_off()
// internally). Sent first so a note stuck from earlier testing/a previous
// song doesn't keep ringing under this one. Channel byte is required by the
// MIDI message shape but doesn't matter for this particular CC.
$: ccn(123).ccv(1).midi('USBSID-Pico').midichan(1).init()  // All Notes Off (SID + FMOpl)

// CC_FMEN (CC 88) is sticky per-channel firmware state - if a channel was
// ever set to target the FMOpl chip (manual CC testing, or one of the
// FMOpl demo songs), it stays that way until told otherwise. Reset
// channels 1/2/3 to SID here rather than assuming the board's current state.
$: ccn(88).ccv(0).midi('USBSID-Pico').midichan(1).init()  // Reset channel 1 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('USBSID-Pico').midichan(2).init()  // Reset channel 2 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('USBSID-Pico').midichan(3).init()  // Reset channel 3 to SID (CC_FMEN off)

$: ccn(7).ccv(1).midi('USBSID-Pico').midichan(1).progNum(3).init()  // SID1: Bass
$: ccn(7).ccv(1).midi('USBSID-Pico').midichan(2).progNum(5).init()  // SID2: Organ pad
$: ccn(7).ccv(1).midi('USBSID-Pico').midichan(3).progNum(1).init()  // SID3: Sawtooth lead

// setcps(0.5) // uncomment and tune to taste - slower suits this one

// Bass: root note under each chord, one per cycle
$: note("<c2 g1 a1 f1>")
   .midi('USBSID-Pico').midichan(1)

// Chords: sustained triad under the melody
$: chord("<C G Am F>").voicing()
   .midi('USBSID-Pico').midichan(2)

// Melody: four-note arpeggio per chord, common-tone voice leading between
// chords (c4 carries over Am->F, e4 carries over C->Am) so the line moves
// smoothly instead of jumping around
$: note("<[e4 g4 c5 e5] [d4 g4 b4 d5] [c4 e4 a4 c5] [c4 f4 a4 c5]>")
   .midi('USBSID-Pico').midichan(3)

// To stop: hush()
