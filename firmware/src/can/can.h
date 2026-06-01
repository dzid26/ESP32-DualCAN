#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/twai.h"
#include "can/rate_limit.h"

#define CAN_RX_BUF        16  /* per-bus software ring buffer depth for Berry scripts */
#define CAN_FRAME_CACHE   32  /* number of CAN IDs to cache latest frame for */

/* CAN-layer error codes */
#define CAN_ERR_FULL    (-202)
#define CAN_ERR_NO_FRAME (-203)

/* Latest received frame data for a CAN ID */
typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
    bool     received;
} cached_frame_t;

typedef struct {
    int                bus_id;
    rate_limiter_t     rate_limiter;
    bool               loaded;
    bool               sim_mode;       /* true = TX goes to log, not bus */

    /* Frame cache: latest received frame per CAN ID (linear scan). */
    cached_frame_t     frames[CAN_FRAME_CACHE];
    int                frame_count;

    /* Software RX ring buffer: can_poll fills this so Berry can_recv_raw()
     * sees frames that arrived before the timer callback fires. */
    twai_message_t     rx_buf[CAN_RX_BUF];
    uint8_t            rx_head;
    uint8_t            rx_count;
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
int  can_send(can_t *c, uint32_t id, uint8_t *data, uint8_t dlc);

void can_set_sim_mode(can_t *c, bool enabled);
