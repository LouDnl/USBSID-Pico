/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * asid_buffer.c
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Huge thanks to Thomas Jansson for all his ASID improvement work (https://github.com/thomasj)
 *
 * The contents of this file are based upon and heavily inspired by the sourcecode from
 * TherapSID by Twisted Electrons: https://github.com/twistedelectrons/TherapSID
 * TeensyROM by Sensorium Embedded: https://github.com/SensoriumEmbedded/TeensyROM
 * SID Factory II by Chordian: https://github.com/Chordian/sidfactory2
 *
 * Any licensing conditions from either of the above named sources automatically
 * apply to this code
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

#include <stdlib.h>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/malloc.h>

#include "globals.h"
#include "config.h"
#include "pio.h"
#include "gpio.h"
#include "logging.h"
#include "asid.h"
#include "sid.h"


/* config.c */
extern Config usbsid_config;

/* bus.c */
extern uint32_t clockcycles(void);

/* PIO */
const PIO raster_pio = pio1;
static uint sm_buffer, offset_buffer;
bool buffer_sm_started = false;
bool buffer_sm_claimed = false;

/* Arrival rate tracking */
#define ARRIVAL_HISTORY_SIZE 8
#define MULTISID_TIMEOUT_FRAMES 30  /* Reset to 1-SID after this many frames without SID2+ */
#define NOWRITES_TIMEOUT_FRAMES 100  /* Disable everything after this amount of frames */

static bool still_receiving = false;
static uint32_t arrival_times[ARRIVAL_HISTORY_SIZE];
volatile uint8_t arrival_index = 0;
volatile uint8_t arrival_count = 0;
volatile uint32_t calculated_rate = 0;
static uint16_t base_rate = 0;  /* The rate from env message */
static uint8_t sid_count_estimate = 1;  /* Estimated number of SIDs in tune */
volatile /* static */ uint8_t frames_since_nowrites = 0;  /* Frames since last SID2/3/4 message */
static uint8_t frames_since_multisid = 0;  /* Frames since last SID2/3/4 message */

/* IRQ */
static int pio_irq;
static int8_t buffer_irq;
const int BUFFPIOIRQ = 2;
bool buffer_irq_started = false;
volatile uint32_t irq_now_at = 0;
volatile uint32_t irq_end_at = 0;
volatile uint32_t irq_prev_at = 0;

/* Ring buffer */
static const uint8_t ASID_FRAME_WRITES_MAX = 28;
static uint8_t ring_get(void);
static int ring_diff(void);
volatile uint16_t corrected_rate = 0;
uint8_t diff_size = (2 * (4 * ASID_FRAME_WRITES_MAX)); /* = 224 bytes | 112 bytes == 1 frame, was 64 bytes */
typedef struct {
  uint16_t ring_read;
  uint16_t ring_write;
  bool is_allocated;
  uint8_t * __restrict__ ringbuffer;
} ring_buffer_t;
static ring_buffer_t __not_in_flash("asid_buffer") asid_ringbuffer;

/* Dynamic ring buffer sizing */
static const uint16_t RING_SIZE_MIN = (10 * 224);     /* 2240 bytes - minimum size */
static const uint16_t RING_SIZE_DEFAULT = (20 * 224); /* 4480 bytes - default/starting size */
static const uint16_t RING_SIZE_MAX = (150 * 224);    /* 33600 bytes - maximum size (~150 frames, ~33KB) */
static const uint16_t RING_SIZE_STEP = (20 * 224);    /* 4480 bytes - grow/shrink increment */
volatile uint16_t ring_size = RING_SIZE_DEFAULT;      /* Current effective size */
static uint16_t ring_size_allocated = 0;              /* Actual allocated size */
/* TODO: REMOVE ~ TEMPORARY */
volatile bool in_asid_irq = false;


/**
 * Here be magicians
 */

/**
 * @brief Set the baseline rate from env message
 *
 * @param uint16_t rate ~ The framedelta_us from ASID env message
 */
void set_base_rate(uint16_t rate)
{
  base_rate = rate;
  /* Reset tracking when base rate changes to avoid stale data */
  arrival_index = 0;
  arrival_count = 0;
  calculated_rate = 0;
  for (int i = 0; i < ARRIVAL_HISTORY_SIZE; i++) {
    arrival_times[i] = 0;
  }
  return;
}

/**
 * @brief Clamp value to asymmetric bounds around base_rate
 *
 * @param uint32_t rate ~ The rate to clamp
 *
 * @return uint32_t ~ Clamped rate within bounds
 *
 * Multi-SID tunes need faster consumption:
 * - 2-SID: rate = base_rate / 2
 * - 3-SID: rate = base_rate / 3
 * - 4-SID: rate = base_rate / 4
 * - Fast CIA-timed 2-SID: can need up to base_rate / 6
 * Not working, the buffer just overflows:
 * - 7x speed SID tunes: can need up to base_rate / 7 (Callapse_to_Center.sid Mibri)
 *
 * Allow rate to go as low as base_rate / 6 (~17%) for fast
 * CIA-timed multi-SID tunes.
 */
static uint32_t clamp_to_base_rate(uint32_t rate)
{
  if (base_rate == 0) {
    return rate;  /* No base rate set, can't clamp */
  }

  /* Asymmetric bounds:
   * - Minimum: base_rate / 6 (for fast CIA-timed multi-SID)
   * - Maximum: base_rate * 1.2 (20% slower is plenty) */
  uint32_t min_rate = base_rate / 6;
  if (min_rate < 1000) min_rate = 1000;  /* Absolute minimum */

  uint32_t max_rate = base_rate + (base_rate / 5);  /* +20% */

  if (rate < min_rate) return min_rate;
  if (rate > max_rate) return max_rate;
  return rate;
}

/**
 * @brief Track packet arrival and calculate dynamic rate
 * @note Call this from decode_asid_message for SID1 only
 * @note Auto-detects base_rate if not set by env message
 * @note Auto-resets on long gaps (new tune detection)
 *
 * @return uint32_t ~ calculated frame rate in clock cycles (0 if not enough data)
 */
uint32_t track_asid_arrival(void)
{
  still_receiving = true;

  uint32_t now = clockcycles();

  /* Detect long gap (new tune started without stop/start commands)
   * If gap > 500ms (~500000 based on observed timing values), treat as new tune
   * Typical frame delta is 16000-20000, so 500000 = ~25-30 frames missed */
  if (arrival_count > 0) {
    uint8_t last_idx = (arrival_index + ARRIVAL_HISTORY_SIZE - 1) % ARRIVAL_HISTORY_SIZE;
    uint32_t gap = now - arrival_times[last_idx];
    if (gap > 500000) {
      /* Long gap detected - reset tracking for new tune */
      usASID("Long gap detected (%u) - new tune assumed\n", gap);
      arrival_index = 0;
      arrival_count = 0;
      sid_count_estimate = 1;
      frames_since_multisid = 0;
      base_rate = 0;  /* Reset base rate for new auto-detection */
      ring_size = RING_SIZE_DEFAULT;  /* Reset buffer size for new tune */
      for (int i = 0; i < ARRIVAL_HISTORY_SIZE; i++) {
        arrival_times[i] = 0;
      }
    }
  }

  arrival_times[arrival_index] = now;
  arrival_index = (arrival_index + 1) % ARRIVAL_HISTORY_SIZE;

  if (arrival_count < ARRIVAL_HISTORY_SIZE) {
    arrival_count++;
    return 0;  /* Not enough samples yet */
  }

  /* Calculate average delta from oldest to newest */
  uint8_t oldest = arrival_index;  /* Next write position = oldest */
  uint8_t newest = (arrival_index + ARRIVAL_HISTORY_SIZE - 1) % ARRIVAL_HISTORY_SIZE;

  /* Sanity check: oldest must be non-zero (not from cleared array) */
  if (arrival_times[oldest] == 0) {
    return 0;
  }

  uint32_t total_delta = arrival_times[newest] - arrival_times[oldest];
  uint32_t avg_delta = total_delta / (ARRIVAL_HISTORY_SIZE - 1);

  /* Absolute sanity bounds (reasonable frame rates: 10Hz to 500Hz)
   * Based on log values, typical avg_delta is 5000-25000
   * PAL ~20000, NTSC ~17000, CIA-timed tunes can go much faster */
  if (avg_delta < 1000 || avg_delta > 50000) {
    return 0;  /* Outside reasonable range */
  }

  /* If base_rate not set (player skipped env message), auto-detect it ONCE.
   * Don't update base_rate mid-tune - CIA-timed tunes have variable rates
   * and we should let the dynamic adjustment handle that, not keep changing base_rate. */
  if (base_rate == 0) {
    /* Use measured rate as base_rate - only set once */
    base_rate = (uint16_t)avg_delta;
    usASID("Auto-detected base_rate: %u\n", base_rate);
  }

  /* Don't validate against base_rate for CIA-timed tunes.
   * Just use the measured rate directly - the clamping will
   * keep it within reasonable bounds. */
  calculated_rate = avg_delta;

  return calculated_rate;
}

/**
 * @brief Grow the ring buffer when approaching overflow
 *
 * @return boolean true ~ if buffer was grown
 * @return boolean false ~ if already at max
 *
 * Handles wrapped ring buffer by relocating data:
 * - If write >= read: data is contiguous, just extend size
 * - If write < read: data wrapped, need to move the tail portion
 */
static bool ring_buffer_grow(void)
{
  if (ring_size >= RING_SIZE_MAX) {
    return false;  /* Already at maximum */
  }

  uint16_t new_size = ring_size + RING_SIZE_STEP;
  if (new_size > RING_SIZE_MAX) {
    new_size = RING_SIZE_MAX;
  }

  /* Handle wrapped data: relocate the portion at the start of buffer */
  if (asid_ringbuffer.ring_write < asid_ringbuffer.ring_read) {
    /* Data is in two segments: [read..ring_size) and [0..write)
     * Move [0..write) to [ring_size..ring_size+write) to make it contiguous */
    if (asid_ringbuffer.ring_write > 0) {
      memmove(&asid_ringbuffer.ringbuffer[ring_size],
              &asid_ringbuffer.ringbuffer[0],
              asid_ringbuffer.ring_write);
      asid_ringbuffer.ring_write = ring_size + asid_ringbuffer.ring_write;
    }
  }

  ring_size = new_size;
  usASID("Buffer grown to %u bytes (diff=%d)\n", ring_size, ring_diff());
  return true;
}

/**
 * @brief Reset ring buffer size to default
 * @note Call when starting a new tune to reclaim memory
 */
void ring_buffer_reset_size(void)
{
  ring_size = RING_SIZE_DEFAULT;
  usASID("Buffer size reset to %u bytes\n", ring_size);
  return;
}

/**
 * @brief Update SID count when we see a multi-SID message
 * @note Call from decode_asid_message when processing SID2/3/4 messages
  *
 * @param uint8_t sid_num ~ The SID number (1-4) from the message type
 */
void update_sid_count(uint8_t sid_num)
{
  still_receiving = true;

  /* Reset timeout - we just saw a multi-SID message */
  frames_since_multisid = 0;

  if (sid_num > sid_count_estimate) {
    sid_count_estimate = sid_num;
    usASID("SID count updated to %u\n", sid_count_estimate);
  }
  return;
}

/**
 * @brief Dynamically adjust buffer rate based on calculated rate and buffer state
 * @param uint32_t target_rate ~ The calculated frame rate from track_asid_arrival (0 if not available)
 *
 * Strategy:
 * 1. Use calculated rate (CR) as the primary guide for rate
 * 2. Use actual SID count from received messages (set via update_sid_count)
 * 3. Apply buffer-based corrections only when buffer is critically full/empty
 * 4. Grow buffer if approaching overflow
 */
void adjust_buffer_rate_dynamic(uint32_t target_rate)
{
  still_receiving = true;

  /* Get current buffer fullness */
  int diff = ring_diff();

  /* Track frames since last multi-SID message (only on SID1 frames) */
  if (target_rate > 0 && sid_count_estimate > 1) {
    frames_since_multisid++;
    if (frames_since_multisid > MULTISID_TIMEOUT_FRAMES) {
      /* Haven't seen SID2/3/4 for a while - assume single-SID now */
      usASID("No multi-SID messages for %u frames, resetting to 1-SID\n",
          frames_since_multisid);
      sid_count_estimate = 1;
      frames_since_multisid = 0;
    }
  }

  /* Calculate target rate based on CR and actual SID count */
  int32_t new_rate;
  if (target_rate > 0) {
    /* Use calculated rate divided by SID count */
    new_rate = (int32_t)(target_rate / sid_count_estimate);
  } else {
    /* No valid CR yet, use current rate with buffer adjustment */
    new_rate = (int32_t)corrected_rate;
  }

  /* Check if buffer is approaching overflow - grow if needed */
  int headroom = (int)ring_size - diff;
  if (headroom < (diff_size * 4)) {
    /* Less than 4 frames of headroom - try to grow */
    if (!ring_buffer_grow()) {
      /* Can't grow - emergency rate reduction needed */
      if (headroom < diff_size) {
        /* Critical: less than 1 frame headroom, aggressive reduction */
        new_rate = new_rate * 3 / 4;  /* 25% reduction */
        usWRN("Emergency rate reduction: %d\n", new_rate);
      }
    }
  }

  /* Apply buffer-based fine-tuning based on buffer fullness percentage */
  int fill_percent = (diff * 100) / (int)ring_size;
  if (fill_percent > 95) {
    new_rate -= 500;  /* Near overflow - aggressive */
  } else if (fill_percent > 90) {
    new_rate -= 300;
  } else if (fill_percent > 80) {
    new_rate -= 150;
  } else if (fill_percent > 60) {
    new_rate -= 50;
  } else if (diff < (diff_size / 2)) {
    new_rate += 100;  /* Buffer nearly empty */
  } else if (diff < diff_size) {
    new_rate += 50;
  }
  /* else: buffer in acceptable range, use rate as-is */

  /* Clamp to bounds */
  new_rate = (int32_t)clamp_to_base_rate((uint32_t)(new_rate > 0 ? new_rate : 1));

  /* Apply if significantly different (small hysteresis) */
  int32_t rate_diff = (int32_t)corrected_rate - new_rate;
  if (rate_diff < 0) rate_diff = -rate_diff;

  if (rate_diff > 50) {
    pio_sm_put(raster_pio, sm_buffer, (uint32_t)new_rate);
    corrected_rate = (uint16_t)new_rate;
  }
  return;
}

/**
 * @brief Reset arrival tracking state
 * @note Clears timestamps and counters but preserves base_rate
 * @note base_rate is only set via set_buffer_rate()
 */
void reset_arrival_tracking(void)
{
  arrival_index = 0;
  arrival_count = 0;
  calculated_rate = 0;
  sid_count_estimate = 1;  /* Reset SID count estimate */
  frames_since_multisid = 0;  /* Reset multi-SID timeout */
  /* Clear stale timestamps to prevent bogus deltas */
  for (int i = 0; i < ARRIVAL_HISTORY_SIZE; i++) {
    arrival_times[i] = 0;
  }
  /* Note: don't reset base_rate here, it's set from set_buffer_rate() */
  return;
}


/**
 * Here be IRQ and PIO magic mushrooms
 */

/**
 * @brief buffer interrupt handler
 * @note writes data from the ringbuffer to the SIDs
 */
void __not_in_flash_func(buffer_irq_handler)(void)
{
  in_asid_irq = true;
  irq_prev_at = irq_now_at;
  irq_now_at = clockcycles();

  /* inline extern as only used here */
  extern void cycled_write_operation(uint8_t address, uint8_t data, uint16_t cycles);

  /* Retrieve the current diff */
  int current_diff = ring_diff();
  /* Only start if head and tail are not the same */
  if(asid_ringbuffer.ring_read != asid_ringbuffer.ring_write) {
    // if (current_diff > (diff_size + (28 * 4))) {  // Ensure we keep diff_size bytes after reading
    /* Only consume if we have enough data AND will stay above minimum */
    if (current_diff > diff_size) {  // Ensure we keep diff_size bytes after reading
      for (size_t pos = 0; pos < 28; pos++) {
        uint8_t reg = ring_get();
        uint8_t val = ring_get();
        uint8_t c_hi = ring_get();
        uint8_t c_lo = ring_get();
        if (reg != 0xffu) {
          cycled_write_operation(reg,val,(c_hi<<8|c_lo));
        }
      }
    }
  }

  if (!still_receiving) {
    frames_since_nowrites++;
    if (frames_since_nowrites > NOWRITES_TIMEOUT_FRAMES) {
      extern void deinit_asid_buffer(void);
      usASID("More then 100 frames since last write, deactivating\n");
      frames_since_nowrites = frames_since_multisid = 0;
      base_rate = corrected_rate = calculated_rate = 0;
      arrival_index = arrival_count = 0;
      irq_prev_at = irq_now_at = irq_end_at = 0;
      pio_interrupt_clear(raster_pio, BUFFPIOIRQ);
      deinit_asid_buffer();
      return;
    }
  }

  irq_end_at = clockcycles();
  if ((irq_end_at-irq_now_at) <= 1) {
    still_receiving = false;
  } else {
    frames_since_nowrites = 0;
    still_receiving = true;
  }
  in_asid_irq = false;

  /* Interrupt cleared at end of routine
   * if play becomes irregular, irq's might be
   * misfiring and moving the clearing to the
   * beginning of the routine might help
   */
  pio_interrupt_clear(raster_pio, BUFFPIOIRQ);
  return;
}

/**
 * @brief initialize the buffer irq
 * @note sets the interrupt number and handler
 * @note sets the irq source and enables the irq
 */
void init_buffer_irq(void)
{
  if (!buffer_irq_started) {
    usASID("Enabling buffer IRQ\n");
    buffer_irq = pis_interrupt2; /* PIO_INTR_SM2_LSB hardware/pio.h */
    pio_irq = pio_get_irq_num(raster_pio, 0);
    irq_set_exclusive_handler(pio_irq, buffer_irq_handler);
    pio_set_irq0_source_enabled(raster_pio, buffer_irq + sm_buffer, true);
    irq_set_enabled(pio_irq, true);
    buffer_irq_started = true;
  }
  return;
}

/**
 * @brief initializes the raster_pio
 * @note also claims the pio statemachine
 */
void init_buffer_pio(void)
{ /* buffer cycle counter */
  sm_buffer = 0;  /* PIO1 SM0 */
  if (!buffer_sm_claimed) {
    usASID("Starting & claiming buffer pio\n");
    pio_sm_claim(raster_pio, sm_buffer);
    offset_buffer = pio_add_program(raster_pio, &raster_buffer_program);
    pio_sm_config c_buffer = raster_buffer_program_get_default_config(offset_buffer);
    sm_config_set_fifo_join(&c_buffer, PIO_FIFO_JOIN_TX);
    pio_sm_init(raster_pio, sm_buffer, offset_buffer, &c_buffer);
    buffer_sm_claimed = true;
  }
  return;
}

/**
 * @brief enable the raster_pio statemachine
 */
void start_buffer_pio(void)
{
  if (!buffer_sm_started) {
    usASID("Enabling buffer statemachine\n");
    pio_sm_set_enabled(raster_pio, sm_buffer, true);
    buffer_sm_started = true;
  }
  return;
}

/**
 * @brief stop the asid buffer handling
 * @note frees the irq and stops the irq_handler
 * @note disables the raster_pio statemachine
 * @note unclaims the statemadhine on the pio
 */
void stop_buffer_pio(void)
{
  if (buffer_irq_started) {
    irq_set_enabled(pio_irq, false);
    pio_set_irq0_source_enabled(raster_pio, buffer_irq + sm_buffer, false);
    irq_remove_handler(pio_irq, buffer_irq_handler);
    buffer_irq_started = false;
    usASID("Buffer IRQ released\n");
  }
  if (buffer_sm_started) {
    pio_sm_set_enabled(raster_pio, sm_buffer, false);
    buffer_sm_started = false;
    usASID("Buffer statemachine disabled\n");
  }
  if (buffer_sm_claimed) {
    pio_remove_program(raster_pio, &raster_buffer_program, offset_buffer);
    pio_sm_unclaim(raster_pio, sm_buffer);
    buffer_sm_claimed = false;
    usASID("Buffer statemachine released\n");
  }
  return;
}

/**
 * @brief sets the asid buffer raster rate
 * @note the raster_rate pio has a 1 cycle overhead
 * @note this requires the rate to be corrected with
 * @note subtraction of a single cycle.
 * @note the function does this if the provided
 * @note rate is higher than zero.
 *
 * @param uint16_t rate ~ the buffer irq rate to set
 */
void set_buffer_rate(uint16_t rate)
{
  corrected_rate = ((rate > 0) ? (rate - 1) : (usbsid_config.refresh_rate - 1));

  /* Store as base rate for dynamic adjustment bounds */
  set_base_rate(corrected_rate);

  usASID("Rate received %u Rate set %u Base rate %u\n", rate, corrected_rate, base_rate);
  if (!buffer_irq_started) init_buffer_irq();
  pio_sm_put(raster_pio, sm_buffer, corrected_rate);
  start_buffer_pio();
  return;
}


/**
 * Here be ringbuffer shizzle
 */

/**
 * @brief reset the head and tail to zero
 */
static void ring_buffer_reset(void)
{
  asid_ringbuffer.ring_read = asid_ringbuffer.ring_write = 0;
  return;
}

/**
 * @brief returns the difference between head and tail
 *
 * @return int ~ the head -> tail difference
 */
static int __not_in_flash_func(ring_diff)(void)
{
  return (asid_ringbuffer.ring_write - asid_ringbuffer.ring_read + ring_size) % ring_size;
}

/**
 * @brief Check if buffer would overflow with next write
 *
 * @return boolean ~ true if there's no room for another frame (112 bytes)
 * @return boolean ~ false if there is
 */
static bool ring_would_overflow(void)
{
  int headroom = (int)ring_size - ring_diff();
  return headroom < 112;  /* Need room for at least 1 frame (28 writes * 4 bytes) */
}

/**
 * @brief put an item into the ringbuffer
 * @note Checks for overflow and drops data if buffer is full
 *
 * @param uint8_t item ~ item to store
 */
static void ring_put(uint8_t item)
{
  /* Check for overflow - if we're about to overwrite unread data, skip this write */
  uint16_t next_write = (asid_ringbuffer.ring_write + 1) % ring_size;
  if (next_write == asid_ringbuffer.ring_read) {
    /* Buffer full - would overflow. Drop this byte to prevent corruption. */
    usERR("Buffer overflow - dropping data\n");
    return;
  }

  asid_ringbuffer.ringbuffer[asid_ringbuffer.ring_write] = item;
  asid_ringbuffer.ring_write = next_write;
  return;
}

/**
 * @brief retrieves an item from the ring buffer
 *
 * @return uint8_t ~ the retrieved ring item
 */
static uint8_t __not_in_flash_func(ring_get)(void)
{
  uint8_t item = asid_ringbuffer.ringbuffer[asid_ringbuffer.ring_read];
  asid_ringbuffer.ring_read = (asid_ringbuffer.ring_read + 1) % ring_size;
  return item;
}

/**
 * @brief intialize the ring buffer
 * @note Allocates maximum size upfront to allow dynamic growth
 */
void asid_ring_init(void)
{
  if (!asid_ringbuffer.is_allocated) {
    if (asid_ringbuffer.ringbuffer != NULL) { free(asid_ringbuffer.ringbuffer); }
    /* Allocate max size upfront - allows growth without reallocation */
    asid_ringbuffer.ringbuffer = (uint8_t*)calloc(RING_SIZE_MAX, 1);
    ring_size_allocated = RING_SIZE_MAX;
    ring_size = RING_SIZE_DEFAULT;  /* Start with default logical size */
    asid_ringbuffer.is_allocated = true;
    ring_buffer_reset();
    usASID("Ringbuffer initialized (allocated=%u, effective=%u)\n",
      ring_size_allocated, ring_size);
  }
  return;
}

/**
 * @brief de-intialize the ring buffer
 */
void asid_ring_deinit(void)
{
  if (asid_ringbuffer.is_allocated) {
    ring_buffer_reset();
    free(asid_ringbuffer.ringbuffer);
    asid_ringbuffer.ringbuffer = NULL;
    asid_ringbuffer.is_allocated = false;
  }
  return;
}

/**
 * @brief write data to the ringbuffer
 * @note writes 4 items per call
 *
 * @param uint8_t reg ~ SID register
 * @param uint8_t val ~ value
 * @param uint16_t c  ~ delay cycles
 */
void asid_ring_write(uint8_t reg, uint8_t val, uint16_t c)
{
  ring_put(reg);
  ring_put(val);
  ring_put((uint8_t)((c&0xff00u)>>8));
  ring_put((uint8_t)(c&0xffu));
  return;
}
