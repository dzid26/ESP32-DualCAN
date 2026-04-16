#include "can/msg_codec.h"

#include <math.h>
#include <string.h>

/* ---- Bit extraction / insertion ---- */

uint64_t msg_extract_raw(const uint8_t *data, uint8_t start_bit,
                         uint8_t bit_length, bool big_endian)
{
    uint64_t result = 0;

    if (!big_endian) {
        for (int i = 0; i < bit_length; i++) {
            int bp = start_bit + i;
            if (data[bp / 8] & (1u << (bp % 8)))
                result |= (1ULL << i);
        }
    } else {
        int bp = start_bit;
        for (int i = bit_length - 1; i >= 0; i--) {
            if (data[bp / 8] & (1u << (bp % 8)))
                result |= (1ULL << i);
            bp = (bp % 8 == 0) ? bp + 15 : bp - 1;
        }
    }
    return result;
}

static void insert_raw(uint8_t *data, uint8_t start_bit,
                       uint8_t bit_length, bool big_endian, uint64_t raw)
{
    if (!big_endian) {
        for (int i = 0; i < bit_length; i++) {
            int bp = start_bit + i;
            if (raw & (1ULL << i))
                data[bp / 8] |= (1u << (bp % 8));
            else
                data[bp / 8] &= ~(1u << (bp % 8));
        }
    } else {
        int bp = start_bit;
        for (int i = bit_length - 1; i >= 0; i--) {
            if (raw & (1ULL << i))
                data[bp / 8] |= (1u << (bp % 8));
            else
                data[bp / 8] &= ~(1u << (bp % 8));
            bp = (bp % 8 == 0) ? bp + 15 : bp - 1;
        }
    }
}

/* ---- Signal-level encode/decode ---- */

float msg_decode_signal(const dbc_sig_t *sig, const uint8_t *data)
{
    bool be = (sig->flags & DBC_SIG_BIG_ENDIAN) != 0;
    uint64_t raw = msg_extract_raw(data, sig->start_bit, sig->bit_length, be);

    double phys;
    if (sig->flags & DBC_SIG_SIGNED) {
        uint64_t mask = 1ULL << (sig->bit_length - 1);
        int64_t signed_raw = (int64_t)((raw ^ mask) - mask);
        phys = (double)signed_raw * (double)sig->scale + (double)sig->offset;
    } else {
        phys = (double)raw * (double)sig->scale + (double)sig->offset;
    }
    return (float)phys;
}

void msg_encode_signal(const dbc_sig_t *sig, uint8_t *data, float value)
{
    double raw_d = ((double)value - (double)sig->offset) / (double)sig->scale;
    bool be = (sig->flags & DBC_SIG_BIG_ENDIAN) != 0;

    if (sig->flags & DBC_SIG_SIGNED) {
        int64_t raw = (int64_t)round(raw_d);
        uint64_t mask = (1ULL << sig->bit_length) - 1;
        insert_raw(data, sig->start_bit, sig->bit_length, be,
                   (uint64_t)raw & mask);
    } else {
        uint64_t raw = (uint64_t)round(raw_d);
        insert_raw(data, sig->start_bit, sig->bit_length, be, raw);
    }
}

/* ---- Frame-level operations ---- */

void msg_decode_frame(const dbc_t *dbc, const dbc_msg_t *msg,
                      const uint8_t *data, signal_state_t *states,
                      uint32_t now_ms)
{
    int mux_val = -1;
    for (int i = 0; i < msg->sig_count; i++) {
        const dbc_sig_t *sig = &dbc->sigs[msg->sig_start + i];
        if ((sig->flags & DBC_SIG_MUX_MASK) == DBC_SIG_MUX_SELECTOR) {
            mux_val = (int)msg_extract_raw(
                data, sig->start_bit, sig->bit_length,
                (sig->flags & DBC_SIG_BIG_ENDIAN) != 0);
            break;
        }
    }

    for (int i = 0; i < msg->sig_count; i++) {
        uint16_t si = msg->sig_start + i;
        const dbc_sig_t *sig = &dbc->sigs[si];

        uint8_t mt = sig->flags & DBC_SIG_MUX_MASK;
        if (mt == DBC_SIG_MUX_MUXED && (mux_val < 0 || sig->mux_value != mux_val))
            continue;

        float val = msg_decode_signal(sig, data);
        signal_state_t *s = &states[si];
        s->prev = s->value;
        s->value = val;
        s->changed = (val != s->prev);
        s->last_rx_ms = now_ms;
    }
}

void msg_finalize_tx(const dbc_t *dbc, int msg_idx, uint8_t *counter,
                     uint8_t *data, tx_finalize_fn_t finalize)
{
    if (finalize) {
        finalize(dbc, msg_idx, counter, data);
    }
}
