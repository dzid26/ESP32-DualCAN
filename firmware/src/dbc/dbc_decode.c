#include "dbc/dbc_decode.h"

#include <string.h>
#include <math.h>

int dbc_load(dbc_t *dbc, const uint8_t *blob, size_t len)
{
    if (!dbc || !blob || len < sizeof(dbc_header_t)) {
        return -1;
    }

    const dbc_header_t *h = (const dbc_header_t *)blob;
    if (memcmp(h->magic, DBC_MAGIC, 4) != 0 || h->version != DBC_VERSION) {
        return -1;
    }

    size_t msgs_end = h->msgs_offset + (size_t)h->msg_count * sizeof(dbc_msg_t);
    size_t sigs_end = h->sigs_offset + (size_t)h->sig_count * sizeof(dbc_sig_t);
    size_t strs_end = h->strs_offset + h->strs_size;
    if (msgs_end > len || sigs_end > len || strs_end > len) {
        return -1;
    }

    dbc->blob     = blob;
    dbc->blob_len = len;
    dbc->hdr      = h;
    dbc->msgs     = (const dbc_msg_t *)(blob + h->msgs_offset);
    dbc->sigs     = (const dbc_sig_t *)(blob + h->sigs_offset);
    dbc->strs     = (const char *)(blob + h->strs_offset);

    return 0;
}

const dbc_msg_t *dbc_find_msg(const dbc_t *dbc, uint32_t id)
{
    int lo = 0, hi = dbc->hdr->msg_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mid_id = dbc->msgs[mid].id;
        if (mid_id == id) return &dbc->msgs[mid];
        if (mid_id < id) lo = mid + 1;
        else             hi = mid - 1;
    }
    return NULL;
}

int dbc_find_signal(const dbc_t *dbc, const char *name)
{
    for (uint16_t i = 0; i < dbc->hdr->sig_count; i++) {
        if (strcmp(dbc_str(dbc, dbc->sigs[i].name_off), name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

uint64_t dbc_extract_raw(const uint8_t *data, uint8_t start_bit,
                         uint8_t bit_length, bool big_endian)
{
    uint64_t result = 0;

    if (!big_endian) {
        /* Little-endian (Intel) — start_bit is the LSB position.
         * Bit numbering: byte0[0..7], byte1[8..15], ... */
        for (int i = 0; i < bit_length; i++) {
            int bit_pos = start_bit + i;
            int byte_idx = bit_pos / 8;
            int bit_in_byte = bit_pos % 8;
            if (data[byte_idx] & (1u << bit_in_byte)) {
                result |= (1ULL << i);
            }
        }
    } else {
        /* Big-endian (Motorola) — start_bit is the MSB position.
         * Motorola bit numbering per the DBC convention. */
        int bit_pos = start_bit;
        for (int i = bit_length - 1; i >= 0; i--) {
            int byte_idx = bit_pos / 8;
            int bit_in_byte = bit_pos % 8;
            if (data[byte_idx] & (1u << bit_in_byte)) {
                result |= (1ULL << i);
            }
            /* Walk to next bit in Motorola order */
            if (bit_in_byte == 0) {
                bit_pos += 15;  /* jump to MSB of next byte */
            } else {
                bit_pos--;
            }
        }
    }
    return result;
}

static void insert_raw(uint8_t *data, uint8_t start_bit,
                        uint8_t bit_length, bool big_endian,
                        uint64_t raw)
{
    if (!big_endian) {
        for (int i = 0; i < bit_length; i++) {
            int bit_pos = start_bit + i;
            int byte_idx = bit_pos / 8;
            int bit_in_byte = bit_pos % 8;
            if (raw & (1ULL << i)) {
                data[byte_idx] |= (1u << bit_in_byte);
            } else {
                data[byte_idx] &= ~(1u << bit_in_byte);
            }
        }
    } else {
        int bit_pos = start_bit;
        for (int i = bit_length - 1; i >= 0; i--) {
            int byte_idx = bit_pos / 8;
            int bit_in_byte = bit_pos % 8;
            if (raw & (1ULL << i)) {
                data[byte_idx] |= (1u << bit_in_byte);
            } else {
                data[byte_idx] &= ~(1u << bit_in_byte);
            }
            if (bit_in_byte == 0) {
                bit_pos += 15;
            } else {
                bit_pos--;
            }
        }
    }
}

float dbc_decode_signal(const dbc_sig_t *sig, const uint8_t *data)
{
    bool be = (sig->flags & DBC_SIG_BIG_ENDIAN) != 0;
    uint64_t raw = dbc_extract_raw(data, sig->start_bit, sig->bit_length, be);

    double phys;
    if (sig->flags & DBC_SIG_SIGNED) {
        /* Sign-extend */
        uint64_t mask = 1ULL << (sig->bit_length - 1);
        int64_t signed_raw = (int64_t)((raw ^ mask) - mask);
        phys = (double)signed_raw * (double)sig->scale + (double)sig->offset;
    } else {
        phys = (double)raw * (double)sig->scale + (double)sig->offset;
    }
    return (float)phys;
}

void dbc_encode_signal(const dbc_sig_t *sig, uint8_t *data, float value)
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
