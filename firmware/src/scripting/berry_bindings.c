#include "scripting/berry_bindings.h"

#include <stdio.h>
#include <string.h>

#include "can/can.h"
#include "can/msg_codec.h"
#include "led/led_rgb.h"
#include "storage/state.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG_BB = "berry";

static can_t *s_bus[2];

void berry_set_buses(can_t *eng0, can_t *eng1)
{
    s_bus[0] = eng0;
    s_bus[1] = eng1;
}

can_t *berry_get_bus(int bus)
{
    if (bus < 0 || bus > 1) return NULL;
    return s_bus[bus];
}

static can_t *get_bus(int bus)
{
    if (bus < 0 || bus > 1 || !s_bus[bus]) return NULL;
    return s_bus[bus];
}

/* ---- CAN: raw frames, message drafts ----
 * Naming convention:
 *   can_*_raw / can_send_raw, can_recv_raw     — raw CAN, no DBC
 *   can_msg_* / can_msg_get, can_msg_new, can_msg_send — named messages
 *
 * All CAN functions take a bus argument (0 or 1). In simulation mode
 * (can_t.sim_mode), can_send_raw logs the frame instead of transmitting. */




/* can_send_raw(bus:int, id:int, data:bytes) -> nil
 * Transmit a raw CAN frame. No DBC encoding, no signal modification, no
 * checksum/counter injection. bus = 0 or 1, id = 11-bit CAN ID,
 * data = bytes object (max 8 bytes). In sim mode, logs to the UI instead. */
static int l_can_send_raw(bvm *vm)
{
    CHECK_ARITY(vm, 3);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isbytes(vm, 3), "bytes");
    int bus = be_toint(vm, 1);
    uint32_t id = (uint32_t)be_toint(vm, 2);
    size_t len = 0;
    const uint8_t *data = be_tobytes(vm, 3, &len);
    if (len > 8) len = 8;
    can_t *c = get_bus(bus);
    if (c && c->sim_mode) {
        char hex[17] = {0};
        for (size_t i = 0; i < len; i++) {
            hex[2*i]     = "0123456789ABCDEF"[(data[i] >> 4) & 0xF];
            hex[2*i + 1] = "0123456789ABCDEF"[data[i]        & 0xF];
        }
        char msg[48];
        snprintf(msg, sizeof(msg), "SIM tx bus%d 0x%03X [%d] %s", bus, (unsigned)id, (int)len, hex);
        berry_log_push(msg);
    } else {
        can_send(c, id, data, (uint8_t)len);
    }
    be_return_nil(vm);
}

/* can_recv_raw(bus:int, msg_id:int [, timeout_ms:int]) -> bytes | nil
 * Returns the last payload for a CAN ID.  Default timeout is 1000 ms —
 * the first read blocks up to 1 s waiting for initial data.  Pass an
 * explicit timeout to override, or 0 for instant return.
 * Stalls the Berry VM while blocking.
 * waits, never inside a timer callback. */
static int l_can_recv_raw(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    int bus = be_toint(vm, 1);
    uint32_t msg_id = (uint32_t)be_toint(vm, 2);
    can_t *c = get_bus(bus);
    if (!c) be_return_nil(vm);

    uint32_t timeout = be_top(vm) >= 3 ? (uint32_t)be_toint(vm, 3) : 1000;
    uint8_t data[8], dlc;
    if (can_read(c, msg_id, data, &dlc, timeout) >= 0) {
        be_pushbytes(vm, data, dlc);
        be_return(vm);
    }
    be_return_nil(vm);
}

/* can_msg_get(bus:int, msg_id:int) -> draft | nil
 * Returns the latest received frame for msg_id as a message draft (map
 * instance with keys: id, data, dlc). Returns nil if no matching frame has
 * been received. The draft can be modified with msg_sig_set and transmitted
 * via can_msg_send. bus is first for consistency with can_send_raw/
 * can_recv_raw. */
static int l_can_msg_get(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int"); //bus
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int"); //msg_id
    int bus = be_toint(vm, 1);
    uint32_t msg_id = (uint32_t)be_toint(vm, 2);
    can_t *eng = get_bus(bus);
    if (!eng) be_return_nil(vm);

    uint8_t data[8], dlc;
    if (can_read(eng, msg_id, data, &dlc, 0) < 0) be_return_nil(vm);

    // https://berry.readthedocs.io/en/latest/source/en/FFI-Example.html#instantiate-a-list-object-in-a-native-function
    be_getglobal(vm, "map");
    be_newmap(vm);
    be_pushstring(vm, "id");      be_pushint(vm, (bint)msg_id);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "data");    be_pushbytes(vm, data, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "dlc");     be_pushint(vm, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_call(vm, 1);
    be_pop(vm, 1); // pop the arguments
    be_return(vm);
}

/* can_msg_new(id:int, dlc:int) -> draft
 * Creates a zeroed message draft (all bytes = 0) for encoding and
 * transmission. The draft is a bus-free map instance with keys: id, data,
 * dlc. Pass the bus at send time via can_msg_send(bus, draft). The
 * preprocessor rewrites can_msg_new("msg_name") to call this with the
 * resolved ID and DLC from the DBC. */
static int l_can_msg_new(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int"); //msg_id
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int"); //dlc
    uint32_t msg_id = (uint32_t)be_toint(vm, 1);
    int dlc = be_toint(vm, 2);
    if (dlc <= 0 || dlc > 8) dlc = 8;

    uint8_t zeros[8] = {0};

    be_getglobal(vm, "map");
    be_newmap(vm);
    be_pushstring(vm, "id");      be_pushint(vm, (bint)msg_id);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "data");    be_pushbytes(vm, zeros, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_pushstring(vm, "dlc");     be_pushint(vm, dlc);
    be_data_insert(vm, -3); be_pop(vm, 2);
    be_call(vm, 1);
    be_pop(vm, 1);
    be_return(vm);
}

/* can_msg_send(bus:int, draft) -> nil
 * Transmit a message draft on the given bus. Reads id, data, dlc from the
 * draft map instance via dot-member access. bus is first for consistency
 * with can_send_raw/can_recv_raw. The draft is kept bus-agnostic — pass the
 * bus at send time. Signal encoding and counter/checksum update must be done
 * in Berry before calling (use msg_sig_set). */
static int l_can_msg_send(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int"); //bus
    if (!be_isinstance(vm, 2)) {
        ESP_LOGW(TAG_BB, "can_msg_send: trying to send empty message");
        be_return_nil(vm);
    }

    int bus = be_toint(vm, 1);
    can_t *eng = get_bus(bus);
    if (!eng) be_return_nil(vm);

    /* Map class instances store key-value pairs in a hidden .p member.
     * be_getmember only finds direct instance members (.p, methods) but
     * NOT keys inside .p. Use be_getmember(".p") + be_getindex("key")
     * to access them, matching Berry's own be_maplib.c pattern. */
    be_getmember(vm, 2, ".p");
    int p_idx = be_top(vm);

    be_pushstring(vm, "id");
    be_getindex(vm, p_idx);
    uint32_t id = (uint32_t)be_toint(vm, -1);
    be_pop(vm, 1);

    be_pushstring(vm, "dlc");
    be_getindex(vm, p_idx);
    int dlc = be_toint(vm, -1);
    be_pop(vm, 1);

    be_pushstring(vm, "data");
    be_getindex(vm, p_idx);
    size_t len = 0;
    const uint8_t *draft_data = be_tobytes(vm, -1, &len);
    uint8_t data[8];
    if (draft_data) {
        memcpy(data, draft_data, len > 8 ? 8 : len);
    } else {
        memset(data, 0, 8);
        len = 0;
    }
    be_pop(vm, 1);
    be_pop(vm, 1); /* .p */

    can_send(eng, id, data, (uint8_t)dlc);
    be_return_nil(vm);
}

/* ---- CAN utility functions (pure computation, no DBC) ----
 * These operate on raw bytes and are called by the preprocessor-generated
 * code. They implement the CAN signal encoding layer: DBC signal metadata
 * (start_bit, length, byte order, scaling) is resolved at preprocess time
 * and baked into the generated Berry code as integer/float literals. */

/* bit_extract(data:bytes, start_bit:int, bit_length:int, big_endian:bool) -> int
 * Extract an integer bit field from a bytes object. Motorola (big-endian)
 * or Intel (little-endian) byte order. Returns the raw unscaled value. */
static int l_bit_extract(bvm *vm)
{
    CHECK_ARITY(vm, 4);
    CHECK_TYPE(vm, 1, be_isbytes(vm, 1), "bytes");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    size_t len = 0;
    const uint8_t *data = be_tobytes(vm, 1, &len);
    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    uint64_t raw = bit_extract(data, start_bit, bit_length, big_endian);
    be_pushint(vm, (bint)raw);
    be_return(vm);
}

/* bit_insert(data:bytes, start_bit:int, bit_length:int, big_endian:bool, raw:int) -> bytes
 * Returns a new bytes object with the raw integer value inserted at the
 * specified bit position. Berry bytes are immutable so this allocates a new
 * copy (8 bytes max). Used by the preprocessor for msg_sig_set. */
static int l_bit_insert(bvm *vm)
{
    CHECK_ARITY(vm, 5);
    CHECK_TYPE(vm, 1, be_isbytes(vm, 1), "bytes");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    CHECK_TYPE(vm, 5, be_isint(vm, 5), "int");
    size_t len = 0;
    const uint8_t *src = be_tobytes(vm, 1, &len);
    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    uint64_t raw = (uint64_t)(unsigned)be_toint(vm, 5);
    uint8_t buf[8];
    memcpy(buf, src, len > 8 ? 8 : len);
    bit_insert(buf, start_bit, bit_length, big_endian, raw);
    be_pushbytes(vm, buf, len > 8 ? 8 : len);
    be_return(vm);
}

/* msg_sig_get(draft, start_bit:int, bit_length:int, big_endian:bool,
 *             is_signed:bool, scale:real, offset:real) -> real
 * Decode a DBC signal from a message draft: extracts draft.data bytes,
 * reads the bitfield, applies sign, scale, offset. Returns physical value. */
static int l_msg_sig_get(bvm *vm)
{
    CHECK_ARITY(vm, 7);
    if (!be_isinstance(vm, 1)) be_return_nil(vm);
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    CHECK_TYPE(vm, 5, be_isbool(vm, 5), "bool");

    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    bool is_signed = be_tobool(vm, 5);
    float scale = (float)be_toreal(vm, 6);
    float offset = (float)be_toreal(vm, 7);

    /* Berry class instances store internal data under a hidden `.p` member.
     * be_getmember gets it, then be_getindex looks up "data" within it
     * (same pattern as be_maplib.c:m_item for map["key"]). */
    be_getmember(vm, 1, ".p");
    be_pushstring(vm, "data");
    be_getindex(vm, -2);
    if (!be_isbytes(vm, -1)) {
        be_pop(vm, 1);
        be_return_nil(vm);
    }
    size_t len = 0;
    const uint8_t *data = be_tobytes(vm, -1, &len);
    be_pop(vm, 1);

    float val = signal_decode(data, start_bit, bit_length, big_endian, is_signed, scale, offset);
    be_pushreal(vm, (breal)val);
    be_return(vm);
}

/* msg_sig_set(draft, start_bit:int, bit_length:int, big_endian:bool,
 *             is_signed:bool, scale:real, offset:real, value:real) -> nil
 * Encode a physical value into a message draft: converts to raw, writes
 * the bitfield in draft.data bytes in-place. No return value. */
static int l_msg_sig_set(bvm *vm)
{
    CHECK_ARITY(vm, 8);
    if (!be_isinstance(vm, 1)) {
        ESP_LOGW(TAG_BB, "msg_sig_set: draft is nil");
        be_return_nil(vm);
    }
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    CHECK_TYPE(vm, 4, be_isbool(vm, 4), "bool");
    CHECK_TYPE(vm, 5, be_isbool(vm, 5), "bool");

    uint8_t start_bit = (uint8_t)be_toint(vm, 2);
    uint8_t bit_length = (uint8_t)be_toint(vm, 3);
    bool big_endian = be_tobool(vm, 4);
    bool is_signed = be_tobool(vm, 5);
    float scale = (float)be_toreal(vm, 6);
    float offset = (float)be_toreal(vm, 7);
    float value = (float)be_toreal(vm, 8);

    /* Same `.p` + getindex pattern as l_msg_sig_get. */
    be_getmember(vm, 1, ".p");
    be_pushstring(vm, "data");
    be_getindex(vm, -2);
    if (!be_isbytes(vm, -1)) {
        be_pop(vm, 1);
        ESP_LOGW(TAG_BB, "msg_sig_set: draft has no data bytes");
        be_return_nil(vm);
    }
    size_t len = 0;
    uint8_t *data = (uint8_t *)be_tobytes(vm, -1, &len);
    signal_encode(data, start_bit, bit_length, big_endian, is_signed, scale, offset, value);
    be_pop(vm, 1);

    be_return_nil(vm);
}

/* ---- LED ---- */

/* led_set(r:int, g:int, b:int) -> nil
 * Set the on-board RGB LED to the given colour. Each channel 0–255. */
static int l_led_set(bvm *vm)
{
    CHECK_ARITY(vm, 3);
    CHECK_TYPE(vm, 1, be_isint(vm, 1), "int");
    CHECK_TYPE(vm, 2, be_isint(vm, 2), "int");
    CHECK_TYPE(vm, 3, be_isint(vm, 3), "int");
    uint8_t r = (uint8_t)be_toint(vm, 1);
    uint8_t g = (uint8_t)be_toint(vm, 2);
    uint8_t b = (uint8_t)be_toint(vm, 3);
    led_rgb_set(r, g, b);
    be_return_nil(vm);
}

/* led_off() -> nil
 * Turn the on-board RGB LED off. */
static int l_led_off(bvm *vm)
{
    (void)vm;
    led_rgb_off();
    be_return_nil(vm);
}

/* ---- State (NVS) ----
 * Key-value storage persisted to NVS flash under the "script" namespace.
 * Values survive reboot. All keys and values are strings. */

/* state_set(key:str, value:str) -> nil
 * Write a string value to persistent storage. Overwrites existing. */
static int l_state_set(bvm *vm)
{
    CHECK_ARITY(vm, 2);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    CHECK_TYPE(vm, 2, be_isstring(vm, 2), "string");
    state_set("script", be_tostring(vm, 1), be_tostring(vm, 2));
    be_return_nil(vm);
}

/* state_get(key:str [, default]) -> str | default | nil
 * Read a string value from persistent storage. If key is missing, returns
 * the optional default or nil if no default given. */
static int l_state_get(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    static char buf[256];
    if (state_get("script", be_tostring(vm, 1), buf, sizeof(buf)) == ESP_OK) {
        be_pushstring(vm, buf);
        be_return(vm);
    }
    if (be_top(vm) >= 2) {
        be_pushvalue(vm, 2);
        be_return(vm);
    }
    be_return_nil(vm);
}

/* state_remove(key:str) -> nil
 * Delete a key from persistent storage. No-op if key does not exist. */
static int l_state_remove(bvm *vm)
{
    CHECK_ARITY(vm, 1);
    CHECK_TYPE(vm, 1, be_isstring(vm, 1), "string");
    state_remove("script", be_tostring(vm, 1));
    be_return_nil(vm);
}

/* ---- millis() helper ---- */

/* millis() -> int
 * Milliseconds since boot. Wraps at 2^31 (~24 days). */
static int l_millis(bvm *vm)
{
    be_pushint(vm, (bint)(esp_timer_get_time() / 1000));
    be_return(vm);
}

/* ---- Registration ----
 * Called once during Berry VM initialisation to expose all native functions.
 * The names here are what Berry scripts see at global scope. Keep the
 * completions in webui/src/editor/berry-completions.ts in sync. */

void berry_register_bindings(bvm *vm)
{
    /* Raw CAN I/O: no DBC, no encoding */
    be_regfunc(vm, "can_send_raw",   l_can_send_raw);
    be_regfunc(vm, "can_recv_raw",   l_can_recv_raw);

    /* Message drafts: DBC-aware, named fields {id, bus, data, dlc} */
    be_regfunc(vm, "can_msg_get",    l_can_msg_get);
    be_regfunc(vm, "can_msg_new",    l_can_msg_new);
    be_regfunc(vm, "can_msg_send",   l_can_msg_send);

    /* CAN bit-level utilities: used by preprocessor-generated code */
    be_regfunc(vm, "bit_extract",    l_bit_extract);
    be_regfunc(vm, "bit_insert",     l_bit_insert);
    be_regfunc(vm, "msg_sig_get", l_msg_sig_get);
    be_regfunc(vm, "msg_sig_set", l_msg_sig_set);

    /* On-board RGB LED */
    be_regfunc(vm, "led_set", l_led_set);
    be_regfunc(vm, "led_off", l_led_off);

    /* Persistent key-value storage (NVS flash, "script" namespace) */
    be_regfunc(vm, "state_set", l_state_set);
    be_regfunc(vm, "state_get", l_state_get);
    be_regfunc(vm, "state_remove", l_state_remove);

    /* Timers and actions (berry_registry.c) */
    berry_register_registry(vm);

    /* System */
    be_regfunc(vm, "millis", l_millis);
}
