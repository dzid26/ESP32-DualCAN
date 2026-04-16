#include "can/tesla.h"
#include "can/msg_codec.h"

#include <string.h>

uint8_t tesla_checksum(uint16_t addr, int checksum_byte_idx,
                       const uint8_t *data, int len)
{
    uint32_t sum = (addr & 0xFF) + ((addr >> 8) & 0xFF);
    for (int i = 0; i < len; i++) {
        if (i != checksum_byte_idx) sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

static bool ends_with(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (nlen < slen) return false;
    return strcmp(name + nlen - slen, suffix) == 0;
}

void tesla_finalize_tx(const dbc_t *dbc, int msg_idx,
                       uint8_t *counter, uint8_t *data)
{
    const dbc_msg_t *msg = &dbc->msgs[msg_idx];
    int checksum_sig = -1;

    for (int i = 0; i < msg->sig_count; i++) {
        uint16_t si = msg->sig_start + i;
        const dbc_sig_t *sig = &dbc->sigs[si];
        const char *name = dbc_str(dbc, sig->name_off);

        if (ends_with(name, "_Counter") || ends_with(name, "_counter")) {
            (*counter)++;
            uint64_t mask = (1ULL << sig->bit_length) - 1;
            msg_encode_signal(sig, data, (float)(*counter & mask));
        }
        if (ends_with(name, "_Checksum") || ends_with(name, "_checksum")) {
            checksum_sig = si;
        }
    }

    if (checksum_sig >= 0) {
        const dbc_sig_t *csig = &dbc->sigs[checksum_sig];
        int cb = csig->start_bit / 8;
        uint8_t csum = tesla_checksum((uint16_t)msg->id, cb, data, msg->dlc);
        msg_encode_signal(csig, data, (float)csum);
    }
}
