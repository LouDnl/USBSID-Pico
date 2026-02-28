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

#include "globals.h"
#include "config_constants.h"
#include "config.h"
#include "usbsid.h"
#include "sid.h"
#include "logging.h"
#include "sid_armsid.h"
#include "sid_fpgasid.h"
#include "sid_pdsid.h"
#include "sid_backsid.h"


/* config.c */
extern ConfigError apply_config(bool at_boot);
extern Config usbsid_config;
extern RuntimeCFG cfg;

/* config_socket.c */
extern ConfigError apply_detection_results(const DetectionResult *det);
extern Socket default_socket(int id);

/* config_bus.c */
extern void apply_runtime_config(const Config *config, RuntimeCFG *rt);

/* bus.c */
extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);
extern uint8_t cycled_read_operation(uint8_t address, uint16_t cycles);
extern uint16_t cycled_delay_operation(uint16_t cycles);
extern void clear_bus(int sidno);

/* sid.c */
extern void reset_sid_registers(void);
extern void reset_sid(void);


/**
 * @brief This routine works on real MOS SID chips and does not work on SKPico
 * @note https://github.com/GideonZ/1541ultimate/blob/master/software/6502/sidcrt/player/advanced/detection.asm
 *
 * @param  uint8_t start_addr
 * @return uint8_t 0 = unknown, 2 = 8580, 3 = 6581
 */
uint8_t detect_sid_model(uint8_t start_addr)
{ /*  */
  usCFG("Detect SID Model\n");
  clear_bus(start_addr / 0x20);
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
  usCFG("  RA%d RB%d $%02x detect_sid_model sidtype raw %02x\n",
    restart_a, restart_b, start_addr, sidtype);
  usCFG("  RA%d RB%d $%02x detect_sid_model readtest %02x\n",
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
    usCFG("  Restart %d (ST:%u RT:%u)\n", restart_a, sidtype, readtest);
    goto restart_a;
  }
end:
  /* output 0 = 8580, 1 = 6581, 2 = unknown
   * that is: Carry flag is set for 6581, and clear for 8580. */
  usCFG("  $%02x detect_sid_model sidtype raw %02x\n", start_addr, sidtype);
  usCFG("  $%02x detect_sid_model readtest %02x\n", start_addr, readtest);  /* SHOULD READ 3 */
  sidtype = (sidtype == 0 ? 2 : sidtype == 1 ? 3 : 0);  /* return 0 = unknown, 2 = 8580, 3 = 6581 */
  if (readtest != 3) sidtype = 0;  /* if the readtest does not read 3, return unknown */
  if (readtest != 3 && zero_count > 0) sidtype = 0;  /* if reads return 0 more then 0 times, return unknown */
  usCFG("  $%02x detect_sid_model return %u\n", start_addr, sidtype);
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
  usCFG("Detect SID Version\n");
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
  usCFG("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
  if (sidtype == 0 || sidtype == 255) zero_count++;
  if (restart == 3) goto end;
  if (sidtype != 2 && sidtype != 3) {
    restart++;
    usCFG("  Restart %d (ST:%u)\n", restart, sidtype);
    goto restart;
  }
end:
  restart = 0;
  usCFG("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
  sidtype = ((sidtype >= 1) && (sidtype % 2) == 0 ? 2 : 3);
  sidtype = (zero_count > 0 ? 0 : sidtype);
  usCFG("  $%02x detect_sid_version return %02x\n", start_addr, sidtype);
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
{
  usCFG("Detect SID Reflex\n");
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
    usCFG("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
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
    usCFG("  $%02x failsafe_a %02x failsafe_b %02x\n", failsafe_a, failsafe_b);
    usCFG("  $%02x detect_sid_version raw %02x\n", start_addr, sidtype);
    if (failsafe_a == 0 && failsafe_b == 0 || failsafe_a == 255 && failsafe_b == 255) { /* BUG: NOT WORKING */
      sidtype = 0; /* Unknown */
    } else {
      sidtype = 3; /* 6581 */
    }
  }
end:;
  usCFG("  $%02x detect_sid_version return %02x\n", start_addr, sidtype);
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
  usCFG("Detect SID version SKpico\n");
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
    usCFG("  Restart %d (ST:%u)\n", restart, sidtype);
    goto restart;
  }
end:
  restart = 0;
  usCFG("  $%02x detect_sid_version_skpico_deprecated raw %02x\n", start_addr, sidtype);
  sidtype = (sidtype < 4 ? sidtype : 0);
  usCFG("  $%02x detect_sid_version_skpico_deprecated %02x\n", start_addr, sidtype);
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
  usCFG(" Detect FMOpl @: %02x\n", base_address);
  int restart = 0;
restart:
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x60, 10);
  cycled_write_operation(base_address, 0x04, 10);
  cycled_write_operation((base_address + 0x10), 0x80, 10);
  uint8_t r = cycled_read_operation(base_address, 10);
  usCFG("  Read FMOpl = $c0? %02x\n", r);
  if (restart == 3) goto end;
  if (r != 0xC0) {
    restart++;
    goto restart;
  }
end:
  return (r == 0xC0 ? true : false);
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
  usCFG("  Check for SIDKick-pico @ $%02x\n", base_address);
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
    usCFG("  Found SIDKick-pico @ $%02x version is: %.36s\n", base_address, skpico_version);
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
  usCFG("  Check for FPGASID @ $%02x\n", base_address);
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
  usCFG("  Read Identify 0x%04X ($%02x,$%02x) @ $%02x\n", fpgasid_id, idHi, idLo, base_address);
  if (fpgasid_id == FPGASID_ID) {
    usCFG("  Found FPGASID @ $%02x\n", base_address);
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
  usCFG("  Check for ARMSID @ $%02x\n", base_address);
  cycled_write_operation((ARMSID_A + base_address),ARMSID_D1,6); /* 0x53 'S' */
  sleep_us(10);
  cycled_write_operation((ARMSID_B + base_address),ARMSID_D2,6); /* 0x49 'I' */
  sleep_us(10);
  cycled_write_operation((ARMSID_C + base_address),ARMSID_D3,6); /* 0x44 'D' */
  sleep_us(10);
  sleep_ms(10);

  uint8_t id1 = cycled_read_operation((ARMSID_R1 + base_address), 4); /* expect 'N' */
  sleep_us(10);
  uint8_t id2 = cycled_read_operation((ARMSID_R2 + base_address), 4); /* expect 'O' */
  sleep_us(10);

  /* usCFG("ARMSID Detect: 0b%08b 0b%08b $%02x $%02x\n", id1, id2, id1, id2); */
  if ((id1 == ARMSID_ID1) && (id2 == ARMSID_ID2)) { /* 0x4e 'N' && 0x4f 'O' */

    /* Try detect if ARM2SID */
    cycled_write_operation((ARMSID_C + base_address),ARMSID_D2,6); /* 0x49 'I' */
    sleep_us(10);
    cycled_write_operation((ARMSID_B + base_address),ARMSID_D2,6); /* 0x49 'I' */
    sleep_us(10);
    sleep_ms(10);
    id1 = cycled_read_operation((ARMSID_R1 + base_address), 4); /* expect 'L' or 'R' */
    sleep_us(10);
    cycled_write_operation((ARMSID_A + base_address),0,6); /* 0 */
    sleep_us(10);

    /* usCFG("ARM2SID Detect: 0b%08b 0b%08b $%02x $%02x\n", id1, id2, id1, id2); */
    if ((id1 == ARMSID_ID1) && (id2 == ARMSID_ID2)) {
      usCFG("  Found ARM2SID @ $%02x\n", base_address);
      return 2;
    }
    usCFG("  Found ARMSID @ $%02x\n", base_address);
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
  usCFG("  Check for PDsid @ $%02x\n", base_address);
  cycled_write_operation((PDREG_P + base_address),PDSID_P,6); /* 0x50 */
  cycled_delay_operation(4); /* 4 cycles */
  cycled_write_operation((PDREG_D+ base_address),PDSID_D,6); /* 0x44 */
  cycled_delay_operation(4); /* 4 cycles */
  uint8_t pdsid_id = cycled_read_operation((PDREG_D + base_address),6);
  if (pdsid_id == PDSID_ID) { /* 0x53 'S' */
    usCFG("  Found PDsid @ $%02x\n", base_address);
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
  usCFG("  Check for BackSID @ $%02x\n", base_address);
  cycled_write_operation((BACKSID_REG + base_address),0x00,6); /* 0x50 */
  sleep_ms(33); /* 2 jiffies of 16,8ms each */
  uint8_t backsid_id = cycled_read_operation((BACKSID_RD + base_address),6);
  if (backsid_id == BACKSID_ID) { /* 0x53 'S' */
    usCFG("  Found BackSID @ $%02x\n", base_address);
    return true;
  }
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
{
  usCFG("Detecting ChipType @ $%02x\n", base_address);

  /* Test in order of prevalence/speed */
  if (detect_skpico(base_address)) {
    return CHIP_SKPICO;
  }

  uint8_t armsid = detect_armsid(base_address);
  if (armsid == 1) {
    return CHIP_ARMSID;
  } else if (armsid == 2) {
    return CHIP_ARM2SID;
  }

  if (detect_fpgasid(base_address)) {
    return CHIP_FPGASID;
  }

  if (detect_pdsid(base_address)) {
    return CHIP_PDSID;
  }

  if (detect_backsid(base_address)) {
    return CHIP_BACKSID;
  }

  usCFG("No clone found @ $%02x, assuming real SID\n", base_address);
  return CHIP_REAL;
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
  usCFG("Detecting SIDType @ $%02x (chiptype=%d)\n", base_address, chiptype);

  uint8_t sidtype;
  /* Use waveform method */
  sidtype = detect_sid_model(base_address);
  if (sidtype == SID_UNKNOWN) {
    /* Try again using oscillator method */
    sidtype = detect_sid_version(base_address);
  }
  if (sidtype == SID_UNKNOWN) { /* Fallback for CHIP_ARMSID, CHIP_BACKSID */
    sidtype = detect_sid_reflex(base_address);
  }

  /* If unknown, check for FMOpl */
  if (chiptype == CHIP_SKPICO && sidtype == SID_UNKNOWN && detect_fmopl(base_address)) {
    return SID_FMOPL;
  }

  usCFG("Detected SIDType @ $%02x = %d\n", base_address, sidtype);
  return sidtype;
}

/**
 * @brief Runs a Chip and SID detection routine
 *        for all sockets and possible addresses
 *
 * @return DetectionResult
 */
static DetectionResult detect_all(void)
{
  DetectionResult result = { .success = false, .error = CFG_OK };

  usCFG("Running CHIP & SID detection\n");

  /* Create temporary probe config */
  Config probe = {
    .socketOne = default_socket(1),
    .socketTwo = default_socket(2),
    .mirrored = false,
  };

  /* Apply probe config to hardware */
  RuntimeCFG probe_rt;
  apply_runtime_config(&probe, &probe_rt);

  /* Copy to global temporarily (required for bus operations) */
  uint32_t irq = save_and_disable_interrupts();
  RuntimeCFG saved_cfg;
  memcpy(&saved_cfg, &cfg, sizeof(RuntimeCFG));
  memcpy(&cfg, &probe_rt, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  /* Give clones time to finish whatever it's doing */
  sleep_ms(500);

  /* Detect Socket 1 */
  usCFG("\n");
  usCFG("Start SocketOne detection\n");
  result.socket[0].chiptype = detect_chiptype_at(probe.socketOne.sid1.addr); /* 0x00 */
  result.socket[0].present = (result.socket[0].chiptype != CHIP_UNKNOWN);
  result.socket[0].supports_dual = (
    /* DualSID supporting clones */
    result.socket[0].chiptype == CHIP_SKPICO ||
    result.socket[0].chiptype == CHIP_FPGASID ||
    result.socket[0].chiptype == CHIP_ARM2SID
  );

  /* Detect Socket 2 */
  usCFG("\n");
  usCFG("Start SocketTwo detection\n");
  result.socket[1].chiptype = detect_chiptype_at(probe.socketTwo.sid1.addr); /* 0x20 */
  result.socket[1].present = (result.socket[1].chiptype != CHIP_UNKNOWN);
  result.socket[1].supports_dual = (
    /* DualSID supporting clones */
    result.socket[1].chiptype == CHIP_SKPICO ||
    result.socket[1].chiptype == CHIP_FPGASID ||
    result.socket[1].chiptype == CHIP_ARM2SID
  );

  probe.socketOne.chiptype = result.socket[0].chiptype;
  probe.socketTwo.chiptype = result.socket[1].chiptype;
  /* If dual supported, update probe config and re-apply */
  usCFG("\n");
  if (result.socket[0].supports_dual) {
    usCFG("Applying SocketOne dualsid presets\n");
    probe.socketOne.dualsid = true;
    /* Shift id's and addresses if SocketOne is dual */
    probe.socketOne.sid1.addr = 0x00; /* was 0x00 */
    probe.socketOne.sid1.id   = 0;    /* was 0x00 */
    probe.socketOne.sid2.addr = 0x20; /* was 0xff */
    probe.socketOne.sid2.id   = 1;    /* was 0xff */
  } else {
    usCFG("Applying SocketOne single sid presets\n");
    probe.socketOne.dualsid   = false;
    probe.socketOne.sid1.addr = 0x00; /* was 0x00 */
    probe.socketOne.sid1.id   = 0;    /* was 0x00 */
    probe.socketOne.sid2.addr = 0xff; /* was 0xff */
    probe.socketOne.sid2.id   = 255;  /* was 0xff */
  }
  if (result.socket[1].supports_dual) {
    usCFG("Applying SocketTwo dualsid presets\n");
    probe.socketTwo.dualsid = true;
    /* SocketTwo base address based on SocketOne dual support */
    uint8_t base = result.socket[0].supports_dual ? 0x40 : 0x20;
    /* Shift id's and addresses if SocketTwo is dual */
    probe.socketTwo.sid1.addr = base;              /* 0x20 or 0x40 */
    probe.socketTwo.sid1.id   = (base >> 5);       /* shift right 5 is 1 or 2 (divide by 10, divide by 2) */
    probe.socketTwo.sid2.addr = (base + 0x20);     /* 0x40 or 0x60 */
    probe.socketTwo.sid2.id   = ((base >> 5) + 1); /* same as above but 1 higher (id's are ones based) */
  } else {
    usCFG("Applying SocketTwo single sid presets\n");
    probe.socketTwo.dualsid = false;
    /* SocketTwo base address based on SocketOne dual support */
    uint8_t base = result.socket[0].supports_dual ? 0x40 : 0x20;
    probe.socketTwo.sid1.addr = base;        /* 0x20 or 0x40 */
    probe.socketTwo.sid1.id   = (base >> 5); /* shift right 5 is 1 or 2 (divide by 10, divide by 2) */
    probe.socketTwo.sid2.addr = 0xff;        /* 0x40 or 0x60 */
    probe.socketTwo.sid2.id   = 255;         /* same as above but 1 higher (id's are ones based) */
  }

  usNFO("\n");
  usCFG("Starting SID detection\n");

  /* Re-apply probe config for SID detection */
  apply_runtime_config(&probe, &probe_rt);
  irq = save_and_disable_interrupts();
  memcpy(&cfg, &probe_rt, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  /* Tired yet? */
  sleep_ms(250);

  /* Detect SID types */
  if (result.socket[0].present) {
    result.socket[0].sid1_type = detect_sidtype_at(0x00, result.socket[0].chiptype);
    if (result.socket[0].supports_dual) {
      result.socket[0].sid2_type = detect_sidtype_at(0x20, result.socket[0].chiptype);
    } else {
      result.socket[0].sid2_type = SID_NA;
    }
  } else {
    result.socket[0].sid1_type = SID_NA;
    result.socket[0].sid2_type = SID_NA;
  }

  if (result.socket[1].present) {
    uint8_t s2_addr = result.socket[0].supports_dual ? 0x40 : 0x20;
    result.socket[1].sid1_type = detect_sidtype_at(s2_addr, result.socket[1].chiptype);
    if (result.socket[1].supports_dual) {
      result.socket[1].sid2_type = detect_sidtype_at(s2_addr + 0x20, result.socket[1].chiptype);
    } else {
      result.socket[1].sid2_type = SID_NA;
    }
  } else {
    result.socket[1].sid1_type = SID_NA;
    result.socket[1].sid2_type = SID_NA;
  }

  /* Restore original runtime config */
  irq = save_and_disable_interrupts();
  memcpy(&cfg, &saved_cfg, sizeof(RuntimeCFG));
  restore_interrupts(irq);

  result.success = true;

  usNFO("\n");
  usCFG("[DETECT] Complete: S1(%d,%d,%d) S2(%d,%d,%d)\n",
    result.socket[0].chiptype, result.socket[0].sid1_type, result.socket[0].sid2_type,
    result.socket[1].chiptype, result.socket[1].sid1_type, result.socket[1].sid2_type);

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
  usNFO("\n");
  usCFG("Starting auto detect routine\n");

  /* Run detection */
  DetectionResult det = detect_all();

  /* Apply results */
  ConfigError err = apply_detection_results(&det); // TODO
  if (err != CFG_OK) {
    usERR("Detection results invalid: %s\n", config_error_str(err));
    return err;
  }

  /* Apply config to hardware */
  err = apply_config(at_boot);
  if (err != CFG_OK) {
    usERR("Applying configuration failed: %s\n", config_error_str(err));
    return err;
  }

  /* Reset SID registers after detection */
  reset_sid_registers();

  usCFG("Auto detection completed\n");
  return CFG_OK;
}
