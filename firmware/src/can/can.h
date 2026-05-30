#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/twai.h"
#include "dbc/dbc_pack.h"
#include "can/msg_codec.h"
#include "can/rate_limit.h"

#define CAN_MAX_CALLBACKS 32
#define CAN_RX_BUF        16  /* per-bus software ring buffer depth for Berry scripts */

/* CAN-layer error codes for int-returning functions.
 * Callers check < 0 for generic failure, or compare against
 * a specific code for a precise error message. */
#define CAN_DBC_ERR     (-201)   /* DBC lookup failed (msg or sig not found) */
#define CAN_ERR_FULL    (-202)   /* callback registry / resource full */

typedef void (*can_signal_cb_t)(int sig_idx, float value, float prev, void *ctx);

typedef struct {
    uint8_t  data[8];
    uint8_t  dlc;
    uint8_t  counter;
    bool     received;
} can_msg_state_t;

typedef struct {
    int                bus_id;
    dbc_t              dbc;
    signal_state_t    *signals;      /* array[sig_count] */
    can_msg_state_t   *messages;     /* array[msg_count] — latest rx + counter */
    tx_finalize_fn_t   finalize_fn;
    rate_limiter_t     rate_limiter;
    bool               loaded;
    bool               sim_mode;       /* true = TX goes to log, not bus */

    struct { int sig_idx; can_signal_cb_t cb; void *ctx; int tag; } callbacks[CAN_MAX_CALLBACKS];
    int                cb_count;

    /* Software RX ring buffer: can_poll fills this so Berry can_recv_raw()
     * sees frames that arrived before the timer callback fires. */
    twai_message_t     rx_buf[CAN_RX_BUF];
    uint8_t            rx_head;
    uint8_t            rx_count;
} can_t;

int  can_init(can_t *c, int bus_id, const uint8_t *dbc_blob, size_t dbc_len,
              tx_finalize_fn_t finalize);
void can_free(can_t *c);
int  can_poll(can_t *c, uint32_t now_ms);
bool can_rx_pop(can_t *c, twai_message_t *out);

/* Raw frame observer — called for every frame received on any bus.
 * Used by the trace feature to forward frames to the UI. Single global slot. */
typedef void (*can_raw_observer_t)(int bus_id, const twai_message_t *frame, uint32_t now_ms);
void     can_set_raw_observer(can_raw_observer_t cb);
uint32_t can_last_rx_ms(int bus_id);  /* ms timestamp of last received frame, 0 if none */
uint16_t can_bus_rx_rate(int bus_id); /* RX frames/sec over last ~1 s, 0 when idle */

int  can_on_change(can_t *c, const char *sig_name, can_signal_cb_t cb, void *ctx, int tag);
/* Scoped variant: only matches a signal living inside `msg_name`. Use this from
 * Berry, where the script knows the message context — DBC signal names are not
 * unique across messages, and the bare can_on_change picks the first match. */
int  can_on_change_scoped(can_t *c, const char *msg_name, const char *sig_name,
                           can_signal_cb_t cb, void *ctx, int tag);
void can_off_by_tag(can_t *c, int tag);   /* remove every callback registered with this tag */
const signal_state_t *can_signal(const can_t *c, const char *name);
/* Scoped read (msg_name, sig_name). Same rationale as can_on_change_scoped. */
const signal_state_t *can_signal_scoped(const can_t *c, const char *msg_name, const char *sig_name);

int  can_read(can_t *c, uint32_t msg_id, uint8_t *out_data, uint8_t *out_dlc);
void can_encode(const can_t *c, int sig_idx, uint8_t *data, float value);
int  can_send(can_t *c, int msg_idx, uint8_t *data, uint8_t dlc);

void can_set_sim_mode(can_t *c, bool enabled);
