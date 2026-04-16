#include "dbc/dbc_pack.h"

#include <string.h>

int dbc_load(dbc_t *dbc, const uint8_t *blob, size_t len)
{
    if (!dbc || !blob || len < sizeof(dbc_header_t)) return -1;

    const dbc_header_t *h = (const dbc_header_t *)blob;
    if (memcmp(h->magic, DBC_MAGIC, 4) != 0 || h->version != DBC_VERSION) return -1;

    size_t msgs_end = h->msgs_offset + (size_t)h->msg_count * sizeof(dbc_msg_t);
    size_t sigs_end = h->sigs_offset + (size_t)h->sig_count * sizeof(dbc_sig_t);
    size_t strs_end = h->strs_offset + h->strs_size;
    if (msgs_end > len || sigs_end > len || strs_end > len) return -1;

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
        if (strcmp(dbc_str(dbc, dbc->sigs[i].name_off), name) == 0) return (int)i;
    }
    return -1;
}
