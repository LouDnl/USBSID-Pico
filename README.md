# USBSID-Pico
USBSID-Pico is a RPi Pico based board for interfacing one or two MOS SID chips and/or hardware SID emulators with your PC over USB.

# Hardware features
- Supports all MOS SID chips e.g. MOS6581, MOS6582 & MOS8580 (untested)
- Supports all SID chip replacements e.g. [SIDKick-Pico](https://github.com/frntc/SIDKick-pico), [SwinSID](https://github.com/dmantione/swinsid), ARMSID (untested), FPGASID (untested)
- 1 MHz oscillator (external optional)
  - if no external clock is detecten will generate a 1 MHz square wave using pio
  - if an external clock is detected will read the external clock signal
- Power via USB
  - 5v stepup for clean 5v VCC to both SID chips
  - 12v or 9v stepup for clean VDD to both SID chips
    - 12v/9v jumper, defautls to 12v ~ inspired by [SIDBlaster-Nano](https://codeberg.org/CBMretro/SIDBlaster-USB_Nano) design
- Audio out filter as in the [C64 schematics](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/c64/250469-rev.A-right.gif)
  - With optional 8580 cap
- Audio jumper
  - SID1 audio left & right
  - SID1 audio left & SID2 audio right

# Firmware features
The firmware is still in development
- Custom protocol for playing SID files or usage with emulators
  - An adapted version of Vice emulator is available via de link under examples
  - SidBerry for command line SID file play is available via de link under examples
  - More to come?
- Midi ASID (in) support (heavily inspired by multiple sources ~ links to follow)
  - Play SID files in your (midi supporting) browser via [Deepsid](https://deepsid.chordian.net/) by Chordian
  - Play SID files in your (midi supporting) browser via [IneSID](https://inesid.fazibear.me/) by Fazibear
- Midi support in development
- WebUSB support in development
- Single or dual SID support (firmware specific)
  - Remaps known SID addresses to $D400 & $D440
- Dual, triple or quad SID support with SIDKick-Pico (firmware specific)
  - Remaps known SID addresses to $D400, $D420, $D440 & $D460
- Quad SID support (addresses remixed) with SIDKick-Pico (firmware specific)
  - SID2->SID3 SID3->SID2
- Onboard LED acts as VUE meter calculated by the voices of SID1
- Uses the [TinyUSB](https://github.com/hathach/tinyusb) stack


# Supported platforms
While in development any examples will only work on Linux. \
Chrome webbrowser ASID is supported in Linux and Android.


# PCB (in development)
## v0.2 improved testboard
![USBSID-Pico-v0 2-top](https://github.com/user-attachments/assets/7fef5747-a0c0-4f19-8eb0-a5afc29294dd)
## v0.1 testboard
![USBSID-Pico-v0 1-top](https://github.com/user-attachments/assets/6e2e1ac0-b5a5-4d62-a71d-1be0ada15490)

# Showcase
While in development showcase videos and/or audio links are subject to be changed or updated
### Dual MOS6581
**USBSID-Pico & Vice playing Spy vs Spy** \
[![Dual MOS6581](https://img.youtube.com/vi/Me79wiWPoXc/0.jpg)](https://www.youtube.com/watch?v=Me79wiWPoXc) \
**USBSID-Pico & Vice = me playing Commando** \
[![Dual MOS6581](https://img.youtube.com/vi/5YYzJu9KSuY/0.jpg)](https://www.youtube.com/watch?v=5YYzJu9KSuY)

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

Visit my [Youtube channel](https://www.youtube.com/channel/UCOu1hPBTsEbG7ZFnk9-29KQ) to see more examples

# Examples with USBSID-Pico support
[**Vice**](https://github.com/LouDnl/Vice-USBSID) fork is available @ https://github.com/LouDnl/Vice-USBSID \
[**SidBerry**](https://github.com/LouDnl/SidBerry) fork is available @ https://github.com/LouDnl/SidBerry
