// USBSID-Pico Strudel demo 6/6: "Big Top" - a kitchen-sink showcase over an
// Em-C-G-D circus/EDM loop. Not a genre demo like 1-5, this one exists to
// make you HEAR the MIDI engine's own features, not just play notes through it:
//   - multi-keypress:  SID3 holds a real 3-note chord that the firmware's own
//     arpeggiator (not Strudel) cycles through at MIDI-clock speed
//   - multi-patch:     SID1's bass AND FMOpl's hook both swap Program Change
//     mid-song, live, timed to the section change (bar 3)
//   - unison + detune: SID2's lead runs CC_UNIS with CC_UDET spread - one
//     mono voice, thickened, not a chord
//   - dual LFOs:       LFO1 (vibrato, pitch) on the unison lead, LFO2 (wah,
//     cutoff) on the arp'd chord voice
// All four parts share the same chord underneath, so bar 4's downbeat lands
// every part on D at once - SID1 bass, SID2 unison lead, SID3's arp'd chord
// and FMOpl's hook all converge there. That's the whole pool sounding at
// once, which is as "multi-keypress" as a demo file gets without a real
// keyboard.
//
// Channel map for THIS user's board (2xSID physical config: socket 1 hosts
// a clone presenting as 2x8580, socket 2 hosts a clone presenting as
// 1x8580 + 1xFMOpl): channels 1/2/3 = SID1/2/3 one-SID-each, channel 4 =
// FMOpl. Same custom (non-firmware-default) mapping as every other demo in
// this folder - see pop_classical.strudel.js for the full explanation. If
// your board's channel config differs, adjust the midichan() calls below.
//
// Patch choices (repo/src/midi_patch.c factory bank):
//   14 Wide bass       - pulse ~65% duty, punchy (bars 1-2)
//   3  Bass            - sawtooth, low-passed, tighter (bars 3-4, the drop)
//   6  Filtered lead   - sawtooth through a resonant low-pass (unison lead)
//   1  Sawtooth lead   - plain, clean base for the clock-synced arp chord
//   FMOpl 1 Electric piano (bars 1-2) / FMOpl 4 Bell (bars 3-4, the drop)
//
// CC map used here (repo/src/midi_defs.h): CC_VOL=7, CC_FMEN=88, CC_ANOF=123,
// CC_UNIS=41, CC_UDET=42, CC_LFOW/R/D/T=0/2/3/4, CC_LFO2W/R/D/T=13/14/15/16,
// CC_ARPM/R/O/E=6/10/11/12.

let init = register('init', (pat) => pat.filterWhen((begin) => begin<1))

// CC_ANOF (All Notes Off) is pool-wide - clears every held SID and FMOpl
// voice before this song sets up its own state.
$: ccn(123).ccv(1).midi('Midi Through Port-0').midichan(1).init()

// CC_FMEN, CC_UNIS and CC_ARPE are all sticky per-channel firmware state -
// none of them reset themselves between songs, so explicitly force every
// channel back to a known baseline before turning any of them on again.
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // ch1 SID target (FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(2).init()   // ch2 SID target
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(3).init()   // ch3 SID target
$: ccn(88).ccv(1).midi('Midi Through Port-0').midichan(4).init()   // ch4 -> FMOpl
$: ccn(41).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // unison off ch1
$: ccn(41).ccv(0).midi('Midi Through Port-0').midichan(2).init()   // unison off ch2 (set on below)
$: ccn(41).ccv(0).midi('Midi Through Port-0').midichan(3).init()   // unison off ch3
$: ccn(12).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // arp off ch1
$: ccn(12).ccv(0).midi('Midi Through Port-0').midichan(2).init()   // arp off ch2
$: ccn(12).ccv(0).midi('Midi Through Port-0').midichan(3).init()   // arp off ch3 (set on below)

// Volume - SIDs boot silent and need this before anything is audible.
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(1).init()
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(2).init()
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(3).init()
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(4).init()

// Starting patches - bass and FMOpl hook both get swapped again mid-song,
// see the progNum() patterns further down. SID2/SID3 keep one patch each
// for the whole song.
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(1).progNum(14).init()  // SID1: Wide bass
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(2).progNum(6).init()   // SID2: Filtered lead
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(3).progNum(1).init()   // SID3: Sawtooth lead
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(4).progNum(1).init()   // FMOpl: Electric piano

// SID2 unison: one fat mono voice instead of a chord - CC_UNIS on, CC_UDET
// sets the detune spread. Turning unison on drops the channel to 1 note/SID,
// which is exactly what we want here: this lead is meant to sound like one
// thick oscillator, not three thin ones.
$: ccn(41).ccv(1).midi('Midi Through Port-0').midichan(2).init()   // CC_UNIS: on
$: ccn(42).ccv(0.4).midi('Midi Through Port-0').midichan(2).init() // CC_UDET: moderate spread

// SID2 LFO1: slow pitch vibrato, subtle - gives the unison lead some life
// without turning it into a siren.
$: ccn(0).ccv(0.0).midi('Midi Through Port-0').midichan(2).init()  // CC_LFOW: Triangle
$: ccn(2).ccv(0.25).midi('Midi Through Port-0').midichan(2).init() // CC_LFOR: slow-ish rate
$: ccn(3).ccv(0.3).midi('Midi Through Port-0').midichan(2).init()  // CC_LFOD: subtle depth
$: ccn(4).ccv(0.0).midi('Midi Through Port-0').midichan(2).init()  // CC_LFOT: Pitch

// SID3 LFO2: square-wave wah on the filter cutoff, deep and syncopated -
// this is what turns the plain arp'd chord into something that breathes.
$: ccn(13).ccv(0.6).midi('Midi Through Port-0').midichan(3).init() // CC_LFO2W: Square
$: ccn(14).ccv(0.3).midi('Midi Through Port-0').midichan(3).init() // CC_LFO2R: moderate rate
$: ccn(15).ccv(0.7).midi('Midi Through Port-0').midichan(3).init() // CC_LFO2D: deep
$: ccn(16).ccv(1.0).midi('Midi Through Port-0').midichan(3).init() // CC_LFO2T: Cutoff

// SID3 arpeggiator, MIDI-clock-synced (see arp_chord_clocksync.strudel.js
// for the full clock-division writeup) - this is what turns the held chord
// below into the "multi-keypress" shimmer: three real simultaneous note-ons,
// cycled fast enough by the firmware's own engine to read as one texture.
$: ccn(6).ccv(0.0).midi('Midi Through Port-0').midichan(3).init()  // CC_ARPM: Up
$: ccn(11).ccv(0.0).midi('Midi Through Port-0').midichan(3).init() // CC_ARPO: 0 extra octaves, stay in the triad
$: ccn(10).ccv(0.85).midi('Midi Through Port-0').midichan(3).init()// CC_ARPR: fast clock division
$: ccn(12).ccv(1).midi('Midi Through Port-0').midichan(3).init()   // CC_ARPE: on

// setcps(0.85) // uncomment and tune to taste - up-tempo, circus-EDM pace

// The clock - required for CC_ARPR's division to mean anything; without it
// the arp falls back to its free-running ~20Hz ceiling instead.
$: midicmd("clock*96, start").midi('Midi Through Port-0')

// SID1 bass: pumping eighth-notes, root follows the progression. Patch
// swaps from Wide bass to the tighter Bass patch exactly at bar 3 - a live
// Program Change mid-song, not just a boot-time setting.
$: note("<[e1*8] [c1*8] [g1*8] [d1*8]>")
   .midi('Midi Through Port-0').midichan(1)
$: progNum("<14 14 3 3>").midi('Midi Through Port-0').midichan(1)

// SID2 unison lead: syncopated riff, one thick detuned voice throughout.
$: note("<[e4 ~ g4 b4] [c4 ~ e4 g4] [g3 ~ b3 d4] [d4 ~ fs4 a4]>")
   .midi('Midi Through Port-0').midichan(2)

// SID3: the held chord the arpeggiator turns into a shimmer - one triad per
// bar, sustained, so many fast arp steps read as one chord rather than a run.
$: chord("<Em C G D>").voicing()
   .midi('Midi Through Port-0').midichan(3)

// FMOpl hook: syncopated top line, answering the unison lead. Patch swaps
// from Electric piano to Bell at the same bar-3 drop as the SID1 bass, so
// both patch changes land together and the section change is unmistakable.
$: note("<[b4 ~ e5 ~ d5 ~ b4 ~] [g4 ~ c5 ~ b4 ~ g4 ~] [d4 ~ g4 ~ fs4 ~ d4 ~] [a4 ~ d5 ~ cs5 ~ a4 ~]>")
   .midi('Midi Through Port-0').midichan(4)
$: progNum("<1 1 4 4>").midi('Midi Through Port-0').midichan(4)

// To stop: hush()
