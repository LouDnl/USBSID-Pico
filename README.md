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
[Monty on the Run by Rob Hubbard](https://csdb.dk/sid/?id=14328) \
[![Single MOS6581](https://img.youtube.com/vi/PAHPY8jR4rA/0.jpg)](https://www.youtube.com/watch?v=PAHPY8jR4rA)

### Dual SIDKick-Pico
**2x MOS8580 emulation one for left channel and one right channel** \
[Voodoo People! by Jammer](https://csdb.dk/sid/?id=56742): \
[![Dual SIDKick-Pico](https://img.youtube.com/vi/JPYiq4AGKQ4/0.jpg)](https://www.youtube.com/watch?v=JPYiq4AGKQ4) \
[13:37 by Fairlight](https://csdb.dk/release/?id=242855): \
[![Dual SIDKick-Pico](https://img.youtube.com/vi/9m9uz6quuqE/0.jpg)](https://www.youtube.com/watch?v=9m9uz6quuqE)

**2x MOS8580 emulation** \
[Game of Thrones by Genesis Project](https://csdb.dk/release/?id=157533): \
[![Dual SIDKick-Pico](https://img.youtube.com/vi/lShZ3DHaJg8/0.jpg)](https://www.youtube.com/watch?v=lShZ3DHaJg8)

**4x MOS8580 emulation** \
[Quad Core demo by Singular](https://csdb.dk/release/?id=159071): \
[![Dual SIDKick-Pico as 4x MOS8580 SID](https://img.youtube.com/vi/zWvMhORM-sg/0.jpg)](https://www.youtube.com/watch?v=zWvMhORM-sg)


# Examples with USBSID-Pico support
[**Vice**](https://github.com/LouDnl/Vice-USBSID) fork is available @ https://github.com/LouDnl/Vice-USBSID \
[**SidBerry**](https://github.com/LouDnl/SidBerry) fork is available @ https://github.com/LouDnl/SidBerry
