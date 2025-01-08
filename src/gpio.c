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

#include "globals.h"
#include "config.h"
#include "gpio.h"
#include "logging.h"
#include "sid.h"


/* Init external vars */
extern Config usbsid_config;
extern uint8_t sid_memory[];
extern uint8_t one, two, three, four;
extern int sock_one, sock_two, sids_one, sids_two, numsids, act_as_one;
extern double cpu_us, sid_hz, sid_mhz, sid_us;

/* Init vars */
PIO bus_pio = pio0;
static uint sm_control, offset_control;
static uint sm_data, offset_data;
static uint sm_clock, offset_clock;
static uint sm_delay, offset_delay;
static int dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay;
static uint16_t control_word, delay_word;
static uint32_t data_word, read_data, dir_mask;
static float sidclock_frequency, busclock_frequency;
static int pico_hz = 0;

/* Read GPIO macro
 *
 * The following 2 lines (var naming changed) are copied from SKPico code by frenetic
 * see: https://github.com/frntc/SIDKick-pico
 */
register uint32_t b asm( "r10" );
volatile const uint32_t *BUSState = &sio_hw->gpio_in;


void init_gpio()
{
  /* GPIO defaults for PIO bus */
  gpio_set_dir(RES, GPIO_OUT);
  gpio_set_function(RES, GPIO_FUNC_SIO);
  gpio_put(RES, 0);
  gpio_put(RES, 1);  /* RESET TO HIGH */
  gpio_init(CS1);
  gpio_init(CS2);
  gpio_init(RW);
  gpio_put(CS1, 1);
  gpio_put(CS2, 1);
  gpio_put(RW, 0);
  gpio_set_dir(CS1, GPIO_OUT);
  gpio_set_dir(CS2, GPIO_OUT);
  gpio_set_dir(RW, GPIO_OUT);
}

void init_vu(void)
{
  #if defined(PICO_DEFAULT_LED_PIN)  /* Cannot use VU on PicoW :( */
  /* PWM led */
  gpio_init(BUILTIN_LED);
  gpio_set_dir(BUILTIN_LED, GPIO_OUT);
  gpio_set_function(BUILTIN_LED, GPIO_FUNC_PWM);
  /* Init Vu */
  int led_pin_slice = pwm_gpio_to_slice_num(BUILTIN_LED);
	pwm_config configLED = pwm_get_default_config();
	pwm_config_set_clkdiv(&configLED, 1);
	pwm_config_set_wrap(&configLED, 65535);  /* LED max */
	pwm_init(led_pin_slice, &configLED, true);
	gpio_set_drive_strength(BUILTIN_LED, GPIO_DRIVE_STRENGTH_2MA);
	pwm_set_gpio_level(BUILTIN_LED, 0);  /* turn off led */

  #if defined(USE_RGB)
  { /* Init RGB */
    gpio_set_drive_strength(WS2812_PIN, GPIO_DRIVE_STRENGTH_2MA);
  }
  #endif
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  /* For Pico W devices we need to initialise the driver etc */
  cyw43_arch_init();
  /* Ask the wifi "driver" to set the GPIO on or off */
  cyw43_arch_gpio_put(BUILTIN_LED, usbsid_config.LED.enabled);
  #endif
}

void setup_piobus(void)
{
  pico_hz = clock_get_hz(clk_sys);
  busclock_frequency = (float)pico_hz / (usbsid_config.clock_rate * 32) / 2;  /* Clock frequency is 8 times the SID clock */
  CFG("[BUS CLK INIT] START\n");
  CFG("[PI CLK]@%dMHz [DIV]@%.2f [BUS CLK]@%.2f [CFG SID CLK]%d\n",
     (pico_hz / 1000 / 1000),
     busclock_frequency,
     ((float)pico_hz / busclock_frequency / 2),
     (int)usbsid_config.clock_rate);

  { /* control bus */
    offset_control = pio_add_program(bus_pio, &bus_control_program);
    sm_control = 1;  /* PIO1 SM1 */
    pio_sm_claim(bus_pio, sm_control);
    for (uint i = RW; i < CS2 + 1; ++i)
      pio_gpio_init(bus_pio, i);
    pio_sm_config c_control = bus_control_program_get_default_config(offset_control);
    sm_config_set_out_pins(&c_control, RW, 3);
    sm_config_set_in_pins(&c_control, D0);
    sm_config_set_jmp_pin(&c_control, RW);
    sm_config_set_clkdiv(&c_control, busclock_frequency);
    pio_sm_init(bus_pio, sm_control, offset_control, &c_control);
    pio_sm_set_enabled(bus_pio, sm_control, true);
  }

  { /* databus */
    offset_data = pio_add_program(bus_pio, &data_bus_program);
    sm_data = 2;  /* PIO1 SM2 */
    pio_sm_claim(bus_pio, sm_data);
    for (uint i = D0; i < A5 + 1; ++i) {
      pio_gpio_init(bus_pio, i);
    }
    pio_sm_config c_data = data_bus_program_get_default_config(offset_data);
    pio_sm_set_pindirs_with_mask(bus_pio, sm_data, PIO_PINDIRMASK, PIO_PINDIRMASK);  /* WORKING */
    sm_config_set_out_pins(&c_data, D0, A5 + 1);
    sm_config_set_fifo_join(&c_data, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c_data, busclock_frequency);
    pio_sm_init(bus_pio, sm_data, offset_data, &c_data);
    pio_sm_set_enabled(bus_pio, sm_data, true);
  }

  { /* delay counter */
    sm_delay = 3;  /* PIO1 SM3 */
    pio_sm_claim(bus_pio, sm_delay);
    offset_delay = pio_add_program(bus_pio, &delay_timer_program);
    pio_sm_config c_delay = delay_timer_program_get_default_config(offset_delay);
    sm_config_set_sideset_pins(&c_delay, NIL0);
    sm_config_set_sideset(&c_delay, 1, false, false);
    sm_config_set_fifo_join(&c_delay, PIO_FIFO_JOIN_TX);
    pio_gpio_init(bus_pio, NIL0);
    gpio_set_function(NIL0, pio_get_funcsel(bus_pio));
    pio_sm_set_consecutive_pindirs(bus_pio, sm_delay, NIL0, 1, true);
    pio_sm_init(bus_pio, sm_delay, offset_delay, &c_delay);
    pio_sm_set_enabled(bus_pio, sm_delay, true);
  }

  CFG("[BUS CLK INIT] FINISHED\n");
}

void setup_dmachannels(void)
{ /* NOTE: Do not manually assign DMA channels, this causes a Panic on the  PicoW */
  CFG("[DMA CHANNELS INIT] START\n");
  dma_claim_mask(0b1111);

  { /* dma controlbus */
    // dma_tx_control = dma_claim_unused_channel(true);
    dma_tx_control = 0;
    dma_channel_config tx_config_control = dma_channel_get_default_config(dma_tx_control);
    channel_config_set_transfer_data_size(&tx_config_control, DMA_SIZE_16);
    channel_config_set_read_increment(&tx_config_control, true);
    channel_config_set_write_increment(&tx_config_control, false);
    channel_config_set_dreq(&tx_config_control, pio_get_dreq(bus_pio, sm_control, true));
    dma_channel_configure(dma_tx_control, &tx_config_control, &bus_pio->txf[sm_control], NULL, 1, false);
  }

  { /* dma tx databus */
    // dma_tx_data = dma_claim_unused_channel(true);
    dma_tx_data = 1;
    dma_channel_config tx_config_data = dma_channel_get_default_config(dma_tx_data);
    channel_config_set_transfer_data_size(&tx_config_data, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_config_data, true);
    channel_config_set_write_increment(&tx_config_data, false);
    channel_config_set_dreq(&tx_config_data, pio_get_dreq(bus_pio, sm_data, true));
    dma_channel_configure(dma_tx_data, &tx_config_data, &bus_pio->txf[sm_data], NULL, 1, false);
  }

  { /* dma rx databus */
    // dma_rx_data = dma_claim_unused_channel(true);
    dma_rx_data = 2;
    dma_channel_config rx_config = dma_channel_get_default_config(dma_rx_data);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_dreq(&rx_config, pio_get_dreq(bus_pio, sm_control, false));
    dma_channel_configure(dma_rx_data, &rx_config, NULL, &bus_pio->rxf[sm_control], 1, false);
  }

  { /* dma delaytimerbus */
    // dma_tx_delay = dma_claim_unused_channel(true);
    dma_tx_delay = 3;
    dma_channel_config tx_config_delay = dma_channel_get_default_config(dma_tx_delay);
    channel_config_set_transfer_data_size(&tx_config_delay, DMA_SIZE_16 /* DMA_SIZE_32 */);
    channel_config_set_dreq(&tx_config_delay, pio_get_dreq(bus_pio, sm_delay, true));
    dma_channel_configure(dma_tx_delay, &tx_config_delay, &bus_pio->txf[sm_delay], NULL, 1, false);
  }
  CFG("[DMA CHANNELS CLAIMED] C:%d TX:%d RX:%d D:%d\n", dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay);

  CFG("[DMA CHANNELS INIT] FINISHED\n");
}

void sync_pios(void)
{ /* Sync PIO's */
  #if PICO_PIO_VERSION == 0
  CFG("[RESTART PIOS] Pico & Pico_w\n");
  pio_sm_restart(bus_pio, 0b1111);
  #elif PICO_PIO_VERSION > 0  // NOTE: rp2350 only
  CFG("[SYNC PIOS] Pico2\n");
  pio_clkdiv_restart_sm_multi_mask(bus_pio, 0 /* 0b111 */, 0b1111, 0);
  #endif
}

void restart_bus(void)
{
  CFG("[RESTART BUS START]\n");
  /* disable delay timer dma */
  dma_channel_unclaim(dma_tx_delay);
  /* disable databus rx dma */
  dma_channel_unclaim(dma_rx_data);
  /* disable databus tx dma */
  dma_channel_unclaim(dma_tx_data);
  /* disable control bus dma */
  dma_channel_unclaim(dma_tx_control);
  /* disable delay */
  pio_sm_set_enabled(bus_pio, sm_delay, false);
  pio_remove_program(bus_pio, &delay_timer_program, offset_delay);
  pio_sm_unclaim(bus_pio, sm_delay);
  /* disable databus */
  pio_sm_set_enabled(bus_pio, sm_data, false);
  pio_remove_program(bus_pio, &data_bus_program, offset_data);
  pio_sm_unclaim(bus_pio, sm_data);
  /* disable control bus */
  pio_sm_set_enabled(bus_pio, sm_control, false);
  pio_remove_program(bus_pio, &bus_control_program, offset_control);
  pio_sm_unclaim(bus_pio, sm_control);
  /* start piobus */
  setup_piobus();
  /* start dma */
  setup_dmachannels();
  /* sync pios */
  sync_pios();
  CFG("[RESTART BUS END]\n");
}

/* Detect clock signal */
int detect_clocksignal(void)
{
  CFG("[DETECT CLOCK] START\n");
  int c = 0, r = 0;
  gpio_init(PHI);
  gpio_set_pulls(PHI, false, true);
  for (int i = 0; i < 20; i++) {
    b = *BUSState; /* read complete bus */
    // DBG("[BUS]0x%"PRIu32", 0b"PRINTF_BINARY_PATTERN_INT32"", b, PRINTF_BYTE_TO_BINARY_INT32(b));
    r |= c = (b & bPIN(PHI)) >> PHI;
    // DBG(" [PHI2]%d [R]%d\r\n", c, r);
  }
  CFG("[RESULT] %d: %s\n", r, (r == 0 ? "INTERNAL CLOCK" : "EXTERNAL CLOCK"));
  CFG("[DETECT CLOCK] END\n");
  return r;  /* 1 if clock detected */
}

/* Init nMHz square wave output */
void init_sidclock(void)
{
  // float freq = (float)clock_get_hz(clk_sys) / usbsid_config.clock_rate / 2;
  sidclock_frequency = (float)clock_get_hz(clk_sys) / usbsid_config.clock_rate / 2;
  CFG("[SID CLK INIT] START\n");
  CFG("[PI CLK]@%.2fMHz [DIV]@%.2f [SID CLK]@%.2f [CFG SID CLK]%d\n",
    (float)clock_get_hz(clk_sys) / 1000 / 1000,
    sidclock_frequency,
    ((float)clock_get_hz(clk_sys) / sidclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  offset_clock = pio_add_program(bus_pio, &clock_program);
  sm_clock = 0;  /* PIO1 SM0 */
  pio_sm_claim(bus_pio, sm_clock);
  clock_program_init(bus_pio, sm_clock, offset_clock, PHI, sidclock_frequency);
  CFG("[SID CLK INIT] FINISHED\n");
}

/* De-init nMHz square wave output */
void deinit_sidclock(void)
{
  CFG("[DE-INIT CLOCK]\n");
  clock_program_deinit(bus_pio, sm_clock, offset_clock, clock_program);
}

uint8_t __not_in_flash_func(bus_operation)(uint8_t command, uint8_t address, uint8_t data)
{
  if ((command & 0xF0) != 0x10) {
    return 0; // Sync bit not set, ignore operation
  }
  bool is_read = (command & 0x0F) == 0x01;
  pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, 4));  /* Preset the statemachine IRQ to not wait for a 1 */
  pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, 5));  /* Preset the statemachine IRQ to not wait for a 1 */
  switch (command & 0x0F) {
    case PAUSE:
      control_word = 0b110110;
      dma_channel_set_read_addr(dma_tx_control, &control_word, true); /* Control lines RW, CS1 & CS2 DMA transfer */
      break;
    case CLEAR_BUS:
      dir_mask = 0b1111111111111111;
      data_word = (dir_mask << 16) | 0x0;
      dma_channel_set_read_addr(dma_tx_data, &data_word, true); /* Data & Address DMA transfer */
      break;
    case WRITE:
      sid_memory[address] = data;
    case READ:
    default:
      // control_word = 0b110000;
      control_word = 0b111000;
      data_word = (address & 0x3F) << 8 | data;
      dir_mask = 0x0;
      dir_mask |= (is_read ? 0b1111111100000000 : 0b1111111111111111);
      data_word = (dir_mask << 16) | data_word;
      control_word |= (is_read ? 1 : 0);
      switch (address) {
        case 0x00 ... 0x1F:
          control_word |= one;
          break;
        case 0x20 ... 0x3F:
          control_word |= two;
          break;
        case 0x40 ... 0x5F:
          control_word |= three;
          break;
        case 0x60 ... 0x7F:
          control_word |= four;
          break;
      }
      dma_channel_set_read_addr(dma_tx_data, &data_word, true); /* Data & Address DMA transfer */
      pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, 22));
      dma_channel_set_read_addr(dma_tx_control, &control_word, true); /* Control lines RW, CS1 & CS2 DMA transfer */
      pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, 22));
      break;
  }

  switch (command & 0x0F) {
    case READ:
      read_data = 0x0;
      dma_channel_set_write_addr(dma_rx_data, &read_data, true);
      dma_channel_wait_for_finish_blocking(dma_rx_data);
      GPIODBG("[R]$%08x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16"\r\n", read_data, PRINTF_BYTE_TO_BINARY_INT32(read_data), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word));
      return (read_data >> 24) & 0xFF;
    case PAUSE:
    case WRITE:
      dma_channel_wait_for_finish_blocking(dma_tx_control);
      GPIODBG("[W]$%08x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16"\r\n", data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word));
      return 0;
    case CLEAR_BUS:
      /* don't wait, just fall through */
    default:
      return 0;
  }
}

void __not_in_flash_func(cycled_bus_operation)(uint8_t address, uint8_t data, uint16_t cycles)
{
  delay_word = cycles;
  // dma_channel_set_read_addr(dma_tx_delay, &delay_word, true); /* Delay cycles DMA transfer */
  if (cycles >= 2) {  // ISSUE: This is randomly chosen!
    dma_channel_set_read_addr(dma_tx_delay, &delay_word, true); /* Delay cycles DMA transfer */
  } else {
    pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, 4));  /* Preset the statemachine IRQ to not wait for a 1 */
    pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, 5));  /* Preset the statemachine IRQ to not wait for a 1 */
  }
  if (address == 0xFF && data == 0xFF) return;
  sid_memory[address] = data;
  control_word = 0b111000;  /* Always WRITE never READ */
  // control_word = 0b110000;  /* Always WRITE never READ */
  data_word = (address & 0x3F) << 8 | data;
  dir_mask = 0b1111111111111111;  /* Always OUT never IN */
  data_word = (dir_mask << 16) | data_word;
  switch (address) {
    case 0x00 ... 0x1F:
      control_word |= one;
      break;
    case 0x20 ... 0x3F:
      control_word |= two;
      break;
    case 0x40 ... 0x5F:
      control_word |= three;
      break;
    case 0x60 ... 0x7F:
      control_word |= four;
      break;
  }
  dma_channel_set_read_addr(dma_tx_control, &control_word, true); /* Control lines RW, CS1 & CS2 DMA transfer */
  dma_channel_set_read_addr(dma_tx_data, &data_word, true); /* Data & Address DMA transfer */
  dma_channel_wait_for_finish_blocking(dma_tx_control);
  return;
}

void enable_sid(void)
{
  gpio_put(RES, 1);
  for (int i = 0; i < numsids; i++) {
    bus_operation((0x10 | WRITE), ((0x20 * i) + 0x18), 0x0F);  /* Volume to 0 */
  }
}

void disable_sid(void)
{
  for (int i = 0; i < numsids; i++) {
    bus_operation((0x10 | WRITE), ((0x20 * i) + 0x18), 0x0);  /* Volume to 0 */
  }
  gpio_put(CS1, 1);
  gpio_put(CS2, 1);
  gpio_put(RES, 0);
}

void clear_bus(void)
{
  bus_operation((0x10 | CLEAR_BUS), 0x0, 0x0);
}

void pause_sid(void)
{
  bus_operation((0x10 | PAUSE), 0x0, 0x0);
}

void reset_sid(void)
{
  /* disable_sid();
  clear_bus();
  enable_sid(); */
  gpio_put(CS1, 1);
  gpio_put(CS2, 1);
  gpio_put(RES, 0);
  gpio_put(RES, 1);
}

void clear_sid_registers(void)
{
  for (int sid = 0; sid < numsids; sid++) {
    for (int reg = 0; reg < count_of(sid_registers); reg++) {
      bus_operation((0x10 | WRITE), sid_registers[reg], 0x0);
    }
  }
  clear_bus();
}
