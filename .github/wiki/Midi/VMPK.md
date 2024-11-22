# Virtual MIDI Piano Keyboard
_Excerpt from VMPK website_
[VMPK](https://vmpk.sourceforge.io/)
Virtual MIDI Piano Keyboard is a MIDI events generator and receiver. It doesn't produce any sound by itself, but can be used to drive a MIDI synthesizer (either hardware or software, internal or external). You can use the computer's keyboard to play MIDI notes, and also the mouse. You can use the Virtual MIDI Piano Keyboard to display the played MIDI notes from another instrument or MIDI file player. To do so, connect the other MIDI port to the input port of VMPK.

## Custom instruments file
In VMPK you can use a custom instruments file. Below are the contents of my personal `custom.ins` that I use to test USBSID-Pico with VMPK.  
You can load this file on the `Behaviour` tab in the `Edit -> Preferences` menu.  
![vmpk](assets/vmpk-instruments-file.png)  
Be sure to copy the contents below into a new file and save it as `somename.ins`.  
```ini
  ; ----------------------------------------------------------------------

  .Patch Names

  [1VoiceSID]
  0=01 SID 1 Noise
  1=02 SID 1 Pulse
  2=03 SID 1 Sawtooth
  3=04 SID 1 Triangle

  [3VoiceSID]
  0=01 SID 1 Noise
  1=02 SID 1 Pulse
  2=03 SID 1 Sawtooth
  3=04 SID 1 Triangle

  [Synthcart]
  0=01 SAWTOOTH BASS
  1=02 GRITTY BASS
  2=03 PORTAMENTO 5THS
  3=04 SAW PORTAMENTO
  4=05 PULSE 5THS
  5=06 PULSE HIGH PORT
  6=07 TRINGL HIGH LONG
  7=08 TRIANGLE DROP
  8=09 SID EXPLOSION
  9=10 MUTE
  10=11 FILTER BASS
  11=12 SWEEP ARP
  12=13 PLUCK ARP
  13=14 SLOW ARP
  14=15 FILTER STACK 1
  15=16 FILTER STACK 2
  16=17 PULSAR
  17=18 VIBRATO LEAD
  18=19 SLOW RISE
  19=20 BENDING ECHO
  20=21 CHANNEL SAW
  21=22 ARP LEAD
  22=23 LASER BASS
  23=24 TROMBONE BLAST
  24=25 NOISY SQUARE ARP
  25=26 TRIANGLE SYNC
  26=27 MONO SYNC ECHO
  27=28 CLEAN SAWTOOTH
  28=29 CLEAN TRIANGLE
  29=30 CLEAN SQUARE


  ; ----------------------------------------------------------------------

  .Note Names

  ; ----------------------------------------------------------------------

  .Controller Names

  [MOS SID Controllers]
  1=Pulse Width
  7=Master Volume
  20=Note Frequency
  21=Noise waveform
  22=Pulse waveform
  23=Sawtooth waveform
  24=Triangle waveform
  25=Test bit
  26=Ring modulator bit
  27=Sync bit
  28=Gate bit
  29=Gate auto enable bit
  110=Frequency Cutoff
  111=Filter resonance
  112=Voice 3 disconnect
  113=Filter channel 1
  114=Filter channel 2
  115=Filter channel 3
  116=Filter external
  117=High pass
  118=Band pass
  119=Low pass
  73=Attack Time
  75=Decay
  64=Sustain
  72=Release Time

  ; ----------------------------------------------------------------------

  .RPN Names

  ; ----------------------------------------------------------------------

  .NRPN Names

  ; ----------------------------------------------------------------------

  .Instrument Definitions

  [MOS SID]
  Control=MOS SID Controllers
  Patch[0]=3VoiceSID
  Patch[1]=1VoiceSID
  Patch[9]=Synthcart
```
