#include "protocol/frame_buf.h"

#include <string.h>

void frame_buf_init(frame_buf_t *fb, uint8_t *backing, size_t cap)
{
    fb->buf = backing;
    fb->cap = cap;
    fb->len = 0;
    fb->peek_total = 0;
    fb->peek_type = 0;
}

int frame_buf_append(frame_buf_t *fb, const uint8_t *data, size_t n)
{
    if (fb->len + n > fb->cap) {
        fb->len = 0;
        fb->peek_total = 0;
        return -1;
    }
    memcpy(fb->buf + fb->len, data, n);
    fb->len += n;
    return 0;
}

int frame_buf_next(frame_buf_t *fb, const uint8_t **payload, size_t *payload_len)
{
    fb->peek_total = 0;
    if (fb->len < FRAME_BUF_HDR_LEN) return 0;

    uint32_t header = (uint32_t)fb->buf[0]
                    | ((uint32_t)fb->buf[1] << 8)
                    | ((uint32_t)fb->buf[2] << 16)
                    | ((uint32_t)fb->buf[3] << 24);
    /* Top nibble = frame type, bottom 28 bits = payload length. */
    uint8_t  type = (uint8_t)((header >> 28) & 0xF);
    uint32_t jlen = header & 0x0FFFFFFFu;
    if (jlen > fb->cap - FRAME_BUF_HDR_LEN) {
        fb->len = 0;
        return -1;
    }
    if (fb->len < FRAME_BUF_HDR_LEN + jlen) return 0;

    *payload = fb->buf + FRAME_BUF_HDR_LEN;
    *payload_len = jlen;
    fb->peek_total = FRAME_BUF_HDR_LEN + jlen;
    fb->peek_type = type;
    return 1;
}

void frame_buf_consume(frame_buf_t *fb)
{
    if (fb->peek_total == 0 || fb->peek_total > fb->len) return;
    size_t remaining = fb->len - fb->peek_total;
    if (remaining > 0) {
        memmove(fb->buf, fb->buf + fb->peek_total, remaining);
    }
    fb->len = remaining;
    fb->peek_total = 0;
}
