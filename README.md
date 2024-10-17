![USBSID-Pico](images/pcbv1.0.jpg)<br>
![USBSID-Pico](images/usbsid.png)![LouD](images/loud.png)
# USBSID-Pico
USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer, phone, ASID supporting player or USB midi controller.  

* [Project status](#project-status)
* [Firmware](#firmware)
* [Hardware](#hardware)
* [Important PCB information](#important-pcb-information)
* [Schematic and BOM](#schematic-and-bom)
* [PCB in action](#usbsid-pico-in-action)
* [Software examples](#software)
* [Acknowledgements](#acknowledgements)
* [Disclaimer](#disclaimer)
* [License](#license)
* [Changelog](#changelog)

# Features
Information about and explanation of features are _Coming Soon™_  
![USBSID-Pico](images/pcbv0.2.png)

# Questions & Support
Any questions about or support with USBSID-Pico can be asked in the [discussions](https://github.com/LouDnl/USBSID-Pico/discussions).

# Project status
PCB has reached v1.0 🥳  
Firmware is in beta stage and still in development.  
While in development any mentioned features, options, etc. are subject to change.
### Supported platforms
_In development_  
Linux: Vice, RetroDebugger, SidBerry, JSidplay2, USB Midi, ASID (in webbrowser) SID Play  
Windows/Android: USB Midi, ASID (in webbrowser) SID Play

# Firmware
See the [firmware changelog](#changelog) for more information on what's changed and previous releases.
### Firmware versions
Use `usbsidpico.uf2` for regular green rp2040 Pico boards.  
Use `usbsidpico-rgb.uf2` for black clone rp2040 Pico boards with RGB LED onboard.  
**WARNING!** Do _NOT_ use the RGB firmware for the regular green rp2040 Pico boards.
### Firmware features ~ version 0.2.0-BETA
The firmware is still in development so features might change, be added or removed.
- By default both sockets are enabled and the configuration is set to 2 SID's.
- Custom CDC protocol for playing SID files or usage with emulators
- Midi (in) ASID support (heavily inspired by multiple sources)
  - Play SID files in your (midi supporting) browser via [Deepsid](https://deepsid.chordian.net/) by Chordian
  - Play SID files in your (midi supporting) browser via [IneSID](https://inesid.fazibear.me/) by Fazibear
- Midi device support over USB
  - Use your USBSID-Pico as Synth with your Midi controller
- Two SID sockets with up to 4 SID's (e.g. SKPico) supported
  - Socket one address range $00 ~ $3F (default $00 ~ $1F)
  - Socket two address range $20 ~ $7F (default $20 ~ $3F)
  - Configurable (platform independent (Linux/Windows) tool still in development)
- Onboard LED acts as VU meter calculated by the voices of SID1
- Onboard RGB LED acts as second VU meter calculated by the voices of SID1 (default)
  - Requires Black Pico clone board with RGB LED onboard!
  - SID voices to use for calculation can be changed in config
- Uses the [TinyUSB](https://github.com/hathach/tinyusb) stack
### ISSUES that need addressing (Any help is welcome)
* Digiplay does not work like it should. This is a driver issue (client side).
  At one point I had this working with Async LIBUSB but forgot to backup it, DOH!  
  See the [discussion](https://github.com/LouDnl/USBSID-Pico/discussions/1) about this.
### Building
You can build the firmware using the Pico SDK 2.0.0 and TinyUSB from it's Github repo, not the one included in the SDK!

# Hardware
### Where to buy
You can buy assembled boards (minus Pico) while available from me - send me a message on any of my socials - or at [PCBWay here](https://www.pcbway.com/project/shareproject/USBSID_Pico_c99c9748.html).  
Ordering at PCBWay will require you to order atleast 5 boards + assembly.
### PCB Features
- Supports all MOS SID chips e.g. MOS6581, MOS6582 & MOS8580
- Supports SID chip replacements e.g. [SIDKick-Pico](https://github.com/frntc/SIDKick-pico), [SwinSID](https://github.com/dmantione/swinsid), ARMSID (untested), FPGASID (untested)
- 1 MHz oscillator (external optional, enabled via jumper pad)
  - if no external clock is detected USBSID-Pico will generate a 1 MHz square wave using pio
    - clock speed is configurable
  - if an external clock is detected will read the external clock signal
- Power via USB
  - 5v stepup for clean 5v VCC to both SID chips
  - 12v or 9v stepup for clean VDD to both SID chips
    - 12v/9v jumper, defautls to 12v ~ inspired by [SIDBlaster-Nano](https://codeberg.org/CBMretro/SIDBlaster-USB_Nano) design
- Audio out filter as in the [C64 schematics](https://www.zimmers.net/anonftp/pub/cbm/schematics/computers/c64/250469-rev.A-right.gif)
  - With optional 6581 resistor, solder the 6581 (mislabeled 8580!) jumper pad in each audio circuit for this.
- Audio jumper
  - SID1 audio left & right
  - SID1 audio left & SID2 audio right
- Optional EXT-IN pulldown resistor as filter bypass to reduce filter noise for Digiplay on 8580 SID's
### PCB Development
* v1.0 release board<br>
  <img src="images/v1.0-top.png" width="30%">
* v0.2 improved testboard<br>
  <img src="images/v0.2-top.png" width="30%">
* v0.1 testboard<br>
  <img src="images/v0.1-top.png" width="30%">

# Important PCB information
[<img src="images/v1.0-explained.png" width="30%">](images/v1.0-explained.png)<br/>
_Click image for larger view_
1. 12 volt or 9 volt selection jumper
  - open = 12 volt (for 6581 SID only!!)
  - closed = 9 volt (for 8580 SID)
2. Socket 2 6581 / 8580 SID type selection jumpers
  - both closed left = 6581
  - both closed right = 8580
3. Socket 1 6581 / 8580 SID type selection jumpers
  - both closed left = 6581
  - both closed right = 8580
4. Audio channel selection jumper
  - closed on Socket 1 side = left & right channel for socket 1
  - closed on Socket 2 side = left channel for socket 2 & right channel for socket 1
5. Address line A5 for adressess above $1F (SKPico 2nd SID for example)
6. Uart debugging output port
7. Optional 330k pulldown resistor hooked up to EXT-IN for 8580 filter bypass in socket 2
  - solder closed = enabled
8. Optional 330k pulldown resistor hooked up to EXT-IN for 8580 filter bypass in socket 1
  - solder pad closed = enabled
9. Optional 1MHz crystal socket (not included in BOM)
  - solder pad closed = enabled (when a crystal is socketed) 
  - this disables the internal clock generation on the Pico
10. Optional 1k resistor for 6581 SID in socket 2
  - ATTENTION: This solder pad label is incorrect, it should read 6581
  - solder pad closed = enabled
11. Optional 1k resistor for 6581 SID in socket 2
  - ATTENTION: This solder pad label is incorrect, it should read 6581
  - solder pad closed = enabled
12. Socket 2 audio out via dupont connector
  - Ground and S2 as labeled
13. Socket 1 audio out via dupont connector
  - Ground and S1 as labeled
14. Reset button

# Schematic and BOM
If you're up to it you can create your own development board by using the provided [schematic](resources/v1.0-schematic.pdf) and [interactive BOM](https://loudnl.github.io/).

# Examples
### USBSID-Pico in action
While in development any videos and/or audio links are subject to be changed or updated.
#### MOS8580 chip
| **Layers<br>Finnish Gold** | **Mojo<br>Bonzai & Pretzel Logic** | **Spy vs Spy II<br>The Island Caper** |
|:-:|:-:|:-:|
| [![MOS8580](https://img.youtube.com/vi/UQVDTNV3mgs/1.jpg)](https://www.youtube.com/watch?v=UQVDTNV3mgs) | [![MOS8580](https://img.youtube.com/vi/lXxlArB3VS4/1.jpg)](https://www.youtube.com/watch?v=lXxlArB3VS4) | [![MOS8580](https://img.youtube.com/vi/B4iYnZELbSc/1.jpg)](https://www.youtube.com/watch?v=B4iYnZELbSc) |

Visit my [Youtube channel](https://www.youtube.com/channel/UCOu1hPBTsEbG7ZFnk9-29KQ), [other socials](https://github.com/LouDnl) or the [SHOWCASE](SHOWCASE.md) page to see more examples.
### Software
_Available examples with USBSID-Pico support:_<br>
[**HardSID USB / SidBlaster USB**](examples/hardsid-sidblaster) driver example<br>
[**Vice**](https://github.com/LouDnl/Vice-USBSID) fork is available @ https://github.com/LouDnl/Vice-USBSID<br>
[**SidBerry**](https://github.com/LouDnl/SidBerry) fork is available @ https://github.com/LouDnl/SidBerry<br>
[**RetroDebugger**](https://github.com/LouDnl/RetroDebugger) fork is available @ https://github.com/LouDnl/RetroDebugger<br>

# Acknowledgements
Special thanks goes out to [Tobozo](https://github.com/tobozo/) for making the USBSID-Pico logo and for his (mental🤣) since starting this project.

Some portions of this code and board are heavily inspired on projects by other great people.
Some of those projects are - in no particular order:
* [SIDKICK-pico by Frenetic](https://github.com/frntc/SIDKick-pico)
* [SIDBlaster USB Nano by CBMretro](https://codeberg.org/CBMretro/SIDBlaster-USB_Nano)
* [TherapSID by Twisted Electrons](https://github.com/twistedelectrons/TherapSID)
* [TeensyROM by Sensorium Embedded](https://github.com/SensoriumEmbedded/TeensyROM)
* [SID Factory II by Chordian](https://github.com/Chordian/sidfactory2)
* [Cynthcart by PaulSlocum](https://github.com/PaulSlocum/cynthcart)

# Disclaimer
I do this stuff in my free time for my enjoyment. Since I like to share my joy in creating this with everyone I try my best to provide a working PCB and Firmware. I am in no way an electronics engineer and can give __no guarantee__ that this stuff does not break or damage your hardware, computer, phone, or whatever you try to hook it up to. Be sure to take great care when inserting any real MOS SID chips into the board. While everything has been tested with real chips, this is in no way a guarantee that nothing could go wrong. Use of this board and firmware at your own risk! I am in no way responsible for your damaged hardware. That being said, have fun!

# License
### Software License ~ GNUv2
All code written by me in this repository islicensed under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
Any code in this repository that is not written by me automatically falls under the licensing conditions by the authors of said code as mentioned in the source code header.
### Hardware License ~ Creative Commons
<p xmlns:cc="http://creativecommons.org/ns#" xmlns:dct="http://purl.org/dc/terms/"><a property="dct:title" rel="cc:attributionURL" href="https://github.com/LouDnl/USBSID-Pico">USBSID-Pico PCB</a> by <a rel="cc:attributionURL dct:creator" property="cc:attributionName" href="https://github.com/LouDnl">LouD</a> is licensed under <a href="https://creativecommons.org/licenses/by-nc-nd/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/nc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/nd.svg?ref=chooser-v1" alt=""></a></p>

# Changelog
__Version: 0.2.0-BETA__
* Basic Midi support
  * Fixed to 6 voices polyfonic with 2 SID's up to 12 voices with 2x SKPico.
* WebUSB support
  * Proof of concept available in examples folder
  * Currently uses an adapted version of Hermit's emulator
* Improved GPIO handling
  * Bus control via DMA and PIO for cycle exact writes and reads
* Customizable config (config tool in development) 
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
  * Onboard LED works as VU
    - RGB LED support for clone Pico boards
