#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/twai.h"
#include "can/rate_limit.h"

#define CAN_RX_BUF        16  /* per-bus software ring buffer depth for Berry scripts */
#define CAN_FRAME_CACHE   32  /* number of CAN IDs that can be cached */
#define CAN_CACHE_DEPTH    3  /* ring buffer depth per cache slot */

/* CAN-layer error codes */
#define CAN_ERR_FULL    (-202)
#define CAN_ERR_NO_FRAME (-203)

/* Ring buffer of twai_message_t with configurable depth.
 * frames[] is sized for the largest use (CAN_RX_BUF = 16).
 * depth determines the actual wrap point (CAN_CACHE_DEPTH for cache
 * slots, CAN_RX_BUF for rx_buf).
 * frames[0].identifier == 0 means a cache slot is free.
 * head = next write index, tail = oldest (read) index. */
typedef struct {
    twai_message_t frames[CAN_RX_BUF];
    uint8_t        depth;    /* actual ring depth */
    uint8_t        head;     /* next write index */
    uint8_t        tail;     /* oldest (read) index */
    uint8_t        count;    /* frames stored (0..depth) */
} msg_ring_t;

typedef struct {
    int                bus_id;
    rate_limiter_t     rate_limiter;
    bool               loaded;
    bool               sim_mode;       /* true = TX goes to log, not bus */

    /* Frame cache: per-ID ring buffers. Only caches IDs scripts have read. */
    msg_ring_t         msg_cache[CAN_FRAME_CACHE];

    /* RX ring buffer: flat FIFO of mixed IDs for can_recv_raw() / can_rx_pop(). */
    msg_ring_t         rx_buf;
} can_t;

int  can_init(can_t *c, int bus_id);
void can_free(can_t *c);
int  can_poll(can_t *c, uint32_t now_ms);
bool can_rx_pop(can_t *c, twai_message_t *out);

/* Raw frame observer — called for every frame received on any bus.
 * Used by the trace feature to forward frames to the UI. Single global slot. */
typedef void (*can_raw_observer_t)(int bus_id, const twai_message_t *frame, uint32_t now_ms);
void     can_set_raw_observer(can_raw_observer_t cb);
uint32_t can_last_rx_ms(int bus_id);  /* ms timestamp of last received frame, 0 if none */
uint16_t can_bus_rx_rate(int bus_id); /* RX frames/sec over last ~1 s, 0 when idle */

/* Read the latest received frame data for a CAN ID from the frame cache.
 * Returns message index (>=0) on success, <0 if no frame received for this ID. */
int  can_read(can_t *c, uint32_t msg_id, uint8_t *out_data, uint8_t *out_dlc);

/* Send a raw CAN frame. Rate-limited per ID. */
int  can_send(can_t *c, uint32_t id, const uint8_t *data, uint8_t dlc);

void can_set_sim_mode(can_t *c, bool enabled);
