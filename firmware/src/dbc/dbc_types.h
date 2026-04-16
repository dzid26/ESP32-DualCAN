#pragma once

#include <stdint.h>

/*
 * Binary DBC format — compiled from DBC text by the browser or dbc2bin.py.
 * All multi-byte values are little-endian (native on RISC-V ESP32-C6).
 * Structures are designed to be loaded directly from a buffer without
 * deserialization — just cast and go.
 *
 * Layout in the binary blob:
 *   [header] [messages...] [signals...] [string_table]
 */

#define DBC_MAGIC       "DBC\0"
#define DBC_VERSION     1

typedef struct __attribute__((packed)) {
    uint8_t  magic[4];         /* "DBC\0" */
    uint8_t  version;
    uint8_t  bus_id;
    uint16_t msg_count;
    uint16_t sig_count;        /* total signals across all messages */
    uint16_t _reserved;
    uint32_t msgs_offset;      /* byte offset from start to message array */
    uint32_t sigs_offset;      /* byte offset from start to signal array */
    uint32_t strs_offset;      /* byte offset from start to string table */
    uint32_t strs_size;        /* string table size in bytes */
} dbc_header_t;                /* 28 bytes */

typedef struct __attribute__((packed)) {
    uint32_t id;               /* CAN ID (11- or 29-bit) */
    uint16_t name_off;         /* offset into string table */
    uint8_t  dlc;
    uint8_t  sig_count;        /* signals in this message */
    uint16_t sig_start;        /* index of first signal in signal array */
    uint16_t _pad;
} dbc_msg_t;                   /* 12 bytes */

/* Signal flags byte */
#define DBC_SIG_SIGNED      (1 << 0)
#define DBC_SIG_BIG_ENDIAN  (1 << 1)
#define DBC_SIG_MUX_MASK    (3 << 2)
#define DBC_SIG_MUX_NONE    (0 << 2)
#define DBC_SIG_MUX_SELECTOR (1 << 2)  /* this signal IS the multiplexor */
#define DBC_SIG_MUX_MUXED   (2 << 2)   /* this signal is multiplexed */

typedef struct __attribute__((packed)) {
    uint16_t name_off;         /* offset into string table */
    uint8_t  start_bit;
    uint8_t  bit_length;
    uint8_t  flags;            /* DBC_SIG_* */
    uint8_t  mux_value;        /* mux selector value (when MUX_MUXED) */
    uint16_t _pad;
    float    scale;
    float    offset;
} dbc_sig_t;                   /* 16 bytes */
