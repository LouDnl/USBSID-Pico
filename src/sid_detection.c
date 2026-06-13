/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * sid_detection.c
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

#include <globals.h>
#include <usbsid_constants.h>
#include <config.h>
#include <bus.h>
#include <config_socket.h>
#include <config_bus.h>
#include <logging.h>
#include <sid.h>
#include <sid_armsid.h>
#include <sid_fpgasid.h>
#include <sid_pdsid.h>
#include <sid_backsid.h>
#include <sid_sidemu.h>
#if PCB_VERSION_INT >= 15
#include <gpio.h>
#endif


/* Local volatile variable to stop any logging when detecting socket changes */
static volatile bool detection_logging = true;


/**
 * @brief Set the detection logging object
 *
 * @param boolean enabled
 */
void set_detection_logging(bool enabled)
{
  detection_logging = enabled;
  return;
}

/**
 * @brief This routine works on real MOS SID chips and does not work on SKPico
 * @note https://github.com/GideonZ/1541ultimate/blob/master/software/6502/sidcrt/player/advanced/detection.asm
 *
 * @param  uint8_t start_addr
 * @return uint8_t 0 = unknown, 2 = 8580, 3 = 6581
 */
uint8_t detect_sid_model(uint8_t start_addr)
{
  if(detection_logging) usSID("Detect SID Model\n");
  clear_sid_registers_at_addr(start_addr);
  uint8_t zero_count = 0;
  int restart_a = 0;
restart_a:;
  /* lda #$48  ~ 2 cycles */
  /* sta $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x48, 5);
  /* sta $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0F), 0x48, 3);
  /* lsr ~ 2 cycles */
  /* sta $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x24, 5);  /* <-- 4 cycles instead of 6 */
  /* lda $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1B), 3);  /* <-- 3 cycles instead of 4 */
  /* tax       ~ 2 cycles */
  /* and #$fe  ~ 2 cycles */
  /* bne       ~ 2 cycles */
  /* lda $d41b ~ 4 cycles */
  if (sidtype == 0 || sidtype == 255) zero_count++;
  int restart_b = 0;
restart_b:;
  uint8_t readtest = cycled_read_operation((start_addr + 0x1B), 7);  /* SHOULD READ 3 */
  if(detection_logging) usSID("  RA%d RB%d $%02x detect_sid_model sidtype raw %02x\n",
    restart_a, restart_b, start_addr, sidtype);
  if(detection_logging) usSID("  RA%d RB%d $%02x detect_sid_model readtest %02x\n",
    restart_a, restart_b, start_addr, readtest);  /* SHOULD READ 3 */
  if (restart_b == 1) {
    restart_b = 0;
    goto continue_a;
  }
  if (readtest != 3) {
    restart_b++;
    goto restart_b;
  }
continue_a:;
  if (restart_a == 3) goto end;
  if (sidtype != 0 && sidtype != 1) {
    restart_a++;
    if(detection_logging) usSID("  Restart %d (ST:%u RT:%u)\n", restart_a, sidtype, readtest);
    goto restart_a;
  }
end:
  /* output 0 = 8580, 1 = 6581, 2 = unknown
   * that is: Carry flag is set for 6581, and clear for 8580. */
  if(detection_logging) usSID("  $%02x detect_sid_model sidtype raw %02x\n", start_addr, sidtype);
  if(detection_logging) usSID("  $%02x detect_sid_model readtest %02x\n", start_addr, readtest);  /* SHOULD READ 3 */
  sidtype = (sidtype == 0 ? 2 : sidtype == 1 ? 3 : 0);  /* return 0 = unknown, 2 = 8580, 3 = 6581 */
  if (readtest != 3) sidtype = 0;  /* if the readtest does not read 3, return unknown */
  if (readtest != 3 && zero_count > 0) sidtype = 0;  /* if reads return 0 more then 0 times, return unknown */
  if(detection_logging) usSID("  $%02x detect_sid_model return %u\n", start_addr, sidtype);
  return sidtype;
}

/**
 * @brief This routine works on real MOS SID chips and did not work on SKPico pre 022 firmware
 * @note https://www.codebase64.net/doku.php?id=base:detecting_sid_type_-_safe_method
 *
 * @param  uint8_t start_addr
 * @return uint8_t 0 = unknown, 2 = 8580, 3 = 6581
 */
uint8_t detect_sid_version(uint8_t start_addr)
{
  if(detection_logging) usSID("Detect SID Version\n");
  clear_sid_registers_at_addr(start_addr);
  uint8_t zero_count = 0;
  int restart = 0;
restart:
  /* LDA #$ff  ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0xff, 5);  /* Set testbit in voice 3 control register to disable oscillator */
  /* STA $d40e ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0e), 0xff, 3);  /* Set frequency in voice 3 to $ffff */
  /* STA $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0f), 0xff, 3);  /* Set frequency in voice 3 to $ffff */
  /* LDA #$20  ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x20, 5);  /* Set Sawtooth wave and gatebit off to start oscillator again */
  /* LDA $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1B), 3);  /* Accu now has different value depending on sid model (6581=3/8580=2) */
  if(detection_logging) usSID("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
  if (sidtype == 0 || sidtype == 255) zero_count++;
  if (restart == 3) goto end;
  if (sidtype != 2 && sidtype != 3) {
    restart++;
    if(detection_logging) usSID("  Restart %d (ST:%u)\n", restart, sidtype);
    goto restart;
  }
end:
  restart = 0;
  if(detection_logging) usSID("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
  sidtype = ((sidtype >= 1) && (sidtype % 2) == 0 ? 2 : 3);
  sidtype = (zero_count > 0 ? 0 : sidtype);
  if(detection_logging) usSID("  $%02x detect_sid_version return %02x\n", start_addr, sidtype);
  return sidtype;
}

/**
 * @brief This routine is used as fallback, seems to work all the time :)
 * @note https://www.codebase64.net/doku.php?id=base:detecting_sid_type
 *
 * @param  uint8_t start_addr
 * @return uint8_t 0 = unknown, 2 = 8580, 3 = 6581
 */
uint8_t detect_sid_reflex(uint8_t start_addr)
{ /* ISSUE: Seems to cause desync only solvable with an MCU restart
     and not solvable with reset registers or reset sids */
  if(detection_logging) usSID("Detect SID Reflex\n");
  clear_sid_registers_at_addr(start_addr);
  /* clear sid registers */
  for (uint reg = 0; reg < MAX_REGS; reg++) {
    /* STA $D400,x ~ 5 cycles */
    /* DEX         ~ 2 cycles*/
    /* BPL         ~ 2 cycles */
    cycled_write_operation((start_addr | sid_registers[reg]), 0x00, 8);
  }
  /* LDA #$02  ~ 2 cycles */
  /* STA $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0f), 0x02, 5);
  /* LDA #$30  ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x30, 3);
  /* LDY #$00  ~ 2 cycles */
	/* LDX #$00  ~ 2 cycles */
  cycled_delay_operation(3);
  uint8_t REG_X = 0, REG_Y = 0;
restart:;
  /* LDA $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1b), 3);
  if (sidtype & BIT_7 && sidtype != 255) { /* Signed integer minus value */
    if(detection_logging) usSID("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
    sidtype = 2; /* 8580 */
    goto end;
  }
  REG_X--;
  if (REG_X != 0) goto restart;
  REG_Y--;
  if (REG_Y != 0) goto restart;
  if (sidtype == 0) {
    uint8_t failsafe_a = cycled_read_operation((start_addr + 0x1b), 3); /* Read OSC3 */
    uint8_t failsafe_b = cycled_read_operation((start_addr + 0x1c), 3); /* Read ENV3 */
    if(detection_logging) usSID("  $%02x failsafe_a %02x failsafe_b %02x\n", start_addr, failsafe_a, failsafe_b);
    if(detection_logging) usSID("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
    if (failsafe_a == 0 && failsafe_b == 0 || failsafe_a == 255 && failsafe_b == 255) { /* BUG: NOT WORKING */
      sidtype = 0; /* Unknown */
    } else {
      sidtype = 3; /* 6581 */
    }
  }
end:;
  if(detection_logging) usSID("  $%02x detect_sid_version return %02x\n", start_addr, sidtype);
  return sidtype;
}

/**
 * @brief Adapted version of the above, explicitely for SIDKick-pico
 * @deprecated no longer needed due to skpico fw adjustments
 * @note https://www.codebase64.net/doku.php?id=base:detecting_sid_type_-_safe_method
 *
 * @param uint8_t start_addr, the socket address
 * @return uint8_t the sid type
 */
uint8_t detect_sid_version_skpico_deprecated(uint8_t start_addr)  /* Not working on real SIDS!? */
{
  if(detection_logging) usSID("Detect SID version SKpico\n");
  clear_sid_registers_at_addr(start_addr);
  int restart = 0;
restart:
  /* LDA #$ff ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0xFF, 1000);  /* Set testbit in voice 3 control register to disable oscillator */
  /* STA $d40e ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0E), 0xFF, 1000);  /* Set frequency in voice 3 to $ffff */
  sleep_ms(1);
  /* STA $d40f ~ 4 cycles */
  cycled_write_operation((start_addr + 0x0F), 0xFF, 1000);  /* Set frequency in voice 3 to $ffff */
  sleep_ms(1);
  /* LDA #$20 ~ 2 cycles */
  /* STA $d412 ~ 4 cycles */
  cycled_write_operation((start_addr + 0x12), 0x20, 1000);  /* Set Sawtooth wave and gatebit OFF to start oscillator again */
  sleep_ms(1);
  /* LDA $d41b ~ 4 cycles */
  uint8_t sidtype = cycled_read_operation((start_addr + 0x1B), 1000);  /* Accu now has different value depending on sid model (6581=3/8580=2) */
  if (restart == 3) goto end;
  if (sidtype != 2 && sidtype != 3) {
    restart++;
    if(detection_logging) usSID("  Restart %d (ST:%u)\n", restart, sidtype);
    goto restart;
  }
end:
  restart = 0;
  if(detection_logging) usSID("  $%02x detect_sid_version_skpico_deprecated raw %02x\n", start_addr, sidtype);
  sidtype = (sidtype < 4 ? sidtype : 0);
  if(detection_logging) usSID("  $%02x detect_sid_version_skpico_deprecated %02x\n", start_addr, sidtype);
  return sidtype;  /* that is: Carry flag is set for 6581, and clear for 8580. */
}

/**
 * @brief Routine to detect if an emulated FMOpl chip is present
 * @note made for SKPico
 *
 * @param uint8_t base_address
 * @return true
 * @return false
 */
bool detect_fmopl(uint8_t base_address)
{
  if(detection_logging) usSID(" Detect FMOpl @: %02x\n", base_address);
  clear_sid_registers_at_addr(base_address);
  int restart = 0;
restart:
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x60, 10);
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x80, 10);
  uint8_t r = cycled_read_operation(base_address, 10);
  if(detection_logging) usSID("  Read FMOpl = $c0? %02x\n", r);
  if (restart == 3) goto end;
  if (r != 0xC0) {
    restart++;
    goto restart;
  }
end:
  return (r == 0xC0 ? true : false);
}

/**
 * @brief Detect SID type
 * @note Could return a false positive, always check configuration after use
 *
 * @param uint8_t base_address
 * @return uint8_t SIDType
 */
SIDType detect_sidtype_at(uint8_t base_address, uint8_t chiptype)
{
  if(detection_logging) usSID("Detecting SIDType @ $%02x (chiptype=%d)\n", base_address, chiptype);

  uint8_t sidtype;
  /* Use waveform method */
  sidtype = detect_sid_model(base_address);
  if (sidtype == SID_UNKNOWN) {
    /* Try again using oscillator method */
    sidtype = detect_sid_version(base_address);
  }
  /* Fallback for CHIP_ARMSID, CHIP_BACKSID */
  if (sidtype == SID_UNKNOWN
    && (chiptype == CHIP_ARMSID) || (chiptype == CHIP_BACKSID)) {
    sidtype = detect_sid_reflex(base_address);
  }

  /* If unknown, check for FMOpl */
  if (chiptype == CHIP_SKPICO && sidtype == SID_UNKNOWN && detect_fmopl(base_address)) {
    if(detection_logging) usSID("Detected SIDType %d @ $%02x = %s\n", sidtype, base_address, sid_type_name(sidtype));
    return SID_FMOPL;
  }

  if(detection_logging) usSID("Detected SIDType %d @ $%02x = %s\n", sidtype, base_address, sid_type_name(sidtype));
  return sidtype;
}

/**
 * @brief Internal function used in SID auto & socket change detection and routines
 *
 * @param result
 * @param s_addr1
 * @param s_addr2
 * @param socket
 * @return DetectionResult
 */
static DetectionResult detect_socket_sid(DetectionResult result, uint8_t s_addr1, uint8_t s_addr2, const int socket)
{
  if(detection_logging) {
    usSID("\n");
    usSID("Start %s SID detection\n", config_socket_num(socket));
  }

  if (result.socket[socket].present) {
    result.socket[socket].sid1_type = detect_sidtype_at(s_addr1, result.socket[socket].chiptype);
    if (result.socket[socket].supports_dual) {
      result.socket[socket].sid2_type = detect_sidtype_at(s_addr2, result.socket[socket].chiptype);
    } else {
      result.socket[socket].sid2_type = SID_NA;
    }
  } else {
    result.socket[socket].sid1_type = SID_NA;
    result.socket[socket].sid2_type = SID_NA;
  }
  return result;
}

/**
 * @brief Detect SIDKick-pico presence
 *
 * @param uint8_t base_address
 * @return true
 * @return false
 */
bool detect_skpico(uint8_t base_address)
{
  if(detection_logging) usSID("  Check for SIDKick-pico @ $%02x\n", base_address);
  clear_sid_registers_at_addr(base_address);
  /* SKPico routine */
  char skpico_version[36] = {0};
  cycled_write_operation((0x1F + base_address), 0xFF, 0); /* Init config mode */
  cycled_write_operation((0x1D + base_address), 0xFA, 0); /* Extend config mode */
  for (int i = 0; i < 36; i++) {
    cycled_write_operation((0x1E + base_address), 0xE0 + i, 0);
    skpico_version[i] = cycled_read_operation((0x1D + base_address), 0);
    if (i >= 2 && i <= 5) skpico_version[i] |= 0x60;
  }
  /* Reset after writes
   * is needed for Real SID's to recover for SID detection
   * but breaks SKPico SID detection */
  cycled_write_operation((0x1D + base_address), 0xFB, 0); /* Exit config mode */
  if (skpico_version[2] == 0x70
      && skpico_version[3] == 0x69
      && skpico_version[4] == 0x63
      && skpico_version[5] == 0x6F) {
    if(detection_logging) usSID("  Found SIDKick-pico @ $%02x version is: %.36s\n", base_address, skpico_version);
    return true;
  }
  return false;
}

/**
 * @brief Detect FPGASID presence
 *
 * @param uint8_t base_address
 * @return true
 * @return false
 */
bool detect_fpgasid(uint8_t base_address)
{
  if(detection_logging) usSID("  Check for FPGASID @ $%02x\n", base_address);
  clear_sid_registers_at_addr(base_address);
  uint8_t idHi, idLo;
  /* Enable configuration mode (if available) */
  cycled_write_operation((0x19 + base_address), 0x80, 6);      /* Write magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0x65, 6);      /* Write magic cookie Lo */
  /* Start identification routine */
  cycled_write_operation((0x1E + base_address), (1 << 7), 6);  /* Set identify bit to 1 */
  idLo = cycled_read_operation((0x19 + base_address), 4);      /* Read identify Hi */
  idHi = cycled_read_operation((0x1A + base_address), 4);      /* Read identify Lo */
  /* Exit configuration mode */
  cycled_write_operation((0x19 + base_address), 0x0, 6);       /* Clear magic cookie Hi */
  cycled_write_operation((0x1A + base_address), 0x0, 6);       /* Clear magic cookie Lo */
  uint16_t fpgasid_id = (idHi << 8 | idLo);
  if(detection_logging) usSID("  Read Identify 0x%04X ($%02x,$%02x) @ $%02x\n", fpgasid_id, idHi, idLo, base_address);
  if (fpgasid_id == FPGASID_ID) {
    if(detection_logging) usSID("  Found FPGASID @ $%02x\n", base_address);
    return true;
  }
  return false;
}

/**
 * @brief Detect ARMSID presence
 * @note based on the Ultimate64 detection routine by @GideonZ
 *
 * @param uint8_t base_address
 * @return true
 * @return false
 */
uint8_t detect_armsid(uint8_t base_address)
{
  if(detection_logging) usSID("  Check for ARMSID @ $%02x\n", base_address);
  clear_sid_registers_at_addr(base_address);
  /* Clear / reset ARM(2)SID with 3 writes */
  cycled_write_operation((ARMSID_W1 + base_address),0x00,6); /* $1d -> 0x00 */
  cycled_write_operation((ARMSID_W1 + base_address),0x00,6); /* $1d -> 0x00 */
  cycled_write_operation((ARMSID_W1 + base_address),0x00,6); /* $1d -> 0x00 */
  sleep_us(10);
  cycled_write_operation((ARMSID_W1 + base_address),ARMSID_S,6); /* $1d -> 0x53 'S' */
  sleep_us(10);
  cycled_write_operation((ARMSID_W2 + base_address),ARMSID_I,6); /* $1e -> 0x49 'I' */
  sleep_us(10);
  cycled_write_operation((ARMSID_W3 + base_address),ARMSID_D,6); /* $1f -> 0x44 'D' */
  sleep_us(10);
  sleep_ms(10);

  /* Read OSC3 */
  uint8_t id1 = cycled_read_operation((ARMSID_R1 + base_address), 4); /* $1b <- expect 'N' */
  sleep_us(10);
  /* Read ENV3 */
  uint8_t id2 = cycled_read_operation((ARMSID_R2 + base_address), 4); /* $1c <- expect 'O' */
  sleep_us(10);

  if(detection_logging) usSID("  ARMSID Detect: 0b%08b 0b%08b $%02x $%02x\n", id1, id2, id1, id2);
  if ((id1 == ARMSID_ID1) && (id2 == ARMSID_ID2)) { /* expect 0x4e 'N' && 0x4f 'O' */

    /* Try detect if ARM2SID */
    cycled_write_operation((ARMSID_W3 + base_address),ARMSID_I,6); /* $1f -> 0x49 'I' */
    sleep_us(10);
    cycled_write_operation((ARMSID_W2 + base_address),ARMSID_I,6); /* $1e -> 0x49 'I' */
    sleep_us(10);
    sleep_ms(10);
    uint8_t id1 = cycled_read_operation((ARMSID_R1 + base_address), 4); /* $1b <- expect 'L' */
    uint8_t id2 = cycled_read_operation((ARMSID_R2 + base_address), 4); /* $1c <- expect 'L' */
    sleep_us(10);
    /* Clear / reset ARM(2)SID with 3 writes */
    cycled_write_operation((ARMSID_W1 + base_address),0x00,6); /* $1d -> 0x00 */
    cycled_write_operation((ARMSID_W1 + base_address),0x00,6); /* $1d -> 0x00 */
    cycled_write_operation((ARMSID_W1 + base_address),0x00,6); /* $1d -> 0x00 */
    sleep_us(10);

    if(detection_logging) usSID("  ARM2SID Detect: 0b%08b 0b%08b $%02x $%02x\n", id1, id2, id1, id2);
    if ((id1 == ARM2SID_ID1) /* expect #$02 */
      && ((id2 == ARM2SID_ID2) || (id2 == ARM2SID_ID3))) { /* expect 'L' or 'R' */
      if(detection_logging) usSID("  Found ARM2SID @ $%02x\n", base_address);
      return 2;
    }
    if(detection_logging) usSID("  Found ARMSID @ $%02x\n", base_address);
    return 1;
  }
  return 0;
}

/**
 * @brief Detect PDSID presence
 *
 * @param uint8_t base_address
 * @return true
 * @return false
 */
bool detect_pdsid(uint8_t base_address)
{
  if(detection_logging) usSID("  Check for PDsid @ $%02x\n", base_address);
  clear_sid_registers_at_addr(base_address);
  cycled_write_operation((PDREG_P + base_address),PDSID_P,6); /* 0x50 */
  cycled_delay_operation(4); /* 4 cycles */
  cycled_write_operation((PDREG_D+ base_address),PDSID_D,6); /* 0x44 */
  cycled_delay_operation(4); /* 4 cycles */
  uint8_t pdsid_id = cycled_read_operation((PDREG_D + base_address),6);
  if (pdsid_id == PDSID_ID) { /* 0x53 'S' */
    if(detection_logging) usSID("  Found PDsid @ $%02x\n", base_address);
    return true;
  }
  return false;
}

/**
 * @brief Detect BackSID presence
 * @note Reverse engineered from `backsid223.prg`
 *
 * @param uint8_t base_address
 * @return true
 * @return false
 */
bool detect_backsid(uint8_t base_address)
{
  if(detection_logging) usSID("  Check for BackSID @ $%02x\n", base_address);
  clear_sid_registers_at_addr(base_address);
  cycled_write_operation((BACKSID_REG + base_address),0x00,6); /* 0x50 */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t backsid_id = cycled_read_operation((BACKSID_RD + base_address),6);
  if (backsid_id == BACKSID_ID) { /* 0x53 'S' */
    if(detection_logging) usSID("  Found BackSID @ $%02x\n", base_address);
    return true;
  }
  return false;
}

/**
 * @brief Detect SIDEmu presence
 *
 * @param uint8_t base_address
 * @return true
 * @return false
 */
bool detect_sidemu(uint8_t base_address)
{
  if(detection_logging) usSID("  Check for SIDEmu @ $%02x\n", base_address);
  clear_sid_registers_at_addr(base_address);

  char sidemu_identifier[17] = {0};

  /* Enable config mode */
  cycled_write_operation((SIDEMU_CFG_OFFSET + base_address),SIDEMU_CMD_1,6); /* #66 */
  clockcycle_delay(SIDEMU_WAIT_CYCLES);
  cycled_write_operation((SIDEMU_CFG_OFFSET + base_address),SIDEMU_CMD_2,6); /* #69 */
  clockcycle_delay(SIDEMU_WAIT_CYCLES);

  cycled_write_operation((SIDEMU_CFG_OFFSET + base_address),SIDEMU_OFFS_ID,6); /* #$fe */
  clockcycle_delay(SIDEMU_WAIT_CYCLES);
  uint8_t num = 0x0;
RETRY:;
  cycled_write_operation((SIDEMU_CFG_DATA + base_address),num,6); /* 0x0 */
  clockcycle_delay(SIDEMU_WAIT_CYCLES);
  uint8_t sidemu_id = cycled_read_operation((SIDEMU_CFG_DATA + base_address),6);
  if ((sidemu_id & 0x3f) == 0) {
    if ((num > 0) && sidemu_identifier[0] == 0x53) { /* 0x53 = 'S' */
      if(detection_logging) usSID("  Found %.17s @ $%02x\n", sidemu_identifier, base_address);
    } else {
      goto FAIL;
    }
  } else {
    sidemu_identifier[num] = sidemu_id;
    num++;
    if (num > 20) goto FAIL;
    goto RETRY;
  }

  /* Disable config mode */
  cycled_write_operation((SIDEMU_CFG_OFFSET + base_address),SIDEMU_CMD_1,6); /* #66 */
  clockcycle_delay(SIDEMU_WAIT_CYCLES);
  cycled_write_operation((SIDEMU_CFG_OFFSET + base_address),SIDEMU_CMD_3,6); /* #96 */
  clockcycle_delay(SIDEMU_WAIT_CYCLES);
  return true;
FAIL:;
  return false;
}

/**
 * @brief Detect chip type
 * @note Could return a false positive, always check configuration after use
 *
 * @param uint8_t base_address
 * @return uint8_t ChipType
 */
ChipType detect_chiptype_at(uint8_t base_address)
{ /* QUESTION: Maybe clear registers between detections? */
  if(detection_logging) usSID("Detecting ChipType @ $%02x\n", base_address);

  /* Detect in order of prevalence/speed */

  uint8_t armsid = detect_armsid(base_address);
  if (armsid == 1) {
    return CHIP_ARMSID;
  } else if (armsid == 2) {
    return CHIP_ARM2SID;
  }

  if (detect_fpgasid(base_address)) {
    return CHIP_FPGASID;
  }

  if (detect_backsid(base_address)) {
    return CHIP_BACKSID;
  }

  if (detect_skpico(base_address)) {
    return CHIP_SKPICO;
  }

  /* This detection routine is known to freeze when
     a BackSID or SKPico is present */
  if (detect_sidemu(base_address)) {
    return CHIP_SIDEMU;
  }

  /* This detection routine is known to cause desync, so we do this as last */
  if (detect_pdsid(base_address)) {
    return CHIP_PDSID;
  }

  if(detection_logging) usSID("No clone found @ $%02x, assuming real SID for now\n", base_address);
  return CHIP_REAL;
}

/**
 * @brief Internal function used in SID auto & socket change detection and routines
 *
 * @param result
 * @param probe
 * @param socket
 * @return DetectionResult
 */
static DetectionResult detect_socket_chip(DetectionResult result, Config * probe, const int socket)
{
  /* Detect Socket n */
  if(detection_logging) {
    usNFO("\n");
    usSID("Start %s Chip detection\n", config_socket_num(socket));
  }
  switch (socket) {
    case 0:
      result.socket[socket].chiptype = detect_chiptype_at(probe->socketOne.sid1.addr); /* 0x00 */
      probe->socketOne.chiptype = result.socket[socket].chiptype;
      break;
    case 1:
      result.socket[socket].chiptype = detect_chiptype_at(probe->socketTwo.sid1.addr); /* 0x00 */
      probe->socketTwo.chiptype = result.socket[socket].chiptype;
      break;
    // case 2:
      // result.socket[socket].chiptype = detect_chiptype_at(probe->socketThree.sid1.addr;); /* 0x00 */
      // probe->socketThree.chiptype = result.socket[socket].chiptype;
      // break;
    // case 3:
      // result.socket[socket].chiptype = detect_chiptype_at(probe->socketFour.sid1.addr;); /* 0x00 */
      // probe->socketFour.chiptype = result.socket[socket].chiptype;
      // break;
    default:
      break;
  }
  result.socket[socket].present = (result.socket[socket].chiptype != CHIP_UNKNOWN);
  result.socket[socket].supports_dual = (
    /* DualSID supporting clones */
    result.socket[socket].chiptype == CHIP_SKPICO ||
    result.socket[socket].chiptype == CHIP_FPGASID ||
    result.socket[socket].chiptype == CHIP_ARM2SID
  );
  if (socket > 0 || socket <= 4) { /* socket 2, 3 or 4 only */
    /* Fallback to false if arm2sid (3x SID max) */
    result.socket[socket].supports_dual = (
      /* If socket before this contains ARM2SID */
      (result.socket[(socket - 1)].chiptype == CHIP_ARM2SID &&
       /* And this socket contains ARM2SID */
       result.socket[socket].chiptype == CHIP_ARM2SID)
       /* Then this socket cannot support dualsid */
      ? false : result.socket[socket].supports_dual
    );
  }
  return result;
}

/**
 * @brief Internal function to verify if there is indeed a Chip present in the socket
 *        and update the probe config data if none detected
 *
 * @param result
 * @param socket
 * @return DetectionResult
 */
static DetectionResult verify_socket_chip(DetectionResult result, const int socket)
{
  /* If possible real chip or unknown chip */
  if ((result.socket[socket].chiptype == CHIP_REAL) || (result.socket[socket].chiptype == CHIP_UNKNOWN)) {
    /* If both SID types are N/A */
    if (((result.socket[socket].sid1_type == SID_UNKNOWN) && (result.socket[socket].sid1_type == SID_UNKNOWN))
       || ((result.socket[socket].sid1_type == SID_UNKNOWN) && (result.socket[socket].sid1_type == SID_NA))) {

      /* Then there must not be a SID Chip present I guess!? */
      result.socket[socket].chiptype       = CHIP_UNKNOWN;
      result.socket[socket].present        = false;
      result.socket[socket].supports_dual  = false;
      result.socket[socket].sid1_type      = SID_NA;
      result.socket[socket].sid2_type      = SID_NA;
    }
  }

  return result;
}

/**
 * @brief Internal function for updating a probe config
 *
 * @param result
 * @param probe
 * @param socket
 */
static void update_probe_config_from_detection(DetectionResult result, Config * probe, const int socket)
{
  bool dualsid = result.socket[socket].supports_dual;
  if(detection_logging) {
    usSID("\n");
    usSID("Updating %s %s probe config\n",
      config_socket_num(socket),
    (dualsid ? "dualsid" : "single sid"));
  }
  /* socket 2, 3 or 4 only */
  uint8_t base =
    result.socket[((socket > 0 || socket <= 4) ? (socket - 1) : socket)].supports_dual
    /* SocketTwo/Three/Four base address based on previous socket dual support */
    ? 0x40 : 0x20;
  switch (socket) {
    case 0:
      probe->socketOne.dualsid   = dualsid;
      /* Shift id's and addresses if SocketOne is dual */
      probe->socketOne.sid1.addr = 0x00; /* was 0x00 */
      probe->socketOne.sid1.id   = 0;    /* was 0x00 */
      probe->socketOne.sid2.addr = (dualsid ? 0x20 : 0xff); /* was 0xff */
      probe->socketOne.sid2.id   = (dualsid ? 1 : 255);     /* was 0xff */
      break;
    case 1:
      probe->socketTwo.dualsid   = dualsid;
      /* Shift id's and addresses if SocketTwo is dual and based on SocketOne dual support */
      probe->socketTwo.sid1.addr = base;                                /* 0x20 or 0x40 */
      probe->socketTwo.sid1.id   = (base >> 5);                         /* shift right 5 is 1 or 2 (divide by 10, divide by 2) */
      probe->socketTwo.sid2.addr = (dualsid ? (base + 0x20) : 0xff);    /* 0x40, 0x60 or 0xff */
      probe->socketTwo.sid2.id   = (dualsid ? ((base >> 5) + 1) : 255); /* same as above but 1 higher (id's are ones based) */
      break;
    // case 2:
      // break;
    // case 3:
      // break;
    default:
      break;
  }

  return;
}

/**
 * @brief Runs a Chip and SID detection routine
 *        for all sockets and possible addresses
 *
 * @return DetectionResult
 */
DetectionResult detect_all(void)
{
  /* result including sockets */
  DetectionResult result = { .success = false, .error = CFG_OK };

  if(detection_logging) usSID("Running Chip & SID detection\n");
  set_busconfig_logging(false);

  /* Create temporary probe config */
  Config probe = {
    .socketOne = default_socket(1),
    .socketTwo = default_socket(2),
    .mirrored = false,
  };

  /* Apply probe config to hardware */
  RuntimeCFG probe_rt; /* New empty runtime cfg */
  apply_runtime_config(&probe, &probe_rt); /* init probe_rt with probe config */

  /* Copy to global temporarily (required for bus operations) */
  uint32_t irq = save_and_disable_interrupts();
  RuntimeCFG saved_cfg; /* New temporary runtime cfg for safe keeping current cfg */
  memcpy(&saved_cfg, &cfg, sizeof(RuntimeCFG)); /* copy running cfg to temp cfg */
  memcpy(&cfg, &probe_rt, sizeof(RuntimeCFG));  /* copy probe cfg to running cfg */
  restore_interrupts(irq);
  /* Give clones time to finish whatever it's doing */
  sleep_ms(500);

  /* Detect SocketOne Chip */
  result = detect_socket_chip(result, &probe, SOCK_ONE);
  /* Update probe config (for dual or single sid detection) */
  update_probe_config_from_detection(result, &probe, SOCK_ONE);

  /* Re-apply probe config for SID detection */
  apply_runtime_config(&probe, &probe_rt); /* re-init probe_rt with probe config */
  irq = save_and_disable_interrupts();
  memcpy(&cfg, &probe_rt, sizeof(RuntimeCFG)); /* copy probe cfg to running cfg */
  restore_interrupts(irq);
  /* Tired yet? */
  sleep_ms(250);

  /* Detect SocketOne SID types */
  result = detect_socket_sid(result, probe.socketOne.sid1.addr, probe.socketOne.sid2.addr, SOCK_ONE);
  /* Verify SocketOne results */
  result = verify_socket_chip(result, SOCK_ONE);

  /* Detect SocketTwo Chip */
  result = detect_socket_chip(result, &probe, SOCK_TWO);
  /* Update probe config (for dual or single sid detection) */
  update_probe_config_from_detection(result, &probe, SOCK_TWO);

  /* Re-apply probe config for SID detection */
  apply_runtime_config(&probe, &probe_rt);
  irq = save_and_disable_interrupts();
  memcpy(&cfg, &probe_rt, sizeof(RuntimeCFG));
  restore_interrupts(irq);
  /* Tired yet? */
  sleep_ms(250);

  /* Detect SocketTwo SID types */
  result = detect_socket_sid(result, probe.socketTwo.sid1.addr, probe.socketTwo.sid2.addr, SOCK_TWO);
  /* Verify SocketTwo results */
  result = verify_socket_chip(result, SOCK_TWO);

  /* Restore original runtime config */
  irq = save_and_disable_interrupts();
  memcpy(&cfg, &saved_cfg, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  result.success = true;

  set_busconfig_logging(false);
  if(detection_logging) {
    usNFO("\n");
    usSID("Chip & SID detection complete: S1(%d,%d,%d) S2(%d,%d,%d)\n",
      result.socket[0].chiptype, result.socket[0].sid1_type, result.socket[0].sid2_type,
      result.socket[1].chiptype, result.socket[1].sid1_type, result.socket[1].sid2_type);}

  return result;
}

/**
 * @brief Handles chip and SID detection and configuration changes
 *
 * @param bool at_boot
 * @return * ConfigError
 */
ConfigError sid_auto_detect(bool at_boot)
{
#if PCB_VERSION_INT >= 15
  /* Set base voltages, only does anything if PCB version is >= v1.5 */
  set_base_voltages(500);
#endif

  if(detection_logging) {
    usNFO("\n");
    usSID("Starting auto detect routine\n");
  }

  /* Run detection */
  DetectionResult det = detect_all();

  /* Apply results */
  err = apply_detection_results(&det);
  if (err != CFG_OK) {
#if PCB_VERSION_INT >= 15
    voltage_state_off(); /* Turn off regulators if an error occurs */
#endif
    usERR("Error applying detection results: %s\n", config_error_str(err));
    return err;
  }

  /* Apply config to hardware */
  err = apply_config(at_boot);
  if (err != CFG_OK) {
#if PCB_VERSION_INT >= 15
    voltage_state_off(); /* Turn off regulators if an error occurs */
#endif
    usERR("Applying configuration failed: %s\n", config_error_str(err));
    return err;
  }

  /* Reset SID registers after detection */
  reset_sid_registers();

  usNFO("\n");
  if(detection_logging) usSID("Auto detection completed\n");
#if PCB_VERSION_INT >= 15
  voltage_state_off(); /* Turn off regulators after detection routine */
#endif
  return CFG_OK;
}

/**
 * @brief Handles chip and SID detection and configuration changes
 * @note Special function used in config_socket:apply_preset()
 *
 * @return * ConfigError
 */
ConfigError sid_auto_detect_silent(void)
{
#if PCB_VERSION_INT >= 15
  bool voltage_state = ((get_pin_states() & 0b11) ? true : false);
  set_base_voltages(500);
#endif
  /* Run detection */
  DetectionResult det = detect_all();

  /* Apply results */
  err = apply_detection_results(&det);
  if (err != CFG_OK) {
#if PCB_VERSION_INT >= 15
    if (!voltage_state) { voltage_state_off(); }; /* Turn off regulators if they were off before */
#endif
    usERR("Detection results invalid: %s\n", config_error_str(err));
    return err;
  }

  /* Reset SID registers after detection */
  reset_sid_registers();

#if PCB_VERSION_INT >= 15
  if (!voltage_state) { voltage_state_off(); }; /* Turn off regulators if they were off before */
#endif

  return CFG_OK;
}
