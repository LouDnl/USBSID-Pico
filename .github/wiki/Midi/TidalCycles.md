# Index
* [Tidal Cycles](#tidal-cycles)
* [SuperCollider](#supercollider)
  - [Startup file (example)](#startup-file-example)
* [Tidal examples](#tidal-examples)
  - [Individual voice setup](#individual-voice-setup)
  - [Test instruments](#test-intruments)
  - [Note/Music tests](#notemusic-tests)

# Tidal Cycles
_Excerpt from the Tidal Cycles website_
[Tidal Cycles](https://tidalcycles.org/) (or 'Tidal' for short) is a free/open source live coding environment for algorithmic patterns, written in Haskell. Tidal is using SuperCollider, another open-source software, for synthesis and I/O.
## Tidal Cycles and USBSID-Pico
With it's Midi over USB support you can use USBSID-Pico with [Tidal Cycles](https://tidalcycles.org/).  
This however does require you to follow the installation guide on the [documentation pages](https://tidalcycles.org/docs/).
* [Linux](https://tidalcycles.org/docs/getting-started/linux_install/)
* [MacOS](https://tidalcycles.org/docs/getting-started/macos_install/)
* [Windows](https://tidalcycles.org/docs/getting-started/windows_install/)

# SuperCollider

## Startup file (example)
Below is a copy of my personal SuperCollider startup file, use this as example only.
```Haskell
  /*
  This is an example startup file.
  If you want to modify it, best copy it elsewhere to keep the git repository clean.

  You can then load it by calling
  "path/to/my/superdirt_startup.scd".load
  from the supercollider startup file

  The supercollider startup file is to be found in
  Platform.userAppSupportDir +/+ "startup.scd",
  or via the menu File > Open startup file
  */

  /* Run with: sclang superdirt_startup.scd   */

  (
  s.reboot { // server options are only updated on reboot
    // configure the sound server: here you could add hardware specific options
    // see http://doc.sccode.org/Classes/ServerOptions.html
    s.options.numBuffers = 1024 * 256; // increase this if you need to load more samples
    s.options.memSize = 8192 * 32; // increase this if you get "alloc failed" messages
    s.options.numWireBufs = 2048; // increase this if you get "exceeded number of interconnect buffers" messages
    s.options.maxNodes = 1024 * 32; // increase this if you are getting drop outs and the message "too many nodes"
    s.options.numOutputBusChannels = 2; // set this to your hardware output channel size, if necessary
    s.options.numInputBusChannels = 2; // set this to your hardware output channel size, if necessary
    // boot the server and start SuperDirt
    s.waitForBoot {
      ~dirt.stop; // stop any old ones, avoid duplicate dirt (if it is nil, this won't do anything)
      ~dirt = SuperDirt(2, s); // two output channels, increase if you want to pan across more channels
      ~dirt.loadSoundFiles;   // load samples (path containing a wildcard can be passed in)
      // for example: ~dirt.loadSoundFiles("/Users/myUserName/Dirt/samples/*");
      // s.sync; // optionally: wait for samples to be read
      ~dirt.start(57120, 0 ! 12);   // start listening on port 57120, create two busses each sending audio to channel 0
      SuperDirt.default = ~dirt; // make this instance available in sclang (optional)

      // optional, needed for convenient access from sclang:
      (
        ~d1 = ~dirt.orbits[0]; ~d2 = ~dirt.orbits[1]; ~d3 = ~dirt.orbits[2];
        ~d4 = ~dirt.orbits[3]; ~d5 = ~dirt.orbits[4]; ~d6 = ~dirt.orbits[5];
        ~d7 = ~dirt.orbits[6]; ~d8 = ~dirt.orbits[7]; ~d9 = ~dirt.orbits[8];
        ~d10 = ~dirt.orbits[9]; ~d11 = ~dirt.orbits[10]; ~d12 = ~dirt.orbits[11];
      );

      // directly below here, in your own copy of this file, you could add further code that you want to call on startup
      // this makes sure the server and ~dirt are running
      // you can keep this separate and make it easier to switch between setups
      // by using "path/to/my/file.scd".load and if necessary commenting out different load statements
      // ...
      if (s.hasBooted, {
        try {
          postln("SuperDirt: Disconnect existing USBSID-Pico");
          ~midiOut.disconnect(0);
          postln("SuperDirt: Dispose MIDIClient");
          MIDIClient.disposeClient;
        } { |info|
          postln("SuperDirt: Disconnect or Dispose not nescessary, continue");
        };

        try {
          postln("SuperDirt: MIDIClient init");
          MIDIClient.init;  // Init midi and list endpoints
          postln("SuperDirt: Adding USBSID-Pico");
          ~midiOut = MIDIOut.newByName("USBSID-Pico","USBSID-Pico MIDI 1");
          // Add MIDI output in SuperDirt
          // Make sure any offset on the MIDI side is also set to 0, then gradually adjust one of them until they align. If they stay in alignment when you change the cps, all is good!
          postln("SuperDirt: set latency to 0");
          ~midiOut.latency = 0;
          postln("SuperDirt: connect to usp");
          ~midiOut.connect(1);  // 1 is a guess here, its the position of the port by name in the destinations array
          postln("SuperDirt: add usp as sound to library");
          ~dirt.soundLibrary.addMIDI(\usp, ~midiOut);
        } { |error|
          postln("SuperDirt: USBSID-Pico connection failed");
        };
      });

    };

    s.latency = 0.3; // increase this if you get "late" messages
  };
  );
```

# Tidal examples
Below are my personal notes and scribbles, if they don't work then they don't work.

## Individual voice setup
```Haskell
  setcps (115/60/4)  -- sets 135bpm to cycles per second, equal to 0.5625

  {- Setup voice 1 to 12 on channel 1 to 12 -}
  let
    stackn = stack . map pure
  in
    once $ midichan (stackn [0 .. 11]) # stack [
      ccn 0x20 # ccv 1,    -- bank 1
      ccn 0x01 # ccv 10,   -- set pwm start
      ccn 0x07 # ccv 127,  -- set volume to full
      ccn 0x49 # ccv 0,--30,   -- set attack
      ccn 0x4B # ccv 0,--90,   -- set decay
      ccn 0x40 # ccv 127,  -- set sustain
      ccn 0x48 # ccv 60,   -- set release
      progNum 1            -- set waveform for noteon noteoff
    ] # s "usp"

  {- Single channel adaption -}
  once $ stack [
    ccn 0x20 # ccv 1,    -- bank 1
    ccn 0x01 # ccv 10,   -- set pwm start
    ccn 0x07 # ccv 127,  -- set volume to full
    ccn 0x49 # ccv 0,    -- set attack
    ccn 0x4B # ccv 0,    -- set decay
    ccn 0x40 # ccv 90,   -- set sustain
    ccn 0x48 # ccv 0,    -- set release
    progNum 1,           -- set waveform for noteon noteoff
    ccn 0x18 # ccv 127   -- set second alternating waveform
  ] # s "usp" # midichan 0

  once $ ccn 0x15 # ccv 0 # s "usp" # midichan 1  -- 0x80 noise off
  once $ ccn 0x16 # ccv 0 # s "usp" # midichan 1  -- 0x40 pulse off
  once $ ccn 0x17 # ccv 0 # s "usp" # midichan 1  -- 0x20 saw off
  once $ ccn 0x18 # ccv 0 # s "usp" # midichan 1  -- 0x10 triangle off

  {- disable all waveforms on one voice at once -}
  once $ stack [
    ccn 0x15 # ccv 0,
    ccn 0x16 # ccv 0,
    ccn 0x17 # ccv 0,
    ccn 0x18 # ccv 0
  ] # s "usp" # midichan 0
```
## Test intruments
```Haskell
  {- Lets create an instrument on voice 1 -}
  -- Sawtooth Bass
  once $ stack [
    ccn 0x20 # ccv 1,    -- bank 1
    ccn 0x01 # ccv 64,   -- set pwm @ 0x800 (2048) = 64 ~ 63,515506688 (0xFFF max mapped to 127 in steps of 0,03125)
    ccn 0x07 # ccv 127,  -- set volume to max
    ccn 0x49 # ccv 0,    -- set attack
    ccn 0x4B # ccv 100,  -- set decay
    ccn 0x40 # ccv 127,  -- set sustain
    ccn 0x48 # ccv 40,   -- set release
    ccn 0x6E # ccv 6,    -- set frequency cutoff @ 0xB0 (176) = 6 ~ 5,5 (0xFFF max mapped to 127 in steps of 0,03125)
    ccn 0x71 # ccv 0,    -- filter on voice 1
    ccn 0x6F # ccv 100,  -- set resonance on filter to 0xB (11) = (0xF max mapped to 127 in steps of 8)
    ccn 0x77 # ccn 0,    -- enable lowpass filter
    ccn 0x78 # ccn 0,    -- enable bandpass filter
    ccn 0x79 # ccn 0,    -- enable highpass filter
    ccn 0x15 # ccv 0,    -- noise
    ccn 0x16 # ccv 0,    -- pulse
    ccn 0x17 # ccv 0,    -- sawtooth
    ccn 0x18 # ccv 0,    -- triangle
    progNum 1           -- set pulse waveform for noteon noteoff
  ] # s "usp" # midichan 0

  {- Supporting noise on voice 2 -}
  once $ stack [
    ccn 0x20 # ccv 1,    -- bank 1
    ccn 0x01 # ccv 0,    -- set pwm start
    ccn 0x07 # ccv 127,  -- set volume to full
    ccn 0x49 # ccv 0,    -- set attack
    ccn 0x4B # ccv 0,    -- set decay
    ccn 0x40 # ccv 30,   -- set sustain
    ccn 0x48 # ccv 40,   -- set release
    ccn 0x15 # ccv 0,    -- noise
    ccn 0x16 # ccv 0,    -- pulse
    ccn 0x17 # ccv 0,    -- sawtooth
    ccn 0x18 # ccv 0,    -- triangle
    progNum 0            -- set noise waveform for noteon noteoff
  ] # s "usp" # midichan 1

  -- instrument test
  do
    resetCycles
    d1 $ n "c2(1,6) c2(1,6) c2(1,6) c2(3,6)" # speed "2 2 2 2" # sound "usp" # midichan 0
    d2 $ press $ n "g5(1,6) g5(1,6) g5(1,6) g5(2,6)" # speed "2 2 2 2" # sound "usp" # midichan 1
    -- d2 $ n "g5(1,6) g5(1,6) g5(2,8) g5(1,6)" # speed "1 1 1 1" # sound "usp" # midichan 1
    -- d1 $ n "-15 -15 [-13 -15] [-13 -12]" # speed 1.5 # sound "usp" # midichan 0
    -- d2 $ ((1/8) ~>) $ n "15 15 [17 15] [17 18]" # speed "3 3 3 3" # sound "usp" # midichan 1
    -- d1 $ n (off 0.125 (|+ 7) "<c a f e>") # sound "usp" # midichan 0
    -- d2 $ ((1/8) ~>) $ n "g5(1,6) g5(1,6) g5(1,6) g5(2,8)" # speed "1 1 1 1" # sound "usp" # midichan 1
    -- d2 $ ((1/8) ~>) $ n "15 15 [17 15] [17 18]" # speed "3 3 3 3" # sound "usp" # midichan 1

  d1 $ n "-15 -15 [-13 -15] [-13 -12]" # speed 1.5 # sound "usp" # midichan 0
  d1 $ n "0 0 [2 0] [2 3]" # speed 1.5 # sound "usp" # midichan 0
```
## Note/Music tests
```Haskell
  p 100 $ off "e" (|+ n 12) $ struct "1(<11 13>,16)" $ s "usp" # midichan 0 # n "c'maj'i a'min g'maj f'maj" # octave "<3 2 4>" # progNum "<1 2 3>"
  p 110 $ struct "1(4,8)" $ s "usp" # n "c g" # octave 1 # midichan 1

  p 110 $ struct "1(<11 13>,16)" $ s "usp" # n "f0 e" # octave 1 # midichan 0

  p 120 $ jux rev $ n (off 0.125 (|+ 12) $ off 0.125 (|+ 7)  "<c*2 a(3,8) f(3,8,2) e*2>")
    # sound "usp" # midichan 1
    # legato 2

  do
    resetCycles
    d1 $ n (off 0.125 (|+ 7)  "<c*2 a(3,8) f(3,8,2) e*2>")
      # s "usp" # midichan 0

  p 20 $ every 2 ("<0.25 0.125 0.5>" <~)
    $ n "<c*2>" -- long tone
    -- $ n "<a(3,8)>"
    # progNum "1"
    -- $ n "<f(3,8,2)>"  --
    -- $ n "<e*2>"  --
    -- $ n "<c*2 a(3,8) f(3,8,2) e*2>"
    -- # progNum "3"
    # sound "usp" # midichan 1

  d1 $ every 2 ("<0.25 0.125 0.5>" <~) $ sound "bd*2 [[~ lt] sn:3] lt:1 [ht mt*2]"
    -- # squiz "<1 2.5 2>"
    -- # room (slow 4 $ range 0 0.2 saw)
    -- # sz 0.5
    -- # orbit 1
```
