/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * usbsid.c
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

/*--------------------------------------------------------------------
 * GLOBAL VARS / FUNCTION DECLARING
 *--------------------------------------------------------------------*/

uint8_t memory[65536];
uint16_t vue;
midi_machine midimachine;
extern void midi_init(void);


/*--------------------------------------------------------------------
 * MAIN LOOPS
 *--------------------------------------------------------------------*/

void core1_main(void)
{
  if (detect_clock() == 0) {
    gpio_deinit(PHI);
    square_wave();
  } else {
    /* Do nothing gpio is input pulled low */
  }

  #if defined(USE_RGB)
  if (detect_smps() == 1) {
    init_rgb();
    rgb_mode = true;
  } else {
    /* Do nothing gpio is input pulled low */
    rgb_mode = false;
  }
  #endif

  init_memory();

  int x = 0;
  while (1)
  {
    usbdata == 1 ? led_vuemeter_task() : led_breathe_task();
    if (vue == 0) x++;
    if (x >= 10) {
      x = 0;
      usbdata = 0;
    }
  }
}

int main()
{
  // set_sys_clock_pll(120 * MHZ,1,1);  /* 120MHz ? */
  // set_sys_clock_pll( 1500000000, 6, 2 );  /* 125MHz */
  set_sys_clock_pll( 1500000000, 6, 2 );  /* 300MHz */
  memset(buffer, 0, sizeof buffer);  /* clear USB read buffer */
  memset(data, 0, sizeof data);  /* clear USB write buffer */

  /* Init board */
  board_init();
  /* Init USB */
  tud_init(BOARD_TUD_RHPORT);

  /* Init SID */
  initPins();

  /* Init logging */
  init_logging();  /* Initialize logging ~ placed after board_init as this resets something making the first print not print */

  // multicore_reset_core1();  /* Reset core 1 before use */
  multicore_launch_core1(core1_main);  /* Init core 1 */

  sleep_us(100);  /* wait for core1 init */
  resetSID();     /* reset the SID */
  sleep_us(100);  /* Give the SID time to boot */

  midi_init();

  /* Loop IO tasks forever */
  while (1) {
    if (tud_task_event_ready())
    {
      tud_task();  // tinyusb device task
      read_task();  // cdc read task
      webserial_read_task();  // webusb read task
      midi_task();  // midi read task
      // sysex_task();  // sysex read task
      /* midi_task(); */  // Disabled for now
    }
  }
}


/*--------------------------------------------------------------------
 * SETUP FUNCTIONS
 *--------------------------------------------------------------------*/

void init_logging(void)
{
  #if defined(USBSID_UART)
  uint baud_rate = 115200;
  int tx_pin = TX;
  int rx_pin = RX;
  stdio_uart_init_full(uart0,
                       baud_rate,
                       tx_pin,
                       rx_pin);
  sleep_ms(100);  /* leave time for uart to settle */
  stdio_flush();
  #endif
}

void init_memory(void)
{
  for (unsigned int i = 0; i < 65536; i++) {
    memory[i] = 0x00; // fill with NOPs
  }
}

void square_wave(void)
{
  PIO pio = pio0;
  uint offset = pio_add_program(pio, &clock_program);
  uint sm = pio_claim_unused_sm(pio, true);
  // clock_program_init(pio, sm, offset, PHI, 30.0f);  // 2.08Mhz @ GPIO22
  // clock_program_init(pio, sm, offset, PHI, 60.0f);  // 1.04Mhz with clock at 125Mhz and 300Mhz @ GPIO22
  clock_program_init(pio, sm, offset, PHI, 62.5f);  // 1.00Mhz with clock at 125Mhz and 300Mhz @ GPIO22
}

int detect_clock(void)
{
  uint32_t b;
  extern uint32_t *BUSState;
  int c = 0, r = 0;
  gpio_init(PHI);
  gpio_set_pulls(PHI, false, true);
  for (int i = 0; i < 20; i++)
  {
    extern uint32_t *BUSState;
    b = *BUSState; /* read complete bus */
    r |= c = (b & bPIN(PHI)) >> PHI;
  }
  return r;  /* 1 if clock detected */
}

#if defined(USE_RGB)
void init_rgb(void)
{
  // ws2812_sm = initProgramWS2812();
  // pio_sm_put(pio1, ws2812_sm, 0xffffff);
  // pio_sm_put( pio1, ws2812_sm, 0 );
	r_ = g_ = b_ = 0;

  PIO pio = pio1;
  ws2812_sm = pio_claim_unused_sm(pio, true);
  uint offset = pio_add_program(pio, &ws2812_program);
  ws2812_program_init(pio, ws2812_sm, offset, WS2812_PIN, 800000, IS_RGBW);
  put_pixel(urgb_u32(0,0,0));
}

int detect_smps(void)
{  // TODO: FINISH!
   rgb_mode = false;
   return !(gpio_get(24));
}
#endif


/*--------------------------------------------------------------------
 * BUFFER TASKS
 *--------------------------------------------------------------------*/

void handle_buffer_task(unsigned char buff[4])
{
  // pause = ((buff[0] & 0x80) >> 7);
  // reset = ((buff[0] & 0x40) >> 6);
  // rw    = ((buff[0] & 0x10) >> 4);
  addr  = ((buff[1] << 8) | buff[2]);
  val   = (buff[3] & 0xFF);
  memory[addr] = val;


  /* Set address for SID no# */
  addr = sid_address(addr);


  switch (buff[0])
  {
  // if (pause == 1)
  // {
  case PAUSE:
    pauseSID();
    break;
  // }
  // if (reset == 1)
  // {  /* ignore everything else if reset is true */
  case RESET_SID:
    resetSID();
    break;
  // }
  // switch (rw)
  // {
  case RESET_USB:
    RESET_PICO();
    break;
  case READ:
    memset(data, 0, sizeof data);  /* clear write buffer */
    memset(rdata, 0, sizeof rdata);  /* clear read buffer */
    rdata[0] = readSID(addr);  /* read the SID datapins */
    for (int i = 0; i < sizeof(data); i++)
    {
      // data[i] = readSID(addr);  /* assign the result to teach of the 4 bytes */
      data[i] = rdata[0];  /* assign the result to teach of the 4 bytes */
    }
    /* write the result to the USB client */
    if (dtype == cdc) {
      write_task();
    } else if (dtype == wusb) {
      webserial_write_task();
    }
    break;
  case WRITE:
    writeSID(addr, val);  /* write the address and value to the SID */
    break;
  default:
    break;
  }
  return;
}

void handle_asidbuffer_task(uint8_t a, uint8_t d)
{
  dtype = asid;
  addr = (0xD400 | a);
  val  = d;
  memory[addr] = val;
  addr = sid_address(addr);
  writeSID(addr, val);
  return;
}


/*--------------------------------------------------------------------
 * IO TASKS
 *--------------------------------------------------------------------*/

void read_task(void)
{
  if (tud_cdc_connected()) {
    while (tud_cdc_available()) {  /* NOTICE: If disabled this breaks the Pico USB availability  */
      usbdata = 1;
      read = tud_cdc_read(buffer, sizeof(buffer)); /* Read data from client */
      tud_cdc_read_flush();
      if (read == 4) {  /* 4 as in exactly 4 bytes */
        dtype = cdc;
        handle_buffer_task(buffer);
      }
      memset(buffer, 0, sizeof buffer);  /* clear read buffer */
    }
  }
}

void write_task(void)
{
  if (tud_cdc_connected()) {
    if (tud_cdc_write_available()) {  /* NOTICE: Still testing */ // <- this uses IF instead of WHILE on purpose to avoid a write back loop
      write = tud_cdc_write(data, sizeof(data));  /* write data to client */
      // write = tud_cdc_write(rxdata, sizeof(rxdata));  /* write data to client */
      tud_cdc_write_flush();
      memset(data, 0, sizeof data);  /* clear write buffer */
    }
  }
}

void webserial_read_task(void)
{
  if ( web_serial_connected )
  {
    while ( tud_vendor_available() ) {
      usbdata = 1;
      read = tud_vendor_read(buffer, sizeof(buffer));
      tud_vendor_read_flush();
      if (read == 4) { /* 4 as in exactly 4 bytes */
      // NOTE: This 4 byte minimum renders any webserial test useless
        dtype = wusb;
        handle_buffer_task(buffer);
      }
      memset(buffer, 0, sizeof buffer);  /* clear read buffer */
    }
  }
}

void webserial_write_task(void)
{
  if (web_serial_connected) {
    // if (tud_vendor_available()) {
    if (tud_vendor_mounted()) {
      write = tud_vendor_write(data, sizeof(data));
      tud_vendor_flush();
      memset(data, 0, sizeof data);  /* clear write buffer */
    }
  }
}

void midi_task(void)
{
  while ( tud_midi_n_available(0, 0) ) {
    usbdata = 1;
    // tud_midi_packet_read(sysexMachine.packetbuffer);  /* WORKS ~ PACKET IS 4 BYTES */
    // tud_midi_stream_read(midimachine.readbuffer, 1);  /* WORKS ~ Handles 2 Byte at a time */
    // memset(midimachine.streambuffer, 0, sizeof midimachine.streambuffer);

    /* Working stream config */
    // int s = tud_midi_stream_read(midimachine.streambuffer, sizeof(midimachine.streambuffer));  /* WORKS ~ STREAM IS WHAT EVER THE MAX USB BUFFER SIZE IS! */
    // process_stream(midimachine.streambuffer);


    /* Working config */
    int s = tud_midi_n_stream_read(0, 0, midimachine.readbuffer, 1);  /* WORKS ~ Handles 1 Byte at a time */
    process_buffer(midimachine.readbuffer[0]);
  }
}

// void midi_task(void)
// {
//   while ( tud_midi_n_available(1, 0) ) {
//     usbdata = 1;
//     /* Working packet config */
//     int p = tud_midi_n_packet_read(1, midimachine.packetbuffer);  /* WORKS ~ PACKET IS 4 BYTES */

//   }
// }


/*--------------------------------------------------------------------
 * LED TASKS
 *--------------------------------------------------------------------*/

void led_vuemeter_task(void)
{
  uint8_t osc1, osc2, osc3, osc4, osc5, osc6;
  osc1 = (memory[0xD400] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 1 */
  osc2 = (memory[0xD407] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 2 */
  osc3 = (memory[0xD40E] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 3 */
  #if defined(USE_RGB)
  osc4 = (memory[0xD420] * 0.596);  /* Frequency in Hz of SID2 @ $D400 Oscillator 1 */
  osc5 = (memory[0xD427] * 0.596);  /* Frequency in Hz of SID2 @ $D400 Oscillator 2 */
  osc6 = (memory[0xD42E] * 0.596);  /* Frequency in Hz of SID2 @ $D400 Oscillator 3 */
  #endif

  vue = abs((osc1 + osc2 + osc3) / 3.0f);
  vue = map(vue, 0, HZ_MAX, 0, VUE_MAX);

  pwm_set_gpio_level(BUILTIN_LED, vue);

  #if defined(USE_RGB)
  if (rgb_mode) {  // TODO: r_, g_ and b_ should be the modulo of the additions
    r_ = 0, g_ = 0, b_ = 0, o1 = 0, o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0;
    o1 = map(osc1, 0, 255, 0, 43);
    o2 = map(osc2, 0, 255, 0, 43);
    o3 = map(osc3, 0, 255, 0, 43);
    o4 = map(osc4, 0, 255, 0, 43);
    o5 = map(osc5, 0, 255, 0, 43);
    o6 = map(osc6, 0, 255, 0, 43);
    r_ += color_LUT[o1][0][0] + color_LUT[o2][1][0] + color_LUT[o3][2][0] + color_LUT[o4][3][0] + color_LUT[o5][4][0] + color_LUT[o6][5][0] ;
    g_ += color_LUT[o1][0][1] + color_LUT[o2][1][1] + color_LUT[o3][2][1] + color_LUT[o4][3][1] + color_LUT[o5][4][1] + color_LUT[o6][5][1] ;
    b_ += color_LUT[o1][0][2] + color_LUT[o2][1][2] + color_LUT[o3][2][2] + color_LUT[o4][3][2] + color_LUT[o5][4][2] + color_LUT[o6][5][2] ;
    put_pixel(urgb_u32(rgbb(r_), rgbb(g_), rgbb(b_)));
  }
  #endif
}

void led_breathe_task(void)
{
  static uint32_t start_ms = 0;

  if ( board_millis() - start_ms < breathe_interval_ms) return;  /* not enough time */
  start_ms += breathe_interval_ms;

  if (pwm_value >= VUE_MAX)
    updown = 0;
  if (pwm_value <= 0)
    updown = 1;

  if (updown == 1 && pwm_value < VUE_MAX)
    pwm_value += BREATHE_STEP;

  if (updown == 0 && pwm_value > 0)
    pwm_value -= BREATHE_STEP;
  pwm_set_gpio_level(BUILTIN_LED, pwm_value);
  #if defined(USE_RGB)
  if (rgb_mode) put_pixel(urgb_u32(0,0,0));
  #endif
}


/*--------------------------------------------------------------------
 * SUPPORTING FUNCTIOMS
 *--------------------------------------------------------------------*/

uint16_t sid_address(uint16_t addr)
{ /* Set address for SID no# */
  /* D500, DE00 or DF00 is the second sid in SIDTYPE1, 3 & 4 */
  /* D500, DE00 or DF00 is the third sid in all other SIDTYPES */
  if (((addr) & SIDUMASK) != SID1ADDR) {  /* Not in $D400 range but in D5, DE or DF */
    switch (SIDTYPES) {
      case SIDTYPE1:
      case SIDTYPE2:
        addr = (SID2ADDR | (addr & SID1MASK));
        break;
      case SIDTYPE3:
        addr = (SID3ADDR | (addr & SID1MASK));
        break;
      case SIDTYPE4:
        addr = (SID3ADDR | (addr & SID2MASK));
        break;
      case SIDTYPE5: /* UNTESTED */
        addr = (SID1ADDR | (addr & SIDUMASK));
      }
  } else {  /* $D400 */
    switch (SIDTYPES) {
    case SIDTYPE1:
    case SIDTYPE3:
    case SIDTYPE4:
      addr; /* $D400 -> $D479 */
      break;
    case SIDTYPE2:
      addr = ((addr & SIDLMASK) >= 0x20) ? (SID2ADDR | (addr & SID1MASK)) : addr ; /* */
      break;
    case SIDTYPE5:
      addr =
          ((addr & SIDLMASK) >= 0x20) && ((addr & SIDLMASK) <= 0x39) /* $D420~$D439 -> $D440~$D459 */
              ? (addr + 0x20)
              : ((addr & SIDLMASK) >= 0x40) && ((addr & SIDLMASK) <= 0x59) /* $D440~$D459 -> $D420~$D439 */
                    ? (addr - 0x20)                    : addr;
      break;
    }
  }
  return addr;
}

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#if defined(USE_RGB)
void put_pixel(uint32_t pixel_grb) {
  pio_sm_put_blocking(pio1, ws2812_sm, pixel_grb << 8u);
}

uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t) (r) << 8) | ((uint32_t) (g) << 16) | (uint32_t) (b);
}

uint8_t rgbb(double inp)
{
  int r = abs((inp / 255) * BRIGHTNESS);
  return (uint8_t)r;
};
#endif


/*--------------------------------------------------------------------
 * DEVICE CALLBACKS
 *--------------------------------------------------------------------*/

void tud_mount_cb(void)
{
  // NOTE: Disabled for now, causes dual volume write with a CLICK sound on PWM
  // enableSID(); /* NOTICE: Testing if this is causing the random lockups */
}

void tud_umount_cb(void)
{
  // NOTE: Disabled for now, causes dual volume write with a CLICK sound on PWM
  // disableSID();  /* NOTICE: Testing if this is causing the random lockups */
}

void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  tud_mounted() ? enableSID() : disableSID();  /* NOTICE: Testing if this is causing the random lockups */
}

void tud_resume_cb(void)
{
  tud_mounted() ? enableSID() : disableSID();  /* NOTICE: Testing if this is causing the random lockups */
}


/*--------------------------------------------------------------------
 * CDC CALLBACKS
 *--------------------------------------------------------------------*/

void tud_cdc_rx_cb(uint8_t itf)
{
  (void)itf;
}

void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char)
{
  (void)itf;
  (void)wanted_char;
}

void tud_cdc_tx_complete_cb(uint8_t itf)
{
  (void)itf;
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  (void) itf;
  (void) rts;

  if ( dtr ) {
    /* Terminal connected */
    usbdata = 1;
  }
  else
  {
    /* Terminal disconnected */
    breathe_interval_ms = BREATHE_ON;
    usbdata = 0;  /* ISSUE: Led breather is not invoked again */
  }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
  (void)itf;
  (void)p_line_coding;
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
  (void)itf;
  (void)duration_ms;
}


/*--------------------------------------------------------------------
 * WEBUSB VENDOR CLASS CALLBACKS
 *--------------------------------------------------------------------*/

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP) return true;

  switch (request->bmRequestType_bit.type) {
    case TUSB_REQ_TYPE_VENDOR:
      switch (request->bRequest) {
        case VENDOR_REQUEST_WEBUSB:
          // match vendor request in BOS descriptor
          // Get landing page url
          // return tud_control_xfer(rhport, request, (void*)(uintptr_t) &desc_url, desc_url.bLength); /* Do we want this? */
          return tud_control_status(rhport, request);
        case VENDOR_REQUEST_MICROSOFT:
          if ( request->wIndex == 7 ) {
            // Get Microsoft OS 2.0 compatible descriptor
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20+8, 2);

            return tud_control_xfer(rhport, request, (void*)(uintptr_t) desc_ms_os_20, total_len);
            // return true;
          } else {
            return false;
          }
        default: break;
      }
      break;
    case TUSB_REQ_TYPE_CLASS:
      if (request->bRequest == 0x22) {
        // Webserial simulate the CDC_REQUEST_SET_CONTROL_LINE_STATE (0x22) to connect and disconnect.
        web_serial_connected = (request->wValue != 0);

        // Always lit LED if connected
        if (web_serial_connected) {
          // board_led_write(true);
          // blink_interval_ms = BLINK_ALWAYS_ON;

          /* tud_vendor_write_str("\r\nWebUSB interface connected\r\n"); */
          // tud_vendor_write_flush();
          usbdata = 1;
        } else {
          // blink_interval_ms = BLINK_MOUNTED;
        }
        // response with status OK
        return tud_control_status(rhport, request);
      }
      break;
    default:
      break;
  }

  // stall unknown request
  return false;
}
