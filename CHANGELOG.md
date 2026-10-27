# Changelog
Please refer to the [releases page](https://github.com/LouDnl/USBSID-Pico/releases) for more information on version changes

* Fix toggle mono/stereo causing a playing freeze
* Add ffwd for the onboard player
* Remove reset SID when already resetting SID registers before the call
* Update web config tool with new USBSID-Player code
* Add Network SID Device (NSD) server over WiFi for pico_w/pico2_w
  - Add Bluetooth Classic SPP transport for the same NSD protocol
  - NSD server uses protocol version 5  
    Add remote enable/disable of FM OPL on a SID socket
  - FM OPL over NSD follows the spec
    - TRY_WRITE_EX/TRY_READ_EX $DF40 = FM OPL address port, $DF50 = data port
    - any other $DFxx address writes nothing and keeps its delay
    - replaces the private form ($DF00 plus OPL register as one entry)
  - TRY_SET_FM_OPL resets FM OPL on enable, BUSY when MIDI or ASID holds the bus
  - Needs USBSID-Player newer than v1.4.0 for FM OPL over NSD
  - Builds with WiFi/Bluetooth NSD get the _net filename suffix
  - Fix TRY_READ_EX accepting an FM OPL address ($DF00-$DFFF) as read target
  - Apply NSD enable, port and discovery settings live, no reboot needed
  - Fix NSD listener failing to bind after a board initiated close
  - Fix NSD pending cycle count race between the two cores
  - Reads and resets wait for queued writes to play out
  - Fix NSD registers $80 and up (SID 5-16) aliasing onto SID 1-4
  - TRY_SET_SID_COUNT accepts 1-16 SIDs (was 1-8)
  - MUTE drops queued writes of the muted voice
  - Increase NSD write ring to 4096 entries
* Add IP-to-Bluetooth NSD bridge example for TCP-only NSD clients
* Add dedicated WebSerial CDC interface for Windows
* Merge Cynthcart into the onboard USBSID-Player builds
  - both get the _em filename suffix
* pico2_w boards have a single em_net filename suffix
* On pico1_w boards the emulator and wireless are too large to run together  
  - these boards have separate builds for _em and _net filename suffixes  
* Add WiFi/Bluetooth NSD commands to commandline configtool
* Add WIFI_READ_IP config command (WiFi state and IPv4 address)
* Add experimental Multiboard clock link (wire RX/PHI2 pin to all boards)
* Add help menu's from commandline tools to dedicated README's
* Change cfg_usbsid and send_sid to use USBSID-Pico-driver
* Fix send_sid not sending the last byte of the play time
* Fix send_sid -sid - losing the tail of the tune and play time 0
* Fix ASID write order (0x30) applied to the wrong registers
* Fix ASID writing $00 to register 0 after boot, 255 cycle waits dropped
* Validate ASID write order packet
* Validate ASID FM OPL message length
* Update SIDKICK-pico clock profile on clock rate change
* Add batch READ of up to 63 registers (firmware 0.8.1+)
* Add experimental SID register stream on vendor interface 7
  - only built with -DENABLE_SID_STREAM=1, sets US_FEATURES bit 6
* Fix SID register reads overwriting the register mirror on real SIDs
  - only $19-$1C are readable, clone chips keep full readback
* Fix USB control request logging printing over UART during play
* Fix read-only config queries claiming the SID bus and never releasing it

#### Version: 0.8.0
* Add full MIDI implementation overhaul
  - add flash-backed MIDI config storage
  - add engine, handler, queue, voice, patch and arp table modules
  - add MIDI clock control and sid bus spinlock
* Add FMOpl (OPL2/3) MIDI control, fixes and new config item support
* Add MIDI patch and arpeggiator table examples
* Add MIDI example tools (CC controller, patch editor)
* Add VMPK/Strudel/TidalCycles MIDI examples
* Migrate embedded Cynthcart to the onboard USBSID-Player, add cc variant
* Add embedded USBSID-Player for Pico and Pico W (rp2040) builds
* Deprecate and remove "Adorable" onboard C64 emulator (emudore) support
* Fix SIDs getting disabled on USB dismount
* Fix last_preset config setting for all PCB versions
* Guard ASID writes during reset and while ASID is disabled
* Fix stale DMA functions
* Add overclock warning (250MHz) for embedded Player and Cynthcart
* Fix compilation error caused by octal-looking version numbers
* Update GitHub and GitLab CI build workflows
* Update firmware, Cynthcart, PCB v1.0/v1.3/v1.5, Configtool and Player manuals
* Add docstrings across source, add extra compiler attributes

#### Version: 0.7.7
* Fix board -> host WebUSB writes to be 64 bytes
* Add mute/clear to prevent lingering OPL audio
* Fix WebUSB config read
* Remove deprecated item

#### Version: 0.7.6
* Add and implement new onboard SID player features
* Add default support for song lengths to send_sid
* Update web config tool
* Update buildtools workflow

#### Version: 0.7.5
* Optimize Vu handling, fix Vu on embedded play without USB
* Fix ASID regression
* FMOpl fixes and added support for new config items
* Fix timing issue in detect_all, fix issue in update_probe_config_from_detection
* Fix FPGASID detection leaving it in config state
* Guard buffer overrun in buffer tick
* Fix SKPico firmware type detection
* Force RP2350 to 250MHz with onboard SID Player
* Add CDC interface guard, remove unused RAM pointers for SID Player
* Fix claim/detach/release/unclaim kernel driver issues in CLI config tool
* Update CLI config tool for new features
* Update config-tool-web with USBSID-Player only handlers
* Add Cartridge Case by spotUp, v1.5 remix by LouD

#### Version: 0.7.4
* Add new USBSID-Player for Web, add player supporting code
* Fix v1.5 regression in SID play after socket change detection
* Update Vendor class (WebUSB) for new web player

#### Version: 0.7.3
* Fix voltage apply when saving config without reboot
* Return last_preset in config buffer
* Set current preset based on current config (v1.5+ boards)
* Add web player URL loading

#### Version: 0.7.2
* Fix FPGASID config address display error
* Fix disable-autodetect feature for the config-tool GUI to work correctly
* Add additional SID player commands
* Add USB command to read compiled USBSID features
* Add v1.3 KiCad files, update v1.3 gerbers/schematic/BOM, add v1.3 OSHWA certification
* Add Volume-daughterboard files (v1.5 hardware), add v1.5 BOM and schematic
* Consolidate build pipelines into a single GitLab CI runner
  - Fix race conditions and toolchain errors in build pipeline
  - Enable Windows and MacOS ARM builds on gitlab.com

#### Version: 0.7.1
* Add init READ/WRITE CHIPCONFIG commands
* Add init READ/WRITE CLONECHIP commands
* Add GET_AUDIO config command
* Enforce stdio_flush after each UART log write
* Add required config commands to header

#### Version: 0.7.0
* Add initial v1.5 PCB support
  - Fix voltage set order to stop clone SIDs misbehaving
  - Fix incorrect packet length on reads (must be 64 bytes for WebUSB)
* [BREAKING CHANGE] Rewrite configuration logic, improve and clear up logging and SID/chip detection
  - Add limited ARMSID, PDSID and BACKSID support
* Add SIDEmu detection and support
* Add FPGASID preset support
* Change midi->SID control to a user defined channel, save settings per channel (like an instrument)
* Enable direct Midi access even when onboard emulator is present
* Rename midi_cc to midi_defs
* Deprecate usbsid_doubletap
* Add Cynthcart VST (by Raros)
* Add first iteration of new webconfig tool
* Header cleanup: internalize externs
* Add stdio_flush during PIO bus setup/sync, fixes memory corruption
* Add workaround for lingering data in the vendor fifo
* Fixes for new SKPico firmware versions
* Stall Core1 until completely finished booting, add Firmware compilation log type

#### Version: 0.6.4-BETA
* Fix onboard SID player playback issues

#### Version: 0.6.3-BETA
* Log USBSID version at end of boot, reset SIDs at end of boot
* Add PDSID detect and SID type config, rework auto detection
* Start rebuilding native Midi
* Fix assertion panic on invalid raster_pio value
* Add webconfig tool source
* Add VirtualMidiPianoKeyboard instrument file for embedded Cynthcart

#### Version: 0.6.2-BETA
* Add socket flip command to config-tool
* Reset SID registers after socket flip/change
* Fix lockups when using register r10 without verification
* Add GitLab CI config
* Add warning/error logging for ASID buffer rate and overflow

#### Version: 0.6.1-BETA
* Add config option to flip sockets at runtime (1 <--> 2)
* Add single SID socket 2 config option
* Re-introduce ASID (ring)buffer for write_ordered messages (e.g. SIDFactoryII), add set_asid_env
* ASID handling overhaul
* Fix hardfault/lockup on RP2350 when toggling the mono/stereo pin
* Fix RP2350 boot freeze waiting on a semaphore
* Fix clockcycle_delay for RP2040 with chained DMA channels, move to bus.c
* Logging overhaul
* Also reset the writeorder when midi/asid is no longer mounted
* Add shortcuts to command line arguments
* Add custom naming parameter to build scripts, add MacOS support

#### Version: 0.6.0-BETA
* Add first iteration of onboard SID player (PRG support, tune skip, buffer)
* Add first iteration of onboard C64 emulator (emudore based Cynthcart)
  - Add shared memory support between core0/core1
  - Increase midi->cynthcart queue size
* Overclock RP2350 for onboard SID player, optimize build to -O3
* Add Pico2 RGB support
* Fix muting of all volume ($18) registers
* Add PDSID type switch command, clone SID config commands, config error check + fallback
* Add non-cycled direct PIO write operation
* Add optional cycled_delayed_write_operation and cycled_write_operation_nondma functions
* Split gpio.c into separate files, add clock counter
* Rework branch prediction, remove endless loop workaround
* Fix SID register reset
* Fix sid_memory not cleared by clear_sid_registers
* Move bus bit related settings into set_bus_bits
* Disable unsupported DMA type on RP2040
* Start re-implementation of Midi handling
* Add first iteration of UART SID writes
* Add build script for CLI config tool
* Update GUI config tool (credits @ISL/Samar)
* Add v1.3 gerbers, update v1.0 BOM, OSHWA certification for v1.0 PCB
* Add Dual USBSID-Pico C64 Case (by OlefinMakes)

#### Version: 0.5.0-BETA
* Add autodetection routine to first boot on new firmware and default firmware settings
* Double tap the reset button to start autodetection routine
  - This only works on Pico1 (rp2040) boards!
* Improve SID and Clone detection
* Add custom Sysex command for Stereo/Mono switch (v1.3 PCB only)
* Add PCB version to USB ProductId
* Disable fifo buffer for USB Vendor class (WebUSB) for faster writes
* Add support for ordered ASID writes
* Start rework ASID handling for sidfactory2 (thanks @thomasj)
* Rework config handling at runtime
* Add initial FPGASID support
* Fix SID reads for FPGASID
* Lots of minor changes and fixes
* Code splicing into separate files for readability

#### Version: 0.4.0-BETA
* Fix SID reads
* Fix bug causing mirrored SID config from working
* Fix ASID/Midi cutoff during play
* Fix ASID play on Pico2 not working
* Fix reset reason not working on Pico2
* Fix Pico2 default config error withing same fw version
* Improve buffer handling
  - migrate for loops into state machines
* Improve SID test runs
  - add option to stop tests
  - run tests from Core 1
* Improve and compress Vu (RGB) LED handling
  - migrate Vu LED PWM into PIO PWM
  - remove use of large RGB lookup table
* Improvemets in SID bus handling
* Improve bootup sequence
* Update TinyUSB API use
* Update NTSC clockspeeds
* Update SDK dependency to 2.1
* Add support for PCB v1.3 audio switch
* Add reserverd flash partition for persistent storage
* Add optional clockrate locking
  - this locks the clockrate in config, it can no longer be changed
* Add workaround fix for SID3 not always working
* Add FMOpl support (Clone SID only)
* Add Pico2W support
* Add auto config website opening on default config

#### Version: 0.3.0-BETA
* USB buffer handling overhaul
  - Add support for incoming packets of variable sizes
  - Add more command types
  - Add commands for WebUSB
* GPIO init overhaul
  - Fix DMA panic on Pico_w
  - Add syncing of PIO's
  - Add stream buffer support
  - Add check for Clone type sid to reset_set SKPico does not handle resets very well
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
* Update web config tool
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
