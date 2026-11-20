# USBSID-Pico commandline configtool

## Help menu
```shell
❯ cfg_usbsid -h
------------------------------------------------------------------------------------------------------------------------
USBSID-Pico commandline configtool
For the tool to work correct, this tool requires firmware v0.2.4 and up

Usage:
$ cfg_usbsid [arguments]

Some commands can be combined in a single call, there is no need to run the tool once per command.
Commands marked with a ¹ can be combined and run in the order given, for example:
  $ cfg_usbsid -sc 1 -lc 1 -sau 1 -s
Commands without a ¹ run alone or must come last, they end the run or reboot the board.
A '-s' is deferred: it saves once after all other commands, wherever it is given.

--[OPTIONS]-------------------------------------------------------------------------------------------------------------
  -h,       --help              : Show this help message
  -h itm,   --help item         : Show item help message, items are:
                                  options,sidchips,config,features,network,presets,ini,manual,midi,other
                                  or the first three letters of each item
¹ -v,       --version           : Read and print USBSID-Pico firmware version
  -lb,      --list-boards       : List every attached board's serial number and exit
  -b=ID,    --board=ID          : Connect to board ID (index from -lb) or serial, default first board
  -reboot,  --reboot-usp        : Reboot USBSID-Pico
  -boot,    --bootloader        : Reboot USBSID-Pico to the bootloader for firmware upload
  -skpico   --sidkickpico       : Enter SIDKICK-pico config mode (skips any non skpico command following this command)
--[CHIP/SIDS]-----------------------------------------------------------------------------------------------------------
  -sidtest N                    : Run SID test routine on available SID's
                                  0: All, 1: 0x00, 2: 0x20, 3: 0x40, 4: 0x60
  -stoptests                    : Interrupt and stop any running tests
¹ -auto N,  --auto-detect-all N : Send run autodetection routine command to device and reboot
                                  0: Save, load and apply config
                                  1: Save to flash and reboot (default if not supplied)
¹ -dsid N,  --detect-sid-types N: Send SID autodetect command to device, returns the config as with '-r' afterwards
                                  Leave N empty for sid detection in both sockets (updates but does not save config)
                                  Provide N to detect with a method below (does not update config, logs to uart)
                                  0: Voice 3 oscillator detection routine
                                  1: Voice 3 waveform detection routine
                                  2: Reflex detection routine
                                  3: Voice 3 delayed waveform detection routine (Some clones)
¹ -dclone,  --detect-clone-types: Send clone autodetect command to device, returns the config as with '-r' afterwards
--[CONFIG]--------------------------------------------------------------------------------------------------------------
¹ -r,       --read-config       : Read and print USBSID-Pico config settings
¹ -rc,      --read-clock-speed  : Read and print USBSID-Pico SID clock speed
¹ -rs,      --read-sock-config  : Read and print USBSID-Pico socket config settings only
¹ -rn,      --read-num-sids     : Read and print USBSID-Pico configured number of SID's only
¹ -rn,      --read-num-sids     : Read and print USBSID-Pico configured number of SID's only
¹ -ack,     --acknowledge       : Acknowledge the configuration to apply voltage to the sockets (v1.5+ only!)
                                  __MAKE SURE YOU READ AND VERIFY THE CONFIG FIRST!__  (v1.5+ only!)
¹ -a,       --apply-config      : Apply the current config settings (from USBSID-Pico memory) that you changed with '-w'
¹ -s,       --save-config       : Send the save config command to USBSID-Pico
  -sr,      --save-reboot       : Send the save config command to USBSID-Pico and reboot it
¹ -rl,      --reload-config     : Reload the config stored in flash, does not return anything
¹           --config-defaults   : Reset USBSID-Pico config to defaults
                                  Add optional positional argument `1` to reboot USBSID-Pico afterwards
  Combine:                        `cfg_usbsid -r -rc -rs -rn` or `cfg_usbsid -a -s`
  Save:                           '-s' saves once after all other commands, wherever it is given
                                  Saved by the command itself: -lc, -sau, -sad, -pad, -ack, -sapp and the presets
                                  Needs '-s': -sc, -tau, -lau, -flip, -dsid, -dclone and all '-w' settings
                                  Has its own save: WiFi/NSD with -wapply, USB network/NSD with -unapply, MIDI with -mss
--[FEATURES]------------------------------------------------------------------------------------------------------------
¹ -mute                         : Mute all SID's
¹ -unmute                       : Umute all SID's
¹ -reset,   --reset-sids        : Reset all SID's
¹ --reset-sid-registers         : Reset all SID registers
¹ -flip,    --hotflip-sockets   : (Hot)Flip sockets SocketOne becomes SocketTwo, SocketTwo becomes SocketOne
¹ -sc N,    --set-clock N       : Set and apply USBSID-Pico SID clock speed
                                  0: 1000000 (Default), 1: 985248 (PAL), 2: 1022727 (NTSC),
                                  3: 1023440 (DREAN), 4: 1022730 (NTSC2)
  -lclk N,  --link-clock N      : Link the clock to the external clock input, the board reboots on change: 1/0
¹ -lc N,    --lock-clockrate N  : Lock and save the clock rate from changing: True (1) False (0)
¹ -tau,     --toggle-audio      : Toggle the mono/stereo audio switch (PCB v1.3+ only!)
¹ -sau,     --set-audio N       : Set and save the mono/stereo audio switch (PCB v1.3+ only!)
                                  0: Mono, 1:Stereo
¹ -lau N,   --lock-audio N      : Lock and the audio switch in it's current state until reboot: True (1) False (0)
¹ -sad N,   --sock-autodetect N : Disable/enable and save the automatic socket change detection on boot (PCBv1.5+ only!)
                                  0: Disabled, 1:Enabled
¹ -pad N,   --preset-detect N   : Disable/enable and save the silent automatic socket detection on preset selection
                                  0: Disabled, 1:Enabled
  -rxring N, --rx-ring N        : Disable/enable and save the USB RX ringbuffer, the board reboots on change
                                  0: Disabled, 1:Enabled
  Combine:                        `cfg_usbsid -sc 1 -lc 1 -sau 1 -sad 1 -pad 1`
--[NETWORK WIFI/BLUETOOTH]-(ONLY AVAILABLE ON _W PICO'S!)---------------------------------------------------------------
¹ -wifis,   --wifi-status       : Read and print WiFi link / NSD session status (and WiFi state / IP if supported)
¹ -rnc,     --read-net-config   : Read and print WiFi/NSD/Bluetooth network config (and IP if supported)
¹ -ssid S,  --wifi-ssid S       : Set the WiFi SSID to S
¹ -psk P,   --wifi-psk P        : Set the WiFi PSK/password to P (write-only, never read back)
¹ -host H,  --wifi-hostname H   : Set the WiFi hostname to H
¹ -wen N,   --wifi-enable N     : Enable (1) or Disable (0) WiFi
¹ -bten N,  --bt-nsd-enable N   : Enable (1) or Disable (0) Bluetooth Network SID Device (SPP transport)
¹ -wapply,  --wifi-apply        : Persist the network config (WiFi/NSD) to flash and apply live
¹ -wforget, --wifi-forget       : Erase stored WiFi credentials and reset network config to defaults
  Combine:                        every option above, `-wapply` last to persist and apply, for example
                                  `cfg_usbsid -ssid NAME -psk PASS -host usbsid -wen 1 -wapply`
--[NETWORK NSD]-(REQUIRES NSD SUPPORT IN THE FIRMWARE)------------------------------------------------------------------
¹ -nsden N, --nsd-enable N      : Enable (1) or Disable (0) Network SID Device (NSD)
¹ -discen N,--disc-enable N     : Enable (1) or Disable (0) UDP discovery, applied live, persisted by -wapply or -unapply
¹ -nsdp N,  --nsd-port N        : Set the Network SID Device port to N
  Persist with `-wapply` (WiFi) or `-unapply` (USB network), for example
                                  `cfg_usbsid -nsden 1 -discen 1 -nsdp 6581 -wapply`
--[NETWORK USB]-(ONLY AVAILABLE WITH CUSTOM FIRMWARE!)------------------------------------------------------------------
¹ -unipm <server|dhcp|static>,
    --usbnet-ip-mode M          : Set the USB network (NCM) IP mode (or 2/0/1), persisted and applied by -unapply
                                  server (default): board 192.168.65.1/24, serves the USB link only
                                  dhcp: needs a host bridge from the USB net interface to the LAN
                                  static: needs host routing/proxy ARP or a bridge (set addresses with -unst)
                                  Do not bridge the USB net interface into a LAN while in server mode
¹ -unst I,N,G,
    --usbnet-static I,N,G       : Set the USB network (NCM) static IP, netmask and gateway (dotted, gateway optional),
                                  persisted and applied by -unapply
¹ -unipc,   --usbnet-ipcfg      : Read and print the USB network (NCM) IP mode and stored static IP config
¹ -unapply, --usbnet-apply      : Persist the network config (USB network/NSD) to flash and apply live, same as -wapply
¹ -unforget,--usbnet-forget     : Reset the network config to defaults and persist it, same as -wforget
  Combine:                        `cfg_usbsid -unipm static -unst 192.168.65.2,255.255.255.0 -unapply`
--[PRESETS]-------------------------------------------------------------------------------------------------------------
  -single,  --single-sid        : Socket 1 enabled @ single SID, Socket 2 disabled
  -single2, --single-sid-s2     : Socket 1 disabled, Socket 2 enabled @ single SID
  -dual,    --dual-sid          : Socket 1 enabled @ single SID, Socket 2 enabled @ single SID
  -duals1,  --dual-sid-socket1  : Socket 1 enabled @ dual SID, Socket 2 disabled
  -duals2,  --dual-sid-socket2  : Socket 1 disabled, Socket 2 enabled @ dual SID
  -triple1, --triple-sid1       : Socket 1 enabled @ dual SID, Socket 2 enabled @ single SID
  -triple2, --triple-sid2       : Socket 1 enabled @ single SID, Socket 2 enabled @ dual SID
  -quad,    --quad-sid          : Socket 1 enabled @ dual SID, Socket 2 enabled @ dual SID
  -mirrored,--mirrored-sid      : Socket 1&2 enabled @ single SID, each socket receives the same writes
  -dualmirror                   : Socket 1&2 enabled @ dual SID, each socket receives the same writes
  -dualflipped                  : Socket 1&2 enabled @ single SID, Socket 2 is primary socket
  -quadflipped                  : Socket 1&2 enabled @ dual SID, Socket 2 is primary socket
  -quadmixed                    : Socket 1&2 enabled @ dual SID, Mix up the SID addresses
  (Quad SID only!)                Socket 1 SIDS @ $00 & $40, Socket 2 SIDS @ $20, $60 
  -qflipmixed                   : Socket 1&2 enabled @ dual SID, Socket 2 is primary socket & Mix up the SID addresses
  (Quad SID only!)                Socket 1 SIDS @ $20 & $60, Socket 2 SIDS @ $00, $40 
--[INI FILE CONFIGURATION]----------------------------------------------------------------------------------------------
  -default, --default-ini       : Generate an ini file with default USBSID-Pico config named `USBSID-Pico-cfg.ini`
  -export F,--export-config F   : Read config from USBSID-Pico and export it to provided ini file or default in
                                  current directory when not provided
  -import F,--import-config F   : Read config from provided ini file, write to USBSID-Pico, send save command
                                  and read back config for a visual confirmation
--[MANUAL CONFIGURATION]------------------------------------------------------------------------------------------------
  All the following options require '-w'
  Please do not forget to use '-a' or '-s' after writing config settings manually
  this is required for the settings to have any effect after a change
  -c N,     --sid-clock N       : Change SID clock to
                                  0: 1000000 (Default), 1: 985248 (PAL), 2: 1022727 (NTSC),
                                  3: 1023440 (DREAN), 4: 1022730 (NTSC2)
  -l N,     --lockclockrate N   : Lock the clock rate from changing: True (1) False (0)
  -led N,   --led-enabled N     : LED is Enabled (1) or Disabled (0)
  -lbr N,   --led-breathe N     : LED idle breathing is Enabled (1) or Disabled (0)
  -rgb N,   --rgb-enabled N     : RGBLED is Enabled (1) or Disabled (0)
  -rgbbr N  --rgb-breathe N     : RGBLED idle breathing is Enabled (1) or Disabled (0)
  -rgbsid N,--rgb-sidtouse N    : Set the SID number the RGBLED uses (1, 2, 3 or 4)
  -br N,    --rgb-brightness N  : Set the RGBLED Brightness to N (0 ~ 255)
  -fm N,    --fmopl-enabled N   : FMOpl is Enabled (1) or Disabled (0)
   (Requires a socket set to Clone chip and chiptype to FMOpl)
  -au ,     --audio-switch N    : Set the mono/stereo audio switch (PCB v1.3+ only!)
                                  0: Mono, 1:Stereo
  -la N,    --lockaudio N       : Lock the audio switch from being changed: True (1) False (0)
  -sock N,  --socket N          : Configure socket N ~ 1 or 2
  The following options additionally require '-sock N'
  Note that you can only configure 1 socket at a time!
  -en N,    --enabled N         : Socket is Enabled (1) or Disabled (0)
  -ds N,    --dualsid N         : DualSID is On (1) or Off (0)
  -chip N,  --chiptype N        : Set the socket chiptype to:
                                  0: MOS, 1: Unknown, 2: SKPico, 3: ARMSID, 4: ARM2SID
                                  5: FPGASID, 6: RedipSID, 7: PDSID, 8: BackSID
  -sid1 N,  --sid1type N        : Set the 1st SID type for the socket to:
  -sid2 N,  --sid2type N        : Set the 2nd SID type for the socket to:
                                  0: Unknown, 1: N/A, 2: 8580, 3: 6581, 4: FMOpl
                                  (Available for 'Clone' chiptype only!)
  -a1 N,    --as-one N          : Socket 2 mirrors socket 1 (Socket 2 setting only!)
                                  Enabled (1) or Disabled (0)
  Combine:                        `cfg_usbsid -w -c 1 -led 1 -rgb 1 -br 64 -a`
                                  `cfg_usbsid -w -sock 1 -en 1 -chip 1 -a`
--[MIDI]----------------------------------------------------------------------------------------------------------------
¹ -mls,     --midi-load-state   : Load the saved MIDI config (channels, patches, arp tables, CC map) from flash
¹ -mss,     --midi-save-state   : Save the current MIDI config to flash
¹ -mrs,     --midi-reset-state  : Reset the MIDI config to defaults and erase all saved MIDI config slots
¹ -sapp N,  --set-c64-app N     : Set and save the embedded C64 MIDI app (embedded emulator only!)
                                  0: Cynthcart, 1: SID SYNTH
¹ -startapp,   --start-c64-app  : Start the configured C64 MIDI app (embedded emulator only!)
¹ -stopapp,    --stop-c64-app   : Stop the configured C64 MIDI app (embedded emulator only!)
¹ -restartapp, --restart-c64-app: Restart the configured C64 MIDI app (embedded emulator only!)
  Combine:                        `cfg_usbsid -sapp 1 -mss -startapp`
--[OTHER]---------------------------------------------------------------------------------------------------------------
¹ -d,       --debug             : Enable debug prints
  -configr N                    : Read back N bytes from a write config command, use _before_ `-config`
                                  Example: `cfg_usbsid -configr 1 -config CMD`
  -config   --config-command    : Send custom config command, requires followup hex strings
  -command                      : Send arbitrary command in hex, requires followup hex strings
------------------------------------------------------------------------------------------------------------------------
```
