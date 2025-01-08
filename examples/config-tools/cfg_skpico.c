/*
 * USBSID-Pico is a RPi Pico (RP2040) based board for interfacing one or two
 * MOS SID chips and/or hardware SID emulators over (WEB)USB with your computer,
 * phone or ASID supporting player
 *
 * cfg_skpico.c
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


#define _POSIX_C_SOURCE 199309L
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>  // `errno`
#include <stdint.h> // `UINT64_MAX`
#include <stdio.h>  // `printf()`
#include <string.h> // `strerror(errno)`
#include <libusb.h>

/* Compile with:
 * gcc -g3 -L/usr/local/lib cfg_skpico.c -o cfg_skpico $(pkg-config --libs --cflags libusb-1.0)
 */

#define VENDOR_ID      0xcafe //5553
#define PRODUCT_ID     0x4011
#define ACM_CTRL_DTR   0x01
#define ACM_CTRL_RTS   0x02

static struct libusb_device_handle *devh = NULL;
static unsigned char encoding[] = { 0x00, 0x10, 0x0E, 0x00, 0x00, 0x00, 0x08 };
static int ep_out_addr = 0x02;
static int ep_in_addr  = 0x82;

static int len, rc, actual_length, transferred;
static int usid_dev = -1;

/* ---------------------- */

// static int read, write;
static uint8_t base_address = 0x0;

/* ---------------------- */

unsigned char * config_names[/* 64 */] = {
  "CFG_SID1_TYPE",       //  0   //  1  // 0 ... 3  // 6581, 8580, 8580+digiboost, none
  "CFG_SID1_DIGIBOOST",  //  1   // 12  // 0 ... 15, 0x0 ... 0xF
  "CFG_REGISTER_READ",   //  2   //  1  // Always 1
  "CFG_SID1_VOLUME",     //  3   // 14  // 0 ... 14, 0x0 ... 0xE
  "","","","",           //  4 -> 7
  "CFG_SID2_TYPE",       //  8   //  3  // 0 ... 4  // 6581, 8580, 8580+digiboost, none, FMopl
  "CFG_SID2_DIGIBOOST",  //  9   // 12  // 0 ... 15, 0x0 ... 0xF
  "CFG_SID2_ADDRESS",    // 10   //  0  // 0 ... 5  // sidFlags{ CS, A5, A8, A5|A8, A8, A8 }
  "CFG_SID2_VOLUME",     // 11   // 14  // 0 ... 14, 0x0 ... 0xE
  "CFG_SID_PANNING",     // 12   //  5  // 0 ... 14, 0x0 ... 0xE
  "","","","","","",     // 13 -> 18
  "","","","","","",     // 19 -> 24
  "","","","","","",     // 25 -> 30
  "","","","","","",     // 31 -> 36
  "","","","","","",     // 37 -> 42
  "","","","","","",     // 43 -> 48
  "","","","","","",     // 49 -> 54
  "","",                 // 55 -> 56
  "CFG_TRIGGER",         // 57   //  0  // Paddles
  "CFG_SID_BALANCE",     // 58   //  7  // 0 ... 14, 0x0 ... 0xE
  "CFG_CLOCKSPEED",      // 59   //  0  // 0 ... 2  // c64clock{ 985248, 1022727, 1023440 }
  "CFG_POT_FILTER",      // 60   // 16  // Paddles
  "CFG_DIGIDETECT",      // 61   //  0  // 0 ... 1
  "","",                 // 62 -> 63
};

unsigned char * sid_types[] = {
  "6581", "8580", "8580+digiboost", "none", "FMopl"
};
unsigned char * sid2_address[] = {
  "CS", "A5", "A8", "A5|A8", "A8", "A8"
};
unsigned char * clock_speed[] = {
  "985248", "1022727", "1023440"
};

static uint8_t config[64] = {0};
uint8_t read_data[1];
uint8_t read_buffer[3]  = { 0x1, 0x0, 0x0 };
uint8_t write_buffer[3]  = { 0x0, 0x0, 0x0 };
uint8_t command_buffer[3]  = { 0x0, 0x0, 0x0 };
uint8_t init_configmode[3] = { 0x0, 0x1F, 0xFF };       /* Inits and extends config mode */
uint8_t config_unknown[3] = { 0x0, 0x1D, 0xFA };        /* Unknown configuration option */
uint8_t config_exit[3] = { 0x0, 0x1D, 0xFB };           /* Exit config mode */
uint8_t config_update[3] = { 0x0, 0x1D, 0xFE };         /* Update configuration */
uint8_t config_writeupdate[3] = { 0x0, 0x1D, 0xFF };    /* Update and write configuration */
uint8_t config_write[3] = { 0x0, 0x1D, 0x0 };           /* Write complete config to memory */
uint8_t start_config_write[3] = { 0x0, 0x1E, 0x0 };     /* Write single value to config */
uint8_t set_dac_mode_mono8[3] = { 0x0, 0x1F, 0xFC };    /* Set DAC mode to mono */
uint8_t set_dac_mode_stereo8[3] = { 0x0, 0x1F, 0xFB };  /* Set DAC mode to stereo */

/* ---------------------- */


int usbsid_init(void)
{
    if (devh != NULL) {
        libusb_close(devh);
    }

    rc = libusb_init(NULL);
    if (rc != 0) {
        fprintf(stderr, "Error initializing libusb: %s: %s\n",
        libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    devh = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, PRODUCT_ID);
    if (!devh) {
        fprintf(stderr, "Error finding USB device\n");
        rc = -1;
        goto out;
    }

    for (int if_num = 0; if_num < 2; if_num++) {
        if (libusb_kernel_driver_active(devh, if_num)) {
            libusb_detach_kernel_driver(devh, if_num);
        }
        rc = libusb_claim_interface(devh, if_num);
        if (rc < 0) {
            fprintf(stderr, "Error claiming interface: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
            goto out;
        }
    }

    rc = libusb_control_transfer(devh, 0x21, 0x22, ACM_CTRL_DTR | ACM_CTRL_RTS, 0, NULL, 0, 0);
    if (rc != 0 && rc != 7) {
        fprintf(stderr, "?Error configuring line state during control transfer: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    rc = libusb_control_transfer(devh, 0x21, 0x20, 0, 0, encoding, sizeof(encoding), 0);
    if (rc != 0 && rc != 7) {
        fprintf(stderr, "Error configuring line encoding during control transfer: %d, %s: %s\n",
            rc, libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }

    usid_dev = (rc == 0 || rc == 7) ? 0 : -1;

    if (usid_dev < 0)
    {
        fprintf(stderr, "Could not open SID device USBSID.\n");
        goto out;
    }

    fprintf(stdout, "usbsid_init: detected [rc]%d [usid_dev]%d\n", rc, usid_dev);

	return usid_dev;
out:
    if (devh != NULL)
        libusb_close(devh);
    libusb_exit(NULL);
    return rc;

}

void usbsid_close(void)
{
  for (int if_num = 0; if_num < 2; if_num++) {
    libusb_release_interface(devh, if_num);
    if (libusb_kernel_driver_active(devh, if_num)) {
        libusb_detach_kernel_driver(devh, if_num);
    }
  }
  libusb_close(devh);
  libusb_exit(NULL);
}

/* ---------------------- */

void write_chars(unsigned char * data, int size)
{
  int actual_length;
  data[1] |= base_address;
  if (libusb_bulk_transfer(devh, ep_out_addr, data, size, &actual_length, 0) < 0) {
    fprintf(stderr, "Error while sending char\n");
  }
  /* printf("[W]$%02X:%02X\n", data[1], data[2]); */
}

int read_chars(unsigned char * data, int size)
{
  int actual_length;
  data[1] |= base_address;
  int rc = libusb_bulk_transfer(devh, ep_in_addr, data, size, &actual_length, 0);
  /* printf("[R]$%02X:%02X\n", read_buffer[1], data[0]); */
  if (rc == LIBUSB_ERROR_TIMEOUT) {
    printf("timeout (%d)\n", actual_length);
    return -1;
  } else if (rc < 0) {
    fprintf(stderr, "Error while waiting for char\n");
    return -1;
  }
  return actual_length;
}

/* ---------------------- */

void reset_sids(void)
{
  command_buffer[0] = 0x3;
  write_chars(command_buffer, 3);
}

void config_mode(void)
{
  write_chars(init_configmode, 3);  /* Start or extend config mode */
}

void end_config_mode(void)
{
  write_chars(config_exit, 3);  /* Exit config mode */
}

void update_config(void)
{
  write_chars(config_update, 3);  /* Update config, doesn't save */
}

void save_config(void)
{
  write_chars(config_writeupdate, 3);  /* Update & save config */
}

void read_config(void)
{
  config_mode();

  for (int i = 0; i < 64 ; i++) {
    read_buffer[1] = 0x1d;
    int len;
    write_chars(read_buffer, sizeof(read_buffer));
    len = read_chars(read_data, sizeof(read_data));
    config[i] = read_data[0];
    /* if(i == 64) printf("\n"); */
  }

  config_mode();

  printf("[PRINT CFG SETTINGS START]\n");
  for (size_t i = 0; i < 64; ++i) {
    if (i >= 4 && i <= 7) continue;
    if (i >= 13 && i <= 56) continue;
    if (i == 62 || i == 63) continue;
    if (i == 0 || i == 8) {
        printf("[%02ld] %s: %02X ~ %s\n", i, config_names[i], config[i], sid_types[config[i]]);
        continue;
    }
    if (i == 10) {
        printf("[%02ld] %s: %02X ~ %s\n", i, config_names[i], config[i], sid2_address[config[i]]);
        continue;
    }
    if (i == 59) {
        printf("[%02ld] %s: %02X ~ %s\n", i, config_names[i], config[i], clock_speed[config[i]]);
        continue;
    }
    printf("[%02ld] %s: %02X\n", i, config_names[i], config[i]);
  }
  printf("[PRINT CFG SETTINGS END]\n");

  return;
}

void write_config(void)
{
  // config_mode();
  write_chars(start_config_write, sizeof(start_config_write));
  for (int i = 0; i < 64 ; i++) {
    config_write[2] = config[i];  /* config value */
    write_chars(config_write, sizeof(config_write));
  }
  /* update_config(); */
  /* save_config(); */
}

/* ---------------------- */

int main(int argc, char **argv)
{
    fprintf(stdout, "Detecting Linux usbsid boards.\n");

    if (usid_dev != 0) {
        rc = usbsid_init();
        if (rc != 0) {
            return -1;
        }
    }

    /* zero length read to clear any lingering data */
    unsigned char buffer[1];
    libusb_bulk_transfer(devh, ep_out_addr, buffer, 0, &transferred, 1);

    for (int param_count = 1; param_count < argc; param_count++) {
      if (!strcmp(argv[param_count], "-sock") || !strcmp(argv[param_count], "--socket")) {
        param_count++;
        int socket = atoi(argv[param_count]);
        int socket_base = (socket == 1) ? 0 : (socket == 2) ? 1 : 0;
        base_address = (socket_base * 0x60);
        printf("Using USBSID-Pico socket %d with base address 0x%02X\n", socket, base_address);
      }
    }

    for (int param_count = 1; param_count < argc; param_count++) {
      if (!strcmp(argv[param_count], "-r") || !strcmp(argv[param_count], "--read")) {
        param_count++;
        printf("Read config\n");
        read_config();
        end_config_mode();
        usbsid_close();
        break;
      }
      if (!strcmp(argv[param_count], "-w") || !strcmp(argv[param_count], "--write")) {
        param_count++;
        printf("Write config ~ read current config first\n");
        read_config();
        for (int param_count = 1; param_count < argc; param_count++) {
          if (!strcmp(argv[param_count], "-s1") || !strcmp(argv[param_count], "--sidone")) {
            param_count++;
            int sid = atoi(argv[param_count]) - 1;
            if (sid < 4) {
              printf("SID one from: %s to: %s\n", sid_types[config[0]], sid_types[sid]);
              config[0] = sid;
            }
          }
          if (!strcmp(argv[param_count], "-s2") || !strcmp(argv[param_count], "--sidtwo")) {
            param_count++;
            int sid = atoi(argv[param_count]) - 1;
            if (sid < 5) {
              printf("SID two from: %s to: %s\n", sid_types[config[8]], sid_types[sid]);
              config[8] = sid;
            }
          }
          if (!strcmp(argv[param_count], "-s1vol") || !strcmp(argv[param_count], "--sidone-volume")) {
            param_count++;
            int vol = atoi(argv[param_count]) - 1;
            if (vol >= 0 && vol <= 14) {
              printf("SID one volume from: %d to: %d\n", config[3], vol);
              config[3] = vol;
            }
          }
          if (!strcmp(argv[param_count], "-s2vol") || !strcmp(argv[param_count], "--sidtwo-volume")) {
            param_count++;
            int vol = atoi(argv[param_count]) - 1;
            if (vol >= 0 && vol <= 14) {
              printf("SID two volume from: %d to: %d\n", config[11], vol);
              config[11] = vol;
            }
          }
          if (!strcmp(argv[param_count], "-s1db") || !strcmp(argv[param_count], "--sidone-digiboost")) {
            param_count++;
            int db = atoi(argv[param_count]) - 1;
            if (db >= 0 && db <= 14) {
              printf("SID one digiboost from: %d to: %d\n", config[1], db);
              config[1] = db;
            }
          }
          if (!strcmp(argv[param_count], "-s2db") || !strcmp(argv[param_count], "--sidtwo-digiboost")) {
            param_count++;
            int db = atoi(argv[param_count]) - 1;
            if (db >= 0 && db <= 14) {
              printf("SID two digiboost from: %d to: %d\n", config[9], db);
              config[9] = db;
            }
          }
          if (!strcmp(argv[param_count], "-io") || !strcmp(argv[param_count], "--iopina5a8")) {
            param_count++;
            int io = atoi(argv[param_count]) - 1;
            if (io < 6) {
              printf("SKPico SID 2 IO pin from: %s to: %s\n", sid2_address[config[10]], sid2_address[io]);
              config[10] = io;
            }
          }
        }
        // Default
        config[12] =  7;  // panning
        config[58] =  7;  // balance
        config[61] =  1;  // digidetect on

        printf("Start writing config\n");
        config_mode();
        write_config();

        printf("Start saving config\n");
        sleep(0.5);
        save_config();

        printf("Reset SIDS and wait for boot\n");
        sleep(0.5);
        reset_sids();
        sleep(1);
        printf("Start reading config\n");
        read_config();

        sleep(0.5);
        usbsid_close();
        break;
      }
    }

    return 0;
};
