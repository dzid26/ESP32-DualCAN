#include "dbc/msg_mirror.h"

#include <stdlib.h>
#include <string.h>

int msg_mirror_init(msg_mirror_t *mirror, const dbc_t *dbc)
{
    mirror->count = dbc->hdr->msg_count;
    mirror->dbc = dbc;
    mirror->entries = calloc(mirror->count, sizeof(msg_mirror_entry_t));
    if (!mirror->entries) return -1;
    return 0;
}

void msg_mirror_free(msg_mirror_t *mirror)
{
    free(mirror->entries);
    mirror->entries = NULL;
    mirror->count = 0;
}

static int msg_index(const msg_mirror_t *mirror, uint32_t can_id)
{
    const dbc_msg_t *m = dbc_find_msg(mirror->dbc, can_id);
    if (!m) return -1;
    return (int)(m - mirror->dbc->msgs);
}

void msg_mirror_rx(msg_mirror_t *mirror, uint32_t can_id,
                   const uint8_t *data, uint8_t dlc)
{
    int idx = msg_index(mirror, can_id);
    if (idx < 0) return;
    msg_mirror_entry_t *e = &mirror->entries[idx];
    if (dlc > 8) dlc = 8;
    memcpy(e->data, data, dlc);
    e->dlc = dlc;
    e->received = true;
}

int msg_mirror_draft(const msg_mirror_t *mirror, uint32_t msg_id,
                     uint8_t *out_data, uint8_t *out_dlc)
{
    int idx = msg_index(mirror, msg_id);
    if (idx < 0) return -1;
    const msg_mirror_entry_t *e = &mirror->entries[idx];
    const dbc_msg_t *msg = &mirror->dbc->msgs[idx];
    memcpy(out_data, e->data, 8);
    *out_dlc = e->received ? e->dlc : msg->dlc;
    return idx;
}

uint8_t tesla_checksum(uint16_t addr, int checksum_byte_idx,
                       const uint8_t *data, int len)
{
    uint32_t sum = (addr & 0xFF) + ((addr >> 8) & 0xFF);
    for (int i = 0; i < len; i++) {
        if (i != checksum_byte_idx) {
            sum += data[i];
        }
    }
    return (uint8_t)(sum & 0xFF);
}

/* Check if a signal name ends with a suffix (case-sensitive). */
static bool name_ends_with(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (nlen < slen) return false;
    return strcmp(name + nlen - slen, suffix) == 0;
}

void msg_mirror_prepare_tx(msg_mirror_t *mirror, int msg_idx, uint8_t *data)
{
    const dbc_t *dbc = mirror->dbc;
    const dbc_msg_t *msg = &dbc->msgs[msg_idx];
    msg_mirror_entry_t *e = &mirror->entries[msg_idx];

    int checksum_sig = -1;

    for (int i = 0; i < msg->sig_count; i++) {
        uint16_t si = msg->sig_start + i;
        const dbc_sig_t *sig = &dbc->sigs[si];
        const char *name = dbc_str(dbc, sig->name_off);

        if (name_ends_with(name, "_Counter") || name_ends_with(name, "_counter")) {
            e->counter++;
            uint64_t mask = (1ULL << sig->bit_length) - 1;
            dbc_encode_signal(sig, data, (float)(e->counter & mask));
        }

        if (name_ends_with(name, "_Checksum") || name_ends_with(name, "_checksum")) {
            checksum_sig = si;
        }
    }

    /* Compute checksum last, after counter is updated. */
    if (checksum_sig >= 0) {
        const dbc_sig_t *csig = &dbc->sigs[checksum_sig];
        int checksum_byte = csig->start_bit / 8;
        uint8_t csum = tesla_checksum((uint16_t)msg->id, checksum_byte,
                                       data, msg->dlc);
        dbc_encode_signal(csig, data, (float)csum);
    }
}
