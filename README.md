# USBSID-Pico
USBSID-Pico is a RPi Pico based board for interfacing one or two MOS SID chips and/or hardware SID emulators with your PC over USB.


# Supported platforms
While in development any examples will only work on Linux


# Features
- Supports all MOS SID chips e.g. MOS6581,MOS6582 & MOS8580
- Supports all SID chip replacements e.g. SIDKick-Pico, SwinSID, ARMSID (untested), FPGASID (untested)
- Single or dual SID support (firmware specific)
  - Remaps SID addresses to $D400 & $D440
- Triple or quad SID support with SIDKick-Pico (firmware specific)
  - Remaps SID addresses to $D400, $D420, $D440 & $D460
- 1 MHz oscillator (external optional)
  - if no external clock is detecten will generate a 1 MHz square wave using pio
  - if an external clock is detected will read the external clock signal
- Uses the TinyUSB stack


# Showcase (development board)
**[Single MOS6581](https://www.youtube.com/watch?v=PAHPY8jR4rA)** \
[![Single MOS6581](https://img.youtube.com/vi/PAHPY8jR4rA/0.jpg)](https://www.youtube.com/watch?v=PAHPY8jR4rA)

**[Dual SIDKick-Pico one for left channel and one right channel)](https://www.youtube.com/watch?v=z3Mg-cSK1HA)** \
[![Dual SIDKick-Pico one for left channel and one right channel](https://img.youtube.com/vi/z3Mg-cSK1HA/0.jpg)](https://www.youtube.com/watch?v=z3Mg-cSK1HA)

**[Dual SIDKick-Pico as 4x MOS6582 SID](https://www.youtube.com/watch?v=UoN68pCP4Lc)** \
[![Dual SIDKick-Pico as 4x MOS6582 SID](https://img.youtube.com/vi/UoN68pCP4Lc/0.jpg)](https://www.youtube.com/watch?v=UoN68pCP4Lc)
