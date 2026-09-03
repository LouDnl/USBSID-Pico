// USBSID-Pico Strudel demo 5/5: "Frame Rate" - a classic C64 SID "arp chord"
// driven by a real MIDI clock, not the firmware's free-running arpeggiator.
//
// Background: a real SID only has 3 real voices, so C64 music faking a
// chord on one voice cycles rapidly through its notes (root/3rd/5th) at
// roughly the video frame rate (50Hz PAL / 60Hz NTSC) so it reads as one
// sustained chord rather than a rippling arpeggio - see e.g.
// https://www.lemon64.com/forum/viewtopic.php?t=65987. This firmware's own
// arpeggiator (repo/src/midi_handler.c, arp_tick()) has TWO speed modes:
//   - free-running (no MIDI clock present): hard-capped at 0.5-20 steps/sec
//     - nowhere near fast enough for the real frame-rate trick.
//   - MIDI-clock-synced: divides the incoming clock into 8 steps, 24 pulses
//     (a whole note) down to 1 pulse - and 1 pulse, at a normal tempo, DOES
//     reach the real 45-60Hz range. This demo drives that path specifically.
//
// UNVERIFIED ASSUMPTION, same honesty rule as the FMOpl clock override
// elsewhere in this project: Strudel's midicmd("clock*N") sends N evenly
// spaced MIDI clock bytes per CYCLE, and Strudel's own docs don't state how
// that relates to real MIDI's fixed 24-pulses-per-quarter-note standard (a
// gap in Strudel's own documentation, not something guessed from memory
// without checking). Assuming Strudel's default cycle = 4 quarter notes
// (standard 4/4), 24 PPQN * 4 = 96 ticks/cycle is the spec-correct value,
// used below. If the arp chord sounds twice as fast or half as fast as the
// stated tempo implies, that assumption is wrong for your Strudel version -
// try clock*48 or clock*192 instead, by ear.

let init = register('init', (pat) => pat.filterWhen((begin) => begin<1))

// CC_ANOF (All Notes Off, CC 123) - pool-wide, silences any stuck note from
// earlier testing before this song's own state gets set up.
$: ccn(123).ccv(1).midi('Midi Through Port-0').midichan(1).init()

// Reset channels to plain SID targeting (CC_FMEN off) - same hygiene as
// every other demo here, in case FMOpl targeting was left on from testing.
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(1).init()
$: ccn(88).ccv(0).midi('Midi Through Port-0').midichan(2).init()

$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(1).progNum(0).init()   // SID1: Default patch (plain triangle) - the arp voice
$: ccn(7).ccv(1).midi('Midi Through Port-0').midichan(2).progNum(3).init()   // SID2: Bass

// Arpeggiator setup on channel 1 - this is what actually turns held chord
// notes into the fast note-cycling trick, see CC map in the MIDI manual.
$: ccn(6).ccv(0.0).midi('Midi Through Port-0').midichan(1).init()    // CC_ARPM: Up (cycle root->3rd->5th->repeat)
$: ccn(11).ccv(0.0).midi('Midi Through Port-0').midichan(1).init()   // CC_ARPO: 0 extra octaves - stay inside the triad, the classic effect
$: ccn(10).ccv(1.0).midi('Midi Through Port-0').midichan(1).init()   // CC_ARPR: fastest MIDI-clock division (1 pulse) - only matters once a clock is present
$: ccn(12).ccv(1).midi('Midi Through Port-0').midichan(1).init()     // CC_ARPE: arp on for channel 1 - notes now feed the arp's held pattern instead of sounding directly

// setcps(0.53) // ~128 BPM (1 cycle = 4 beats: cps = BPM/240). At 128 BPM,
// 24 MIDI clock pulses/quarter note works out to ~19.5ms/pulse - a 1-pulse
// arp division lands at ~51Hz, squarely in the real PAL/NTSC frame-rate
// zone this demo is built to demonstrate. Slower tempos drop below it,
// faster tempos push past 60Hz - listen and retune to taste.

// The MIDI clock itself - must keep running for the firmware's clock-synced
// arp path to activate at all; comment this out and the arp instantly falls
// back to its free-running 20Hz ceiling instead.
$: midicmd("clock*96, start").midi('Midi Through Port-0')

// The arp chord: a sustained held triad per chord, one per cycle - long
// enough (a full cycle each) for many fast arp steps to audibly cycle
// through and read as one held chord, not a rippling run.
$: chord("<Am Em F G>").voicing()
   .midi('Midi Through Port-0').midichan(1)

// Bass: plain, not arpeggiated, grounds the progression underneath.
$: note("<a1(1,2) e1(1,2) f1(1,2) g1(1,2)>")
   .midi('Midi Through Port-0').midichan(2)

// To stop: hush()
