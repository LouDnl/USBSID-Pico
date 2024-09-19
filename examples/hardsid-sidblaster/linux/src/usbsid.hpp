#pragma once
#ifndef _USBSID_DRIVER_H_
#define _USBSID_DRIVER_H_

#include <cstring>
#include <cstdio>
#include <cassert>
#include <chrono>
#include <thread>
#include <cstdint>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

#include <errno.h>
#include <libusb.h>
#include <pthread.h>
#include "usbsid-macros.hpp"

#ifndef ASYNC_THREADING  /* Set Async threading to default 0 if not defined in Makefile */
#define ASYNC_THREADING 0
#endif

#define VENDOR_ID      0xcafe
#define PRODUCT_ID     0x4011
#define ACM_CTRL_DTR   0x01
#define ACM_CTRL_RTS   0x02

#define LEN_IN_BUFFER 1
#define LEN_OUT_BUFFER 3
#define LEN_OUT_BUFFER_ASYNC 64

int ep_out_addr = 0x02;
int ep_in_addr  = 0x82;
struct libusb_device_handle *devh = NULL;
struct libusb_transfer *transfer_out = NULL;  /* OUT-going transfers (OUT from host PC to USB-device) */
struct libusb_transfer *transfer_in = NULL;  /* IN-coming transfers (IN to host PC from USB-device) */
libusb_context *ctx = NULL;
uint8_t * in_buffer;
uint8_t * out_buffer;
int read_completed, write_completed;
int actual_length = 0, rc = -1, sids_found = 0, usid_dev = -1;
pthread_t ptid;
int exit_thread = 0;

extern unsigned char result[LEN_IN_BUFFER]; /* variable where read data is copied into */
extern int out_buffer_length;


// Clock cycles for the MOS6502
#define CLOCK_CYCLES 100000

// Clock speed: 0.985 MHz (PAL) or 1.023 MHz (NTSC)
#define CLOCK_DEFAULT 1000000  // 1MHz = 1us
#define CLOCK_PAL      985248  // 0.985 MHz
#define CLOCK_NTSC    1022727  // 1.023 MHz
#define CLOCK_DREAN   1023440  // 1.023 MHz

// Refreshrates in microseconds
#define HERTZ_DEFAULT 20000  // 50Hz ~ 20000 == 20us
#define HERTZ_50      19950  // 50Hz ~ 20000 == 20us / 50.125Hz ~ 19.950124688279us exact
#define HERTZ_60      16715  // 60Hz ~ 16667 == 16.67us / 59.826Hz ~ 16.715140574332 exact


// int usbsid_init(void);
// int usbsid_close(void);
void usbsid_reset(void);
int usbsid_open(void);
int usbsid_read(uint16_t addr, int chipno);
void usbsid_store(uint16_t addr, uint8_t val, int chipno);

/*  */

typedef std::chrono::high_resolution_clock::time_point timestamp_t;
typedef std::chrono::nanoseconds  duration_t;
typedef std::nano                 ratio_t;

int64_t CycleFromTimestamp(timestamp_t timestamp);
int WaitForCycle(int cycle);


timestamp_t           m_StartTime = std::chrono::high_resolution_clock::now();
timestamp_t           m_NextTime = std::chrono::high_resolution_clock::now();
timestamp_t           m_CurrentTime = std::chrono::high_resolution_clock::now();
double                m_CPUcycleDuration = ratio_t::den / CLOCK_PAL;
double                m_InvCPUcycleDurationNanoSeconds = 1.0 / (1000000000 / CLOCK_PAL);

/* USBSID-Pico driver functions */

/* Setup USBSID driver */
int usbSIDSetup(void);

/* Break down USBSID driver */
int usbSIDExit(void);

/* Async write thread */
void* usbSIDStart(void *);

/* Write buffer to USBSID */
void usbSIDWrite(unsigned char *buff);

/* Read from USBSID ~ callback reads into result buffer*/
void usbSIDRead_toBuff(unsigned char *writebuff);

/* Read from USBSID ~ returning function */
unsigned char usbSIDRead(unsigned char *writebuff, unsigned char *buff);

/* Pause USBSID */
void usbSIDPause(void);

/* Reset USBSID */
void usbSIDReset(void);

/* Outgoing callback function */
void LIBUSB_CALL sid_out(struct libusb_transfer *transfer);

/* Incoming callback function */
void LIBUSB_CALL sid_in(struct libusb_transfer *transfer);

/* libusb transfer debugging print */
void print_libusb_transfer(struct libusb_transfer *p_t);

/* Address conversion */
uint16_t sid_address(uint16_t addr);

/* SIDTYPE ~ Defaults to 0
 *
 * 0: Single SID config
 *    SID socket 1 filled with either of
 *    - MOS6581/MOS6582/MOS8580
 *    - SIDKickPico
 *    - SwinSID
 *    - any other single SID hardware emulator
 *    - NOTE: Will use the second SID socket simultaniously
 * 1: Single SID config (dual optional with 1x SIDKickPico)
 *    SID socket 1 filled with either of
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    - MOS6581/MOS6582/MOS8580
 *    - SwinSID
 *    - any other single SID hardware emulator
 * 2: Dual SID config
 *    SID sockets 1 & 2 filled with either of
 *    - MOS6581/MOS6582/MOS8580
 *    - SIDKickPico
 *    - SwinSID
 *    - any other single SID hardware emulator
 * 3: Triple SID config with 1x SIDKickPico and other
 *    SID socket 1 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    SID socket 2 filled with either of
 *    - MOS6581/MOS6582/MOS8580
 *    - SIDKickPico
 *    - SwinSID
 *    - any other single SID hardware emulator
 * 4: Quad SID config with 2x SIDKickPico
 *    SID socket 1 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    SID socket 2 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 * 5: Quad SID mixed config with 2x SIDKickPico
 *    SID socket 1 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    - Acts as SID 1 & 3
 *    SID socket 2 filled with a
 *    - SIDKickPico (A5 connected to A5 for dual SID)
 *    - Acts as SID 2 & 4
 *
 */
#define SIDTYPE0 0
#define SIDTYPE1 1
#define SIDTYPE2 2
#define SIDTYPE3 3
#define SIDTYPE4 4
#define SIDTYPE5 5

/* Temporary */
#define SIDTYPE SIDTYPE4

/* Default config for SID type */
#ifndef SIDTYPE
#define SIDTYPE SIDTYPE0
#endif

/* SID type masks for GPIO */
#define SIDUMASK 0xFF00
#define SIDLMASK 0xFF
#if SIDTYPE == SIDTYPE0
#define NUMSIDS  1
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0x0
#define SID2MASK 0x0
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE1
#define NUMSIDS  2
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 ~ OPTIONAL */
#define SID2MASK 0x3F
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE2
#define NUMSIDS  2
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD440  /* SID 2 - socket 2 */
#define SID2MASK 0x5F
#define SID3ADDR 0x0
#define SID3MASK 0x0
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE3
#define NUMSIDS  3
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 */
#define SID2MASK 0x3F
#define SID3ADDR 0xD440  /* SID 3 - socket 2 */
#define SID3MASK 0x5F
#define SID4ADDR 0x0
#define SID4MASK 0x0
#elif SIDTYPE == SIDTYPE4  /* SKPico only */
#define NUMSIDS  4
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD420  /* SID 2 - socket 1 */
#define SID2MASK 0x3F
#define SID3ADDR 0xD440  /* SID 3 - socket 2 */
#define SID3MASK 0x5F
#define SID4ADDR 0xD460  /* SID 4 - socket 2 */
#define SID4MASK 0x7F
#elif SIDTYPE == SIDTYPE5  /* SKPico only */
#define NUMSIDS  4
#define SID1ADDR 0xD400  /* SID 1 - socket 1 */
#define SID1MASK 0x1F
#define SID2ADDR 0xD440  /* SID 2 - socket 2 */
#define SID2MASK 0x5F
#define SID3ADDR 0xD420  /* SID 3 - socket 1 */
#define SID3MASK 0x3F
#define SID4ADDR 0xD460  /* SID 4 - socket 2 */
#define SID4MASK 0x7F
#endif

#endif /* _USBSID_DRIVER_H_ */
