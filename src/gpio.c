/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * gpio.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024-2025 LouD
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


/* USBSID */
extern uint8_t __not_in_flash("usbsid_buffer") sid_memory[(0x20 * 4)] __attribute__((aligned(2 * (0x20 * 4))));
extern int paused_state, reset_state;

/* Config */
extern void verify_clockrate(void);
extern Config usbsid_config;
extern RuntimeCFG cfg;
extern const char* pcb_version;

/* Config logging */
extern char *mono_stereo[2];

/* Vu */
extern uint16_t vu;

/* Declare variables */
PIO bus_pio = pio0;

/* Declare local variables */
static uint sm_control, offset_control;
static uint sm_data, offset_data;
static uint sm_clock, offset_clock;
static uint sm_delay, offset_delay;
static int dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay;
static uint8_t control_word, read_data;
static uint16_t delay_word;
static uint32_t data_word, dir_mask;
static float sidclock_frequency, busclock_frequency;
static uint8_t volume_state[4] = {0};

#if defined(PICO_DEFAULT_LED_PIN)
PIO led_pio = pio1;
uint sm_pwmled, offset_pwmled;
int dma_pwmled;
int pwm_value = 0;
#if defined(USE_RGB)  /* No RGB LED on _w Pico's */
uint sm_rgbled, offset_rgbled;
int dma_rgbled;
uint32_t rgb_value;
#endif
#endif

/* Read GPIO macro
 *
 * The following 3 lines are partly copied  from and inspired by SKPico code by frenetic
 * see: https://github.com/frntc/SIDKick-pico
 */
register uint32_t b asm( "r10" );
volatile const uint32_t *BUSState = &sio_hw->gpio_in;
volatile const uint32_t *IRQState = &pio0_hw->irq;


void init_gpio(void)
{ /* GPIO defaults for PIO bus */
  gpio_set_dir(RES, GPIO_OUT);
  gpio_set_function(RES, GPIO_FUNC_SIO);
  gpio_put(RES, 0);  /* Make sure we do not yet enable the SID on boot! */
  gpio_init(CS1);
  gpio_init(CS2);
  gpio_init(RW);
  gpio_set_dir(CS1, GPIO_OUT);
  gpio_set_dir(CS2, GPIO_OUT);
  gpio_set_dir(RW, GPIO_OUT);
  gpio_put(CS1, 1);
  gpio_put(CS2, 1);
  gpio_put(RW, 1);
  /* GPIO defaults for audio switch */
  #if defined(HAS_AUDIOSWITCH)
  gpio_set_dir(AU_SW, GPIO_OUT);
  gpio_set_function(AU_SW, GPIO_FUNC_SIO);
  gpio_put(AU_SW, usbsid_config.stereo_en);  /* Default on ~ stereo */
  #endif
  return;
}

void setup_vu(void)
{
  #if defined(PICO_DEFAULT_LED_PIN)  /* Cannot use VU on PicoW :( */
  { /* PWM led */
    offset_pwmled = pio_add_program(led_pio, &vu_program);
    sm_pwmled = 1;  /* PIO1 SM1 */
    pio_sm_claim(led_pio, sm_pwmled);
    pio_gpio_init(led_pio, BUILTIN_LED);
    pio_sm_set_consecutive_pindirs(led_pio, sm_pwmled, BUILTIN_LED, 1, true);
    pio_sm_config c_ledpwm = vu_program_get_default_config(offset_pwmled);
    sm_config_set_sideset_pins(&c_ledpwm, BUILTIN_LED);
    pio_sm_init(led_pio, sm_pwmled, offset_pwmled, &c_ledpwm);
    pio_sm_set_enabled(led_pio, sm_pwmled, false);
    pio_sm_put(led_pio, sm_pwmled, 65534);  /* VU_MAX = 65534 */
    pio_sm_exec(led_pio, sm_pwmled, pio_encode_pull(false, false));
    pio_sm_exec(led_pio, sm_pwmled, pio_encode_out(pio_isr, 32));
    pio_sm_set_enabled(led_pio, sm_pwmled, true);
  }

  { /* dma pwmled */
    dma_pwmled = dma_claim_unused_channel(true);
    dma_channel_config c_pwmled = dma_channel_get_default_config(dma_pwmled);
    channel_config_set_transfer_data_size(&c_pwmled, DMA_SIZE_32);
    channel_config_set_read_increment(&c_pwmled, false);
    channel_config_set_write_increment(&c_pwmled, false);
    channel_config_set_dreq(&c_pwmled, DREQ_PIO1_TX0);
    dma_channel_configure(dma_pwmled, &c_pwmled, &led_pio->txf[sm_pwmled], &pwm_value, 1, true);
  }

  #if defined(USE_RGB)  /* No RGB LED on _w Pico's */
  { /* Init RGB */
    gpio_set_drive_strength(WS2812_PIN, GPIO_DRIVE_STRENGTH_2MA);
    offset_rgbled = pio_add_program(led_pio, &vu_rgb_program);
    sm_rgbled = 2;  /* PIO1 SM2 */
    pio_sm_claim(led_pio, sm_rgbled);
    pio_gpio_init(led_pio, WS2812_PIN);
    pio_sm_set_consecutive_pindirs(led_pio, sm_rgbled, WS2812_PIN, 1, true);
    pio_sm_config c_rgbled = vu_rgb_program_get_default_config(offset_rgbled);
    sm_config_set_sideset_pins(&c_rgbled, WS2812_PIN);
    /* RGBW LED ? 32 : 24 */
    sm_config_set_out_shift(&c_rgbled, false, true, 24);
    sm_config_set_fifo_join(&c_rgbled, PIO_FIFO_JOIN_TX);
    float freq = 800000;
    int cycles_per_bit = vu_rgb_T1 + vu_rgb_T2 + vu_rgb_T3;
    float div = clock_get_hz(clk_sys) / (freq * cycles_per_bit);
    sm_config_set_clkdiv(&c_rgbled, div);
    pio_sm_init(led_pio, sm_rgbled, offset_rgbled, &c_rgbled);
    pio_sm_set_enabled(led_pio, sm_rgbled, true);
  }

  { /* dma RGBled */
    dma_rgbled = dma_claim_unused_channel(true);
    dma_channel_config c_rgbled = dma_channel_get_default_config(dma_rgbled);
    channel_config_set_transfer_data_size(&c_rgbled, DMA_SIZE_32);
    channel_config_set_read_increment(&c_rgbled, false);
    channel_config_set_write_increment(&c_rgbled, false);
    channel_config_set_dreq(&c_rgbled, DREQ_PIO1_TX1);
    dma_channel_configure(dma_rgbled, &c_rgbled, &led_pio->txf[sm_rgbled], &rgb_value, 1, true);
  }
  #endif
  #elif defined(CYW43_WL_GPIO_LED_PIN)
  /* For Pico W devices we need to initialise the driver etc */
  cyw43_arch_init();
  /* Ask the wifi "driver" to set the GPIO on or off */
  cyw43_arch_gpio_put(BUILTIN_LED, usbsid_config.LED.enabled);
  #endif
  return;
}

void setup_piobus(void)
{
  uint32_t pico_hz = clock_get_hz(clk_sys);
  busclock_frequency = (float)pico_hz / (usbsid_config.clock_rate * 32) / 2;  /* Clock frequency is 8 times the SID clock */

  CFG("[BUS CLK INIT] START\n");
  CFG("[PI CLK]@%dMHz [DIV]@%.2f [BUS CLK]@%.2f [CFG SID CLK]%d\n",
     (pico_hz / 1000 / 1000),
     busclock_frequency,
     ((float)pico_hz / busclock_frequency / 2),
     (int)usbsid_config.clock_rate);

  { /* control bus */
    sm_control = 1;  /* PIO0 SM1 */
    pio_sm_claim(bus_pio, sm_control);
    offset_control = pio_add_program(bus_pio, &bus_control_program);
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
    sm_data = 2;  /* PIO0 SM2 */
    pio_sm_claim(bus_pio, sm_data);
    offset_data = pio_add_program(bus_pio, &data_bus_program);
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
    sm_delay = 3;  /* PIO0 SM3 */
    pio_sm_claim(bus_pio, sm_delay);
    offset_delay = pio_add_program(bus_pio, &delay_timer_program);
    pio_sm_config c_delay = delay_timer_program_get_default_config(offset_delay);
    sm_config_set_fifo_join(&c_delay, PIO_FIFO_JOIN_TX);
    pio_sm_init(bus_pio, sm_delay, offset_delay, &c_delay);
    pio_sm_set_enabled(bus_pio, sm_delay, true);
  }

  CFG("[BUS CLK INIT] FINISHED\n");
  return;
}

void setup_dmachannels(void)
{ /* NOTE: Do not manually assign DMA channels, this causes a Panic on the PicoW */
  CFG("[DMA CHANNELS INIT] START\n");

  /* NOTICE: DMA read address is disabled for now, it is causing confirmed desync on the rp2040 (rp2350 seems to works, but needs improving) */
  /* NOTICE: DMA chaining on rp2350 causes desync with cycled writes, probably due to RP2350-E8 */
  /* NOTE: Until I find a fix for the DMA chaining with the delay counter it's disabled */

  dma_tx_control = dma_claim_unused_channel(true);
  dma_tx_data = dma_claim_unused_channel(true);
  dma_rx_data = dma_claim_unused_channel(true);
  dma_tx_delay = dma_claim_unused_channel(true);

  { /* dma controlbus */
    dma_channel_config tx_config_control = dma_channel_get_default_config(dma_tx_control);
    channel_config_set_transfer_data_size(&tx_config_control, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_config_control, false);
    channel_config_set_write_increment(&tx_config_control, false);
    channel_config_set_dreq(&tx_config_control, DREQ_PIO0_TX1);
    //#if PICO_PIO_VERSION == 0  /* rp2040 only for now, see notice above */
    //channel_config_set_chain_to(&tx_config_control, dma_tx_data); /* Chain to controlbus tx */
    //#endif
    dma_channel_configure(dma_tx_control, &tx_config_control, &bus_pio->txf[sm_control], NULL, 1, false);
  }

  { /* dma tx databus */
    dma_channel_config tx_config_data = dma_channel_get_default_config(dma_tx_data);
    channel_config_set_transfer_data_size(&tx_config_data, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_config_data, false);
    channel_config_set_write_increment(&tx_config_data, false);
    channel_config_set_dreq(&tx_config_data, DREQ_PIO0_TX2);
    dma_channel_configure(dma_tx_data, &tx_config_data, &bus_pio->txf[sm_data], NULL, 1, false);
  }

  { /* dma rx databus */
    dma_channel_config rx_config_data = dma_channel_get_default_config(dma_rx_data);
    channel_config_set_transfer_data_size(&rx_config_data, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config_data, false);
    channel_config_set_write_increment(&rx_config_data, false);
    channel_config_set_dreq(&rx_config_data, DREQ_PIO0_RX1);
    dma_channel_configure(dma_rx_data, &rx_config_data, NULL, &bus_pio->rxf[sm_control], 1, false);
  }

  { /* dma delaytimerbus */
    dma_channel_config tx_config_delay = dma_channel_get_default_config(dma_tx_delay);
    channel_config_set_transfer_data_size(&tx_config_delay, DMA_SIZE_16);
    channel_config_set_read_increment(&tx_config_delay, false);
    channel_config_set_write_increment(&tx_config_delay, false);
    channel_config_set_dreq(&tx_config_delay, DREQ_PIO0_TX3);
    //#if PICO_PIO_VERSION == 0  /* rp2040 only for now, see notice above */
    //channel_config_set_chain_to(&tx_config_delay, dma_tx_control); /* Chain to controlbus tx */
    //#endif
    dma_channel_configure(dma_tx_delay, &tx_config_delay, &bus_pio->txf[sm_delay], NULL, 1, false);
  }
  CFG("[DMA CHANNELS CLAIMED] C:%d TX:%d RX:%d D:%d\n", dma_tx_control, dma_tx_data, dma_rx_data, dma_tx_delay);

  CFG("[DMA CHANNELS INIT] FINISHED\n");
  return;
}

void sync_pios(bool at_boot)
{ /* Sync PIO's */
  #if PICO_PIO_VERSION == 0
  CFG("[RESTART PIOS] Pico & Pico_w\n");
  pio_sm_restart(bus_pio, 0b1111);
  #elif PICO_PIO_VERSION > 0  // NOTE: rp2350 only
  CFG("[SYNC PIOS] Pico2\n");
  pio_clkdiv_restart_sm_multi_mask(bus_pio, 0, 0b1111, 0);
  #endif
  if (!at_boot) {
    pio_sm_clear_fifos(bus_pio, sm_control);
    pio_sm_clear_fifos(bus_pio, sm_data);
    pio_sm_clear_fifos(bus_pio, sm_delay);
  };
  return;
}

void restart_bus_clocks(void)
{
  CFG("[CLK RE-INIT] START\n");
  uint32_t pico_hz = clock_get_hz(clk_sys);
  busclock_frequency = (float)pico_hz / (usbsid_config.clock_rate * 32) / 2;  /* Clock frequency is 8 times the SID clock */
  sidclock_frequency = (float)pico_hz / usbsid_config.clock_rate / 2;
  pio_sm_set_clkdiv(bus_pio, sm_clock, sidclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_control, busclock_frequency);
  pio_sm_set_clkdiv(bus_pio, sm_data, busclock_frequency);

  CFG("[PI CLK]@%dMHz [DIV]@%.2f [BUS CLK]@%.2f [CFG SID CLK]%d\n",
    (pico_hz / 1000 / 1000),
    busclock_frequency,
    ((float)pico_hz / busclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  CFG("[PI CLK]@%dMHz [DIV]@%.2f [SID CLK]@%.2f [CFG SID CLK]%d\n",
    (pico_hz / 1000 / 1000),
    sidclock_frequency,
    ((float)pico_hz / sidclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  CFG("[CLK RE-INIT] FINISHED\n");
  return;
}

void stop_dma_channels(void)
{ // TODO: Fix and finish per RP2040-E13 and RP2350-E5
  // CFG("[STOP DMA CHANNELS]\n");
  /* Clear any Interrupt enable bits as per RP2040-E13 */
  // TODO: FINISH
  /* Atomically abort channels */
  // dma_hw->abort = (1 << dma_tx_delay) | (1 << dma_rx_data) | (1 << dma_tx_data) | (1 << dma_tx_control);
  /* Wait for all aborts to complete */
  // while (dma_hw->abort) tight_loop_contents();
  /* Check and clear any Interrupt enable bits as per RP2040-E13 */
  // TODO: FINISH
  /* Wait for channels to not be busy */
  // while (dma_hw->ch[dma_tx_delay].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  // while (dma_hw->ch[dma_rx_data].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  // while (dma_hw->ch[dma_tx_data].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  // while (dma_hw->ch[dma_tx_control].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();
  return;
}

void start_dma_channels(void)
{ /* NOTE: DO NOT USE, THIS STARTS A TRANSFER IN THE CURRENT CONFIG */
  /* Trigger -> start dma channels all at once */
  // CFG("[START DMA CHANNELS]\n");
  // dma_hw->multi_channel_trigger = (1 << dma_tx_delay) | (1 << dma_rx_data) | (1 << dma_tx_data) | (1 << dma_tx_control);
  return;
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
  sync_pios(false);
  CFG("[RESTART BUS END]\n");
  return;
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
    r |= c = (b & bPIN(PHI)) >> PHI;
  }
  CFG("[RESULT] %d: %s\n", r, (r == 0 ? "INTERNAL CLOCK" : "EXTERNAL CLOCK"));
  CFG("[DETECT CLOCK] END\n");
  return r;  /* 1 if clock detected */
}

/* Init nMHz square wave output */
void init_sidclock(void)
{
  uint32_t pico_hz = clock_get_hz(clk_sys);
  sidclock_frequency = (float)pico_hz / usbsid_config.clock_rate / 2;

  CFG("[SID CLK INIT] START\n");
  CFG("[PI CLK]@%dMHz [DIV]@%.2f [SID CLK]@%.2f [CFG SID CLK]%d\n",
    (pico_hz / 1000 / 1000),
    sidclock_frequency,
    ((float)pico_hz / sidclock_frequency / 2),
    (int)usbsid_config.clock_rate);
  offset_clock = pio_add_program(bus_pio, &clock_program);
  sm_clock = 0;  /* PIO0 SM0 */
  pio_sm_claim(bus_pio, sm_clock);
  clock_program_init(bus_pio, sm_clock, offset_clock, PHI, sidclock_frequency);
  CFG("[SID CLK INIT] FINISHED\n");
  return;
}

/* Start verification, detect and init sequence of SID clock */
void setup_sidclock(void)
{
  /* Verify the clockrare in the config is not out of bounds */
  verify_clockrate();

  /* Run only if PCB version 1.0 */
  if ((strcmp(pcb_version, "1.0") == 0)) {
    /* Detect optional external crystal */
    if (detect_clocksignal() == 0) {
      usbsid_config.external_clock = false;
      gpio_deinit(PHI); /* Disable PHI as gpio */
      init_sidclock();
    } else {  /* Do nothing gpio acts as input detection */
      usbsid_config.external_clock = true;
      usbsid_config.clock_rate = CLOCK_DEFAULT;  /* Always 1MHz */
    }
  } else {
    usbsid_config.external_clock = false;
    gpio_deinit(PHI); /* Disable PHI as gpio */
    init_sidclock();
  }

}

/* De-init nMHz square wave output */
void deinit_sidclock(void)
{
  CFG("[DE-INIT CLOCK]\n");
  clock_program_deinit(bus_pio, sm_clock, offset_clock, clock_program);
  return;
}

static int __not_in_flash_func(set_bus_bits)(uint8_t address, uint8_t data)
{
  /* CFG("[BUS BITS]$%02X:%02X ", address, data); */
  switch (address) {
    case 0x00 ... 0x1F:
      if (cfg.one == 0b110 || cfg.one == 0b111) return 0;
      data_word = (cfg.one_mask == 0x3f ? ((address & 0x1F) + 0x20) : (address & 0x1F)) << 8 | data;
      control_word |= cfg.one;
      break;
    case 0x20 ... 0x3F:
      if (cfg.two == 0b110 || cfg.two == 0b111) return 0;
      data_word = (cfg.two_mask == 0x3f ? ((address & 0x1F) + 0x20) : address & 0x1F) << 8 | data;
      control_word |= cfg.two;
      break;
    case 0x40 ... 0x5F:
      if (cfg.three == 0b110 || cfg.three == 0b111) return 0;
      /* Workaround for addresses in this range, mask doesn't work properly */
      data_word = (cfg.three_mask == 0x3f ? ((address & 0x1F) + 0x20) : (address & 0x1F)) << 8 | data;
      control_word |= cfg.three;
      break;
    case 0x60 ... 0x7F:
      if (cfg.four == 0b110 || cfg.four == 0b111) return 0;
      data_word = (cfg.four_mask == 0x3f ? ((address & 0x1F) + 0x20) : (address & 0x1F)) << 8 | data;
      control_word |= cfg.four;
      break;
  }
  // CFG("$%02X:%02X $%04X 0b"PRINTF_BINARY_PATTERN_INT32" $%04X 0b"PRINTF_BINARY_PATTERN_INT16"\n",
  //   address, data, data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word));
  return 1;
}
// static int __not_in_flash_func(set_bus_bits)(uint8_t address, uint8_t data)
// {
//   /* CFG("[BUS BITS]$%02X:%02X ", address, data); */
//   switch (address) {
//     case 0x00 ... 0x1F:
//       if (cfg.one == 0b110 || cfg.one == 0b111) return 0;
//       data_word = (address & cfg.one_mask) << 8 | data;
//       control_word |= cfg.one;
//       break;
//     case 0x20 ... 0x3F:
//       if (cfg.two == 0b110 || cfg.two == 0b111) return 0;
//       data_word = (address & cfg.two_mask) << 8 | data;
//       control_word |= cfg.two;
//       break;
//     case 0x40 ... 0x5F:
//       if (cfg.three == 0b110 || cfg.three == 0b111) return 0;
//       /* Workaround for addresses in this range, mask doesn't work properly */
//       address &= cfg.three_mask;
//       data_word = (cfg.three_mask == 0x3f ? (address + 0x20) : address) << 8 | data;
//       control_word |= cfg.three;
//       break;
//     case 0x60 ... 0x7F:
//       if (cfg.four == 0b110 || cfg.four == 0b111) return 0;
//       data_word = (address & cfg.four_mask) << 8 | data;
//       control_word |= cfg.four;
//       break;
//   }
//   /* CFG("$%04x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16"\n",
//     data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word)); */
//   return 1;
// }

uint8_t __no_inline_not_in_flash_func(bus_operation)(uint8_t command, uint8_t address, uint8_t data)
{ /* WARNING: DEPRECATED AND NO LONGER WORKS */
  return 0;
  if ((command & 0xF0) != 0x10) {
    return 0; // Sync bit not set, ignore operation
  }
  int sid_command = (command & 0x0F);
  bool is_read = sid_command == 0x01;
  control_word = data_word = dir_mask = 0;
  control_word = 0b110000;
  dir_mask |= (is_read ? 0b1111111100000000 : 0b1111111111111111);
  control_word |= (is_read ? 1 : 0);
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  if (set_bus_bits(address, data) != 1) {
    return 0;
  }
  data_word = (dir_mask << 16) | data_word;
  switch (sid_command) {
    case WRITE:
      sid_memory[(address & 0x7F)] = data;
      pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI));
      pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI));
      dma_channel_set_read_addr(dma_tx_data, &data_word, true); /* Data & Address DMA transfer */
      dma_channel_set_read_addr(dma_tx_control, &control_word, true); /* Control lines RW, CS1 & CS2 DMA transfer */
      break;
    case READ:
      pio_sm_exec(bus_pio, sm_control, pio_encode_irq_set(false, PIO_IRQ0));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_irq_set(false, PIO_IRQ1));  /* Preset the statemachine IRQ to not wait for a 1 */
      pio_sm_exec(bus_pio, sm_data, pio_encode_wait_pin(true, PHI));
      pio_sm_exec(bus_pio, sm_control, pio_encode_wait_pin(true, PHI));
      /* These are in a different order then WRITE on purpose so we actually get the read result */
      dma_channel_set_read_addr(dma_tx_control, &control_word, true); /* Control lines RW, CS1 & CS2 DMA transfer */
      dma_channel_set_read_addr(dma_tx_data, &data_word, true); /* Data & Address DMA transfer */
      read_data = 0x0;
      dma_channel_set_write_addr(dma_rx_data, &read_data, true);
      dma_channel_wait_for_finish_blocking(dma_rx_data);
      GPIODBG("[W]$%08x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16"\n[R]$%08x 0b"PRINTF_BINARY_PATTERN_INT32"\n",
        data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word),
        control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word),
        read_data, PRINTF_BYTE_TO_BINARY_INT32(read_data));
      sid_memory[(address & 0x7F)] = read_data & 0xFF;
      return read_data & 0xFF;
  }
  /* WRITE, G_PAUSE & G_CLEAR_BUS*/
  dma_channel_wait_for_finish_blocking(dma_tx_control);
  GPIODBG("[W]$%08x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16"\n", data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word));
  return 0;
}

uint16_t __no_inline_not_in_flash_func(cycled_delay_operation)(uint16_t cycles)
{ /* This is a blocking function! */
  if (cycles == 0) return 0; /* No point in waiting zero cycles */
  delay_word = cycles;
  pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ0));  /* Clear the statemachine IRQ before starting */
  pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ1));  /* Clear the statemachine IRQ before starting */
  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_hw->multi_channel_trigger = (1u << dma_tx_delay);  /* Delay cycle s DMA transfer */

  for (;;) {  /* Keep mofo waiting yeah! */
    if (((*IRQState & (1u << PIO_IRQ1)) >> PIO_IRQ1) != 1)
      continue;
    pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ0));  /* Clear the statemachine IRQ after finishing */
    pio_sm_exec(bus_pio, sm_delay, pio_encode_irq_clear(false, PIO_IRQ1));  /* Clear the statemachine IRQ after finishing */
    return cycles;
  }

  return 0;
}

uint8_t __no_inline_not_in_flash_func(cycled_read_operation)(uint8_t address, uint16_t cycles)
{
  delay_word = cycles;
  control_word = data_word = dir_mask = 0;
  control_word = 0b111000;
  dir_mask |= 0b1111111100000000;
  control_word |= 1;
  if (set_bus_bits(address, 0x0) != 1) {
    return 0x00;
  }
  data_word = (dir_mask << 16) | data_word;

  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);
  dma_channel_set_write_addr(dma_rx_data, &read_data, false);
  dma_hw->multi_channel_trigger = (
      1u << dma_tx_delay    /* Delay cycles DMA transfer */
  //#if PICO_PIO_VERSION > 0  /* rp2040 only for now, see notice in setup_dmachannels */
    | 1u << dma_tx_control  /* Control lines RW, CS1 & CS2 DMA transfer */
    | 1u << dma_tx_data     /* Data & Address DMA transfer */
  //#endif
    | 1u << dma_rx_data     /* Read data DMA transfer */
  );
  dma_channel_wait_for_finish_blocking(dma_rx_data);  /* Wait for data */
  sid_memory[(address & 0x7F)] = (read_data & 0xFF);
  return (read_data & 0xFF);
}

void __no_inline_not_in_flash_func(cycled_write_operation)(uint8_t address, uint8_t data, uint16_t cycles)
{
  delay_word = cycles;
  sid_memory[(address & 0x7F)] = data;
  vu = (vu == 0 ? 100 : vu);  /* NOTICE: Testfix for core1 setting dtype to 0 */
  control_word = 0b111000;
  dir_mask = 0b1111111111111111;  /* Always OUT never IN */
  if (set_bus_bits(address, data) != 1) {
    return;
  }
  data_word = (dir_mask << 16) | data_word;

  dma_channel_set_read_addr(dma_tx_delay, &delay_word, false);
  dma_channel_set_read_addr(dma_tx_control, &control_word, false);
  dma_channel_set_read_addr(dma_tx_data, &data_word, false);
  dma_hw->multi_channel_trigger = (
      1u << dma_tx_delay    /* Delay cycles DMA transfer */
  //#if PICO_PIO_VERSION > 0  /* rp2040 only for now, see notice in setup_dmachannels */
    | 1u << dma_tx_control  /* Control lines RW, CS1 & CS2 DMA transfer */
    | 1u << dma_tx_data     /* Data & Address DMA transfer */
  //#endif
  );
  /* DMA wait call
   * dma_tx_control ~ normal
   * dma_tx_data ~ not as good as control, maybe a bit more cracks
   * dma_tx_delay ~ white noise
   * neither ~ broken play
   */
  dma_channel_wait_for_finish_blocking(dma_tx_control);

  GPIODBG("[WC]$%04x 0b"PRINTF_BINARY_PATTERN_INT32" $%04x 0b"PRINTF_BINARY_PATTERN_INT16" $%02X:%02X(%u %u)\n",
    data_word, PRINTF_BYTE_TO_BINARY_INT32(data_word), control_word, PRINTF_BYTE_TO_BINARY_INT16(control_word),
    address, data, cycles, delay_word);
  return;
}

void unmute_sid(void)
{
  DBG("[UNMUTE]\n");
  for (int i = 0; i < cfg.numsids; i++) {
    uint8_t addr = ((0x20 * i) + 0x18);
    if ((volume_state[i] & 0xF) == 0) volume_state[i] = (volume_state[i] & 0xF0) | 0x0E;
    sid_memory[addr] = volume_state[i];
    cycled_write_operation(((0x20 * i) + 0x18), volume_state[i], 0);  /* Volume back */
    DBG("[%d] $%02X:%02X\n", i, addr, volume_state[i]);
  }
  return;
}

void mute_sid(void)
{
  DBG("[MUTE]\n");
  for (int i = 0; i < cfg.numsids; i++) {
    uint8_t addr = ((0x20 * i) + 0x18);
    volume_state[i] = sid_memory[addr];
    cycled_write_operation(addr, (volume_state[i] & 0xF0), 0);  /* Volume to 0 */
    DBG("[%d] $%02X:%02X\n", i, addr, (volume_state[i] & 0xF0));
  }
  return;
}

void enable_sid(bool unmute)
{
  paused_state = 0;
  gpio_put(RES, 1);
  if (unmute) unmute_sid();
  return;
}

void disable_sid(void)
{
  paused_state = 1;
  mute_sid();
  gpio_put(CS1, 1);
  gpio_put(CS2, 1);
  gpio_put(RES, 0);
  return;
}

void clear_bus(int sidno)
{
  cycled_write_operation((sidno * 0x20), 0x0, 0);
  return;
}

void clear_bus_all(void)
{
  for (int sid = 0; sid < cfg.numsids; sid++) {
    clear_bus(sid);
  }
  return;
}

void pause_sid(void)
{
  gpio_put(CS1, 1);
  gpio_put(CS2, 1);
  return;
}

void pause_sid_withmute(void)
{
  DBG("[PAUSE STATE PRE] %d\n", paused_state);
  if (paused_state == 0) mute_sid();
  if (paused_state == 1) unmute_sid();
  gpio_put(CS1, 1);
  gpio_put(CS2, 1);
  paused_state = !paused_state;
  DBG("[PAUSE STATE POST] %d\n", paused_state);
  return;
}

void reset_sid(void)
{
  reset_state = 1;
  paused_state = 0;
  memset(volume_state, 0, 4);
  memset(sid_memory, 0, count_of(sid_memory));
  gpio_put(RES, 0);
  if (cfg.chip_one == 0 || cfg.chip_two == 0) {
    cycled_delay_operation(10);  /* 10x PHI(02) cycles as per datasheet for REAL SIDs only */
  }
  gpio_put(RES, 1);
  reset_state = 0;
  return;
}

void clear_sid_registers(int sidno)
{ /* NOTICE: CAUSES ISSUES IF USED RIGHT BEFORE PLAYING */
  for (int reg = 0; reg < count_of(sid_registers) - 4; reg++) {
    cycled_write_operation(((sidno * 0x20) | sid_registers[reg]), 0x0, 0);
  }
  return;
}

void reset_sid_registers(void)
{ /* NOTICE: CAUSES ISSUES IF USED RIGHT BEFORE PLAYING */
  reset_state = 1;
  paused_state = 0;
  for (int sid = 0; sid < cfg.numsids; sid++) {
    clear_sid_registers(sid);
  }
  reset_state = 0;
  return;
}

void toggle_audio_switch(void)
{ /* Toggle the SPST switch stereo <-> mono */
  #if defined(HAS_AUDIOSWITCH)
  static int audio_state = 0b1;
  audio_state ^= 1;
  CFG("[CONFIG] TOGGLE AUDIO SWITCH TO: %d\n", audio_state);
  tPIN(AU_SW);  /* toggle mono <-> stereo */
  #endif
}

void set_audio_switch(bool state)
{ /* Set the SPST switch */
  #if defined(HAS_AUDIOSWITCH)
  CFG("[CONFIG] SET AUDIO SWITCH TO: %d (%s)\n", state, mono_stereo[state]);
  if (state) {
    sPIN(AU_SW);      /* set   mono <-> stereo pin */
  } else cPIN(AU_SW);  /* clear mono <-> stereo pin */
  #endif
}
