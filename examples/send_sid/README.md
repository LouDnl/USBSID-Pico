# USBSID-Pico commandline send_sid tool

## Help menu (last update to this file was on 20260911)
```shell
❯ send_sid
Please supply atleast 1 option!
*** Usage ***

-help / -h: Show this information

  sidfile.sid: send sidfile.sid to USBSID-Pico to start play
  sidtune.prg: send sidtune.prg to USBSID-Pico to start play (psid64 preferred!)
  -sid -: to read _SID_ file data from stdin instead of sidfile.sid (PRG not supported yet!)
  -t N: provide subtune number together with sid to set subtune (defaults to 1))
  -f: Force play on second SID / socket (depends on USBSID-Pico configuration)
  -stop: stop play
  -next: play next subtune
  -prev: play previous subtune
  --songlengths F:  HVSC Songlengths database, to stop when the song ends.
                    Found by itself in $SONGLENGTHS, ~/Songlengths.md5,
                    HVSCROOT or $HVSC_BASE DOCUMENTS/Songlengths.md5, or $HVSCDB.

Play SID file from local storage
./send_sid /path/to/sidfile.sid -t 1

Play PRG file from local storage
./send_sid /path/to/sidfile.prg

Play SID file directly from internet storage
SID=Wavemode_Mainpart.sid ;\
  curl -sS 'https://deepsid.chordian.net/hvsc/_SID%20Happens/'$SID |\
  ./send_sid -sid -
```
