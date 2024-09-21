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

static semaphore_t core1_init;
/* The following 2 lines (var naming changed) are copied from SKPico code by frenetic */
register uint32_t b asm( "r10" );
volatile const uint32_t *BUSState = &sio_hw->gpio_in;
/* https://github.com/frntc/SIDKick-pico */

void core1_main(void)
{
  if (detect_clock() == 0) {
    gpio_deinit(PHI);
    square_wave();
  }  /* Do nothing gpio makes input pulled low */

  memset(sid_memory, 0, sizeof sid_memory);
  int x = 0;
  initVUE();
  sem_release(&core1_init);  /* Release semaphore when core 1 is started */
  while (1)
  {
    /* tight_loop_contents(); */
    usbdata == 1 ? led_vuemeter_task() : led_breathe_task();
    if (vue == 0) x++;
    // x = usbdata = x >= 10 ? 0 : x;
    if (x >= 10) {
      x = 0;
      usbdata = 0;
    }
  }
}

int main()
{
  set_sys_clock_pll( 1500000000, 6, 2 );  /* 300MHz */

  /* Init board */
  board_init();
  /* Init USB */
  tud_init(BOARD_TUD_RHPORT);

  /* Init logging */
  init_logging();  /* Initialize logging ~ placed after board_init as this resets something making the first print not print */

  sem_init(&core1_init, 0, 1);  /* Create a blocking semaphore to wait until init of core 1 is complete */
  multicore_launch_core1(core1_main);  /* Init core 1 */
  sem_acquire_blocking(&core1_init);   /* Wait for core 1 to finish */

  initPins();
  setupPioBus();

  /* Loop IO tasks forever */
  while (1) {
    if (tud_task_event_ready())
    {
      tud_task();  // tinyusb device task
    }
  }
}


/*--------------------------------------------------------------------
 * SETUP FUNCTIONS
 *--------------------------------------------------------------------*/

void init_logging(void)
{
  #if defined(USBSID_UART)
  uint baud_rate = BAUD_RATE;
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

void square_wave(void)
{
  uint offset_clock = pio_add_program(pio, &clock_program);
  uint sm_clock = pio_claim_unused_sm(pio, true);
  clock_program_init(pio, sm_clock, offset_clock, PHI, 62.5f);  // 1.00Mhz with clock at 125Mhz and 300Mhz @ GPIO22
}

int detect_clock(void)
{
  int c = 0, r = 0;
  gpio_init(PHI);
  gpio_set_pulls(PHI, false, true);
  for (int i = 0; i < 20; i++)
  {
    b = *BUSState; /* read complete bus */
    DBG("[PHI2] 0x%x ", b);
    r |= c = (b & bPIN(PHI)) >> PHI;
    DBG("[C] %d [R] %d\r\n", c, r);
  }
  DBG("[R] RESULT %d\r\n", r);
  return r;  /* 1 if clock detected */
}


/*--------------------------------------------------------------------
 * BUFFER TASKS
 *--------------------------------------------------------------------*/

void __not_in_flash_func(handle_buffer_task)(void)
{
  /* Generate address for calculation */
  addr = ((buffer[1] << 8) | buffer[2]);
  /* Set address for SID no# */
  addr = (SIDTYPE == SIDTYPE0)
    ? (addr & 0x1f)
    : (sid_address(addr) & 0x7f);
  /* Apply mask to value and memory for vue */
  sid_memory[addr] = val = (buffer[3] & 0xFF);
  sid_pause = 0;
  switch (buffer[0]) {
    case PAUSE:
      sid_pause = 1;
      pauseSID();
      break;
    case RESET_SID:
      resetSID();
      break;
    case RESET_USB:
      // RESET_PICO(); /* Not implemented yet */
      break;
    case READ:
      data[0] = bus_operation((0x10 | READ), addr, val);  /* write the address to the SID and read the data back */
      /* write the result to the USB client */
      switch (dtype) {
        case 'C':
          write_task();
          break;
        case 'W':
        default:
          break;
      }
      break;
    case WRITE:
      bus_operation(0x10, addr, val);  /* write the address and value to the SID */
      #if defined(DEBUG_TIMING)
      uint64_t calc;
      t2 = time_us_64();
      wdiff = t2 - t1;
      calc = wdiff / 1000;
      TIMING("[W]%llu μs\r\n", wdiff, t1, t2);
      #endif
      break;
    default:
      break;
  }
  return;
}


/*--------------------------------------------------------------------
 * IO TASKS
 *--------------------------------------------------------------------*/

void __not_in_flash_func(read_task)(void)
{
  if (tud_cdc_connected()) {
    if (tud_cdc_available()) {  /* NOTICE: If disabled this breaks the Pico USB availability  */
      /* DBG("%s\r\n", __func__); */
      // usbdata = 1;
      cdcread = tud_cdc_read(buffer, sizeof(buffer)); /* Read data from client */
      tud_cdc_read_flush();
      if (cdcread == BYTES_EXPECTED) {  /* 4 as in exactly 4 bytes */
        #if defined(DEBUG_TIMING)
        switch (buffer[0]) {
          case 0:
            t1 = time_us_64();
            break;
          case 1:
            t3 = time_us_64();
           break;
        }
        #endif
        handle_buffer_task();
       /*  int d = 0;
        // printf("[B] ");
        for (int i = 0; i < BYTES_EXPECTED; i++) {
          d += buffer[i];
          // printf("$%02x ", buffer[i]);
        }
        // printf(" [D]%d\n", d);
        if (d != 0) handle_buffer_task();
 */
      }
      memset(buffer, 0, sizeof buffer);  /* clear in buffer */

    }
  }
}

void __not_in_flash_func(write_task)(void)
{
  if (tud_cdc_connected()) { /* not needed as we know its connected since this is only called from inside the loop */
    if (tud_cdc_write_available()) {  /* NOTICE: Still testing */ // <- this uses IF instead of WHILE on purpose to avoid a write back loop
      tud_cdc_write(data, 1);  /* write exactly 1 byte of data to client */
      tud_cdc_write_flush();

      #if defined(DEBUG_TIMING)
      uint64_t calc;
      t4 = time_us_64();
      rdiff = t4 - t3;
      calc = rdiff / 1000;
      DBG("[R]%llu μs\r\n", rdiff);
      #endif
    }
  }
}


/*--------------------------------------------------------------------
 * LED TASKS
 *--------------------------------------------------------------------*/

void led_vuemeter_task(void)
{
  if (sid_pause != 1 && usbdata == 1 ) {
    uint8_t osc1, osc2, osc3;
    osc1 = (sid_memory[0x00] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 1 */
    osc2 = (sid_memory[0x07] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 2 */
    osc3 = (sid_memory[0x0E] * 0.596);  /* Frequency in Hz of SID1 @ $D400 Oscillator 3 */
    #if defined(USE_RGB)
    uint8_t osc4, osc5, osc6;
    osc4 = (sid_memory[0x20] * 0.596);  /* Frequency in Hz of SID2 @ $D400 Oscillator 1 */
    osc5 = (sid_memory[0x27] * 0.596);  /* Frequency in Hz of SID2 @ $D400 Oscillator 2 */
    osc6 = (sid_memory[0x2E] * 0.596);  /* Frequency in Hz of SID2 @ $D400 Oscillator 3 */
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

    MDBG("[%c] [VOL]$%02x [PWM]$%04x | [V1] $%02X%02X %02X%02X %02X %02X %02X | [V2] $%02X%02X %02X%02X %02X %02X %02X | [V3] $%02X%02X %02X%02X %02X %02X %02X \n",
        dtype, sid_memory[0x18], vue,
        sid_memory[0x00], sid_memory[0x01], sid_memory[0x02], sid_memory[0x03], sid_memory[0x04], sid_memory[0x05], sid_memory[0x06],
        sid_memory[0x07], sid_memory[0x08], sid_memory[0x09], sid_memory[0x0A], sid_memory[0x0B], sid_memory[0x0C], sid_memory[0x0D],
        sid_memory[0x0E], sid_memory[0x0F], sid_memory[0x10], sid_memory[0x11], sid_memory[0x12], sid_memory[0x13], sid_memory[0x14]);
  }
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
 * SUPPORTING FUNCTIONS
 *--------------------------------------------------------------------*/

uint16_t __not_in_flash_func(sid_address)(uint16_t addr)
{
  /* Set address for SID no# */
  /* D500, DE00 or DF00 is the second sid in SIDTYPE1, 3 & 4 */
  /* D500, DE00 or DF00 is the third sid in all other SIDTYPE */
  switch (addr) {
    case 0xD400 ... 0xD499:
      switch (SIDTYPE) {
        case SIDTYPE1:
        case SIDTYPE3:
        case SIDTYPE4:
          return addr; /* $D400 -> $D479 */
          break;
        case SIDTYPE2:
          return ((addr & SIDLMASK) >= 0x20) ? (SID2ADDR | (addr & SID1MASK)) : addr;
          break;
        case SIDTYPE5:
          return ((addr & SIDLMASK) >= 0x20) && ((addr & SIDLMASK) <= 0x39) /* $D420~$D439 -> $D440~$D459 */
                  ? (addr + 0x20)
                  : ((addr & SIDLMASK) >= 0x40) && ((addr & SIDLMASK) <= 0x59) /* $D440~$D459 -> $D420~$D439 */
                  ? (addr - 0x20)
                  : addr;
          break;
      }
      break;
    case 0xD500 ... 0xD599:
    case 0xDE00 ... 0xDF99:
      switch (SIDTYPE) {
        case SIDTYPE1:
        case SIDTYPE2:
          return (SID2ADDR | (addr & SID1MASK));
          break;
        case SIDTYPE3:
          return (SID3ADDR | (addr & SID1MASK));
          break;
        case SIDTYPE4:
          return (SID3ADDR | (addr & SID2MASK));
          break;
        case SIDTYPE5:
          return (SID1ADDR | (addr & SIDUMASK));
          break;
      }
      break;
    default:
      return addr;
      break;
  }

}

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


/*--------------------------------------------------------------------
 * DEVICE CALLBACKS
 *--------------------------------------------------------------------*/

void tud_mount_cb(void)
{
  usb_connected = 1;
  DBG("%s\r\n", __func__);
}

void tud_umount_cb(void)
{
  usb_connected = 0, usbdata = 0;
  disableSID();  /* NOTICE: Testing if this is causing the random lockups */
  DBG("%s\r\n", __func__);
}

void tud_suspend_cb(bool remote_wakeup_en)
{
  /* (void) remote_wakeup_en; */
  usb_connected = 0, usbdata = 0;;
  DBG("%s %d\r\n", __func__, remote_wakeup_en);
}

void tud_resume_cb(void)
{
  usb_connected = 1;
  DBG("%s\r\n", __func__);
}


/*--------------------------------------------------------------------
 * CDC CALLBACKS
 *--------------------------------------------------------------------*/

void tud_cdc_rx_cb(uint8_t itf)
{
  (void)itf; /* itf = 0 */
  // DBG("%s: %x\r\n", __func__, itf);
  usbdata = 1;
  dtype = cdc;
  read_task();  // cdc read task
}

void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char)
{
  // (void)itf;
  // (void)wanted_char;
  DBG("%s: %x, %s\r\n", __func__, itf, wanted_char);
  // write_task();
}

void tud_cdc_tx_complete_cb(uint8_t itf)
{
  (void)itf;
  /* DBG("[R] $%02x [D] $%02x $%02x $%02x $%02x [S]%d\r\n", rdata, data[0], data[1], data[2], data[3], sizeof(data) / sizeof(*data)); */
  // memset(rdata, 0, sizeof data);  /* clear in buffer */
  // DBG("%s: %x\r\n", __func__, itf);
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  /* (void) itf; */
  /* (void) rts; */
  DBG("%s: %x, %d %d\r\n", __func__, itf, dtr, rts);

  if ( dtr ) {
    /* Terminal connected */
    /* usbdata = 1; */
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
  /* (void)itf; */
  /* (void)p_line_coding; */
  DBG("%s: %x, %d %d %d %d\r\n", __func__, itf, p_line_coding->bit_rate, p_line_coding->stop_bits, p_line_coding->parity, p_line_coding->data_bits);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
  /* (void)itf; */
  /* (void)duration_ms; */
  DBG("%s: %x, %x\r\n", __func__, itf, duration_ms);
}


/*--------------------------------------------------------------------
 * WEBUSB VENDOR CLASS CALLBACKS
 *--------------------------------------------------------------------*/

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{ /* BUG: WHEN WEBUSB CALLS THIS, CDC WILL BREAK! LINESTATE IS NOT SET THEN! */
  DBG("%s: %x, %x\r\n", __func__, rhport, stage);
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
