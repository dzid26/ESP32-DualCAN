#pragma once

/*
 * Length-prefixed frame buffer used by the BLE/WS transports. Each frame
 * is a 4-byte little-endian length header followed by that many bytes of
 * payload. The transport may deliver bytes in arbitrary chunks; this
 * helper accumulates them and yields one complete payload at a time.
 *
 * Usage:
 *   static uint8_t backing[16 * 1024];
 *   static frame_buf_t fb;
 *   frame_buf_init(&fb, backing, sizeof(backing));
 *
 *   on_rx(data, n):
 *     frame_buf_append(&fb, data, n);
 *     for (;;) {
 *       const uint8_t *payload; size_t plen;
 *       int rc = frame_buf_next(&fb, &payload, &plen);
 *       if (rc <= 0) break;
 *       handle(payload, plen);
 *       frame_buf_consume(&fb);
 *     }
 */

#include <stddef.h>
#include <stdint.h>

#define FRAME_BUF_HDR_LEN 4

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;        /* bytes currently buffered */
    size_t   peek_total; /* size (header + payload) of the frame returned by
                          * the last frame_buf_next() call; 0 if none */
} frame_buf_t;

void frame_buf_init(frame_buf_t *fb, uint8_t *backing, size_t cap);

/* Append data. Returns 0 on success, -1 if the append would overflow the
 * backing buffer (in which case state is reset to empty). */
int frame_buf_append(frame_buf_t *fb, const uint8_t *data, size_t n);

/* If a complete frame is at the head of the buffer, set the out parameters
 * and return 1. Returns 0 if the buffer doesn't yet hold a full frame.
 * Returns -1 if the length prefix exceeds capacity (state is reset). */
int frame_buf_next(frame_buf_t *fb, const uint8_t **payload, size_t *payload_len);

/* Discard the frame previously returned by frame_buf_next(). */
void frame_buf_consume(frame_buf_t *fb);
