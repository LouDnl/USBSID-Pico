# USBSID-Pico commandline configtool

## Help menu (last update to this file was on 20260911)
```shell
❯ cfg_usbsid -h
------------------------------------------------------------------------------------------------------------------------
USBSID-Pico configtool
For the tool to work correct, this tool requires firmware v0.2.4 and up

Usage:
$ cfg_usbsid [options]
--[OPTIONS]-------------------------------------------------------------------------------------------------------------
  -h,       --help              : Show this help message
  -v,       --version           : Read and print USBSID-Pico firmware version
  -reboot,  --reboot-usp        : Reboot USBSID-Pico
  -boot,    --bootloader        : Reboot USBSID-Pico to the bootloader for firmware upload
  -skpico   --sidkickpico       : Enter SIDKICK-pico config mode (skips any non skpico command following this command)
--[CHIP/SIDS]-----------------------------------------------------------------------------------------------------------
  -sidtest N                    : Run SID test routine on available SID's
                                  0: All, 1: 0x00, 2: 0x20, 3: 0x40, 4: 0x60
  -stoptests                    : Interrupt and stop any running tests
  -auto N,  --auto-detect-all N : Send run autodetection routine command to device and reboot
                                  0: Save, load and apply config
                                  1: Save to flash and reboot (default if not supplied)
  -dsid N,  --detect-sid-types N: Send SID autodetect command to device, returns the config as with '-r' afterwards
                                  Leave N empty for sid detection in both sockets (updates but does not save config)
                                  Provide N to detect with a method below (does not update config, logs to uart)
                                  0: Voice 3 oscillator detection routine
                                  1: Voice 3 waveform detection routine
                                  2: Reflex detection routine
                                  3: Voice 3 delayed waveform detection routine (Some clones)
  -dclone,  --detect-clone-types: Send clone autodetect command to device, returns the config as with '-r' afterwards
--[CONFIG]--------------------------------------------------------------------------------------------------------------
            --config-defaults   : Reset USBSID-Pico config to defaults
                                  Add optional positional argument `1` to reboot USBSID-Pico afterwards
  -r,       --read-config       : Read and print USBSID-Pico config settings
  -rc,      --read-clock-speed  : Read and print USBSID-Pico SID clock speed
  -rs,      --read-sock-config  : Read and print USBSID-Pico socket config settings only
  -rn,      --read-num-sids     : Read and print USBSID-Pico configured number of SID's only
  -rn,      --read-num-sids     : Read and print USBSID-Pico configured number of SID's only
  -ack,     --acknowledge       : Acknowledge the configuration to apply voltage to the sockets (v1.5+ only!)
                                  __MAKE SURE YOU READ AND VERIFY THE CONFIG FIRST!__  (v1.5+ only!)
  -a,       --apply-config      : Apply the current config settings (from USBSID-Pico memory) that you changed with '-w'
  -s,       --save-config       : Send the save config command to USBSID-Pico
  -sr,      --save-reboot       : Send the save config command to USBSID-Pico and reboot it
  -rl,      --reload-config     : Reload the config stored in flash, does not return anything
--[FEATURES]------------------------------------------------------------------------------------------------------------
  -mute                         : Mute all SID's
  -unmute                       : Umute all SID's
  -reset,   --reset-sids        : Reset all SID's
  --reset-sid-registers         : Reset all SID registers
  -flip,    --hotflip-sockets   : (Hot)Flip sockets SocketOne becomes SocketTwo, SocketTwo becomes SocketOne
  -sc N,    --set-clock N       : Set and apply USBSID-Pico SID clock speed
                                  0: 1000000, 1: 985248, 2: 1022727, 4: 1023440
  -lc N,    --lock-clockrate N  : Lock and save the clock rate from changing: True (1) False (0)
  -tau,     --toggle-audio      : Toggle the mono/stereo audio switch (PCB v1.3+ only!)
  -sau,     --set-audio N       : Set and save the mono/stereo audio switch (PCB v1.3+ only!)
                                  0: Mono, 1:Stereo
  -lau N,   --lock-audio N      : Lock and the audio switch in it's current state until reboot: True (1) False (0)
  -sad N,   --sock-autodetect N : Disable/enable and save the automatic socket change detection on boot (PCB v1.5+ only!)
                                  0: Disabled, 1:Enabled
  -pad N,   --preset-detect N   : Disable/enable and save the silent automatic socket detection on preset selection
                                  0: Disabled, 1:Enabled
--[NETWORK]-(ONLY AVAILABLE ON _W PICO'S!)------------------------------------------------------------------------------
  -wifis,   --wifi-status       : Read and print WiFi link / NSD session status
  -rnc,     --read-net-config   : Read and print WiFi/NSD/Bluetooth network config
  -ssid S,  --wifi-ssid S       : Set the WiFi SSID to S
  -psk P,   --wifi-psk P        : Set the WiFi PSK/password to P (write-only, never read back)
  -host H,  --wifi-hostname H   : Set the WiFi hostname to H
  -wen N,   --wifi-enable N     : Enable (1) or Disable (0) WiFi
  -nsden N, --nsd-enable N      : Enable (1) or Disable (0) Network SID Device (NSD)
  -nsdp N,  --nsd-port N        : Set the Network SID Device port to N
  -wapply,  --wifi-apply        : Persist WiFi/NSD config to flash and apply live
  -wforget, --wifi-forget       : Erase stored WiFi credentials and reset network config to defaults
  -bten N,  --bt-nsd-enable N   : Enable (1) or Disable (0) Bluetooth Network SID Device (SPP transport)
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
                                  0: 1000000, 1: 985248, 2: 1022727, 4: 1023440
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
--[OTHER]---------------------------------------------------------------------------------------------------------------
  -d,       --debug             : Enable debug prints
  -configr N                    : Read back N bytes from a write config command, use _before_ `-config`
                                  Example: `cfg_usbsid -configr 1 -config CMD`
  -config   --config-command    : Send custom config command, requires followup hex strings
  -command                      : Send arbitrary command in hex, requires followup hex strings
  -control                      : Send libusb control transfer
-----------------------------------------------------------------------------------------------------------------------
```
