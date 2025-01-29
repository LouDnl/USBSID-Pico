# Changelog

#### Version: 0.2.?-BETA (SNAPSHOT)
* ...
* Beam me up Scotty!
* Continue work on Midi handling

#### Version: 0.2.4-BETA
* USB buffer handling overhaul
  - Add support for incoming packets of variable sizes
  - Add more command types
  - Add commands for WebUSB
* GPIO init overhaul
  - Fix DMA panic on Pico_w
  - Add syncing of PIO's
  - Add stream buffer support
  - Add check for Clone type sid to reset_set
    SKPico does not handle resets very well
  - Move SID clock functions to GPIO
* PIO bus overhaul
  - Add support for clock cycle writes
* Add 4x SID support to ASID handler
* Rework config handling
  - Add optional command for retrieving socket config only
  - Split apply_config into separate functions
  - Update/Fix SID type detection
    - Add delays between detection commands
    - Add retry to try 3 times if detection fails
* General code rework
  - Move util functions into separate util file
* Update examples
  - Add cli config-tool
  - Add USBSID-Pico-driver repo
  - Add SidplayFp repo
  - Add libSidplayFp repo

#### Version: 0.2.3-BETA
* Add workflow for auto version release build
* Fix issue in SID tests not stopping
* Add Support for Pico2 (rp2350) green board
* Add Support for PicoW (rp2040) green board
  - NOTE: Due to the LED being used by the WiFi chip there is no VU support
* Rename Vue to Vu 🤦‍♀️
* Remove `__builtin_` function calls

#### Version: 0.2.2-BETA
* Add direct SID functions
  - Detect SID type
  - Test SID voices
* Add config commands for SID testing functions
* Add function to clear SID registers without SID reboot
* Add some debug logging
* Update config handling
  - Add direct SID functions
  - Add version retrieval support
  - Check if set clockspeed is correct or se default
  - Fix write functions
  - Fix some values on SID count change
* Update USB descriptors
  - Add version in manufacturer string
* Enable CC's for bank 9 (temporary workaround)
* Refactor some whitespace
* Start work on support for PicoW and Pico2 boards

#### Version: 0.2.1-BETA
* Start on update for Midi handling
  * Details will follow at a later time when finished
* Fix ASID play
* Fix config save error

#### Version: 0.2.0-BETA
* Basic Midi support
  * Fixed to 6 voices polyfonic with 2 SID's up to 12 voices with 2x SKPico.
* WebUSB support
  * Proof of concept available in examples folder
  * Currently uses an adapted version of Hermit's emulator
* Improved GPIO handling
  * Bus control via DMA and PIO for cycle exact writes and reads
* Customizable config (config tool in development) 

#### Version: 0.0.1-ALPHA
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
