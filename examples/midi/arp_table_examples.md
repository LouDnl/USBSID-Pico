# ARP Table example presets
To hear one play: "Select on channel" button (sets CC_ARPT + CC_ARPM=Table), then enable CC_ARPE on the LFO/Arpeggiator tab and hold a note.
Slot 2 and 5 are the ones that actually matter for closing out the round-trip test — table 0/1 never exercised a negative offset.

Slot 1 — major triad up (basic, matches slot 0's shape shifted): offsets 0,4,7,4, step_count 4, loop_start 0
f0 50 27 01 00 00 00 04 00 07 00 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 f7

Slot 2 — octave bounce (tests a negative offset for the first time): offsets 0,12,0,-12, step_count 4, loop_start 0
f0 50 27 02 00 00 00 0c 00 00 0f 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 f7

Slot 3 — intro then loop (tests loop_start > 0): offsets 0,7,12,19,0,3,7,10, step_count 8, loop_start 4 (plays 0,7,12,19 once, then repeats 0,3,7,10 forever)
f0 50 27 03 00 00 00 07 00 0c 01 03 00 00 00 03 00 07 00 0a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 08 f7

Slot 4 — full 16-step chromatic run (boundary: max step_count = 16): offsets 0..15, step_count 16, loop_start 0
f0 50 27 04 00 00 00 01 00 02 00 03 00 04 00 05 00 06 00 07 00 08 00 09 00 0a 00 0b 00 0c 00 0d 00 0e 00 0f 00 00 01 00 f7

Slot 5 — negative-heavy (stresses the signed-nibble decode path harder than slot 2 alone): offsets -12,-7,-3,0,3,7,12,0, step_count 8, loop_start 0
f0 50 27 05 0f 04 0f 09 0f 0d 00 00 00 03 00 07 00 0c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 08 f7
