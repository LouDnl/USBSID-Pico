![USBSID-Pico](images/pcbv1.3.jpg)<br>
![USBSID-Pico](images/usbsid.png)![LouD](images/loud.png)
# USBSID-Pico
USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board for interfacing one or two MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer, phone, ASID supporting player or USB midi controller.  

* [Features](#features)
* [Questions & Support](#questions-and-support)
* [Project status](#project-status)
* [Firmware](#firmware)
  * [Firmware versions](#firmware-versions)
  * [How to flash new firmware](#how-to-flash)
  * [Known issues](#known-issues)
* [Important PCB information](#important-pcb-information)
* [Hardware](#hardware)
  * [Where to buy](#where-to-buy)
  * [Cases](#cases)
  * [Schematic and BOM](#schematic-and-bom)
* [PCB in action](#usbsid-pico-in-action)
* [Software examples](#software)
  * [Pre compiled Vice binaries](#precompiled-vice-binaries)
* [Acknowledgements](#acknowledgements)
* [Disclaimer](#disclaimer)
* [License](#license)
* [Changelog](CHANGELOG.md)

# Features
Information about and explanation of features are _Coming Soon™_  
![USBSID-Pico](images/pcbv0.2.png)
#### SID Playing
USBSID-Pico supports various ways of playing SID files.  
Out-of-the box playing is supported by using [Deepsid by Chordian](https://deepsid.chordian.net)  
by selecting `WebUSB (Hermit)` as player in the pulldown menu or  
by selecting `ASID (MIDI)` as player in the pulldown menu.  
Out-of-the box playing is also supported by using [C64jukebox by Kenchis](https://haendel.ddns.net:8443/static/c64jukebox.vue), note that this is still in BETA.  
[SidBerry](https://github.com/LouDnl/SidBerry) is a command line SID file player for Linux & Windows with up to 4 SIDs supported (Windows version is buggy).  
Unofficial support is added to a fork of [Vice](https://github.com/LouDnl/Vice-USBSID), up to 3 SIDs are supported in vsid and up to 4 in x64sc.  
Unofficial support is added to a fork of [sidplayfp]() which requires a fork of [libsidplayfp]().  
Unofficial support is added to a fork of [RetroDebugger](https://github.com/LouDnl/RetroDebugger), up to 4 SIDs are supported.
##### Amiga
[erique](https://github.com/erique) and [koobo](https://github.com/koobo) have added support to [playsid](https://github.com/erique/playsid.library)
##### C64 Music trackers
You should be able to use the ASID fork of [sidfactory2](https://github.com/Chordian/sidfactory2/tree/asid-support) without issues.  
When using Vice or RetroDebugger you can freely use applications like [SID-Wizard](https://sourceforge.net/projects/sid-wizard/) for music creation.
#### Midi
USBSID-Pico acts as Midi device and supports basic Midi in ~ note that Midi is still in development and in Beta phase.  
This means that no support is available here at this time, please visit the Discord for more information.

# Questions and Support
Any questions about or support with USBSID-Pico can be asked 
* on [Discord](https://discord.gg/zG2rxXuT2g)
* or in the [discussions](https://github.com/LouDnl/USBSID-Pico/discussions).

# Project status
Visit the [Project Page](https://github.com/users/LouDnl/projects/1) for an up-to-date list of things being worked on.
Firmware is in beta stage and still in development.  
While in development any mentioned features, options, etc. are subject to change.  
### Code status
|          | Master             | Dev                    |
| -------  | :-----             | :-----                 |
| Build    | [![release][1]][A] | [![build][2]][B]       |
| Commit   | [![commit][3]][C]  | [![commit][4]][D]      |
|          | **Other**          |
| Latest   | [![tag][5]][E]     | [![release][6]][F]     |
| Open     | [![issues][7]][G]  | [![discussions][8]][H] |
| Info     | [![license][9]][I] | [![language][10]][J]   |

[1]: https://github.com/LouDnl/USBSID-Pico/actions/workflows/release.yml/badge.svg?branch=master
[2]: https://github.com/LouDnl/USBSID-Pico/actions/workflows/build.yml/badge.svg?branch=dev
[3]: https://shields.io/github/last-commit/LouDnl/USBSID-Pico/master
[4]: https://shields.io/github/last-commit/LouDnl/USBSID-Pico/dev
[5]: https://shields.io/github/v/tag/LouDnl/USBSID-Pico?sort=semver
[6]: https://shields.io/github/v/release/LouDnl/USBSID-Pico
[7]: https://shields.io/github/issues/LouDnl/USBSID-Pico
[8]: https://shields.io/github/discussions/LouDnl/USBSID-Pico
[9]: https://shields.io/github/license/LouDnl/USBSID-Pico
[10]: https://shields.io/github/languages/top/LouDnl/USBSID-Pico
[A]: https://github.com/LouDnl/USBSID-Pico/actions/workflows/release.yml
[B]: https://github.com/LouDnl/USBSID-Pico/actions/workflows/build.yml
[C]: https://github.com/LouDnl/USBSID-Pico/commits/master/
[D]: https://github.com/LouDnl/USBSID-Pico/commits/dev/
[E]: https://github.com/LouDnl/USBSID-Pico/tags
[F]: https://github.com/LouDnl/USBSID-Pico/releases
[G]: https://github.com/LouDnl/USBSID-Pico/issues
[H]: https://github.com/LouDnl/USBSID-Pico/discussions
[I]: https://github.com/LouDnl/USBSID-Pico/blob/master/LICENSE
[J]: https://github.com/LouDnl/USBSID-Pico
### Test and config your board
You can configurate your board by using the commandline [config-tool](examples/config-tool) [binary](examples/config-tool/cfg_usbsid) (Linux only at the moment) provided in examples  

You can test your board with WebUSB and ASID here: [USBSID](https://usbsid.loudai.nl/?player=webusb).  
If needed you can change your USBSID configuration after selecting WebUSB and clicking on `Open config`.  
<img src="images/usbsidpico-config-dark.png" width="300px"><img src="images/usbsidpico-config-light.png" width="300px"><br>
_The player is set up with some borrowed code from Deepsid using Hermit's JsSID implementation._

#### Debug functions
For testing purposes only you can use the debug functions available on the [USBSID Debug](https://usbsid.loudai.nl/?player=webusb&debug=usbsidpico) site.

### Supported platforms
_In development_  
Linux: Vice, RetroDebugger, SidBerry, SidplayFp, JSidplay2, USB Midi, ASID (in webbrowser) SID Play  
Windows: Vice, SidBerry, USB Midi, ASID (in webbrowser) SID Play  
Android: USB Midi, ASID (in webbrowser) SID Play

### Linux Udev rules
In the [examples/udev](repo/examples/udev-rules/69-usbsid-permissions.rules) directory you can find the udev rules that I use on Linux. This purely an example file that you can use and change to your own needs.  
Steps required for this to work
```bash
  # Check if you are in the plugdev group
  groups  # should show the plugdev group
  # Copy the udev ules file to the correct directory
  sudo cp 69-usbsid-permissions.rules /etc/udev/rules.d
  # Now reload the udev rules
  udevadm control --reload-rules && udevadm trigger
  # Not working? Try reloading the service
  sudo systemctl restart udev
```

### Windows driver
Use [Zadig](https://zadig.akeo.ie/) to install the correct driver for USBSID-Pico.  
<img src="images/zadig-list-all-devices.png" width="200px">
<img src="images/zadig-install-driver.png" width="200px">  
Then configure check, configure and test your board on the [USBSID](https://usbsid.loudai.nl/?player=webusb) config tool website  
<img src="images/usbsid-config-connect.png" width="200px">
<img src="images/usbsid-config-checkversion.png" width="200px">  

# Firmware
See the [firmware changelog](CHANGELOG.md) for more information on what's changed and previous releases.
### Firmware versions
Check your PCB revision, this is under the MOS logo and next to __USBSID-Pico__ on your PCB.  
Firmware filenames containing `v1.0` are for PCB revision __v1.0__ and filenames containing `v1.3` are for PCB revision __v1.3__. Don't worry if you use the incorrect version, this causes no harm.  
<sub>(The X.X in each filename equals for the PCB revision)</sub>  

`usbsidpico-v1.X.uf2` for Pico1 regular green rp2040 Pico boards.  
`usbsidpico-rgb-v1.X.uf2` for Pico1 black clone rp2040 Pico boards with RGB LED onboard.  
`usbsidpico_w-v1.X.uf2` for PicoW regular green rp2040 PicoW boards.  
`usbsidpico2-v1.X.uf2` for Pico2 regular green rp2350 Pico2 boards.  
`usbsidpico2_w-v1.X.uf2` for Pico2W regular green rp2350 Pico2W boards.  
**WARNING!** Do _NOT_ use the RGB firmware for any of the (non black) rp2040 or rp2350 Pico boards that do not contain an RGB LED.
### How to flash
_<ins>**NOTE**: When flashing a new firmware version, all previously configured settings will be reset to default. Use the commandline configtool to save your current settings to a `ini` file if you want to save them!</ins>_  
A Raspberry Pi Pico board is incredibly easy to flash, as it comes with a built in bootloader for flashing new firmwares in the `uf2` format. 
In order to flash a new firmware to your USBSID-Pico you will need to put the Pico into bootloader mode. This can be done in 2 ways:
1. While the Pico is seated on the USBSID-Pico board and with the USB cable plugged into your computer and the Pico do the following:
  - Press and hold the `BOOTSEL` button on the Pico.
  - Press and release the `RST` button on the USBSID-Pico board.
  - Now release the `BOOTSEL` button.
  - A new drive should appear on your computer called `RPI-RP2`.
  - Copy the correct `uf2` firmware file to this directory.
  - After copying the Pico will reboot and your Pico is flashed.
2. When flashing a Pico that is not seated on the board do the following:
  - Plug in the USB cable to your Pico and not into your computer.
  - While holding the `BOOTSEL` button on the Pico plugin the other end of the USB cable into your computer.
  - Now release the `BOOTSEL` button.
  - A new drive should appear on your computer called `RPI-RP2`.
  - Copy the correct `uf2` firmware file to this directory.
  - After copying the Pico will reboot and your Pico is flashed.
### Firmware features
The firmware is still in development so features might change, be added or removed.
- By default both sockets are enabled and the configuration is set to 2 SID's.
- Custom CDC protocol for playing SID files or usage with emulators
- WebUSB support using the same CDC protocol for WebUSB supporting players
  - Play SID files in your browser via [Deepsid](https://deepsid.chordian.net/) by Chordian
  - Play SID files in your browser via [C64jukebox](https://haendel.ddns.net:8443/static/c64jukebox.vue) by Kenchis
- Midi (in) ASID support (heavily inspired by multiple sources)
  - Play SID files in your (midi supporting) browser via [Deepsid](https://deepsid.chordian.net/) by Chordian
  - Play SID files in your (midi supporting) browser via [IneSID](https://inesid.fazibear.me/) by Fazibear
- Midi device support over USB
  - Use your USBSID-Pico as Synth with your Midi controller
- Two SID sockets with up to 4 SID's (e.g. SKPico) supported
  - Socket one address range $00 ~ $7F (default $00 ~ $1F) auto based on configuration settings
  - Socket two address range $00 ~ $7F (default $40 ~ $7F) auto based on configuration settings
  - Configurable via [config-tool](repo/examples/config-tool) for Linux (Windows still in development)
- Onboard LED acts as VU meter calculated by the voices of SID1 (Pico & Pico2 only)
- Onboard RGB LED acts as second VU meter calculated by the voices of SID1 (default)
  - Requires Black Pico clone board with RGB LED onboard!
  - SID voices to use for calculation can be changed in config
- Uses the [TinyUSB](https://github.com/hathach/tinyusb) stack
### Known issues
* Digiplay is better in Vice then SidplayFp at the moment.  
  While not yet at 100%, most tunes will play!  
  See the [discussion](https://github.com/LouDnl/USBSID-Pico/discussions/1) about this.
### Building
You can build the firmware using the Pico SDK 2.1.1 and the included TinyUSB. Be sure to clone the SDK with `--recurse-submodules`.  
After download run `cd pico-sdk/lib/tinyusb` and then `python3 tools/get_deps.py PICO_PLATFORM` where PICO_PLATFORM is either rp2040 or rp2350 depending on the board you are using.

# Hardware
## Where to buy
#### Licensed resellers
[Run Stop Re-Store](https://www.runstoprestore.nl) at [Retro8BITshop](https://www.retro8bitshop.com) is the first reseller to sell licensed USBSID-Pico boards.  
PCB revision v1.0 [product page](https://www.retro8bitshop.com/product/usbsid-pico-by-loud/)  
PCB revision v1.3 [product page](https://www.retro8bitshop.com/product/usbsid-pico-by-loud/) (to be updated)
#### PCBWay
At a minimum of 5 bare or assembled boards it is also possible to purchase PCB's at PCBWay  
[PCB revision v1.0](https://www.pcbway.com/project/shareproject/USBSID_Pico_c99c9748.html)  
[PCB revision v1.3](https://www.pcbway.com/project/shareproject/USBSID_Pico_v1_3_05f2b88e.html)  
No account yet at [PCBWay](https://pcbway.com/g/2458i7)? Please use [my referral link](https://pcbway.com/g/2458i7) to register, thanks!
#### Me (when I have boards)
If available you can purchase (semi) assembled boards minus Pico from me - send me a message on any of my socials.  

## Important PCB information
### PCB revision v1.0
- [View jumper information (online)](doc/PCBv1.0-INFO.adoc)
- [View jumper information (PDF download)](doc/PCBv1.0-INFO.pdf)
### PCB revision v1.3
- [View jumper information (online)](doc/PCBv1.3-INFO.adoc)
- [View jumper information (PDF download)](doc/PCBv1.3-INFO.pdf)

## Cases
All USBSID-Pico community created cases are available in the [cases](cases/) directory, direct links below.  
_Cases for PCB revision v1.0:_
* [Cartridge case](cases/v1.0/spotUP) by @spotUP
* [boxy case](cases/v1.0/schorsch3000) by @schorsch3000

_Cases for PCB revision v1.3:_
* [spotUP Cartridge case revisited](cases/v1.3/spotUP-revisited) by @LouD

### Schematic and BOM
If you want and are up to it you can solder your own PCB or create your own development board using the documents below
#### v1.0
[v1.0 schematic](resources/v1.0-schematic.pdf) and [v1.0 interactive BOM](https://htmlpreview.github.io/?https://github.com/LouDnl/USBSID-Pico/blob/master/resources/v1.0-ibom.html)
#### v1.3
[v1.3 schematic](resources/v1.3-schematic.pdf) and [v1.3 interactive BOM](https://htmlpreview.github.io/?https://github.com/LouDnl/USBSID-Pico/blob/master/resources/v1.3-ibom.html)
### PCB Features ~ v1.3
Includes all features from v1.0 except the audio jumper
- IO controlled Stereo/Mono switch, can be set in config or toggled during play
- Supports mixed voltage!
  e.g. you can use one MOS6581 (12v) together with one MOS6582 (9v) or MOS8580 (9v)
- Voltage is jumper controlled
- SID socket placement is more spread out for:
  - easier filter capacitor access
  - optional ZIF sockets
- 3 voltage regulators for filtered voltages to the SIDS
  - 1x fixed 5 volts and 2x 9 volts or 12 volts
- Better IO port layout
  - Unused GPIO pins for optional expansion boards
  - IO5 pins for quad SID configuration
  - Uart pins
  - SID Ext in pins (requires closing the solder jumper on the bottom)
  - Ground pin
  - New soldermask art ;)
### PCB Features ~ v1.0
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
* v1.3b release board<br>
  [<img src="images/v1.3b-top.png" width="30%">](images/v1.3b-top.png) 
* v1.2 testboard<br>
  [<img src="images/v1.2-top.png" width="30%">](images/v1.2-top.png) 
* v1.1 unfinished and skipped
* v1.0 release board<br>
  [<img src="images/v1.0-top.png" width="30%">](images/v1.0-top.png) 
* v0.2 improved testboard<br>
  [<img src="images/v0.2-top.png" width="30%">](images/v0.2-top.png) 
* v0.1 testboard<br>
  [<img src="images/v0.1-top.png" width="30%">](images/v0.1-top.png) 

# Examples
### USBSID-Pico in action
Videos and/or audio links are subject to be changed or updated at any time.
| **Van Halen's Jump<br>in Stereo!** | **Flamethrower** |
|:-:|:-:|
| [![Dual MOS8580](https://img.youtube.com/vi/lzvkWlDq2TM/1.jpg)](https://www.youtube.com/watch?v=lzvkWlDq2TM)<br><small><sub>by Nordischsound</sub></small> | [![MOS8580](https://img.youtube.com/vi/Mfbj21ntQXA/1.jpg)](https://www.youtube.com/watch?v=Mfbj21ntQXA)<br><small><sub>by Reed</sub></small> |

Visit my [Youtube channel](https://www.youtube.com/channel/UCOu1hPBTsEbG7ZFnk9-29KQ), [other socials](https://github.com/LouDnl) or the [SHOWCASE](SHOWCASE.md) page to see more examples.
### Software
_Available examples with USBSID-Pico support:_<br>
[**USBSID-Pico driver**](https://github.com/LouDnl/USBSID-Pico-driver) repo is available @ https://github.com/LouDnl/USBSID-Pico-driver<br>
[**HardSID USB / SidBlaster USB**](examples/hardsid-sidblaster) emulation driver example<br>
[**Vice**](https://github.com/LouDnl/Vice-USBSID) fork is available @ https://github.com/LouDnl/Vice-USBSID<br>
[**sidplayfp**](https://github.com/LouDnl/sidplayfp-usbsid) for [**libsidplayfp**](https://github.com/LouDnl/libsidplayfp-usbsid) forks are available @ https://github.com/LouDnl/sidplayfp-usbsid and https://github.com/LouDnl/libsidplayfp-usbsid<br>
[**SidBerry**](https://github.com/LouDnl/SidBerry) fork is available @ https://github.com/LouDnl/SidBerry<br>
[**RetroDebugger**](https://github.com/LouDnl/RetroDebugger) fork is available @ https://github.com/LouDnl/RetroDebugger<br>
[**playsid.library**](https://github.com/erique/playsid.library) Amiga implementation is available @ https://github.com/erique/playsid.library<br>
### Precompiled Vice binaries
Pre compiled Vice binaries are available in my fork @ https://github.com/LouDnl/Vice-USBSID/releases

# Acknowledgements
Special thanks goes out to [Tobozo](https://github.com/tobozo/) for making the USBSID-Pico logo and for his (mental🤣) support since starting this project.  
Thanks to [erique](https://github.com/erique) and [koobo](https://github.com/koobo) for creating and implementing Amiga support!  
Thanks to [Chordian](https://github.com/Chordian) for implementing my crappy webusb solution into deepsid.  
Thanks to [Ken](https://sourceforge.net/u/kenchis/profile/) for adding webusb support to jsidplay2's c64jukebox.

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
