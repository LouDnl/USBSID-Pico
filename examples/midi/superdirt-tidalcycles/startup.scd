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
          // ~midiOut = MIDIOut.newByName("USBSID-Pico v1.0", "USBSID-Pico v1.0 MIDI 1");
          // ~midiOut = MIDIOut.newByName("USBSID-Pico v1.3", "USBSID-Pico v1.3 MIDI 1");
          // ~midiOut = MIDIOut.newByName("USBSID-Pico v1.5", "USBSID-Pico v1.5 MIDI 1");
          // ~midiOut = MIDIOut.newByName("USBSID-Pico2 v1.0", "USBSID-Pico2 v1.0 MIDI 1");
          // ~midiOut = MIDIOut.newByName("USBSID-Pico2 v1.3", "USBSID-Pico2 v1.3 MIDI 1");
          ~midiOut = MIDIOut.newByName("USBSID-Pico2 v1.5", "USBSID-Pico2 v1.5 MIDI 1");
          // Add MIDI output in SuperDirt
          // Make sure any offset on the MIDI side is also set to 0, then gradually adjust one of them until they align. If they stay in alignment when you change the cps, all is good!
          postln("SuperDirt: set latency to 0");
          ~midiOut.latency = 0;
          postln("SuperDirt: connect to usbsidpico");
          ~midiOut.connect(1);  // 1 is a guess here, its the position of the port by name in the destinations array
          postln("SuperDirt: add usbsidpico as sound to library");
          ~dirt.soundLibrary.addMIDI(\usbsidpico, ~midiOut);
        } { |error|
          postln("SuperDirt: USBSID-Pico connection failed");
        };
      });

    };

    s.latency = 0.3; // increase this if you get "late" messages
  };
);
