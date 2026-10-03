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


#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>  // `errno`
#include <stdint.h> // `UINT64_MAX`
#include <stdio.h>  // `printf()`
#include <string.h> // `strerror(errno)`
#include <stdbool.h>
#include <ctype.h>
#include <time.h>   // `nanosleep()`

#include "USBSIDInterface.h"

/* Compile with CMake, the tool links USBSID-Pico-driver and the player's songlengths:
 * cmake -S . -B build && cmake --build build
 */

static USBSIDitf us = NULL;
static char target_serial[USBSID_SERIAL_LEN] = {0}; /* Board to open, empty = use target_index */
static int target_index = 0;                         /* Index in enumerate_USBSID() order */

int tune_no = 1;
const char * songlengths_path = NULL;
extern uint32_t find_songlenth_db(bool is_sid, int tune_no, const char * filename, const char * songlengths_path);


enum {
  /* Command bytes */
  PACKET_TYPE      = 0xC0,  /* 0b11000000 ~ 192  */
  CONFIG           = 0x12,  /*    0b10010 ~ 0x12 */

  /* Internal SID player upload */
  UPLOAD_SID_START    = 0xD0,  /* Start command for USBSID to go into receiving mode */
  UPLOAD_SID_DATA     = 0xD1,  /* Init byte for each packet containing data */
  UPLOAD_SID_END      = 0xD2,  /* End command for USBSID to exit receiving mode */
  UPLOAD_SID_SIZE     = 0xD3,  /* Packet containing the actual file size */
  UPLOAD_SID_PLAYTIME = 0xD4,  /* Provide max playtime for current tune, will run 5 minutes otherwise */

  /* Internal SID player control */
  SID_PLAYER_LOAD  = 0xE0,  /* Load SID file into SID player memory and initialize internal SID player */
  SID_PLAYER_START = 0xE1,  /* Start SID file play */
  SID_PLAYER_STOP  = 0xE2,  /* Stop SID file play */
  SID_PLAYER_PAUSE = 0xE3,  /* Pause/Unpause SID file play */
  SID_PLAYER_NEXT  = 0xE4,  /* Next SID subtune play */
  SID_PLAYER_PREV  = 0xE5,  /* Previous SID subtune play */
  SID_PLAYER_TWO   = 0xE6,  /* Force play to play on socket two or sid two */

  SID_PLAYER_MUTE  = 0xE9,  /* Full mute, mute a voice or just one chip/SID */
  SID_PLAYER_MUTED = 0xEA,  /* Read a the muted state */
  SID_PLAYER_TIME  = 0xEB,  /* Read play time of current track */

  FROM_STDIN       = 0x00,  /* Read data from stdin */
  SID_FILE         = 0x01,  /* File is SID */
  PRG_FILE         = 0x02,  /* File is PRG */
};

void teardown_wait(void)
{
  /* 100 milliseconds setup */
  struct timespec delay = {
      .tv_sec = 0,
      .tv_nsec = 100000000 /* 100 million nanoseconds = 100ms */
  };
  nanosleep(&delay, NULL);
  return;
}

/**
 * @brief Case insensitive string compare, serials are uppercase hex
 *
 * @param a first NUL terminated string
 * @param b second NUL terminated string
 * @return bool true when equal ignoring case
 */
static bool str_ieq(const char *a, const char *b)
{
  while (*a && *b) {
    if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    a++; b++;
  }
  return (*a == *b);
}

/**
 * @brief Print one enumerated board line
 *
 * @param i board index in enumerate_USBSID() order
 * @param d board info
 */
static void print_board(int i, const USBSIDdevinfo *d)
{
  printf("  board %d: serial %s @ bus=%u port=", i,
        d->serial[0] ? d->serial : "<unreadable>", d->bus);
  for (int p = 0; p < d->port_path_len; p++)
    printf(p ? ".%u" : "%u", d->port_path[p]);
  printf("\n");
}

/**
 * @brief Enumerate attached boards and select the one usbsid_init() opens
 *
 * @param print_output print every board found
 * @param board -b/--board value, board index or serial, NULL selects the first board
 * @return int 0 on success, 1 on failure or no matching board
 */
static int usbsid_enumerate_boards(bool print_output, const char *board)
{
  /* 1st call: count only (NULL + 0 allowed) */
  int n = enumerate_USBSID(NULL, 0);
  if (n < 0) { fprintf(stderr, "board enumeration failed\n"); return 1; }
  if (n == 0) { fprintf(stderr, "no USBSID-Pico boards found\n"); return 1; }

  USBSIDdevinfo *devs = calloc((size_t)n, sizeof *devs);
  if (!devs) return 1;

  /* 2nd call: fill. Return can exceed capacity if a board was plugged in between */
  int got = enumerate_USBSID(devs, n);
  if (got < 0) { fprintf(stderr, "board enumeration failed\n"); free(devs); return 1; }
  if (got > n) got = n;
  if (got == 0) { fprintf(stderr, "no USBSID-Pico boards found\n"); free(devs); return 1; }

  if (print_output) {
    for (int i = 0; i < got; i++) print_board(i, &devs[i]);
  }

  /* Resolve the requested board, serial match wins over index */
  int sel = -1;
  if (board == NULL) {
    sel = 0;
  } else {
    for (int i = 0; i < got; i++) {
      if (devs[i].serial[0] && str_ieq(devs[i].serial, board)) { sel = i; break; }
    }
    if (sel < 0 && board[0] != '\0') {
      char *end = NULL;
      long idx = strtol(board, &end, 10);
      if (*end == '\0' && idx >= 0 && idx < got) sel = (int)idx;
    }
  }

  if (sel < 0) {
    fprintf(stderr, "No board matches '%s', available boards:\n", board);
    for (int i = 0; i < got; i++) print_board(i, &devs[i]);
    free(devs);
    return 1;
  }

  /* Prefer the serial for opening, fall back to the index when unreadable */
  target_index = sel;
  memset(target_serial, 0, sizeof target_serial);
  if (devs[sel].serial[0])
    snprintf(target_serial, sizeof target_serial, "%s", devs[sel].serial);

  free(devs);
  return 0;
}

/**
 * @brief Take -b=/--board= out of argv, keeping argv[argc] NULL
 *
 * @param argc argument count, decremented when the option is removed
 * @param argv argument vector, option removed in place
 * @param board receives the value after '=', untouched when absent
 * @return int 0 when absent, 1 when found, -1 when given without a value
 */
static int take_board_arg(int *argc, char **argv, const char **board)
{
  for (int i = 1; i < *argc; i++) {
    const char *val = NULL;
    if (!strncmp(argv[i], "-b=", 3)) val = argv[i] + 3;
    else if (!strncmp(argv[i], "--board=", 8)) val = argv[i] + 8;
    else if (!strcmp(argv[i], "-b") || !strcmp(argv[i], "--board")) return -1;
    else continue;

    if (*val == '\0') return -1;
    *board = val;
    for (int j = i; j < *argc; j++) argv[j] = argv[j + 1];
    (*argc)--;
    return 1;
  }
  return 0;
}

/**
 * @brief Open the board selected by usbsid_enumerate_boards() without touching SID state
 *
 * @return int 0 on success, -1 on failure
 */
int usbsid_init(void)
{
  us = create_USBSID();
  if (us == NULL) {
    fprintf(stderr, "Error creating USBSID-Pico driver instance\n");
    return -1;
  }
  /* Serial survives a replug, index is the fallback when the serial was unreadable */
  if (target_serial[0] != '\0')
    settargetserial_USBSID(us, target_serial);
  else
    settargetindex_USBSID(us, target_index);
  /* Passive: no mute, bus clear or clock query that would disturb playback */
  setpassive_USBSID(us, true);
  if (init_USBSID(us, false, false) < 0 || !portisopen_USBSID(us)) {
    fprintf(stderr, "Error finding USB device\n");
    close_USBSID(us);
    us = NULL;
    return -1;
  }
  return 0;
}

/**
 * @brief Close the connection with USBSID-Pico
 *
 */
void usbsid_close(void)
{
  if (us == NULL) return;
  teardown_wait();
  close_USBSID(us);
  us = NULL;
  return;
}

/**
 * @brief Send a player command, report failure
 *
 * @param cmd USBSID_PLAYER_* command
 */
static void player_command(uint8_t cmd)
{
  if (playercommand_USBSID(us, cmd) < 0) {
    fprintf(stderr, "Error sending player command $%02X\n", cmd);
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
 * @brief Read a whole stream into memory
 *
 * @param input_f open stream, may be stdin
 * @param size receives the number of bytes read
 * @return uint8_t* malloc'd buffer, NULL on failure
 */
static uint8_t * read_all(FILE* input_f, size_t * size)
{
  size_t cap = 0x10000, len = 0;
  uint8_t * data = malloc(cap);
  if (data == NULL) return NULL;
  for (;;) {
    if (len == cap) {
      uint8_t * grown = realloc(data, cap * 2);
      if (grown == NULL) { free(data); return NULL; }
      data = grown;
      cap *= 2;
    }
    size_t n = fread(data + len, 1, cap - len, input_f);
    len += n;
    if (n == 0) break;
  }
  if (ferror(input_f)) { free(data); return NULL; }
  *size = len;
  return data;
}

/**
 * @brief Send input file to USBSID-Pico
 *
 * @param input_f
 * @param filetype
 * @return bool true when the upload was sent
 */
static bool send_sid(FILE* input_f, int filetype)
{
  size_t file_size = 0;
  uint8_t * data = read_all(input_f, &file_size);
  if (data == NULL) {
    fprintf(stderr, "Failed to read input\n");
    return false;
  }
  int sent = uploadtune_USBSID(us, data, file_size, (uint8_t)filetype);
  free(data);
  if (sent < 0) {
    fprintf(stderr, "Error uploading file\n");
    return false;
  }
  fprintf(stdout, "Sent %d bytes\n", sent);
  return true;
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
  fprintf(stdout, "-lb / --list-boards: List every attached board and exit\n");
  fprintf(stdout, "-b=ID / --board=ID: Use board ID (index from -lb) or serial, default first board\n");
  fprintf(stdout, "\n");
  fprintf(stdout, "  sidfile.sid: send sidfile.sid to USBSID-Pico to start play\n");
  fprintf(stdout, "  sidtune.prg: send sidtune.prg to USBSID-Pico to start play (psid64 preferred!)\n");
  fprintf(stdout, "  -sid -: to read _SID_ file data from stdin instead of sidfile.sid (PRG not supported yet!)\n");
  fprintf(stdout, "  -t N: provide subtune number together with sid to set subtune (defaults to 1))\n");
  fprintf(stdout, "  -f: Force play on second SID / socket (depends on USBSID-Pico configuration)\n");
  fprintf(stdout, "  -stop: stop play\n");
  //  fprintf(stdout, "* -pause: pause play\n");
  fprintf(stdout, "  -next: play next subtune\n");
  fprintf(stdout, "  -prev: play previous subtune\n");
  //  fprintf(stdout, "* -start: start play\n");
  fprintf(stdout,    "  --songlengths F:  HVSC Songlengths database, to stop when the song ends.\n"
    "                    Found by itself in $SONGLENGTHS, ~/Songlengths.md5,\n"
    "                    HVSCROOT or $HVSC_BASE DOCUMENTS/Songlengths.md5, or $HVSCDB.\n");
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

  /* Board selection, removed from argv to keep it out of the file and option parsers */
  const char *board = NULL;
  if (take_board_arg(&argc, argv, &board) < 0) {
    fprintf(stderr, "-b/--board requires a value: -b=ID or --board=SERIAL\n");
    return EXIT_FAILURE;
  }

  if (argc <= 1) {
    printf("Please supply atleast 1 option!\n");
    print_help();
    return EXIT_SUCCESS;
  }

  bool sentfile = false;
  bool forcetwo = false;
  bool sidfile = false;
  bool prgfile = false;
  uint32_t songlenth = 0;

  if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-") || !strcmp(argv[1], "--")) {
    print_help();
    return EXIT_SUCCESS;
  }

  if (!strcmp(argv[1], "-lb") || !strcmp(argv[1], "--list-boards")) {
    return (usbsid_enumerate_boards(true, NULL) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  /* First board without -b, else the requested index or serial */
  if (usbsid_enumerate_boards(false, board) != 0) {
    goto exit;
  }

  if (usbsid_init()) {
    goto exit;
  }
  {
    char serial[USBSID_SERIAL_LEN] = {0};
    if (getserial_USBSID(us, serial, sizeof serial) > 0)
      fprintf(stdout, "Connected to USBSID-Pico board %d (serial %s)\n", target_index, serial);
    else
      fprintf(stdout, "Connected to USBSID-Pico board %d\n", target_index);
  }

  /* Check if we have a request to force play on SID2 / Socket2 etc. */
  for(int a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "-f")) {
      forcetwo = true;
    } else if (!strcmp(argv[a], "--songlengths") && a + 1 < argc) {
      songlengths_path = argv[a+1];
    } else if((!strcmp(argv[a], "-t") || !strcmp(argv[a], "t")) && a + 1 < argc) {
      tune_no = atoi(argv[a+1]);
    }
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
        songlenth = find_songlenth_db(sidfile, tune_no, filename, songlengths_path);
      }
      {
        fprintf(stdout, "Stopping current playback, if any!\n");
        player_command(USBSID_PLAYER_STOP);
      }
      if (!send_sid((input_f ? input_f : stdin), (sidfile ? SID_FILE : prgfile ? PRG_FILE : FROM_STDIN))) {
        goto exit;
      }
      sentfile = true;
    }

    if(!strcmp(argv[arg], "-sid") || !strcmp(argv[arg], "sid")) { /* Receive from stdin */
      fprintf(stdout, "Sending from stdin\n");
      {
        fprintf(stdout, "Stopping current playback, if any!\n");
        player_command(USBSID_PLAYER_STOP);
      }
      if (!send_sid(stdin, FROM_STDIN)) {
        goto exit;
      }
      sentfile = true;
    }
    if (sentfile) {
      if (songlenth != 0 && songlenth != 300000) { /* 0: unknown, keep the 5 minute default */
        fprintf(stdout, "Sending playtime\n");
        if (playersetplaytime_USBSID(us, songlenth) < 0) {
          fprintf(stderr, "Error sending playtime\n");
        }
      }
      if (forcetwo) {
        fprintf(stdout, "Forcing SID/Socket 2\n");
        player_command(USBSID_PLAYER_TWO);
      }
      {
        fprintf(stdout, "Setting subtune to ");
        uint8_t subtune = 0;
        for(int arg_ = 1; arg_ < argc; arg_++) {
          if((!strcmp(argv[arg_], "-t") || !strcmp(argv[arg_], "t")) && arg_ + 1 < argc) {
            int t = atoi(argv[arg_+1]);
            subtune = (uint8_t)(t > 0 ? (t - 1) : 0);
          }
        }
        fprintf(stdout, "%d\n", (subtune + 1));
        if (playerload_USBSID(us, subtune) < 0) {
          fprintf(stderr, "Error loading tune\n");
        }
      }
      {
        fprintf(stdout, "Starting playback\n");
        player_command(USBSID_PLAYER_START);
      }
      goto done;
    }
    if(!strcmp(argv[arg], "-stop") || !strcmp(argv[arg], "stop") || !strcmp(argv[arg], "s")) {
      fprintf(stdout, "Stopping playback\n");
      player_command(USBSID_PLAYER_STOP);
    }
    if(!strcmp(argv[arg], "-pause") || !strcmp(argv[arg], "pause") || !strcmp(argv[arg], "p")) {
      fprintf(stdout, "(Un)Pausing playback\n");
      player_command(USBSID_PLAYER_PAUSE);
    }
    if(!strcmp(argv[arg], "-next") || !strcmp(argv[arg], "next")|| !strcmp(argv[arg], "n")) {
      fprintf(stdout, "Playing next subtune\n");
      player_command(USBSID_PLAYER_NEXT);
    }
    if(!strcmp(argv[arg], "-prev") || !strcmp(argv[arg], "prev") || !strcmp(argv[arg], "b")) {
      fprintf(stdout, "Playing previous subtune\n");
      player_command(USBSID_PLAYER_PREV);
    }
  }
done:
  result = EXIT_SUCCESS;
exit:
  usbsid_close();
  if (input_f) fclose(input_f);
  return result;
}
