/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * gpio.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024 LouD
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "usbsid.h"

/* The following 2 lines (var naming changed) are copied from frenetic his SKPico code */
register uint32_t b asm( "r10" );
volatile const uint32_t *BUSState = &sio_hw->gpio_in;
/* https://github.com/frntc/SIDKick-pico */

void initPins(void)
{
  for ( int i = 0; i <= 21; i++ )	{  /* init pin 0 -> 21 */
		if (i != TX || i != RX)  /* do not init pins 16 & 17 because uart */
      goto init;
    init:
      gpio_set_dir(i, GPIO_OUT);
      gpio_set_function(i, GPIO_FUNC_SIO);
      gpio_put(i, 0);
  }

  /* RES,CS & RW HIGH */
  gpio_put_masked(INIT_MASK, 0x3C0000);  /* 0b111000000000000000000 */

  /* PWM led */
  gpio_init( BUILTIN_LED );
  gpio_set_dir(BUILTIN_LED, GPIO_OUT);
  gpio_set_function(BUILTIN_LED, GPIO_FUNC_PWM);
  /* Init Vue */
  int led_pin_slice = pwm_gpio_to_slice_num( BUILTIN_LED );
	pwm_config configLED = pwm_get_default_config();
	pwm_config_set_clkdiv( &configLED, 1 );
	pwm_config_set_wrap( &configLED, 65535 );  /* LED max */
	pwm_init( led_pin_slice, &configLED, true );
	gpio_set_drive_strength( BUILTIN_LED, GPIO_DRIVE_STRENGTH_2MA );
	pwm_set_gpio_level( BUILTIN_LED, 0 );  /* turn off led */

  // read_uS = to_us_since_boot(get_absolute_time());
  // write_uS = to_us_since_boot(get_absolute_time());

  #if defined(USE_RGB)
  /* Init RGB */
  gpio_init( 23 );
  // gpio_set_function(23, GPIO_FUNC_PWM);
  gpio_set_dir(23, GPIO_OUT);
  gpio_set_drive_strength( 23, GPIO_DRIVE_STRENGTH_2MA );
  #endif

}

void resetPins(void)
{
  sio_hw->gpio_clr = BUS_PINMASK;
  for ( int i = 0; i < 21; i++ ) {
    gpio_deinit( i );
  }
}

void clearBus(void)
{
  gpio_clr_mask(ADDRPINS_PINMASK | DATAPINS_PINMASK);
}

void pauseSID(void)
{
  gpio_put_masked(CS_MASK, (bPIN(CS) | bPIN(CS2)));
}

void resetSID(void)
{
  // gpio_put(CS, 1);
  // gpio_put(RES, 0);
  // gpio_put_masked(CSRW_MASK, (bPIN(CS) | bPIN(CS2)));

  disableSID();
  // gpio_put(RES, 0);
  clearBus();
  sleep_us(1);
  enableSID();
  // gpio_put(RES, 1);

  // enableSID();
  // gpio_put(RES, 1);
  // gpio_put(CS, 0);
  // gpio_put_masked(bPIN(RW), bPIN(RW));
}

void enableSID(void)
{
  // gpio_put(CS, 0);
  gpio_put(RES, 1);
  // gpio_put_masked((CSRW_MASK | bPIN(RES)), (bPIN(RES) | bPIN(CS) | bPIN(CS2)));
  for (int i = 0; i < NUMSIDS; i++) {
    writeSID(0xD400 | ((0x20 * i) + 0x18), 0x0F);  /* Volume to 15 */
  }
}

void disableSID(void)
{
  for (int i = 0; i < NUMSIDS; i++) {
    writeSID(0xD400 | ((0x20 * i) + 0x18), 0x0);  /* Volume to 0 */
  }
  gpio_put(CS, 1);
  gpio_put(CS2, 1);
  gpio_put(RES, 0);
  // gpio_put_masked((CSRW_MASK | bPIN(RES)), (bPIN(CS) | bPIN(CS2)));
}

uint8_t readSID(uint16_t addr)
{/* ISSUE: Reading the config tool from the SKPico misses data */
  uint32_t cs = ((addr & 0xFF) >= 0x40)  /* addr in range $D440+ ? */
    ? bPIN(CS)    /* SID1 disable, write CS1 1 HIGH */
    : bPIN(CS2);  /* SID2 disable, write CS2 1 HIGH */

  #if SIDTYPES == SIDTYPE0
  cs = 0;  /* Override ChipSelect */
  #endif

  b = 0;  /* empty g */
  gpio_set_dir_masked(DATAPINS_PINMASK, 0);  /* all datapins to input */

  addr = (addr & ADDR_MASK);

  WAIT_FOR_CPU_HALF_CYCLE  /* QUESTION: DO WE ACTUALLY NEED THIS? ~ W/O THIS READING PRG DOESNT WORK PROPERLY */
  // gpio_put(RW, 1);                     /* write to high  */
  uint16_t pinval = (addr << bADDR);  /* addr pin value to write */
  // gpio_put_masked(ADDRPINS_PINMASK, pinval);  /* 0b0111111100000000 */
  gpio_put_masked((CSRW_MASK | ADDRPINS_PINMASK), (bPIN(RW) | cs | pinval));  /* 0b0111111100000000 */
  // gpio_put(cs, 0);  /* chipselect low */
  sleep_us(0.238);  /* 238 nano seconds as per datasheet */
  b = *BUSState; /* read complete bus */
  gpio_put_masked((CSRW_MASK | ADDRPINS_PINMASK | DATAPINS_PINMASK), (bPIN(CS) | bPIN(CS2)));  /* reset bus for writing */

  gpio_set_dir_masked(DATAPINS_PINMASK, 0xFF);  /* all datapins to output */

  return (b & DATAPINS_PINMASK);
}

void writeSID(uint16_t addr, uint8_t val)
{/* ISSUE: Notes are sometimes off !? */
  uint32_t cs = ((addr & 0xFF) >= 0x40)  /* addr in range $D440+ ? */
    ? bPIN(CS)    /* SID1 disable, write CS1 1 HIGH */
    : bPIN(CS2);  /* SID2 disable, write CS2 1 HIGH */

  #if SIDTYPES == SIDTYPE0
  cs = 0;  /* Override ChipSelect */
  #endif

  addr = (addr & ADDR_MASK);  /* create addr values */
  val = (val & DATA_MASK);  /* create data values */

  uint32_t pinval = ((addr << bADDR) | val | cs);  /* create pico pin values */

  WAIT_FOR_VIC_HALF_CYCLE  /* QUESTION: DO WE ACTUALLY NEED THIS? */
  gpio_put_masked((CSRW_MASK | ADDRPINS_PINMASK | DATAPINS_PINMASK), pinval);  /* write cs, rw, addr and data */
  sleep_us(0.360); /* Sleep for exactly 360 ns ~ write pulse width + hold time */
  gpio_put_masked(CSRW_MASK, (bPIN(RW) | bPIN(CS) | bPIN(CS2)));  /* write cs and rw */
}
