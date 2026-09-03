// USBSID-Pico Strudel demo 4/4: "Porch Light" - G-D-Em-C singer-songwriter ballad
// 4 parts: 3 real SIDs (bass/pad/fingerpicked arpeggio) + 1 FMOpl chip
// (electric piano melody). Same channel map and CC_FMEN setup notes as
// edm_house_fmopl.strudel.js - see that file for the full explanation.
//
// Slower, gentler than the two EDM/house demos: mostly quarter/eighth notes
// rather than 16th-note runs, a sustained pad instead of stabs, and a
// fingerpicked-style arpeggio instead of a pumping bassline groove -
// FMOpl's Electric Piano instrument carries the melody, which is exactly
// the kind of warm, slightly-imperfect timbre 2-op FM does well and a
// plain SID waveform can't really reach.

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
// testing would silently break this one. Explicitly reset channels 1/2/3
// to SID and channel 4 to FMOpl every time, don't assume current state.
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // Reset channel 1 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(2).init()  // Reset channel 2 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(3).init()  // Reset channel 3 to SID (CC_FMEN off)
$: ccn(88).ccv(1).midi('Midi Through Port-0').midichan(4).init()  // Enable FMOpl targeting on channel 4 (CC_FMEN)

$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(1).progNum(3).init()  // SID1: Bass
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(2).progNum(7).init()  // SID2: Slow pad
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(3).progNum(4).init()  // SID3: Pluck (fingerpicked)
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(4).progNum(1).init()  // FMOpl: Electric piano

// setcps(0.55) // uncomment and tune to taste - unhurried, ballad pace

// Bass: simple root notes under each chord, mostly held rather than pumping
$: note("<g1(1,2) d1(1,2) e1(1,2) c1(1,2)>")
   .midi('Midi Through Port-0').midichan(1)

// Pad: sustained triads under everything else, the warm backing bed
$: chord("<G D Em C>").voicing()
   .midi('Midi Through Port-0').midichan(2)

// Fingerpicked-style arpeggio: gentler than the house demo's 16th-note
// runs - eighth notes, broken-chord shape rather than a straight run up
$: note("<[g3 b3 d4 g4 d4 b3] [d3 fs3 a3 d4 a3 fs3] [e3 g3 b3 e4 b3 g3] [c3 e3 g3 c4 g3 e3]>")
   .midi('Midi Through Port-0').midichan(3)

// FMOpl electric piano: the melody, phrased simply and mostly stepwise -
// this is the line a voice would sit next to in a real song, so it stays
// out of the way rhythmically rather than competing with the arpeggio
$: note("<[d5 ~ b4 ~ a4 ~ g4 ~] [a4 ~ fs4 ~ d4 ~ ~ ~] [b4 ~ g4 ~ e4 ~ ~ ~] [c5 ~ g4 ~ e4 ~ d4 ~]>")
   .transpose(0)
   .midi('Midi Through Port-0').midichan(4)

// .transpose(-12) transpose uses 12 steps as an octave has 12 notes

// To stop: hush()
