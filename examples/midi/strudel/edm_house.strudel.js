// USBSID-Pico Strudel demo 2/2: "Basement Loop" - Am-F-C-G house/EDM groove
// 3 SIDs, 3 channels, 3 factory patches (0-15). Same channel-mapping and
// boot-volume notes as pop_classical.strudel.js - see that file.
//
// Patch choices (repo/src/midi_patch.c factory bank):
//   14 Wide bass      - pulse ~65% duty, punchy, no filter to fight the stabs
//   6  Filtered lead   - sawtooth through a resonant low-pass, classic stab tone
//   2  Pulse lead      - ~50% duty pulse, crisp and cuts through for the arp

let init = register('init', (pat) => pat.filterWhen((begin) => begin<1))

// CC_ANOF (All Notes Off, CC 123) is pool-wide, not channel-scoped - one
// send silences every held SID voice AND every held FMOpl voice
// (all_notes_off() in midi_handler.c calls midi_fmopl_all_notes_off()
// internally). Sent first so a note stuck from earlier testing/a previous
// song doesn't keep ringing under this one. Channel byte is required by the
// MIDI message shape but doesn't matter for this particular CC.
$: ccn(123).ccv(1).midi('Midi Through Port-0').midichan(1).init()  // All Notes Off (SID + FMOpl)

// CC_FMEN (CC 88) is sticky per-channel firmware state - if a channel was
// ever set to target the FMOpl chip (manual CC testing, or one of the
// FMOpl demo songs), it stays that way until told otherwise. Reset
// channels 1/2/3 to SID here rather than assuming the board's current state.
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // Reset channel 1 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(2).init()  // Reset channel 2 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(3).init()  // Reset channel 3 to SID (CC_FMEN off)

$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(1).progNum(14).init()  // SID1: Wide bass
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(2).progNum(6).init()   // SID2: Filtered lead (stabs)
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(3).progNum(2).init()   // SID3: Pulse lead (arp)

// setcps(0.9) // uncomment and tune to taste - faster suits this one

// Bass: pumping eighth-notes, root follows the progression one bar at a time
$: note("<[a1*8] [f1*8] [c2*8] [g1*8]>")
   .midi('Midi Through Port-0').midichan(1)

// Stabs: off-beat chord hits, classic house "and-of-2/and-of-4" feel
$: chord("<Am F C G>").voicing()
   .struct("~ x ~ x ~ x ~ x")
   .midi('Midi Through Port-0').midichan(2)

// Lead: rolling 16th-note arpeggio through each chord's scale
$: n("<[0 2 4 7]*4>")
   .scale("<A3:minor F3:major C4:major G3:major>")
   .midi('Midi Through Port-0').midichan(3)

// To stop: hush()
