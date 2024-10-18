# Changelog

#### Version: 0.2.1-BETA (UNRELEASED)
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
