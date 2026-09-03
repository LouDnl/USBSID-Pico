# Virtual MIDI Piano Keyboard
_Excerpt from VMPK website_
[VMPK](https://vmpk.sourceforge.io/)
Virtual MIDI Piano Keyboard is a MIDI events generator and receiver. It doesn't produce any sound by itself, but can be used to drive a MIDI synthesizer (either hardware or software, internal or external). You can use the computer's keyboard to play MIDI notes, and also the mouse. You can use the Virtual MIDI Piano Keyboard to display the played MIDI notes from another instrument or MIDI file player. To do so, connect the other MIDI port to the input port of VMPK.

## Custom instruments file
VMPK [uspico.ins](https://github.com/LouDnl/USBSID-Pico/tree/master/examples/midi/vmpk/uspico.ins)  
In VMPK you can use a custom instruments file. Below are the contents of my personal custom `uspico.ins` that I use to test USBSID-Pico with VMPK.  
You can load this file on the `Behaviour` tab in the `Edit -> Preferences` menu.  
![vmpk](assets/vmpk-instruments-file.png)  

## Optional extra buttons/sliders/etc.
The contents of [vpmk.extra_controllers.conf](https://github.com/LouDnl/USBSID-Pico/tree/master/examples/midi/vmpk/vpmk.extra_controllers.conf) are an excerpt from the VMPK config file, on linux located in `~/.config/vmpk.sourceforge.net/VMPK.conf`.  
With these you can add extra buttons, sliders, etc. for the available CC commands.
