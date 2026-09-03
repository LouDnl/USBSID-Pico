// USBSID-Pico Strudel demo: "Lazy Jones (remix)" - a transcription of the
// first four sections of Pex Tufvesson's (Mahoney) Lazy_Jones_remix.sid,
// reconstructed straight from a disassembly of the original .sid tune.
// Ported from the SuperDirt/Tidal version of this demo
// (../superdirt-tidalcycles/lazy_jones_poly.tidal) - see that file for the
// full extraction writeup. This file mirrors its CURRENT state (both voices
// on one MIDI channel), not the channel-per-voice layout its own header
// comment used to describe - see the note below.
//
// How this was extracted: the tune's play routine ($C030) is about as
// minimal as a SID player gets - no instrument table, no ADSR-per-note,
// just two raw frequency-pair tables (voice 1 at $D400/$D401, voice 2 at
// $D407/$D408) walked 4 bytes at a time (lo1,hi1,lo2,hi2), with a hard
// gate-restart (CONTR = 0x20 then 0x21, i.e. gate-off then
// gate-on-with-sawtooth) on EVERY step regardless of whether the pitch
// changed. That hard-restart-every-step trick is the whole "machine gun"
// Lazy Jones sound: voice 1 alternates a note with its own octave
// (b2/b3/b2/b3...) while voice 2 carries the real melody/arpeggio - both
// driven by the exact same envelope (ATTDEC=$66, i.e. attack 6/decay 6;
// SUSREL is never written at all, so it stays at the SID's post-reset 0 -
// sustain 0, release 0. Every note you hear is the attack+decay transient,
// kept alive only by being retriggered every single step).
//
// The two tables run from $C100 to $CB7F (2688 bytes = 672 steps, ~161s at
// this tune's step rate) before the routine loops back to $C100. What's
// below is steps 0-127 - four 32-step sections (a call-response riff, then
// three chromatically-shifting variations) - decoded from each 16-bit SID
// frequency register back to Hz (register * 985248 PAL clock / 16777216)
// and rounded to the nearest MIDI note. Still a loop of an excerpt, not
// the full 161-second piece.
//
// Tempo: the routine advances the tables once every 12 calls of the play
// routine ($03E3, decremented every call, reloaded to 12). No CIA timer is
// ever programmed in this file (grepped for $DC00-$DC0F, found nothing),
// so Play is driven at whatever rate the player calls it - nominally 50Hz
// PAL (12/50s = 240ms/step), but measured audibly slower than the real
// tune. The .tidal source landed on ~200-210ms/step by ear; carried over
// here as .slow(2.1) on both note patterns rather than a global setcps -
// if it's still off, tell me the right ratio and this gets retuned.
//
// Both voices currently target the SAME channel (1) - not the channel-per-
// voice split ("channel 1 = SID1 voice 1, channel 2 = SID2 voice 2") an
// earlier revision of the .tidal source described. That split was dropped
// in favor of one shared channel/timbre, matching the original's identical
// envelope on both voices. This file is also the one being used to chase a
// "second voice drops out while the first stays on" report on real
// hardware (FPGASID, confirmed independent of 1-SID-vs-2-SID setup) - both
// note patterns below fire a note on EVERY step (no rests in either list),
// so if that report reproduces here too it's in the MIDI engine's
// polyphonic voice allocation for a single channel, not in either the
// Tidal or Strudel sequencing layer.
//
// No new firmware patch was added for this - the exact envelope is built
// live from CC (waveform + ADSR CCs) below rather than baked into
// midi_patch.c's factory bank.
//
// CC map used here (repo/src/midi_defs.h): CC_ANOF=123, CC_FMEN=88,
// CC_ARPE=12, CC_UNIS=41, CC_LFOD=3, CC_LFO2D=15, CC_TRIA=23, CC_SAWT=22,
// CC_ATT=17, CC_DEC=18, CC_SUS=19, CC_REL=29, CC_SID1..4=104-107,
// CC_VOICE1..3=108-110. ccv() below is 0-1 (Strudel normalizes to the
// MIDI 0-127 byte), unlike the raw 0-127 ccn/ccv pairs in the .tidal file.
//
// Uses 'Midi Through Port-0' as the MIDI destination, matching every other
// demo in this folder except pop_classical.strudel.js - adjust to your own
// virtual/loopback port name if different.

let init = register('init', (pat) => pat.filterWhen((begin) => begin < 1))

// CC_ANOF (All Notes Off, CC 123) is pool-wide, not channel-scoped - sent
// first so a note stuck from earlier testing/a previous song doesn't keep
// ringing under this one.
$: ccn(123).ccv(1).midi('Midi Through Port-0').midichan(1).init()  // All Notes Off (SID + FMOpl)

// CC_FMEN (CC 88) is sticky per-channel state - reset channels 1-5 to SID
// rather than assuming the board's current state.
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // Reset channel 1 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(2).init()  // Reset channel 2 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(3).init()  // Reset channel 3 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(4).init()  // Reset channel 4 to SID (CC_FMEN off)
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(5).init()  // Reset channel 5 to SID (CC_FMEN off)

// CC_ARPE/CC_UNIS/CC_LFOD/CC_LFO2D and the SID/voice override CCs are all
// sticky per-channel state too - a previous demo could otherwise hijack or
// detune this song's notes. This is the fix for the original "goes out of
// tune" / "not playing together" report against this file.
$: ccn(12).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // arp off
$: ccn(41).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // unison off
$: ccn(3).ccv(0).midi('Midi Through Port-0').midichan(1).init()    // LFO1 depth off
$: ccn(15).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // LFO2 depth off
$: ccn(104).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // SID1 override off
$: ccn(105).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // SID2 override off
$: ccn(106).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // SID3 override off
$: ccn(107).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // SID4 override off
$: ccn(108).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // Voice1 override off
$: ccn(109).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // Voice2 override off
$: ccn(110).ccv(0).midi('Midi Through Port-0').midichan(1).init()  // Voice3 override off

// Build the "hard-restart pluck" timbre from raw CC. Channel boots on the
// default patch (triangle, sustain full), so switch the waveform bit
// explicitly: sawtooth on, triangle off.
$: ccn(23).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // CC_TRIA off
$: ccn(22).ccv(1).midi('Midi Through Port-0').midichan(1).init()   // CC_SAWT on
$: ccn(17).ccv(0.4).midi('Midi Through Port-0').midichan(1).init() // CC_ATT: 6/15
$: ccn(18).ccv(0.4).midi('Midi Through Port-0').midichan(1).init() // CC_DEC: 6/15
$: ccn(19).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // CC_SUS: 0
$: ccn(29).ccv(0).midi('Midi Through Port-0').midichan(1).init()   // CC_REL: 0

// Volume - SIDs boot silent and need this before anything is audible.
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(1).init()

// setcps(0.238) // ~ 1000/(128*stepMs) for stepMs=~33 at cps semantics
// matching Strudel's per-cycle timing; NOT used here - tempo instead comes
// from .slow(2.1) on both note patterns below, same as the .tidal source.

// Voice 1: the "machine gun" - same pitch class hard-restarted an octave
// apart every step, verbatim from $C100, steps 0-127.
$: note("[b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 e3 e4 e3 e4 fs3 fs4 fs3 fs4 b2 b3 b2 b3 b2 b3 b2 b3 as3 as4 as3 as4 as3 as4 as3 as4 fs3 fs4 fs3 fs4 fs3 fs4 fs3 fs4 as3 as4 as3 as4 as3 as4 as3 as4 cs4 cs5 cs4 cs5 cs4 cs5 cs4 cs5 as3 as4 as3 as4 as3 as4 as3 as4 cs4 cs5 cs4 cs5 cs4 cs5 cs4 cs5 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 gs3 gs4 gs3 gs4 gs3 gs4 gs3 gs4 f3 f4 f3 f4 f3 f4 f3 f4 gs3 gs4 gs3 gs4 gs3 gs4 gs3 gs4 f3 f4 f3 f4 f3 f4 f3 f4]")
   .slow(13)
   .legato(1)
   .midi('Midi Through Port-0').midichan(1)

// Voice 2: the real melody/arpeggio line underneath the machine-gun bass,
// same 128 steps, same source table.
$: note("[c3 b4 d5 e5 fs5 c4 c3 c4 c3 b4 d5 e5 fs5 g5 fs5 d5 e5 e4 f3 e4 d5 fs4 fs5 b4 b2 c4 c3 c4 c3 c4 c3 c4 as4 as4 b3 cs5 b3 c5 b3 as4 cs5 fs4 g3 fs4 g3 fs4 g3 fs4 as4 as4 b3 cs5 b3 c5 b3 as4 f4 cs5 cs4 cs5 cs4 cs5 cs4 cs5 f5 f5 ds5 cs5 ds5 f5 b3 as4 cs6 gs5 f5 ds5 f5 ds5 cs4 f5 f3 f4 f3 f4 f3 f4 f5 c5 f3 f4 c5 f4 f3 f4 f3 f4 gs4 gs4 as4 c5 c5 cs5 c5 as4 f4 f4 gs4 f4 as4 f4 f3 f4 gs4 gs4 as4 c5 c5 cs5 c5 as4 f4 f4 gs4 f4 f5 as4 f5 as5]")
   .slow(13)
   .legato(1)
   .midi('Midi Through Port-0').midichan(1)

// Alternate form, same result: both voices as one stack() on one channel,
// closer to the .tidal source's `stack [...] # midichan 0` shape. Left
// here for whoever's chasing the voice-collapse report and wants to A/B
// stack() against two independent $: patterns.
// $: stack(
//   note("[b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 b2 b3 e3 e4 e3 e4 fs3 fs4 fs3 fs4 b2 b3 b2 b3 b2 b3 b2 b3 as3 as4 as3 as4 as3 as4 as3 as4 fs3 fs4 fs3 fs4 fs3 fs4 fs3 fs4 as3 as4 as3 as4 as3 as4 as3 as4 cs4 cs5 cs4 cs5 cs4 cs5 cs4 cs5 as3 as4 as3 as4 as3 as4 as3 as4 cs4 cs5 cs4 cs5 cs4 cs5 cs4 cs5 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 f3 f4 gs3 gs4 gs3 gs4 gs3 gs4 gs3 gs4 f3 f4 f3 f4 f3 f4 f3 f4 gs3 gs4 gs3 gs4 gs3 gs4 gs3 gs4 f3 f4 f3 f4 f3 f4 f3 f4]"),
//   note("[c3 b4 d5 e5 fs5 c4 c3 c4 c3 b4 d5 e5 fs5 g5 fs5 d5 e5 e4 f3 e4 d5 fs4 fs5 b4 b2 c4 c3 c4 c3 c4 c3 c4 as4 as4 b3 cs5 b3 c5 b3 as4 cs5 fs4 g3 fs4 g3 fs4 g3 fs4 as4 as4 b3 cs5 b3 c5 b3 as4 f4 cs5 cs4 cs5 cs4 cs5 cs4 cs5 f5 f5 ds5 cs5 ds5 f5 b3 as4 cs6 gs5 f5 ds5 f5 ds5 cs4 f5 f3 f4 f3 f4 f3 f4 f5 c5 f3 f4 c5 f4 f3 f4 f3 f4 gs4 gs4 as4 c5 c5 cs5 c5 as4 f4 f4 gs4 f4 as4 f4 f3 f4 gs4 gs4 as4 c5 c5 cs5 c5 as4 f4 f4 gs4 f4 f5 as4 f5 as5]")
// ).slow(2.1).legato(1).midi('Midi Through Port-0').midichan(1)

// To stop: hush()
