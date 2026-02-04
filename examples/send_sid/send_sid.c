/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * send_sid.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024-2026 LouD
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
#include <stdbool.h>
#include <ctype.h>

#include <libusb.h>

/* Compile with:
 * gcc -g3 -L/usr/local/lib send_sid.c -o send_sid $(pkg-config --libs --cflags libusb-1.0)
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

enum {
  /* Command bytes */
  PACKET_TYPE      = 0xC0,  /* 0b11000000 ~ 192  */
  CONFIG           = 0x12,  /*    0b10010 ~ 0x12 */

  /* Internal SID player */
  UPLOAD_SID_START = 0xD0,  /* Start command for USBSID to go into receiving mode */
  UPLOAD_SID_DATA  = 0xD1,  /* Init byte for each packet containing data */
  UPLOAD_SID_END   = 0xD2,  /* End command for USBSID to exit receiving mode */
  UPLOAD_SID_SIZE  = 0xD3,  /* Packet containing the actual file size */

  /* Internal SID player */
  SID_PLAYER_LOAD  = 0xE0,  /* Load SID file into SID player memory and initialize internal SID player */
  SID_PLAYER_START = 0xE1,  /* Start SID file play */
  SID_PLAYER_STOP  = 0xE2,  /* Stop SID file play */
  SID_PLAYER_PAUSE = 0xE3,  /* Pause/Unpause SID file play */
  SID_PLAYER_NEXT  = 0xE4,  /* Next SID subtune play */
  SID_PLAYER_PREV  = 0xE5,  /* Previous SID subtune play */

  FROM_STDIN       = 0x00,  /* Read data from stdin */
  SID_FILE         = 0x01,  /* File is SID */
  PRG_FILE         = 0x02,  /* File is PRG */
};

/**
 * @brief Initialize a connection with USBSID-Pico
 *
 * @return int
 */
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
    if (libusb_kernel_driver_active(devh, if_num) == 1) {
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

  if (usid_dev < 0) {
    fprintf(stderr, "Could not open SID device USBSID.\n");
    goto out;
  }

  return usid_dev;
out:
  if (devh != NULL)
  libusb_close(devh);
  libusb_exit(NULL);
  rc = -1;
  return rc;
}

/**
 * @brief Close the connection with USBSID-Pico
 *
 */
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
  return;
}

/**
 * @brief Write data to USBSID-Pico
 *
 * @param data
 * @param size
 */
void write_chars(unsigned char * data, int size)
{
  int actual_length;
  if (libusb_bulk_transfer(devh, ep_out_addr, data, size, &actual_length, 0) < 0) {
    fprintf(stderr, "Error while sending char\n");
  }
  return;
}

/**
 * @brief Helper function to find the last occurrence of a character
 *
 * @param str
 * @param c
 * @return char*
 */
char* find_last_of(char *str, char c) {
  char *last = NULL;
  char *p = str;
  while (*p != '\0') {
    if (*p == c) {
      last = p;
    }
    p++;
  }
  return last;
}

/**
 * @brief Helper function to get the substring after the last dot and convert to lowercase
 *
 * @param fname
 * @return char*
 */
char* get_extension_and_tolower(const char* fname) {
  char *ext_start = find_last_of((char*)fname, '.');

  if (ext_start != NULL) {
    /* Calculate the length of the extension */
    size_t len = strlen(ext_start + 1);
    /* Allocate memory for the extension (including null terminator) */
    char *ext = malloc(len + 1);
    if (ext == NULL) {
      perror("malloc failed");
      exit(EXIT_FAILURE);
    }
    /* Copy the extension */
    strcpy(ext, ext_start + 1);

    /* Transform to lowercase in place */
    for (char *p = ext; *p; ++p) {
      *p = tolower((unsigned char)*p);
    }
    return ext;
  }
  return NULL; /* No extension found */
}

/**
 * @brief Send input file to USBSID-Pico
 *
 * @param input_f
 * @param filetype
 */
static void send_sid(FILE* input_f, int filetype)
{
  int byte;
  unsigned int i = 0U;
  unsigned char buff[64] = {0};
  static int bytecount = 2;

  fseek(input_f, 0L, SEEK_END);
  unsigned int file_size = ftell(input_f);
  rewind(input_f);

  buff[0] = (PACKET_TYPE|CONFIG);
  buff[1] = UPLOAD_SID_START;
  buff[2] = (filetype);
  write_chars(buff, 64);

  buff[1] = UPLOAD_SID_DATA;
  while ((byte = getc(input_f)) != EOF) {
    buff[bytecount++] = byte;
    if (bytecount == 64 || i == (file_size - 1)) {
      bytecount = 2;
      write_chars(buff, 64);
      memset(buff, 0, 64);
      buff[0] = (PACKET_TYPE|CONFIG);
      buff[1] = UPLOAD_SID_DATA;
    }
    ++i;
  }
  memset(buff, 0, 64);
  buff[0] = (PACKET_TYPE|CONFIG);
  buff[1] = UPLOAD_SID_END;
  write_chars(buff, 64);
  memset(buff, 0, 64);
  buff[0] = (PACKET_TYPE|CONFIG);
  buff[1] = UPLOAD_SID_SIZE;
  buff[2] = ((i&0xFF00)>>8); /* High byte */
  buff[3] = (i&0xFF); /* Low byte */
  write_chars(buff, 64);

  return;
}

/**
 * @brief Print help to stdout
 *
 */
void print_help(void)
{
  fprintf(stdout, "*** Usage ***\n");
  fprintf(stdout, "\n");
  fprintf(stdout, "-help / -h: Show this information\n");
  fprintf(stdout, "  sidfile.sid: send sidfile.sid to USBSID-Pico to start play\n");
  fprintf(stdout, "  sidtune.prg: send sidtune.prg to USBSID-Pico to start play (psid64 preferred!)\n");
  fprintf(stdout, "  -sid -: to read _SID_ file data from stdin instead of sidfile.sid (PRG not supported yet!)\n");
  fprintf(stdout, "  -t N: provide subtune number together with sid to set subtune (defaults to 1))\n");
  //  fprintf(stdout, "* -start: start play\n");
  fprintf(stdout, "-stop: stop play\n");
  //  fprintf(stdout, "* -pause: pause play\n");
  fprintf(stdout, "-next: play next subtune\n");
  fprintf(stdout, "-prev: play previous subtune\n");
  fprintf(stdout, "\n");
  fprintf(stdout, "Play SID file from local storage\n");
  fprintf(stdout, "./send_sid /path/to/sidfile.sid -t 1\n");
  fprintf(stdout, "\n");
  fprintf(stdout, "Play PRG file from local storage\n");
  fprintf(stdout, "./send_sid /path/to/sidfile.prg\n");
  fprintf(stdout, "\n");
  fprintf(stdout, "Play SID file directly from internet storage\n");
  fprintf(stdout, "SID=Wavemode_Mainpart.sid ;\\\n");
  fprintf(stdout, "  curl -sS 'https://deepsid.chordian.net/hvsc/_SID%%20Happens/'$SID |\\\n");
  fprintf(stdout, "  ./send_sid -sid -\n");

  return;
}

/**
 * @brief Main entrypoint
 *
 * @param argc
 * @param argv
 * @return int
 */
int main(int argc, char* argv[])
{
  int result = EXIT_FAILURE;
  int arg;
  FILE* input_f = NULL;
  const char* filename = NULL;
  const char* name = "data";

  unsigned char configbuff[5] = {0};
  configbuff[0] = (PACKET_TYPE|CONFIG);

  if (argc <= 1) {
    printf("Please supply atleast 1 option!\n");
    print_help();
    return EXIT_SUCCESS;
  }

  bool sentfile = false;
  bool sidfile = false;
  bool prgfile = false;

  if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-") || !strcmp(argv[1], "--")) {
    print_help();
    return EXIT_SUCCESS;
  }

  if (usbsid_init()) {
    goto exit;
  }

  for(int arg = 1; arg < argc; arg++) {

    /* Make sure we're treating a file, it must have a dot in it */
    if(strchr(argv[arg], '.') != NULL) {
      /* Get the filename from the arguments */
      const char *filename = argv[arg];
      /* Get the extension and convert to lowercase */
      char *ext = get_extension_and_tolower(filename);
      if (strcmp(ext, "sid") == 0) {
        sidfile = true;
        prgfile = false;
      } else if (strcmp(ext, "prg") == 0) {
        sidfile = false;
        prgfile = true;
      } else if (strcmp(ext, "p00") == 0) {
        sidfile = false;
        prgfile = true;
      } else {
        fprintf(stderr, "Failed to open input file: %s\n",ext);
        goto exit;
      }
      {
        fprintf(stdout, "Sending: %s\n",filename);
        input_f = fopen(filename, "rb");
        if (!input_f) {
          fprintf(stderr, "Failed to open input file\n");
          goto exit;
        }
      }
      {
        fprintf(stdout, "Stopping current playback, if any!\n");
        configbuff[1] = SID_PLAYER_STOP;
        write_chars(configbuff, 5);
      }
      send_sid((input_f ? input_f : stdin), (sidfile ? SID_FILE : prgfile ? PRG_FILE : FROM_STDIN));
      sentfile = true;
    }

    if(!strcmp(argv[arg], "-sid") || !strcmp(argv[arg], "sid")) { /* Receive from stdin */
      fprintf(stdout, "Sending from stdin\n");
      {
        fprintf(stdout, "Stopping current playback, if any!\n");
        configbuff[1] = SID_PLAYER_STOP;
        write_chars(configbuff, 5);
      }
      send_sid(stdin, FROM_STDIN);
      sentfile = true;
    }
    if (sentfile) {
      {
        fprintf(stdout, "Setting subtune to ");
        configbuff[1] = SID_PLAYER_LOAD;
        configbuff[2] = 0; //strtol(argv[arg+1], NULL, 16); /* Tune ID, 0 is uploaded SID file */
        configbuff[3] = 0; /* subtune */
        for(int arg_ = 1; arg_ < argc; arg_++) {
          if(!strcmp(argv[arg_], "-t") || !strcmp(argv[arg_], "t")) {
            configbuff[3] = atoi(argv[arg_+1]);
            configbuff[3] = (configbuff[3] != 0 ? (configbuff[3] - 1) : configbuff[3]);
          }
        }
        fprintf(stdout, "%d\n", (configbuff[3] + 1));
        write_chars(configbuff, 5);
      }
      {
        fprintf(stdout, "Starting playback\n");
        configbuff[1] = SID_PLAYER_START;
        write_chars(configbuff, 5);
      }
      goto done;
    }
    if(!strcmp(argv[arg], "-stop") || !strcmp(argv[arg], "stop") || !strcmp(argv[arg], "s")) {
      fprintf(stdout, "Stopping playback\n");
      configbuff[1] = SID_PLAYER_STOP;
      write_chars(configbuff, 5);
    }
    if(!strcmp(argv[arg], "-pause") || !strcmp(argv[arg], "pause") || !strcmp(argv[arg], "p")) {
      fprintf(stdout, "(Un)Pausing playback\n");
      configbuff[1] = SID_PLAYER_PAUSE;
      write_chars(configbuff, 5);
    }
    if(!strcmp(argv[arg], "-next") || !strcmp(argv[arg], "next")|| !strcmp(argv[arg], "n")) {
      fprintf(stdout, "Playing next subtune\n");
      configbuff[1] = SID_PLAYER_NEXT;
      write_chars(configbuff, 5);
    }
    if(!strcmp(argv[arg], "-prev") || !strcmp(argv[arg], "prev") || !strcmp(argv[arg], "b")) {
      fprintf(stdout, "Playing previous subtune\n");
      configbuff[1] = SID_PLAYER_PREV;
      write_chars(configbuff, 5);
    }
  }
done:
  result = EXIT_SUCCESS;
exit:
  usbsid_close();
  if (input_f) fclose(input_f);
  return result;
}
