/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2 (RP2350) based board for
 * interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * usb_descriptors.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

#include "globals.h"


/* MCU externals */
extern uint64_t mcu_get_unique_id(void);

/* USB Constants */
#define USB_PID 0x4011  /* USBSID uses TinyUSB variation ~ 1x CDC + 1x MIDI (Vendor not included) */
#define USB_VID 0xCAFE  /* USBSID uses TinyUSB default - Previous versions used 0x5553 */
#define USB_BCD 0x0210

/* Device Descriptors */
tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD, // at least 2.1 or 3.x for BOS

    // Use Interface Association Descriptor (IAD) for CDC
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,  /* Device release number in binary-coded decimal */

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

/* Descriptor callback
 * Invoked when received GET DEVICE DESCRIPTOR
 * Application return pointer to descriptor
 */
uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

/* Configuration Descriptor */
enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,  /* This gets the descriptor of CDC */
  ITF_NUM_MIDI,
  ITF_NUM_MIDI_STREAMING,  /* This has to be here, even if not used! */
  ITF_NUM_VENDOR,
  #ifdef USB_PRINTF
  ITF_NUM_CDC_2,
  ITF_NUM_CDC_DATA_2,
  #endif
  ITF_NUM_TOTAL
};

/* Endpoint config constants */
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_MIDI_OUT    0x03
#define EPNUM_MIDI_IN     0x83
#define EPNUM_VENDOR_OUT  0x04
#define EPNUM_VENDOR_IN   0x84
#ifdef USB_PRINTF
#define EPNUM_CDC2_NOTIF  0x85
#define EPNUM_CDC2_OUT    0x06
#define EPNUM_CDC2_IN     0x86
#endif

#define USBD_CDC_CMD_MAX_SIZE         8
#define USBD_CDC_IN_OUT_MAX_SIZE     64
#define USBD_MIDI_IN_OUT_MAX_SIZE    64
#define USBD_VENDOR_IN_OUT_MAX_SIZE  64

#ifdef USB_PRINTF
#define CONFIG_TOTAL_LEN   (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_CDC_DESC_LEN)
#else
#define CONFIG_TOTAL_LEN   (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_VENDOR_DESC_LEN)
#endif

/* Full speed configuration */
uint8_t const desc_fs_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),

  // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, USBD_CDC_CMD_MAX_SIZE, EPNUM_CDC_OUT, EPNUM_CDC_IN, USBD_CDC_IN_OUT_MAX_SIZE),

  // Interface number, string index, EP Out & EP In address, EP size
  TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, USBD_MIDI_IN_OUT_MAX_SIZE),

  // Interface number, string index, EP Out & IN address, EP size
  TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 6, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, USBD_VENDOR_IN_OUT_MAX_SIZE),

  #ifdef USB_PRINTF
  // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_2, 4, EPNUM_CDC2_NOTIF, USBD_CDC_CMD_MAX_SIZE, EPNUM_CDC2_OUT, EPNUM_CDC2_IN, USBD_CDC_IN_OUT_MAX_SIZE),
  #endif
};

/* Some Microsoft mumbo jumbo 🪄 */
#define BOS_TOTAL_LEN      (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define MS_OS_20_DESC_LEN  0xB2

/* BOS Descriptor is required for WebUSB */
uint8_t const desc_bos[] =
{
  // total length, number of device caps
  TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),

  // Vendor Code, iLandingPage
  TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),

  // Microsoft OS 2.0 descriptor
  TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

/* BOS Descriptor callback */
uint8_t const * tud_descriptor_bos_cb(void)
{
  return desc_bos;
}

/* MS hassle */
uint8_t const desc_ms_os_20[] =
{
  // Set header: length, type, windows version, total length
  U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

  // Configuration subset header: length, type, configuration index, reserved, configuration total length
  U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN-0x0A),

  // Function Subset header: length, type, first interface, reserved, subset length
  U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_VENDOR, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN-0x0A-0x08),

  // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub compatible ID
  U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // sub-compatible

  // MS OS 2.0 Registry property descriptor: length, type
  U16_TO_U8S_LE(MS_OS_20_DESC_LEN-0x0A-0x08-0x08-0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
  U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
  'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
  'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
  U16_TO_U8S_LE(0x0050), // wPropertyDataLength
	//bPropertyData: “{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}”.
  '{', 0x00, '9', 0x00, '7', 0x00, '5', 0x00, 'F', 0x00, '4', 0x00, '4', 0x00, 'D', 0x00, '9', 0x00, '-', 0x00,
  '0', 0x00, 'D', 0x00, '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00, 'D', 0x00, '-', 0x00,
  '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
  '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

extern uint8_t const desc_ms_os_20[];

/* Device descriptor configuration callback
 * Invoked when received GET CONFIGURATION DESCRIPTOR
 * Application return pointer to descriptor
 * Descriptor contents must exist long enough for transfer to complete
 */
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index; // for multiple configurations

  return desc_fs_configuration;
}

#ifndef USBSID_MANUFACTURER
#define USBSID_MANUFACTURER "ERROR MISSING MANUFACTURER!"
#endif
#ifndef USBSID_PRODUCT
#define USBSID_PRODUCT "ERROR MISSING PRODUCT!"
#endif

/* String Descriptors */
const char *string_desc_arr[] =
{
    (const char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    USBSID_MANUFACTURER,         // 1: Manufacturer
    USBSID_PRODUCT,              // 2: Product
    "USES-MCU-ID",               // 3: Serial, uses chip ID
    "USBSID-Pico Data",          // 4: CDC Interface
    "USBSID-Pico Midi",          // 5: Midi Interface
    "USBSID-Pico WebUSB",        // 6: WebUSB Vendor Interface
};

/* automatically update if an additional string is later added to the table */
#define STRING_DESC_ARR_ELEMENT_COUNT (sizeof(string_desc_arr)/sizeof(string_desc_arr[0]))

/* temporary buffer returned from tud_descriptor_string_cb()
 * relies on only a single outstanding call to this function
 * until the buffer is no longer used by caller.
 * "contents must exist long enough for transfer to complete"
 * Safe because each string descriptor is a separate transfer,
 * and only one is handled at a time.
 * Use of "length" is ambiguous (byte count?  element count?),
 * so use more explicit "BYTE_COUNT" or "ELEMENT_COUNT" instead.
 * */
#define MAXIMUM_DESCRIPTOR_STRING_ELEMENT_COUNT 32
static uint16_t _desc_str[MAXIMUM_DESCRIPTOR_STRING_ELEMENT_COUNT];

/* Descriptor string callback
 * Invoked when received GET STRING DESCRIPTOR request
 * Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
 */
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;

  uint8_t chr_count;

  if ( index == 0)
  {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else if ( index == 3 ) { // special-case for USB Serial number
    /* BOARD ID */
    //  1x  uint16_t for length/type
    // 16x  uint16_t to encode 64-bit value as hex (16x nibbles)
    static_assert(MAXIMUM_DESCRIPTOR_STRING_ELEMENT_COUNT >= 17);
    uint64_t unique_id = mcu_get_unique_id();
    for ( uint_fast8_t t = 0; t < 16; t++ ) {
      uint8_t nibble = (unique_id >> (t * 4u)) & 0xF;
      _desc_str[16-t] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    chr_count = 16;
  } else if ( index >= STRING_DESC_ARR_ELEMENT_COUNT ) { // if not in table, return NULL
    return NULL;
  } else
  {
    // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors
    const char* str = string_desc_arr[index];

    // Cap at max char
    chr_count = (uint8_t) strlen(str);
    if ( chr_count > MAXIMUM_DESCRIPTOR_STRING_ELEMENT_COUNT-1 ) chr_count = MAXIMUM_DESCRIPTOR_STRING_ELEMENT_COUNT-1;

    // Convert ASCII string into UTF-16
    for(uint8_t i=0; i<chr_count; i++)
    {
      _desc_str[1+i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8 ) | (2*chr_count + 2));

  return _desc_str;
}
