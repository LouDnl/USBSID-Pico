# Changelog

#### Version: 0.3.0-BETA (Unreleased)
* ...
* Beam me up Scotty!
* Continue work on Midi handling
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
