#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dbc/dbc_pack.h"
#include "can/msg_codec.h"
#include "can/rate_limit.h"

#define CAN_MAX_CALLBACKS 32

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

    struct { int sig_idx; can_signal_cb_t cb; void *ctx; } callbacks[CAN_MAX_CALLBACKS];
    int                cb_count;
} can_t;

int  can_init(can_t *c, int bus_id, const uint8_t *dbc_blob, size_t dbc_len,
              tx_finalize_fn_t finalize);
void can_free(can_t *c);
void can_poll(can_t *c, uint32_t now_ms);

int  can_on_change(can_t *c, const char *sig_name, can_signal_cb_t cb, void *ctx);
const signal_state_t *can_signal(const can_t *c, const char *name);

int  can_draft(can_t *c, uint32_t msg_id, uint8_t *out_data, uint8_t *out_dlc);
void can_encode(const can_t *c, int sig_idx, uint8_t *data, float value);
int  can_send(can_t *c, int msg_idx, uint8_t *data, uint8_t dlc);

void can_set_sim_mode(can_t *c, bool enabled);
