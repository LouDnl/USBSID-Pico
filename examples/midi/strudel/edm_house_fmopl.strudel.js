// USBSID-Pico Strudel demo 3/4: "Basement Loop II" - Dm-Bb-F-C house/EDM groove
// 4 parts: 3 real SIDs (bass/stabs/arp) + 1 FMOpl chip (lead hook).
//
// Channel map for THIS user's board (2xSID physical config: socket 1 hosts
// a clone presenting as 2x8580, socket 2 hosts a clone presenting as
// 1x8580 + 1xFMOpl - 3 real SID slots + 1 FMOpl slot total, cfg.numsids=4):
// channels 1/2/3 = SID1/2/3 one-SID-each (this board's custom config, not
// firmware stock default), channel 4 = the FMOpl slot. If your own board's
// channel mapping differs, adjust the midichan() calls below.
//
// FMOpl needs one extra step SID channels don't: CC_FMEN (CC 88) must be
// sent on channel 4 BEFORE Program Change or notes reach the OPL chip at
// all - a channel targets a SID by default, targeting FMOpl is opt-in per
// channel (repo/src/midi_fmopl.h). Unlike SID's boot-silent MODVOL, FMOpl
// channels default to volume 127 already (midi_fmopl_init()), so no
// mandatory volume-first step there - CC7 below is sent anyway for parity.

let init = register('init', (pat) => pat.filterWhen((begin) => begin<1))

// CC_ANOF (All Notes Off, CC 123) is pool-wide, not channel-scoped - one
// send silences every held SID voice AND every held FMOpl voice
// (all_notes_off() in midi_handler.c calls midi_fmopl_all_notes_off()
// internally). Sent first so a note stuck from earlier testing/a previous
// song doesn't keep ringing under this one. Channel byte is required by the
// MIDI message shape but doesn't matter for this particular CC.
$: ccn(123).ccv(1).midi('Midi Through Port-0').midichan(1).init()  // All Notes Off (SID + FMOpl)

// CC_FMEN is sticky per-channel firmware state - it doesn't reset itself
// between songs/sessions, so a channel left targeting FMOpl by earlier
// testing (or an earlier run of this song, on the SID channels) would
// silently break this one. Explicitly reset channels 1/2/3 to SID and
// channel 4 to FMOpl every time, don't assume the board's current state.
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // Reset channel 1 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(2).init()  // Reset channel 2 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(3).init()  // Reset channel 3 to SID (CC_FMEN off)
$: ccn(88).ccv(1).midi('Midi Through Port-0').midichan(4).init()  // Enable FMOpl targeting on channel 4 (CC_FMEN)

$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(1).progNum(14).init()  // SID1: Wide bass
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(2).progNum(6).init()   // SID2: Filtered lead (stabs)
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(3).progNum(2).init()   // SID3: Pulse lead (arp)
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(4).progNum(4).init()   // FMOpl: Bell (hook)

// setcps(0.9) // uncomment and tune to taste

// Bass: pumping eighth-notes, root follows the progression one bar at a time
$: note("<[d1*8] [bb0*8] [f1*8] [c1*8]>")
   .midi('Midi Through Port-0').midichan(1)

// Stabs: off-beat chord hits, classic house "and-of-2/and-of-4" feel
$: chord("<Dm Bb F C>").voicing()
   .struct("~ x ~ x ~ x ~ x")
   .midi('Midi Through Port-0').midichan(2)

// Arp: rolling 16th-note arpeggio through each chord's scale
$: n("<[0 2 4 7]*4>")
   .scale("<D3:minor Bb2:major F3:major C3:major>")
   .midi('Midi Through Port-0').midichan(3)

// FMOpl hook: the instantly-recognisable top line, syncopated, sitting above
// everything else - this is the part that shows off the OPL chip's own
// character rather than doubling what a SID could already do
$: note("<[d5 f5 a5 ~ d5 c5 a4 ~] [bb4 d5 f5 ~ bb4 a4 f4 ~] [f4 a4 c5 ~ f4 e4 c4 ~] [c4 e4 g4 ~ c4 bb3 g3 ~]>")
   .transpose(0)
   .midi('Midi Through Port-0').midichan(4)

// .transpose(-12) transpose uses 12 steps as an octave has 12 notes

// To stop: hush()
