# USBSID-Pico
USBSID-Pico is a RPi Pico based board for interfacing one or two MOS SID chips and/or hardware SID emulators with your PC over USB.


# Supported platforms
While in development any examples will only work on Linux


# Features
- Supports all MOS SID chips e.g. MOS6581, MOS6582 & MOS8580
- Supports all SID chip replacements e.g. SIDKick-Pico, SwinSID, ARMSID (untested), FPGASID (untested)
- Single or dual SID support (firmware specific)
  - Remaps known SID addresses to $D400 & $D440
- Dual, triple or quad SID support with SIDKick-Pico (firmware specific)
  - Remaps known SID addresses to $D400, $D420, $D440 & $D460
- 1 MHz oscillator (external optional)
  - if no external clock is detecten will generate a 1 MHz square wave using pio
  - if an external clock is detected will read the external clock signal
- Uses the TinyUSB stack


# Showcase (development board)
While in development showcase videos and/or audio links are subject to be changed or updated
### Single MOS6581
[![Single MOS6581](https://img.youtube.com/vi/PAHPY8jR4rA/0.jpg)](https://www.youtube.com/watch?v=PAHPY8jR4rA)

### Dual SIDKick-Pico
MOS8580 emulation audio recording one for left channel and one right channel: \
https://soundcloud.com/retroloudnl/voodoo-people-8580

MOS6581 emulation one for left channel and one right channel: \
[![Dual SIDKick-Pico (MOS6581) one for left channel and one right channel](https://img.youtube.com/vi/z3Mg-cSK1HA/0.jpg)](https://www.youtube.com/watch?v=z3Mg-cSK1HA)

4x MOS6582 emulation: \
[![Dual SIDKick-Pico as 4x MOS6582 SID](https://img.youtube.com/vi/UoN68pCP4Lc/0.jpg)](https://www.youtube.com/watch?v=UoN68pCP4Lc)
