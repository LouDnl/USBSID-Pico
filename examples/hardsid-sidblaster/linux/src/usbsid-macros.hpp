#pragma once
#ifndef USBSID_MACROS_H_
#define USBSID_MACROS_H_

/* Credits: https://stackoverflow.com/a/25108449 */
/* --- PRINTF_BYTE_TO_BINARY macro's --- */
#define PRINTF_BINARY_PATTERN_INT8 "%c%c%c%c%c%c%c%c"
#define PRINTF_BYTE_TO_BINARY_INT8(i) \
    (((i) & 0x80ll) ? '1' : '0'),     \
        (((i) & 0x40ll) ? '1' : '0'), \
        (((i) & 0x20ll) ? '1' : '0'), \
        (((i) & 0x10ll) ? '1' : '0'), \
        (((i) & 0x08ll) ? '1' : '0'), \
        (((i) & 0x04ll) ? '1' : '0'), \
        (((i) & 0x02ll) ? '1' : '0'), \
        (((i) & 0x01ll) ? '1' : '0')

#define PRINTF_BINARY_PATTERN_INT16 \
    PRINTF_BINARY_PATTERN_INT8 PRINTF_BINARY_PATTERN_INT8
#define PRINTF_BYTE_TO_BINARY_INT16(i) \
    PRINTF_BYTE_TO_BINARY_INT8((i) >> 8), PRINTF_BYTE_TO_BINARY_INT8(i)
#define PRINTF_BINARY_PATTERN_INT32 \
    PRINTF_BINARY_PATTERN_INT16 PRINTF_BINARY_PATTERN_INT16
#define PRINTF_BYTE_TO_BINARY_INT32(i) \
    PRINTF_BYTE_TO_BINARY_INT16((i) >> 16), PRINTF_BYTE_TO_BINARY_INT16(i)
#define PRINTF_BINARY_PATTERN_INT64 \
    PRINTF_BINARY_PATTERN_INT32 PRINTF_BINARY_PATTERN_INT32
#define PRINTF_BYTE_TO_BINARY_INT64(i) \
    PRINTF_BYTE_TO_BINARY_INT32((i) >> 32), PRINTF_BYTE_TO_BINARY_INT32(i)
/* --- end macros --- */

/* #define DEBUG_HARDSID */
#ifdef DEBUG_HARDSID
#define HDBG( ... ) printf( __VA_ARGS__ )
#else
#define HDBG( ... )
#endif

/* #define DEBUG_USBSID */
#ifdef DEBUG_USBSID
#define UDBG( ... ) printf( __VA_ARGS__ )
#else
#define UDBG( ... )
#endif

/* #define DEBUG_USBSIDMEM */
#ifdef DEBUG_USBSIDMEM
#define MDBG(...) printf(__VA_ARGS__)
uint8_t memory[65536];
#else
#define MDBG(...)
#endif

/* #define USBSID_DEBUG */
#ifdef USBSID_DEBUG
#define USBSIDDBG(...) printf(__VA_ARGS__)
#else
#define USBSIDDBG(...) ((void)0)
#endif

#endif /* USBSID_MACROS_H_ */
