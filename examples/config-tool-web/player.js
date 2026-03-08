/**
 * DeepSID / SIDPlayer
 */


function SIDPlayer(emulator) {

  this.paused = false;
  this.stopped = true;
  this.fastForward = false;

  this.ytReady = false;

  this.emulator = emulator.toLowerCase();
  this.voiceMask = [0xF, 0xF, 0xF];
  this.mainVol = 1;

  // this.hermit = null;
  // this.websidem = null;

  this.stereoLevel = -1;

  this.file = "";

  this.bufferSize = {
    hermit: 16384,
    websid: 16384,
  };

  this.advancedSetting = {
    hermit: {},
    websid: {},
  }

  this.filterWebSid = {
		base:				0.02387,	// 6581 filter settings for R2 type
		max:				0.92,
		steepness:			360,
		x_offset:			957,
		distort:			9.36,
		distortOffset:		118400,
		distortScale:		66.1125,
		distortThreshold:	974,
		kink:				325,
	}

  this.emulatorFlags = {
    supportFaster: true,	// True if the handler supports the "Faster" button
    supportEncoding: true,	// True if the handler supports toggling between PAL and NTSC
    supportSeeking: true,	// True if the handler supports seek-clicking the time bar
    supportLoop: true,	// True if the handler supports looping the tune
    forceModel: true,	// True if SID chip model must be set according to the database
    forcePlay: true,	// True to force start playing in all load calls
    hasFlags: true,	// True if showing corner flags in the info box
    slowLoading: true,	// True if the handler is relatively slow at loading tunes
    returnCIA: true,	// True if the handler can return the 16-bit CIA value
    offline: true,	// True if only the skip buttons should be accessible
  }

  // Used by Jürgen's emulators
  this.BASIC_ROM = "lON740NCTUJBU0lDMKhBpx2t96ikq76rgLAFrKSpn6hwqCepHKiCqNGoOqkuqEqpLLhn4VXhZOGysyO4f6qfqlaom6ZdpoWqKeG94cbheqtBpjm8zLxYvBADfbOes3G/l+Dque2/ZOJr4rTiDuMNuHy3ZbStt4u37LYAtyy3N7d5abh5Urh7Krp7Ebt/er9Q6K9G5a99s79a065kFbBFTsRGT9JORVjUREFUwUlOUFVUo0lOUFXUREnNUkVBxExF1EdPVM9SVc5JxlJFU1RPUsVHT1NVwlJFVFVSzlJFzVNUT9BPzldBSdRMT0HEU0FWxVZFUklG2URFxlBPS8VQUklOVKNQUklO1ENPTtRMSVPUQ0zSQ03EU1nTT1BFzkNMT1PFR0XUTkXXVEFCqFTPRs5TUEOoVEhFzk5P1FNURdCrraqv3kFOxE/Svr28U0fOSU7UQULTVVPSRlLFUE/TU1HSUk7ETE/HRVjQQ0/TU0nOVEHOQVTOUEVFy0xFzlNUUqRWQcxBU8NDSFKkTEVGVKRSSUdIVKRNSUSkR88AVE9PIE1BTlkgRklMRdNGSUxFIE9QRc5GSUxFIE5PVCBPUEXORklMRSBOT1QgRk9VTsRERVZJQ0UgTk9UIFBSRVNFTtROT1QgSU5QVVQgRklMxU5PVCBPVVRQVVQgRklMxU1JU1NJTkcgRklMRSBOQU3FSUxMRUdBTCBERVZJQ0UgTlVNQkXSTkVYVCBXSVRIT1VUIEZP0lNZTlRB2FJFVFVSTiBXSVRIT1VUIEdPU1XCT1VUIE9GIERBVMFJTExFR0FMIFFVQU5USVTZT1ZFUkZMT9dPVVQgT0YgTUVNT1LZVU5ERUYnRCBTVEFURU1FTtRCQUQgU1VCU0NSSVDUUkVESU0nRCBBUlJB2URJVklTSU9OIEJZIFpFUs9JTExFR0FMIERJUkVD1FRZUEUgTUlTTUFUQ8hTVFJJTkcgVE9PIExPTsdGSUxFIERBVMFGT1JNVUxBIFRPTyBDT01QTEXYQ0FOJ1QgQ09OVElOVcVVTkRFRidEIEZVTkNUSU/OVkVSSUbZTE9BxJ6hrKG1ocKh0KHiofCh/6EQoiWiNaI7ok+iWqJqonKif6KQop2iqqK6osii1aLkou2iAKMOox6jJKODow1PSw0AICBFUlJPUgAgSU4gAA0KUkVBRFkuDQoADQpCUkVBSwCguujo6Oi9AQHJgdAhpUrQCr0CAYVJvQMBhUrdAwHQB6VJ3QIB8AeKGGkSqtDYYCAIpIUxhDI4pVrlX4UiqKVb5WCq6JjwI6VaOOUihVqwA8ZbOKVY5SKFWLAIxlmQBLFakViI0PmxWpFYxlvGWcrQ8mAKaT6wNYUiuuQikC5gxDSQKNAExTOQIkiiCZhItVfKEPogJrWi92iVYegw+mioaMQ0kAbQBcUzsAFgohBsAAOKCqq9JqOFIr0no4UjIMz/qQCFEyDXqiBFq6AAsSJIKX8gR6vIaBD0IHqmqWmgoyAeq6Q6yPADIMK9qXagoyAeq6mAIJD/bAIDIGClhnqEeyBzAKrw8KL/hjqQBiB5pUzhpyBrqSB5pYQLIBOmkESgAbFfhSOlLYUipWCFJaVfiPFfGGUthS2FJKUuaf+FLuVgqjilX+UtqLAD6MYlGGUikAPGIxixIpEkyND55iPmJcrQ8iBZpiAzpa0AAvCIGKUthVplC4VYpC6EW5AByIRZILijpRSkFY3+AYz/AaUxpDKFLYQupAuIufwBkV+IEPggWaYgM6VMgKSlK6QshSKEIxigAbEi8B2gBMixItD7yJhlIqqgAJEipSNpAMiRIoYihSOQ3WCiACAS4ckN8A2dAALo4FmQ8aIXTDekTMqqbAQDpnqgBIQPvQACEAfJ//A+6ND0ySDwN4UIySLwViQPcC3JP9AEqZnQJckwkATJPJAdhHGgAIQLiIZ6ysjovQACOPmeoPD1yYDQMAULpHHoyJn7Abn7AfA2OOk68ATJSdAChQ846VXQn4UIvQAC8N/FCPDbyJn7AejQ8KZ65gvIuZ2gEPq5nqDQtL0AAhC+mf0Bxnup/4V6YKUrpiygAYVfhmCxX/AfyMilFdFfkBjwA4jQCaUUiNFfkAzwCoixX6qIsV+w1xhg0P2pAKiRK8iRK6UrGGkChS2lLGkAhS4gjqapANAtIOf/pTekOIUzhDSlLaQuhS+EMIUxhDIgHaiiGYYWaKhoovqaSJhIqQCFPoUQYBilK2n/hXqlLGn/hXtgkAbwBMmr0Okga6kgE6YgeQDwDMmr0I4gcwAga6nQhmhopRQFFdAGqf+FFIUVoAGED7Ff8EMgLKgg16rIsV+qyLFfxRXQBOQU8AKwLIRJIM29qSCkSSl/IEerySLQBqUPSf+FD8jwEbFf0BCosV+qyLFfhl+FYNC1TIbjbAYDENfJ//DTJA8wzzjpf6qESaD/yvAIyLmeoBD6MPXIuZ6gMLIgR6vQ9amAhRAgpakgiqPQBYppD6qaaGipCSD7oyAGqRiYZXpIpXtpAEilOkilOUippCD/riCNrSCKraVmCX8lYoViqYugp4UihCNMQ66pvKC5IKK7IHkAyanQBiBzACCKrSArvCA4rqVKSKVJSKmBSCAsqKV6pHvAAurwBIU9hD6gALF60EOgArF6GNADTEuoyLF6hTnIsXqFOphleoV6kALme2wIAyBzACDtp0yup/A86YCQEckjsBcKqLkNoEi5DKBITHMATKWpyTrw1kwIr8lL0PkgcwCppCD/rkygqDilK+kBpCywAYiFQYRCYCDh/7ABGNA8pXqke6Y66PAMhT2EPqU5pDqFO4Q8aGipgaCjkANMaaRMhuPQF6IapD7QA0w3pKU9hXqEe6U7pDyFOYQ6YAipACCQ/yjQA0xZpiBgpkyXqKkDIPujpXtIpXpIpTpIpTlIqY1IIHkAIKCoTK6nIGupIAmpOKU55RSlOuUVsAuYOGV6pnuQB+iwBKUrpiwgF6aQHqVf6QGFeqVg6QCFe2DQ/an/hUogiqOayY3wC6IMLKIRTDekTAivaGiFOWiFOmiFemiFeyAGqZgYZXqFepAC5ntgojosogCGB6AAhAilCKYHhQeGCLF68OjFCPDkyMki0PPw6SCerSB5AMmJ8AWppyD/rqVh0AUgCanwuyB5ALADTKCoTO2nIJ63SMmN8ATJidCRxmXQBGhM76cgcwAga6nJLPDuaGCiAIYUhhWw9+kvhQelFYUiyRmw1KUUCiYiCiYiZRSFFKUiZRWFFQYUJhWlFGUHhRSQAuYVIHMATHGpIIuwhUmESqmyIP+upQ5IpQ1IIJ6taCogkK3QGGgQEiAbvCC/saAApWSRScilZZFJYEzQu2ikSsC/0EwgprbJBtA9oACEYYRmhHEgHaog4rrmcaRxIB2qIAy8qvAF6Iog7bqkccjABtDfIOK6IJu8pmSkY6VlTNv/sSIggACQA0xIsukvTH69oAKxZMU0kBfQB4ixZMUzkA6kZcQukAjQDaVkxS2wB6VkpGVMaKqgALFkIHW0pVCkUYVvhHAgerapYaAAhVCEUSDbtqAAsVCRScixUJFJyLFQkUlgIIaqTLWrIJ638AWpLCD/rgiGEyAY4ShMoKogIasgeQDwNfBDyaPwUMmmGPBLySzwN8k78F4gnq0kDTDeIN29IIe0ICGrIDur0NOpAJ0AAqL/oAGlE9AQqQ0gR6skExAFqQogR6tJ/2A4IPD/mDjpCrD8Sf9pAdAWCDgg8P+ECSCbt8kp0FkokAaK5QmQBaroytAGIHMATKKqIDur0PIgh7QgpraqoADoyvC8sSIgR6vIyQ3Q8yDlqkwoq6UT8AOpICypHSypPyAM4Sn/YKUR8BEwBKD/0ASlP6RAhTmEOkwIr6UT8AWiGEw3pKkMoK0gHqulPaQ+hXqEe2AgprPJI9AQIHMAIJ63qSwg/66GEyAe4aIBoAKpAI0BAqlAIA+sphPQE2AgnrepLCD/roYTIB7hIM6rpRMgzP+iAIYTYMki0Asgva6pOyD/riAhqyCms6ksjf8BIPmrpRPwDSC3/ykC8AYgtatM+KitAALQHqUT0OMgBqlM+6ilE9AGIEWrIDurTGClpkGkQqmYLKkAhRGGQ4REIIuwhUmESqV6pHuFS4RMpkOkRIZ6hHsgeQDQICQRUAwgJOGNAAKi/6AB0AwwdaUT0AMgRasg+auGeoR7IHMAJA0QMSQRUAnohnqpAIUH8AyFB8ki8AepOoUHqSwYhQileqR7aQCQAcggjbQg4rcg2qlMkawg87ylDiDCqSB5APAHySzwA0xNq6V6pHuFQ4REpUukTIV6hHsgeQDwLSD9rkwVrCAGqciq0BKiDcixevBsyLF6hT/IsXrIhUAg+6ggeQCq4IPQ3ExRrKVDpESmERADTCeooACxQ/ALpRPQB6n8oKxMHqtgP0VYVFJBIElHTk9SRUQNAD9SRURPIEZST00gU1RBUlQNANAEoADwAyCLsIVJhEogiqPwBaIKTDekmooYaQRIaQaFJGigASCiu7q9CQGFZqVJpEogZ7gg0LugASBdvLo4/QkB8Be9DwGFOb0QAYU6vRIBhXq9EQGFe0yup4ppEaqaIHkAySzQ8SBzACAkrSCerRgkOCQNMAOwA2Cw/aIWTDekpnrQAsZ7xnqiACRIikipASD7oyCDrqkAhU0geQA46bGQF8kDsBPJASpJAUVNxU2QYYVNIHMATLutpk3QLLB7aQeQd2UN0ANMPbZp/4UiCmUiqGjZgKCwZyCNrUggIK5opEsQF6rwVtBfRg2KKqZ60ALGe8Z6oBuFTdDX2YCgsEiQ2bmCoEi5gaBIIDOupU1Mqa1MCK+lZr6AoKhohSLmImiFI5hIIBu8pWVIpWRIpWNIpWJIpWFIbCIAoP9o8CPJZPADII2thEtoSoUSaIVpaIVqaIVraIVsaIVtaIVuRWaFb6VhYGwKA6kAhQ0gcwCwA0zzvCATsZADTCivyf/QD6mooK4gortMcwCCSQ/aocku8N7Jq/BYyarw0cki0A+leqR7aQCQAcggh7RM4rfJqNAToBjQOyC/saVlSf+opWRJ/0yRs8ml0ANM9LPJtJADTKevIPquIJ6tqSksqSgsqSygANF60ANMcwCiC0w3pKAVaGhM+q04pWTpAKVl6aCQCKmi5WSp4+VlYCCLsIVkhGWmRaRGpQ3wJqkAhXAgFK+QHOBU0BjAydAUIISvhF6IhHGgBoRdoCQgaL5Mb7RgJA4QDaAAsWSqyLFkqIpMkbMgFK+QLeBU0BvASdAlIISvmKKgTE+8IN7/hmSEY4VloACEYmDgU9AKwFTQBiC3/0w8vKVkpGVMorsKSKogcwDgj5AgIPquIJ6tIP2uII+taKqlZUilZEiKSCCet2ioikhM1q8g8a5oqLnqn4VVueufhVYgVABMja2g/yygAIQLIL+xpWRFC4UHpWVFC4UIIPy7IL+xpWVFCyUIRQuopWRFCyUHRQtMkbMgkK2wE6VuCX8laoVqqWmgACBbvKpMYbCpAIUNxk0gpraFYYZihGOlbKRtIKq2hmyEbao45WHwCKkBkASmYan/hWag/+jIytAHpmYwDxiQDLFs0WLw76L/sAKiAeiKKiUS8AKp/0w8vCD9rqogkLAgeQDQ9GCiACB5AIYMhUUgeQAgE7GwA0wIr6IAhg2GDiBzAJAFIBOxkAuqIHMAkPsgE7Gw9skk0Aap/4UN0BDJJdATpRDQ0KmAhQ4FRYVFigmAqiBzAIZGOAUQ6SjQA0zRsaAAhBClLaYuhmCFX+Qw0ATFL/AipUXRX9AIpUbI0V/wfYgYpV9pB5Dh6NDcyUGQBelbOOmlYGhIySrQBakToL9gpUWkRslU0AvAyfDvwEnQA0wIr8lT0ATAVPD1pS+kMIVfhGClMaQyhVqEWxhpB5AByIVYhFkguKOlWKRZyIUvhDCgAKVFkV/IpUaRX6kAyJFfyJFfyJFfyJFfyJFfpV8YaQKkYJAByIVHhEhgpQsKaQVlX6RgkAHIhViEWWCQgAAAACC/saVkpGVgIHMAIJ6tII2tpWYwDaVhyZCQCamloLEgW7zQekybvKUMBQ5IpQ1IoACYSKVGSKVFSCCysWiFRWiFRmiour0CAUi9AQFIpWSdAgGlZZ0BAcggeQDJLPDShAsg965ohQ1ohQ4pf4UMpi+lMIZfhWDFMtAE5DHwOaAAsV/IxUXQBqVG0V/wFsixXxhlX6rIsV9lYJDXohIsog5MN6SiE6UM0PcglLGlC6AE0V/Q50zqsiCUsSAIpKAAhHKiBaVFkV8QAcrIpUaRXxACysqGcaULyMjIkV+iC6kAJAxQCGgYaQGqaGkAyJFfyIqRXyBMs4ZxhXKkIsYL0NxlWbBdhVmoimVYkAPI8FIgCKSFMYQyqQDmcqRx8AWIkVjQ+8ZZxnLQ9eZZOKUx5V+gApFfpTLI5WCRX6UM0GLIsV+FC6kAhXGFcshoqoVkaIVl0V+QDtAGyIrRX5AHTEWyTDWkyKVyBXEY8AogTLOKZWSqmKQiZWWGccYL0MqFcqIFpUUQAcqlRhACysqGKKkAIFWzimVYhUeYZVmFSKilR2CEIrFfhSiIsV+FKakQhV2iAKAAigqqmCqosKQGcSZykAsYimUoqphlKaiwk8Zd0ONgpQ3wAyCmtiAmtTilM+UxqKU05TKiAIYNhWKEY6KQTES8OCDw/6kA8OumOujQoKIVLKIbTDekIOGzIKazIPquqYCFECCLsCCNrSD3rqmyIP+uSKVISKVHSKV7SKV6SCD4qExPtKmlIP+uCYCFECCSsIVOhE9Mja0g4bOlT0ilTkgg8a4gja1ohU5ohU+gArFOhUeqyLFO8JmFSMixR0iIEPqkSCDUu6V7SKV6SLFOhXrIsU6Fe6VISKVHSCCKrWiFTmiFTyB5APADTAivaIV6aIV7oABokU5oyJFOaMiRTmjIkU5oyJFOYCCNraAAIN+9aGip/6AA8BKmZKRlhlCEUSD0tIZihGOFYWCiIoYHhgiFb4RwhWKEY6D/yLFv8AzFB/AExQjQ88ki8AEYhGGYZW+FcaZwkAHohnKlcPAEyQLQC5ggdbSmb6RwIIi2phbgItAFohlMN6SlYZUApWKVAaVjlQKgAIZkhGWEcIiEDYYX6OjohhZgRg9ISf84ZTOkNLABiMQykBHQBMUxkAuFM4Q0hTWENqpoYKIQpQ8wtiAmtamAhQ9o0NCmN6U4hjOFNKAAhE+ETqUxpjKFX4ZgqRmiAIUihiPFFvAFIMe18PepB4VTpS2mLoUihiPkMNAExS/wBSC9tfDzhViGWakDhVOlWKZZ5DLQB8Ux0ANMBraFIoYjoACxIqrIsSIIyLEiZViFWMixImVZhVkoENOKMNDIsSKgAAppBWUihSKQAuYjpiPkWdAExVjwuiDHtfDzsSIwNcixIhAwyLEi8CvIsSKqyLEixTSQBtAe5DOwGsVgkBbQBORfkBCGX4VgpSKmI4VOhk+lU4VVpVMYZSKFIpAC5iOmI6AAYKVPBU7w9aVVKQRKqIVVsU5lX4VapWBpAIVbpTOmNIVYhlkgv6OkVcilWJFOquZZpVnIkU5MKrWlZUilZEggg64gj61ohW9ohXCgALFvGHFkkAWiF0w3pCB1tCB6tqVQpFEgqrYgjLalb6RwIKq2IMq0TLitoACxb0jIsW+qyLFvqGiGIoQjqPAKSIixIpE1mND4aBhlNYU1kALmNmAgj62lZKRlhSKEIyDbtgigALEiSMixIqrIsSKoaCjQE8Q00A/kM9ALSBhlM4UzkALmNGiGIoQjYMQY0AzFF9AIhRbpA4UXoABgIKG3ikipASB9tGigAJFiaGhMyrQgYbfRUJiQBLFQqphIikggfbSlUKRRIKq2aKhoGGUihSKQAuYjmCCMtkzKtCBhtxjxUEn/TAa3qf+FZSB5AMkp8AYg/a4gnrcgYbfwS8qKSBiiAPFQsLZJ/8VlkLGlZbCtIPeuaKhohVVoaGiqaIVQaIVRpVVImEigAIpgIIK3TKKzIKO2ogCGDahgIIK38AigALEiqEyis0xIsiBzACCKrSC4saZk0PCmZUx5ACCCt9ADTPe4pnqke4ZxhHKmIoZ6GGUihSSmI4Z7kAHohiWgALEkSJiRJCB5ACDzvGigAJEkpnGkcoZ6hHtgIIqtIPe3IP2uTJ63pWYwnaVhyZGwlyCbvKVkpGWEFIUVYKUVSKUUSCD3t6AAsRSoaIUUaIUVTKKzIOu3iqAAkRRgIOu3hkmiACB5APADIPG3hkqgALEURUolSfD4YKkRoL9MZ7ggjLqlZkn/hWZFboVvpWFMarggmbmQPCCMutADTPy7pnCGVqJppWmo8M445WHwJJAShGGkboRmSf9pAKAAhFaiYdAEoACEcMn5MMeopXBWASCwuSRvEFegYeBp8AKgaThJ/2VWhXC5BAD1BIVluQMA9QOFZLkCAPUChWO5AQD1AYVisAMgR7mgAJgYpmLQSqZjhmKmZIZjpmWGZKZwhmWEcGkIySDQ5KkAhWGFZmBlVoVwpWVlbYVlpWRlbIVkpWNla4VjpWJlaoViTDa5aQEGcCZlJmQmYyZiEPI45WGwx0n/aQGFYZAO5mHwQmZiZmNmZGZlZnBgpWZJ/4VmpWJJ/4VipWNJ/4VjpWRJ/4VkpWVJ/4VlpXBJ/4Vw5nDQDuZl0ArmZNAG5mPQAuZiYKIPTDekoiW0BIRwtAOUBLQClAO0AZQCpGiUAWkIMOjw5ukIqKVwsBQWAZAC9gF2AXYBdgJ2A3YEasjQ7BhggQAAAAADf15Wy3mAE5sLZIB2OJMWgjiqOyCANQTzNIE1BPM0gIAAAACAMXIX+CArvPACEANMSLKlYel/SKmAhWGp1qC5IGe4qduguSAPu6m8oLkgULipwaC5IEPgqeCguSBnuGggfr2p5aC5IIy60ANMi7ogt7qpAIUmhSeFKIUppXAgWbqlZSBZuqVkIFm6pWMgWbqlYiBeukyPu9ADTIO5SgmAqJAZGKUpZW2FKaUoZWyFKKUnZWuFJ6UmZWqFJmYmZidmKGYpZnCYStDWYIUihCOgBLEihW2IsSKFbIixIoVriLEihW5FZoVvpW4JgIVqiLEihWmlYWClafAfGGVhkAQwHRgsEBRpgIVh0ANM+7ilb4VmYKVmSf8wBWhoTPe4TH65IAy8qvAQGGkCsPKiAIZvIHe45mHw52CEIAAAACAMvKn5oLqiAIZvIKK7TBK7IIy68HYgG7ypADjlYYVhILe65mHwuqL8qQGkasRi0BCka8Rj0AqkbMRk0ASkbcRlCCqQCeiVKfAyEDSpASiwDgZtJmwmayZqsOYwzhDiqKVt5WWFbaVs5WSFbKVr5WOFa6Vq5WKFaphMT7upQNDOCgoKCgoKhXAoTI+7ohRMN6SlJoVipSeFY6UohWSlKYVlTNe4hSKEI6AEsSKFZYixIoVkiLEihWOIsSKFZgmAhWKIsSKFYYRwYKJcLKJXoADwBKZJpEogG7yGIoQjoASlZZEiiKVkkSKIpWORIoilZgl/JWKRIoilYZEihHBgpW6FZqIFtWiVYMrQ+YZwYCAbvKIGtWCVaMrQ+YZwYKVh8PsGcJD3IG+50PJMOLmlYfAJpWYqqf+wAqkBYCArvIViqQCFY6KIpWJJ/yqpAIVlhWSGYYVwhWZM0rhGZmCFJIQloACxJMiq8MSxJEVmMMLkYdAhsSQJgMVi0BnIsSTFY9ASyLEkxWTQC8ipf8VwsSTlZfAopWaQAkn/TDG8pWHwSjjpoCRmEAmqqf+FaCBNuYqiYcn5EAYgmbmEaGCopWYpgEZiBWKFYiCwuYRoYKVhyaCwICCbvIRwpWaEZkmAKqmghWGlZYUHTNK4hWKFY4VkhWWoYKAAogqUXcoQ+5APyS3QBIZn8ATJK9AFIHMAkFvJLvAuyUXQMCBzAJAXyavwDskt8ArJqvAIySvwBNAHZmAgcwCQXCRgEA6pADjlXkxJvWZfJF9Qw6VeOOVdhV7wEhAJIP665l7Q+fAHIOK6xl7Q+aVnMAFgTLS/SCRfEALmXSDiumg46TAgfr1MCr1IIAy8aCA8vKVuRWaFb6ZhTGq4pV7JCpAJqWQkYDARTH65CgoYZV4KGKAAcXo46TCFXkwwvZs+vB/9nm5rJ/2ebmsoAKlxoKMg2r2lOqY5hWKGY6KQOCBJvCDfvUweq6ABqSAkZhACqS2Z/wCFZoRxyKkwpmHQA0wEv6kA4IDwArAJqb2gvSAouqn3hV2puKC9IFu88B4QEqmzoL0gW7zwAhAOIOK6xl3Q7iD+uuZd0NwgSbggm7yiAaVdGGkKMAnJC7AGaf+qqQI46QKFXoZdivACEBOkcakuyJn/AIrwBqkwyJn/AIRxoACigKVlGHkZv4VlpWR5GL+FZKVjeRe/hWOlYnkWv4Vi6LAEEN4wAjDaipAESf9pCmkvyMjIyIRHpHHIqil/mf8Axl3QBqkuyJn/AIRxpEeKSf8pgKrAJPAEwDzQpqRxuf8AiMkw8PjJLvAByKkrpl7wLhAIqQA45V6qqS2ZAQGpRZkAAYqiLzjo6Qqw+2k6mQMBipkCAakAmQQB8AiZ/wCpAJkAAakAoAFggAAAAAD6Ch8AAJiWgP/wvcAAAYag///Y8AAAA+j///+cAAAACv//////3wqAAANLwP//c2AAAA4Q///9qAAAADzsqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqIAy8qRGgvyCiu/BwpWnQA0z5uKJOoAAg1LulbhAPIMy8qU6gACBbvNADmKQHIP67mEgg6rmpTqAAICi6IO2/aEqQCqVh8AalZkn/hWZggTiqOykHcTRYPlZ0Fn6zG3cv7uOFeh2EHCp8Y1lYCn51/efGgDFyGBCBAAAAAKm/oL8gKLqlcGlQkAMgI7xMAOA=";
  this.KERNAL_ROM = "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";
  this.CHAR_ROM = "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ZjwOAHxmZnx4bGYAPGZgPAZmPAB+GBgYGBgYAGZmZmZmZjwAZmZmZmY8GABjY2Nrf3djAGZmPBg8ZmYAZmZmPBgYGAB+BgwYMGB+ABgYGP//GBgYwMAwMMDAMDAYGBgYGBgYGDMzzMwzM8zMM5nMZjOZzGYAAAAAAAAAAPDw8PDw8PDwAAAAAP//////AAAAAAAAAAAAAAAAAAD/wMDAwMDAwMDMzDMzzMwzMwMDAwMDAwMDAAAAAMzMMzPMmTNmzJkzZgMDAwMDAwMDGBgYHx8YGBgAAAAADw8PDxgYGB8fAAAAAAAA+PgYGBgAAAAAAAD//wAAAB8fGBgYGBgY//8AAAAAAAD//xgYGBgYGPj4GBgYwMDAwMDAwMDg4ODg4ODg4AcHBwcHBwcH//8AAAAAAAD///8AAAAAAAAAAAAA////AQMGbHhwYAAAAAAA8PDw8A8PDw8AAAAAGBgY+PgAAADw8PDwAAAAAPDw8PAPDw8Pw5mRkZ+Zw////8P5wZnB//+fn4OZmYP////Dn5+fw///+fnBmZnB////w5mBn8P///Hnwefn5////8GZmcH5g/+fn4OZmZn//+f/x+fnw///+f/5+fn5w/+fn5OHk5n//8fn5+fnw////5mAgJSc////g5mZmZn////DmZmZw////4OZmYOfn///wZmZwfn5//+DmZ+fn////8Gfw/mD///ngefn5/H///+ZmZmZwf///5mZmcPn////nJSAwcn///+Zw+fDmf///5mZmcHzh///gfPnz4H/w8/Pz8/Pw//z7c+Dz50D/8Pz8/Pz88P//+fDgefn5+f/78+AgM/v////////////5+fn5///5/+ZmZn//////5mZAJkAmZn/58Gfw/mD5/+dmfPnz5m5/8OZw8eYmcD/+fPn///////z58/Pz+fz/8/n8/Pz58///5nDAMOZ////5+eB5+f/////////5+fP////gf///////////+fn///8+fPnz5//w5mRiZmZw//n58fn5+eB/8OZ+fPPn4H/w5n54/mZw//58eGZgPn5/4Gfg/n5mcP/w5mfg5mZw/+BmfPn5+fn/8OZmcOZmcP/w5mZwfmZw////+f//+f/////5///5+fP8efPn8/n8f///4H/gf///4/n8/nz54//w5n58+f/5/////8AAP///+fDmYGZmZn/g5mZg5mZg//DmZ+fn5nD/4eTmZmZk4f/gZ+fh5+fgf+Bn5+Hn5+f/8OZn5GZmcP/mZmZgZmZmf/D5+fn5+fD/+Hz8/Pzk8f/mZOHj4eTmf+fn5+fn5+B/5yIgJScnJz/mYmBgZGZmf/DmZmZmZnD/4OZmYOfn5//w5mZmZnD8f+DmZmDh5OZ/8OZn8P5mcP/gefn5+fn5/+ZmZmZmZnD/5mZmZmZw+f/nJyclICInP+ZmcPnw5mZ/5mZmcPn5+f/gfnz58+fgf/n5+cAAOfn5z8/z88/P8/P5+fn5+fn5+fMzDMzzMwzM8xmM5nMZjOZ//////////8PDw8PDw8PD/////8AAAAAAP//////////////////AD8/Pz8/Pz8/MzPMzDMzzMz8/Pz8/Pz8/P////8zM8zMM2bMmTNmzJn8/Pz8/Pz8/Ofn5+Dg5+fn//////Dw8PDn5+fg4P///////wcH5+fn////////AAD////g4Ofn5+fn5wAA////////AADn5+fn5+cHB+fn5z8/Pz8/Pz8/Hx8fHx8fHx/4+Pj4+Pj4+AAA////////AAAA/////////////wAAAP78+ZOHj5///////w8PDw/w8PDw/////+fn5wcH////Dw8PD/////8PDw8P8PDw8A==";

  // Get buffer size from storage if saved
  var bufferSize = localStorage.getItem("buffer_" + emulator);
  if (bufferSize) this.bufferSize[this.emulator] = bufferSize;

  switch (this.emulator) {

    case "websid":

			/**
			 * WebSid by Jürgen Wothke (Tiny'R'Sid)
			 * 
			 * + Can play almost all digi tunes
			 * + SID model and encoding
			 * + Can play 2SID and 3SID tunes
			 * + Can play MUS files in CGSC
			 * + Emulation quality is pretty good
			 * + Can play BASIC program tunes
			 * + Filter adjusting for 6581
			 * + Stereo panning up to 3 SID chips
			 * + Has the best support of visuals
			 */

			// if (typeof this.websidem === "undefined") this.websidem = new SIDBackendAdapter(this.BASIC_ROM, this.CHAR_ROM, this.KERNAL_ROM);
			// if (typeof this.websidem === "undefined") this.websidem = startworker();

			// Continues to...

			/**
			 * WebSid (legacy) by Jürgen Wothke (Tiny'R'Sid)
			 * 
			 * + Can play many types of digi tunes
			 * + SID model and encoding
			 * + Can play 2SID and 3SID tunes
			 * + Can play MUS files in CGSC
			 * - Emulation quality varies at times
			 * - Cannot play BASIC program tunes
			 */
			// if (typeof this.websidem === "undefined") this.websidem = new LegacySIDBackendAdapter();
			// this.websidem.setProcessorBufSize($("body").attr("data-mobile") !== "0" ? 16384 : this.bufferSize[this.emulator]);

			// This is used by the scopes in the dedicated sundry tab
			// if (typeof Tracer === "undefined") Tracer = new ChannelStreamer();

			this.emulatorFlags.supportFaster	= true;
			this.emulatorFlags.supportEncoding	= true;
			this.emulatorFlags.supportSeeking	= false;
			this.emulatorFlags.supportLoop		= true;
			this.emulatorFlags.forceModel		= false;
			this.emulatorFlags.forcePlay		= false;
			this.emulatorFlags.hasFlags			= true;
			this.emulatorFlags.slowLoading		= false;
			this.emulatorFlags.returnCIA		= true;
			this.emulatorFlags.offline			= false;
			break;

    
    case "hermit":

      /**
       * jsSID by Hermit (with OPL synthesis extension)
       *
       * + Very small and compact JS code
       * + Can play 2SID and 3SID tunes
       * + OPL synthesis for SID+FM playback
       * - Cannot play MUS files in CGSC
       * - No encoding options
       * - Cannot play BASIC and digi tunes (RSID)
       * - Some CIA tunes doesn't work either
       */
      this.hermit = new jsSID(($("body").attr("data-mobile") !== "0" ? 16384 : this.bufferSize[this.emulator]), 0.0005);

      this.emulatorFlags.supportFaster = true;
      this.emulatorFlags.supportEncoding = false;
      this.emulatorFlags.supportSeeking = false;
      this.emulatorFlags.supportLoop = true;
      this.emulatorFlags.forceModel = true;
      this.emulatorFlags.forcePlay = false;
      this.emulatorFlags.hasFlags = true;
      this.emulatorFlags.slowLoading = false;
      this.emulatorFlags.returnCIA = true;
      this.emulatorFlags.offline = false;
      break;

    case "webusb":

      /**
       * jsSID by Hermit with WebUSB extension by LouD
       *
       */
      this.hermit = new jsSID(0, 0, false, true);

      this.emulatorFlags.supportFaster = true;
      this.emulatorFlags.supportEncoding = false;
      this.emulatorFlags.supportSeeking = false;
      this.emulatorFlags.supportLoop = true;
      this.emulatorFlags.forceModel = true;
      this.emulatorFlags.forcePlay = false;
      this.emulatorFlags.hasFlags = true;
      this.emulatorFlags.slowLoading = false;
      this.emulatorFlags.returnCIA = true;
      this.emulatorFlags.offline = false;
      break;

    case "asid":

      /**
       * ASID by Thomas Jansson - added by extending Hermit's driver
       *
       * Current status
       * # Uses a silent audio context to get 50Hz buffer
       * # Same features as Hermit's
       */
      this.hermit = new jsSID(0, 0, true, false);

      this.emulatorFlags.supportFaster = true;
      this.emulatorFlags.supportEncoding = false;
      this.emulatorFlags.supportSeeking = false;
      this.emulatorFlags.supportLoop = true;
      this.emulatorFlags.forceModel = true;
      this.emulatorFlags.forcePlay = false;
      this.emulatorFlags.hasFlags = true;
      this.emulatorFlags.slowLoading = false;
      this.emulatorFlags.returnCIA = true;
      this.emulatorFlags.offline = false;
      break;

default:

      alert("ERROR: Invalid SID handler specified");
  }
}

SIDPlayer.prototype = {

  onPlayerReady: function () {
    if (typeof this.callbackPlayerReady === "function") this.callbackPlayerReady();
    this.callbackPlayerReady = null;
  },

  onTrackReadyToPlay: function () {
    if (typeof this.callbackTrackReadyToPlay === "function") this.callbackTrackReadyToPlay();
    this.callbackTrackReadyToPlay = null;
  },

  onTrackEnd: function () {
    if (typeof this.callbackTrackEnd === "function") this.callbackTrackEnd();
  },

  setCallbackPlayerReady: function (callback) { this.callbackPlayerReady = callback; },
  setCallbackTrackReadyToPlay: function (callback) { this.callbackTrackReadyToPlay = callback; },
  setCallbackTrackEnd: function (callback) { this.callbackTrackEnd = callback; },
  setCallbackBufferEnded: function (callback) { this.callbackBufferEnded = callback; },

  /**
   * Load a SID file but do not play it yet. Also handles callbacks to when the file
   * has loaded, and in some cases also when the music has timed out.
   *
   * @handlers all
   *
     * @param {number} subtune		The subtune to be played
   * @param {number} timeout		Number of seconds before the music times out
   * @param {string} file			Fullname (including prepended HVSC root)
   * @param {function} callback 	Function to call after the SID file has loaded
   */
  load: function (subtune, timeout, file, callback) {

    this.voiceMask = [0xF, 0xF, 0xF];
    // viz.lineInGraph = true;

    subtune = this.subtune = typeof subtune === "undefined" ? this.subtune : subtune;
    timeout = this.timeout = typeof timeout === "undefined" ? this.timeout : timeout;
    file = this.file = typeof file === "undefined" ? this.file : file;

    // Show the raw SID filename in the title
    this.rawFilename = file.split("/").slice(-1)[0];
    $(document).attr("title", "LouDSID | " + this.rawFilename + " | Quick RIP from DeepSID");

    // viz.clearStats();

    switch (this.emulator) {

      case "websid":

				if (error) this.setVolume(0);

				var options = {};
				options.track = subtune;
				options.timeout = timeout;
				options.traceSID = true;	// Needed for the oscilloscope sundry box view

				// WebSid HQ: Preset filter for 6581 and stereo default
				// if (this.emulator != "legacy") {
					// this.websidem.setFilterConfig6581(
          // this.filterWebSid.base,
          // this.filterWebSid.max,
          // this.filterWebSid.steepness,
          // this.filterWebSid.x_offset,
          // this.filterWebSid.distort,
          // this.filterWebSid.distortOffset,
          // this.filterWebSid.distortScale,
          // this.filterWebSid.distortThreshold,
          // this.filterWebSid.kink,
					// );

					// -1 = Stereo completely disabled (no panning)
					//  0 = Stereo enhance disabled (only panning)
					// >0 = Stereo enhance enabled: 16384 = Low, 24576 = Medium, 32767 = High
					// this.websidem.setStereoLevel(this.stereoLevel);
				// }

				// Also apply the values in the filter controls of the sundry box
				// ctrls.updateFilterControls();

				// this.setCallbackPlayerReady(function() {
				// 	// The instance should now be ready
				// 	this.WebSid = ScriptNodePlayer.getInstance();

				// 	this.setVolume(1); // Must be set before calling loadMusicFromURL()

				// 	// The three callbacks here: onCompletion, onFail, onProgress
				// 	ScriptNodePlayer.loadMusicFromURL(file, options, (function(){}), (function(){}), (function(){}));

				// 	// Sometimes the volume was not set above; make sure it is now
				// 	setTimeout(function() {
				// 		this.setVolume(1);
				// 	}.bind(this), 500);
					
				// 	if (typeof callback === "function") {
				// 		callback.call(this, error);
				// 	}
				// }.bind(this));

				// Since 'onCompletion' and 'onProgress' (below) are only utilized when loading
				// the file for the first time, 'onTrackReadyToPlay' is used instead for callback.
				// this.setCallbackTrackReadyToPlay(function() {
				// 	// Reset volume in case the "Faster" button-slip trick was used
				// 	if (!error) this.setVolume(1);
				// 	if (typeof callback === "function") {
				// 		callback.call(this, error);
				// 	}
				// }.bind(this));

				// ScriptNodePlayer.createInstance(this.websidem, '', [], false, this.onPlayerReady.bind(this), this.onTrackReadyToPlay.bind(this), this.onTrackEnd.bind(this), undefined, Tracer);

				// if (error || timeout == 0) {
				// 	setTimeout(function() {
				// 		// After half a second just go to the next row
				// 		if (typeof this.callbackTrackEnd === "function")
				// 			this.callbackTrackEnd();
				// 	}.bind(this), 500);
				// }
				break;
      case "hermit":
      case "webusb":
      case "asid":

        // @todo Maybe catch most digi/speech stuff via the 'player' field?
        var error = file.indexOf("_BASIC.") !== -1;
        if (error) this.setVolume(0);

        this.hermit.setloadcallback(function () {
          // Reset volume just to be on the safe side
          if (!error) this.setVolume(1);
          if (typeof callback === "function")
            callback.call(this, error);
        }.bind(this));
        this.hermit.setendcallback(function () {
          if (typeof this.callbackTrackEnd === "function")
            this.callbackTrackEnd();
        }.bind(this), timeout);
        this.hermit.setbuffercallback(function () {
          if (typeof this.callbackBufferEnded === "function")
            this.callbackBufferEnded();
        }.bind(this), timeout);
        // if (this.emulator != "webusb") {  // Auto start not allowed on webusb
        this.hermit.playcont(); // Added as a hack to avoid a nasty console error
        this.hermit.loadinit(file, subtune);
        // }
        // BUG: is always a tune late
        // console.log(this.hermit.getsubtunes());
        // $('#max-subtunes').text(this.hermit.getsubtunes());


        if (error || timeout == 0) {
          setTimeout(function () {
            // After half a second just go to the next row
            if (typeof this.callbackTrackEnd === "function")
              this.callbackTrackEnd();
          }.bind(this), 500);
        }
        break;

    }
  },

  /**
   * Unload and destroy object. Not all handlers support this.
   *
   * @handlers jsidplay2, lemon
   */
  unload: function () {
    switch (this.emulator) {
      case "websid":
      case "hermit":
      case "webusb":
      case "asid":
        this.stop();
        break;
    }
  },

  /**
   * Play the SID tune. Some handlers differ between continuing after a paused state
   * or a cold start. This too is handled whenever necessary.
   *
   * @handlers all except download
   *
   * @param {boolean} forcePlay	TRUE if forcing play state (cold start)
   */
  play: function (forcePlay) {
    if (!this.paused) {
      this.voiceMask = [0xF, 0xF, 0xF];
      // viz.startBufferEndedEffects();
    }
    switch (this.emulator) {
      case "websid":
				if (this.WebSid) {
					if (typeof forcePlay !== "undefined")
						this.WebSid.play();
					else
						this.WebSid.isPaused() ? this.WebSid.resume() : this.WebSid.play();
				}
				this.speed($("#piano-slow").hasClass("button-on") ? viz.slowSpeed : 1);
				break;
      case "hermit":
      case "webusb":
      case "asid":
        if (typeof forcePlay !== "undefined")
          this.hermit.start(this.subtune);
        else {
          this.paused ? this.hermit.playcont() : this.hermit.start(this.subtune);
          this.paused = false;
        }
        break;
    }
    // UpdateRedirectPlayIcons();
    // viz.clearStats();
    // Stop all the <AUDIO> elements in the 'Remix' tab
    $("#topic-remix audio").each(function () {
      var $sound = $(this)[0];
      $sound.pause();
      $sound.currentTime = 0;
    });
  },

  /**
   * Is a song currently playing?
   *
   * @handlers resid, jsidplay2, websid, legacy, youtube
   *
   * @return {boolean}	TRUE if currently playing
   */
  isPlaying: function () {
    var playing;
    switch (this.emulator) {
      case "websid":
				if (this.WebSid)
					playing = !this.WebSid.isPaused();
				break;
      case "hermit":
      case "webusb":
      case "asid":
        // @todo
        break;
    }
    return playing;
  },

  /**
   * Is web browser auto play currently suspended?
   *
   * @handlers all except youtube and download
   *
   * @return {boolean}	TRUE if suspended
   */
  isSuspended: function () {
    var suspended;
    switch (this.emulator) {
      case "websid":
				suspended = ScriptNodePlayer.getWebAudioContext().state == "suspended";
				break;
      case "hermit":
      case "webusb":
      case "asid":
        suspended = this.hermit.issuspended();
        break;
    }
    return suspended;
  },

  /**
   * Pause the SID tune.
   *
   * @handlers all except download
   */
  pause: function () {
    this.paused = true;
    switch (this.emulator) {
      case "websid":
				if (this.WebSid)
					this.WebSid.pause();
				break;
      case "hermit":
      case "webusb":
      case "asid":
        this.hermit.pause();
        break;
    }
  },

  /**
   * Stop the SID tune.
   *
   * @handlers all except download
   */
  stop: function () {
    this.paused = false;
    // viz.stopBufferEndedEffects();
    switch (this.emulator) {
      case "websid":
				if (this.WebSid) {
					this.load(); // Dirty hack to make sure the tune is restarted next time it is played
					this.WebSid.pause();
				}
				break;
      case "hermit":
      case "webusb":
      case "asid":
        this.hermit.playcont(); // Added as a hack to avoid a nasty console error
        this.hermit.stop();
        this.paused = false;
        break;
    }
    // viz.stopScope();
  },

  /**
   * Speed up the SID tune according to a multiplier.
   *
   * Some SID handlers may have a multiplier cap.
   *
   * @handlers all except download
   *
   * @param {number} multiplier	Multipler (1 = normal speed)
   */
  speed: function (multiplier) {
    switch (this.emulator) {
      case "websid":
				// var normalSampleRate = ScriptNodePlayer.getWebAudioSampleRate();
				// if (this.WebSid)
					// this.WebSid.resetSampleRate(normalSampleRate / multiplier);
				break;
      case "hermit":
      case "webusb":
      case "asid":
        this.hermit.setSpeedMultiplier(multiplier);
        break;
    }
  },

  /**
   * Return an array with various information about the SID tune. This is retrieved
   * from the SID file itself when possible, otherwise from the database.
   *
   * @handlers intrinsic: hermit, webusb, asid
   *filename
   * @param {string} override		Override the current emulator/handler string
   *
   * @return {array}				The information array
   */
  getSongInfo: function (override) {
    var result = {};
    var filename;
    // isCGSC = this.file.indexOf("_Compute's Gazette SID Collection") !== -1;
    switch (this.emulator) {
      case "websid":
        filename = this.getFullName();
        this.websidem.updateSongInfo(filename);
        result.actualSubsong = 0;
        result.maxSubsong = 1;
        result.songAuthor = "";
        result.songName = "";
        result.songReleased = "";
        break;
      case "hermit":
      case "webusb":
      case "asid":
        result.actualSubsong = this.subtune;
        // result.maxSubsong = isCGSC ? 0 : this.hermit.getsubtunes() - 1;
        result.maxSubsong = this.hermit.getsubtunes() - 1;
        result.songAuthor = this.hermit.getauthor();
        result.songName = this.hermit.gettitle();
        result.songReleased = this.hermit.getinfo();
        break;
    }
    // If using e.g. temporary upload testing
		if (typeof result.songName == "undefined")
			result.songName = "<?>";
		if (typeof result.songAuthor == "undefined")
			result.songAuthor = "<?>";
		if (typeof result.songReleased == "undefined")
			result.songReleased = "<?>";
		if (isNaN(result.maxSubsong))
			// result.maxSubsong = browser.playlist[browser.songPos].subtunes - 1;
			result.maxSubsong = files[selectedsididx].subtunes - 1;
		return result;
  },


	/**
	 * Return the full name of the song currently ready to play.
	 * 
	 * @handlers all
	 * 
	 * @returns {string} 	Full name (path and file) excluding server root
	 */
	getFullName: function() {
		// return this.file.replace(browser.ROOT_HVSC+"/", "");
		return files[selectedsididx];
	},

  /**
   * Set the main volume (usually controlled by a volume slider).
   *
   * @handlers all except download
   *
   * @param {float} value		Volume (0 to 1; e.g. half is 0.5)
   */
  setMainVolume: function (value) {
    this.mainVol = value;
    switch (this.emulator) {
      case "websid":
				if (this.WebSid) this.WebSid.setVolume(value);
				break;
      case "hermit":
      case "webusb":
      case "asid":
        this.hermit.setvolume(value);
        break;
    }
  },

  /**
   * Set the volume of the SID tune within the span of the main volume.
   *
   * @handlers all except download
   *
   * @param {float} value		Volume (0 to 1; e.g. half is 0.5)
   */
  setVolume: function (value) {
    switch (this.emulator) {
      case "websid":
				if (this.WebSid) this.WebSid.setVolume(value * this.mainVol);
				break;
      case "hermit":
      case "webusb":
      case "asid":
        this.hermit.setvolume(value * this.mainVol);
        break;
    }
  },

  /**
   * Return the current playtime of the SID tune being played.
   *
   * @handlers all except download
   *
   * @return {number}		Number of seconds passed so far
   */
  getCurrentPlaytime: function () {
    var time = 0;
    switch (this.emulator) {
      case "websid":
				if (this.WebSid)
					time = this.WebSid.getCurrentPlaytime();
				break;
      case "hermit":
      case "webusb":
      case "asid":
        time = this.hermit.getplaytime();
        break;
    }
    return isNaN(time) ? 0 : time;
  },

  /**
   * Return the currently active handler/emulator.
   *
   * @handlers all
   *
   * @return {string}		Handler in lower case, e.g. "youtube"
   */
  getHandler: function () {
    return this.emulator;
  },

  /**
   * Disable the timeout of a SID tune. Used for infinite looping.
   *
   * @handlers resid, websid, legacy, lemon, youtube
   */
  disableTimeout: function () {
    switch (this.emulator) {
      case "websid":
				if (this.WebSid)
					this.WebSid.setPlaybackTimeout(-1);
				break;
      case "hermit":
      case "webusb":
      case "asid":
        break;
    }
  },

  /**
   * Enable the timeout of a SID tune.
   *
   * @handlers resid, websid, legacy, lemon, youtube
   *
   * @param {number} length	Number of seconds before the music times out
   */
  enableTimeout: function (length) {
    switch (this.emulator) {
      case "websid":
				if (this.WebSid)
					this.WebSid.setPlaybackTimeout(length * 1000);
				break;
      case "hermit":
      case "webusb":
      case "asid":
        break;
    }
  },

  /**
   * Force the SID chip model to be used. Not all handlers support this.
   *
   * @handlers resid, websid, legacy, hermit, webusb, asid
   *
   * @param {string} model	Use "6581" or "8580"
   */
  setModel: function (model) {
    switch (this.emulator) {
      case "websid":
				this.websidem.setSID6581(model === "6581" ? 1 : 0);
				break;
      case "hermit":
      case "webusb":
      case "asid":
        this.hermit.setmodel(model === "6581" ? 6581.0 : 8580.0);
        break;
    }
  },

  /**
   * Return the SID chip model currently used. Not all handlers support this.
   *
   * @handlers resid, websid, legacy, hermit, webusb, asid
   *
   * @return {*}		Returns "6581" or "8580" (or FALSE if not supported)
   */
  getModel: function () {
    switch (this.emulator) {
      case "websid":
				return this.websidem.isSID6581() ? "6581" : "8580";
      case "hermit":
      case "webusb":
      case "asid":
        return this.hermit.getmodel() === 6581.0 ? "6581" : "8580";
    }
  },

  /**
   * Force the encoding to be used. Not all handlers support this.
   *
   * @handlers resid, websid, legacy
   *
   * @param {string} encoding		Use "NTSC" or "PAL"
   */
  setEncoding: function (encoding) {
    switch (this.emulator) {
      case "websid":
				this.websidem.setNTSC(encoding === "NTSC" ? 1 : 0);
				break;
      case "webusb":
      case "hermit":
      case "asid":
        // Hermit's emulator doesn't support this
        // @todo Try changing: this.hermit.C64_PAL_CPUCLK + this.hermit.PAL_FRAMERATE
        break;
    }
  },

  /**
   * Return the encoding currently used. Not all handlers support this.
   *
   * @handlers websid, legacy
   *
   * @return {*}		Returns "NTSC" or "PAL" (or FALSE if not supported)
   */
  getEncoding: function () {
    switch (this.emulator) {
      case "websid":
				return this.websidem.isNTSC() ? "NTSC" : "PAL";
      case "hermit":
      case "webusb":
      case "asid":
        // Hermit's emulator always defaults to PAL
        return "PAL";
    }
  },

  /**
   * Toggle a a SID voice on or off. This uses a local mask variable which
   * is reset to 1111 every time a new tune is loaded and played. There are
   * 4 bits as some emulators also support toggling a digi channel.
   *
   * @handlers resid, jsidplay2, websid, legacy, hermit, asid
   *
   * @param {number} voice	Voice to toggle (1-4)
   * @param {number} chip		SID chip number (default is 1)
   */
  toggleVoice: function (voice, chip) {
    if (typeof chip === "undefined") chip = 0; else chip -= 1;
    this.voiceMask[chip] ^= 1 << (voice - 1); // Toggle a bit in the '1111' mask
    switch (this.emulator) {
      case "websid":
				if ($("body").attr("data-mobile") === "0")
					this.websidem.enableVoice(chip, voice - 1, this.voiceMask[chip] & 1 << (voice - 1));
				break;
      case "webusb":
        // Stitch a mask together that works with Hermit's emulator (CCCBBBAAA)
        var jsMask = 0;
        for (var jsChip = 0; jsChip < 4; jsChip++)
          jsMask += (this.voiceMask[jsChip] & 7) << (3 * jsChip);
        this.hermit.enableVoices(jsMask);
        break;
      case "hermit":
      case "asid":
        // Stitch a mask together that works with Hermit's emulator (CCCBBBAAA)
        var jsMask = 0;
        for (var jsChip = 0; jsChip < 3; jsChip++)
          jsMask += (this.voiceMask[jsChip] & 7) << (3 * jsChip);
        this.hermit.enableVoices(jsMask);
        break;
    }
  },

  /**
   * Enable all SID voices (including 2SID and 3SID).
   *
   * @handlers resid, jsidplay2, websid, legacy, hermit, webusb, asid
   */
  enableAllVoices: function () {
    this.voiceMask = [0xF, 0xF, 0xF];
    switch (this.emulator) {
      case "websid":
				if ($("body").attr("data-mobile") === "0") {
					for (var chip = 0; chip < 3; chip++) {
						for (var voice = 0; voice < 4; voice++)
							this.websidem.enableVoice(chip, voice, true);
					}
				}
				break;
      case "hermit":
      case "webusb":
      case "asid":
        this.hermit.enableVoices(0x1FF);
        break;
    }
  },

  /**
   * Return the speed relative to 50hz. Not all handlers support this. If
   * 0 is returned, the tune uses VBI. If > 0, it uses CIA.
   *
   * @handlers websid, legacy, hermit, webusb, asid
   *
   * @return {*}		Returns the multiplier value (4 = 4x speed), or FALSE
   */
  getPace: function () {
    switch (this.emulator) {
      case "websid":
				var cia = this.websidem.getRAM(0xDC04) + this.websidem.getRAM(0xDC05) * 256;
				if (cia == 16421) cia = 0;
				// 19654 relates to 1x; lower values speed up the tune
				return cia ? Math.round(19654 / cia) : 0;
      case "hermit":
      case "webusb":
      case "asid":
        var cia = this.hermit.getcia();
        return cia ? Math.round(19654 / cia) : 0;
    }
  },

	/**
	 * Return the type of digi, if used by the song. Not all handlers support this.
	 * 
	 * @handlers websid, legacy
	 * 
	 * @return {string}		Returns a short ID string, or empty if digi is not used
	 */
	getDigiType: function() {
		switch (this.emulator) {
			case "websid":
				return this.websidem.getDigiTypeDesc();
			case "hermit":
			case "webusb":
			case "asid":
				return "";
		}
	},

	/**
	 * Return the sample rate used by the digi samples, if used by the song. Not all
	 * handlers support this.
	 * 
	 * @handlers websid, legacy
	 * 
	 * @return {number}		Returns the sample rate, or 0 if digi is not used
	 */
	getDigiRate: function() {
		switch (this.emulator) {
			case "websid":
				return this.websidem.getDigiRate();
			case "hermit":
			case "webusb":
			case "asid":
				return 0;
		}
	},

  /**
   * Return the SID address for the specified SID chip.
   *
   * @handlers resid, jsidplay2, websid, legacy, hermit, webusb, asid
   *
   * @param {number} chip			SID chip number (1-3)
   *
   * @return {*}					SID chip address (e.g. $D400), 0, or FALSE
   */
  getSIDAddress: function (chip) {
    if (chip == 1) return 0xD400;
    switch (this.emulator) {
      case "websid":
				return this.websidem.getSIDBaseAddr(chip - 1);
      case "hermit":
      case "webusb":
      case "asid":
        return this.hermit.getSIDAddress(chip - 1);
    }
  },

  /**
   * Return the current 8-bit value of a SID register.
   *
   * @handlers resid, jsidplay2, websid, legacy, hermit, webusb, asid
   *
   * @param {number} register		Register $D400 to $D41C
   * @param {number} chip			SID chip number (default is 1)
   *
   * @return {*}					Byte value of the register, or FALSE
   */
  readRegister: function (register, chip) {
    if (register < 0xD400 && register > 0xD41C) return false;
    register -= 0xD400;
    if (typeof chip === "undefined") chip = 0; else chip -= 1;
    switch (this.emulator) {
      case "websid":
				if (this.WebSid) {
					try {
						var value = this.websidem.getSIDRegister(chip, register);
					} catch(e) { /* Ignore type errors */ }
				}
				return value;
      case "hermit":
      case "webusb":
      case "asid":
        return this.hermit.readregister(register + this.hermit.getSIDAddress(chip));
    }
  },

  /**
   * Return the 8-bit value of a C64 memory address.
   *
   * @handlers resid, websid, legacy, hermit, asid
   *
   * @param {number} address		Address $0000 to $FFFF
   *
   * @return {number}				Byte value of the register
   */
  readMemory: function (address) {
    switch (this.emulator) {
      case "websid":
				return this.websidem.getRAM(address);
      case "hermit":
      case "webusb":
      case "asid":
        return this.hermit.readregister(address);
    }
  },

  /**
   * Apply an advanced setting as previously stored or set to a default.
   *
   * @handlers all
   *
   * @param {string} emulator		Emulator, e.g. "resid", "jsidplay2", etc.
   * @param {string} setting 		Setting in one word, e.g. "defemu" or "sampmethod"
   * @param {string} preset 		Default value if not in local storage
   *
   * @return {string}				The value of the advanced setting
   */
  applyAdvancedSetting: function (emulator, setting, preset) {
    var storedValue = localStorage.getItem("advanced_setting_" + emulator + "_" + setting);
    this.advancedSetting[emulator][setting] = storedValue ? storedValue : preset;

    return this.advancedSetting[emulator][setting];
  },
}
