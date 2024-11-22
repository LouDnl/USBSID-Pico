# Index
* [Strudel.cc](#strudelcc)
* [Notes](#notes)
* [Strudel examples](#strudel-examples)
  - [progNum](#prognum)

# Strudel.cc
_Excerpt from Strudel website_
[Getting Started](https://strudel.cc/workshop/getting-started/)
With Strudel, you can expressively write dynamic music pieces.
It is an official port of the [Tidal Cycles](https://tidalcycles.org/) pattern language to JavaScript.
You don’t need to know JavaScript or Tidal Cycles to make music with Strudel. This interactive tutorial will guide you through the basics of Strudel.
The best place to actually make music with Strudel is the [Strudel REPL](https://strudel.cc)
## Strudel and USBSID-Pico
With it's WebMidi support you can use USBSID-Pico directly from your browser with [Strudel.cc](https://strudel.cc)

# Notes
Strudel's WebMidi implementation isn't complete and still under development, see [this issue](https://github.com/tidalcycles/strudel/issues/192) for more information.  
To overcome `progNum` not being available yet there is a workaround in the examples below.
Strudel has a drawback when it comes to `ccv` values. The 0~127 midi range is compressed/remapped to 0~1.

# Strudel Examples
Below are my personal notes and scribbles, if they don't work then they don't work.

## Prognum
Missing `progNum` workaround
```js
  let progNum = register('progNum',
      (prog, chan, pattern) => pattern.onTrigger(
          () => WebMidi
                  .getOutputByName('USBSID-Pico')
                  .sendProgramChange(prog, chan)
          )
  )
```
## Init function
This function `.init()` wil make the bank 0 select only run once
```js
  let init = register('init', (pat) => pat.filterWhen((begin) => begin<1))
  $: ccn(0x20).ccv(0.0).midi('USBSID-Pico').init() // bank 0  
```
## Individual Voice setup
If you want to use the SID voices directly you can use bank 0 and bank 1 for this.  
To configure them use the examples below.
```js
  // SID1 ~ Voice 1
  $: stack(
    ccn(0x20).ccv(0.01), // bank 1
    ccn(0x1).ccv(0.5),   // pwm start
    ccn(0x7).ccv(1),     // volume
    ccn(0x6E).ccv(6),    // set frequency cutoff @ 0xB0 (176) = 6 ~ 5,5 (0xFFF max mapped to 127 in steps of 0,03125)
    ccn(0x71).ccv(0),    // enable filter on voice 1 < bugged
    ccn(0x6F).ccv(0.9),  // set resonance on filter to 0xB (11) = (0xF max mapped to 127 in steps of 8)
    ccn(0x77).ccv(0),    // enable lowpas filter
    ccn(0x78).ccv(0),    // enable bandpass filter
    ccn(0x79).ccv(0),    // enable highpass filter
    ccn(0x49).ccv(0),    // attack
    ccn(0x4b).ccv(0),    // decay
    ccn(0x40).ccv(1),    // sustain
    ccn(0x48).ccv(0.65), // release
    ccn(0x15).ccv(0),    // noise
    ccn(0x16).ccv(0),    // pulse
    ccn(0x17).ccv(1),    // sawtooth
    ccn(0x18).ccv(0)     // triangle
    // progNum(1)  // not working yet
  ).midi('USBSID-Pico').midichan(1).init()
  // SID1 ~ Voice 2
  $: stack(
    ccn(0x20).ccv(0.01), // bank 1
    ccn(0x1).ccv(0),     // pwm start
    ccn(0x7).ccv(1),     // volume
    ccn(0x6E).ccv(6),    // set frequency cutoff @ 0xB0 (176) = 6 ~ 5,5 (0xFFF max mapped to 127 in steps of 0,03125)
    ccn(0x71).ccv(0),    // enable filter on voice 1 < bugged
    ccn(0x6F).ccv(0.9),  // set resonance on filter to 0xB (11) = (0xF max mapped to 127 in steps of 8)
    ccn(0x77).ccv(0),    // enable lowpas filter
    ccn(0x78).ccv(0),    // enable bandpass filter
    ccn(0x79).ccv(1),    // enable highpass filter
    ccn(0x49).ccv(0),    // attack
    ccn(0x4b).ccv(0),    // decay
    ccn(0x40).ccv(0.3),  // sustain
    ccn(0x48).ccv(0.4),  // release
    ccn(0x15).ccv(1),    // noise
    ccn(0x16).ccv(0),    // pulse
    ccn(0x17).ccv(0),    // sawtooth
    ccn(0x18).ccv(0)     // triangle
    // progNum(1)  // not working yet
  ).midi('USBSID-Pico').midichan(2).init()
  // SID1 ~ Voice 3
  $: stack(
    ccn(0x20).ccv(0.01), // bank 1
    ccn(0x1).ccv(0.5),   // pwm start
    ccn(0x7).ccv(1),     // volume
    ccn(0x6E).ccv(6),    // set frequency cutoff @ 0xB0 (176) = 6 ~ 5,5 (0xFFF max mapped to 127 in steps of 0,03125)
    ccn(0x71).ccv(127),    // enable filter on voice 3 < bugged
    ccn(0x6F).ccv(0.9),  // set resonance on filter to 0xB (11) = (0xF max mapped to 127 in steps of 8)
    ccn(0x77).ccv(0),    // enable lowpas filter
    ccn(0x78).ccv(0),    // enable bandpass filter
    ccn(0x79).ccv(0),    // enable highpass filter
    ccn(0x49).ccv(0),    // attack
    ccn(0x4b).ccv(0),    // decay
    ccn(0x40).ccv(1),    // sustain
    ccn(0x48).ccv(0.65), // release
    ccn(0x15).ccv(0),    // noise
    ccn(0x16).ccv(0),    // pulse
    ccn(0x17).ccv(1),    // sawtooth
    ccn(0x18).ccv(0)     // triangle
    // progNum(1)  // not working yet
  ).midi('USBSID-Pico').midichan(3).init()
  // SID2 ~ Voice 1
  $: stack(
    ccn(0x20).ccv(0.01), // bank 1
    ccn(0x1).ccv(0),     // pwm start
    ccn(0x7).ccv(1),     // volume
    ccn(0x6E).ccv(6),    // set frequency cutoff @ 0xB0 (176) = 6 ~ 5,5 (0xFFF max mapped to 127 in steps of 0,03125)
    ccn(0x71).ccv(0),    // enable filter on voice 1 < bugged
    ccn(0x6F).ccv(0.9),  // set resonance on filter to 0xB (11) = (0xF max mapped to 127 in steps of 8)
    ccn(0x77).ccv(1),    // enable lowpas filter
    ccn(0x49).ccv(0),    // attack
    ccn(0x4b).ccv(0),    // decay
    ccn(0x40).ccv(0.3),  // sustain
    ccn(0x48).ccv(0.4),  // release
    ccn(0x15).ccv(1),    // noise
    ccn(0x16).ccv(0),    // pulse
    ccn(0x17).ccv(0),    // sawtooth
    ccn(0x18).ccv(0)     // triangle
    // progNum(1)  // not working yet
  ).midi('USBSID-Pico').midichan(4).init()
  // SID2 ~ Voice 2
  $: stack(
    ccn(0x20).ccv(0.01), // bank 1
    ccn(0x1).ccv(0.5),   // pwm start
    ccn(0x7).ccv(1),     // volume
    ccn(0x6E).ccv(6),    // set frequency cutoff @ 0xB0 (176) = 6 ~ 5,5 (0xFFF max mapped to 127 in steps of 0,03125)
    ccn(0x71).ccv(0),    // enable filter on voice 1 < bugged
    ccn(0x6F).ccv(0.9),  // set resonance on filter to 0xB (11) = (0xF max mapped to 127 in steps of 8)
    ccn(0x77).ccv(0),    // enable lowpas filter
    ccn(0x78).ccv(0),    // enable bandpass filter
    ccn(0x79).ccv(0),    // enable highpass filter
    ccn(0x49).ccv(0),    // attack
    ccn(0x4b).ccv(0),    // decay
    ccn(0x40).ccv(1),    // sustain
    ccn(0x48).ccv(0.65),  // release
    ccn(0x15).ccv(0),    // noise
    ccn(0x16).ccv(0),    // pulse
    ccn(0x17).ccv(1),    // sawtooth
    ccn(0x18).ccv(0)     // triangle
    // progNum(1)  // not working yet
  ).midi('USBSID-Pico').midichan(5).init()
```
# Some test tunes
These lines should be tested individually and ofcourse set to your personal settings
```js
  
  $: note("c1(1,1) c1(1,1) G(1,1) Eb(1,3) Bb(1,6) G(1,3) Eb(1,3) Bb(1,3)").fast(1).midi('USBSID-Pico').midichan(1)  // Imperial march?
  $: note("c1(1,6) c1(1,6) c1(1,6) c1(2,6)").fast(1.2).midi('USBSID-Pico').midichan(1)  // Bass like rythem
  $: note("g5(1,6) g5(1,6) g5(1,6) g5(2,6)").fast(1.2).press().midi('USBSID-Pico').midichan(2)  // Hihat like supporting
  $: note("g4(1,6) g1(1,6) g(1,6) f1(3,6)").fast(1.2).midi('USBSID-Pico').midichan(1)  // note on
  $: note("g1(1,4) c1(1,2) c(1,6) e3(2,6)").fast(1.2).midi('USBSID-Pico').midichan(1)  // note on
  $: note("g1(1,4) c1(1,2) c(1,6) e3(2,6)").fast(1.2).midi('USBSID-Pico').midichan(3)  // note on
  $: note("e1(2,4) c1(1,6) c3(1,3) e1(3,6)").fast(1.5).midi('USBSID-Pico').midichan(5)  // note on
  $: note("g5(1,6) g5(1,6) g5(1,6) g5(2,6)").fast(1.5).press().midi('USBSID-Pico').midichan(2)  // note on
  $: note("g5(1,6) g5(1,6) g5(1,6) g5(2,6)").fast(1.5).press().midi('USBSID-Pico').midichan(4)  // note on

  // Tune
  $: n("[0,7] 4 [2,7] 4")
    .scale("C:<major minor>/2")
    .fast(1,2)
    .midi('USBSID-Pico').midichan(3)

  // Weird ass tune
  $: chord("<C^7 A7b13 Dm7 G7>*2")
  .dict('ireal').layer(
  x=>x.struct("[~ x]*2").voicing()
    .midi('USBSID-Pico').midichan(1)
  ,
  x=>n("0*4").set(x).mode("root:g2").voicing()
    .midi('USBSID-Pico').midichan(5)
  )
```
