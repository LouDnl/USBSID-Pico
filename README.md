![USBSID-Pico](images/pcbv0.2.png)<br>
![USBSID-Pico](images/usbsid.png)![LouD](images/loud.png)
# USBSID-Pico
USBSID-Pico is a RasberryPi Pico based board for interfacing one or two MOS SID chips and/or hardware SID emulators with your PC over USB.
## INDEX
* [Project status](#project-status)
* [Firmware](#firmware)
* [Hardware](#hardware)
* [PCB in action](#usbsid-pico-in-action)
* [Software examples](#software)
* [Acknowledgements](#acknowledgements)
* [License](#license)
* [Changelog](#changelog)

# Project status
While in development any of the here mentioned items are subject to change.
Firmware support will be limited until the PCB is released.
### Supported platforms
During in development any examples will only work on Linux.
Chrome webbrowser ASID is Supported in Linux and on Android.

# Firmware
See the [firmware changelog](#changelog) for more information on previous releases
### Firmware features
The firmware is still in development
- Custom protocol for playing SID files or usage with emulators
  - An adapted version of Vice emulator is available via de link under examples
  - SidBerry for command line SID file play is available via de link under examples
  - More to come?
- Midi (in) ASID support (heavily inspired by multiple sources)
  - Play SID files in your (midi supporting) browser via [Deepsid](https://deepsid.chordian.net/) by Chordian
  - Play SID files in your (midi supporting) browser via [IneSID](https://inesid.fazibear.me/) by Fazibear
- Single or dual SID support (firmware specific)
  - Remaps known SID addresses to $D400 & $D440
- Dual, triple or quad SID support with SIDKick-Pico (firmware specific)
  - Remaps known SID addresses to $D400, $D420, $D440 & $D460
- Quad SID support (addresses remixed) with SIDKick-Pico (firmware specific)
  - SID2->SID3 SID3->SID2
- Onboard LED acts as VUE meter calculated by the voices of SID1
- Onboard RGB LED acts as second VUE meter calculated by the voices of SID1 and SID2
  - Requires Black Pico clone board!
- Uses the [TinyUSB](https://github.com/hathach/tinyusb) stack
### Planned and in development
__Version: 0.1.0-BETA (UNRELEASED)__
- Midi support in development
  - [x] Basic support is finished
  - [ ] Configurable Midi commands
- WebUSB support in development (Currently uses an adapted version of Hermit's emulator)
  - [x] Proof of concept is finished
  - [ ] Needs better client side emulation
- Improve GPIO handling
  - [x] Add bus control via DMA and PIO for cycle perfect writes and reads
- Customizable config
  - [ ] In development
### ISSUES that need addressing (Any help is welcome)
* Digiplay does not work like it should. This is a driver issue (client side).
  At one point I had this working with Async LIBUSB but forgot to backup it, DOH!
### Building
You can build the firmware using the Pico SDK and TinyUSB

# Hardware
The PCB is still in development
### Where to buy
An assembled board - minus Pico - will be available at PCBWay when v1.0 is finished
### PCB Features
- Supports all MOS SID chips e.g. MOS6581, MOS6582 & MOS8580
- Supports SID chip replacements e.g. [SIDKick-Pico](https://github.com/frntc/SIDKick-pico), [SwinSID](https://github.com/dmantione/swinsid), ARMSID (untested), FPGASID (untested)
- 1 MHz oscillator (external optional, enabled via jumper pad)
  - if no external clock is detected USBSID-Pico will generate a 1 MHz square wave using pio
  - if an external clock is detected will read the external clock signal
- Power via USB
  - 5v stepup for clean 5v V CC to both SID chips
  - 12v or 9v stepup for clean VDD to both SID chips
    - 12v/9v jumper, defautls to 12v ~ inspired by [SIDBlaster-Nano](https://codeberg.org/CBMretro/SIDBlaster-USB_Nano) design
- Audio out filter as in the [C64 schematics](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/c64/250469-rev.A-right.gif)
  - With optional 8580 cap, be sure to leave out/or remove for 8580 SID!
- Audio jumper
  - SID1 audio left & right
  - SID1 audio left & SID2 audio right
### PCB Development
* v1.0 in development
* v0.2 improved testboard<br>
  <img src="https://github.com/user-attachments/assets/7fef5747-a0c0-4f19-8eb0-a5afc29294dd" width="30%">
* v0.1 testboard<br>
  <img src="https://github.com/user-attachments/assets/6e2e1ac0-b5a5-4d62-a71d-1be0ada15490" width="30%">


# Examples
### USBSID-Pico in action
While in development any videos and/or audio links are subject to be changed or updated.
#### MOS8580 chip
| **Mojo<br>Bonzai & Pretzel Logic** | **Spy vs Spy II<br>The Island Caper** |
|:-:|:-:|
| [![MOS8580](https://img.youtube.com/vi/lXxlArB3VS4/1.jpg)](https://www.youtube.com/watch?v=lXxlArB3VS4) | [![MOS8580](https://img.youtube.com/vi/B4iYnZELbSc/1.jpg)](https://www.youtube.com/watch?v=B4iYnZELbSc) |

Visit my [Youtube channel](https://www.youtube.com/channel/UCOu1hPBTsEbG7ZFnk9-29KQ) and other socials or the [SHOWCASE](SHOWCASE.md) page to see more examples.
### Software
_Available examples with USBSID-Pico support_<br>
[**Vice**](https://github.com/LouDnl/Vice-USBSID) fork is available @ https://github.com/LouDnl/Vice-USBSID<br>
[**SidBerry**](https://github.com/LouDnl/SidBerry) fork is available @ https://github.com/LouDnl/SidBerry<br>
[**RetroDebugger**](https://github.com/LouDnl/RetroDebugger) fork is available @ https://github.com/LouDnl/RetroDebugger<br>

# Acknowledgements
Some portions of this code is heavily inspired on project by other great people. Some of those projects are - in no particular order:
* [SIDKICK-pico by Frenetic](https://github.com/frntc/SIDKick-pico)
* [TherapSID by Twisted Electrons](https://github.com/twistedelectrons/TherapSID)
* [TeensyROM by Sensorium Embedded](https://github.com/SensoriumEmbedded/TeensyROM)
* [SID Factory II by Chordian](https://github.com/Chordian/sidfactory2)

# License
### Software License ~ GNUv2
All code written by me in this repository islicensed under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
Any code in this repository that is not written by me automatically falls under the licensing conditions by the authors of said code as mentioned in the source code header.
### Hardware License ~ Creative Commons
<p xmlns:cc="http://creativecommons.org/ns#" xmlns:dct="http://purl.org/dc/terms/"><a property="dct:title" rel="cc:attributionURL" href="https://github.com/LouDnl/USBSID-Pico">USBSID-Pico PCB</a> by <a rel="cc:attributionURL dct:creator" property="cc:attributionName" href="https://github.com/LouDnl">LouD</a> is licensed under <a href="https://creativecommons.org/licenses/by-nc-nd/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/nc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/nd.svg?ref=chooser-v1" alt=""></a></p>

# Changelog
__Version: 0.0.1-ALPHA__
* Basic working functions
  * Multiple firmware binaries per config type
  * SID control via GPIO
  * USB support
    - see examples
  * Midi support
    - messy and unfinished
  * ASID support
    - basic functionality
  * Onboard LED works as Vue
    - RGB LED support for clone Pico boards
