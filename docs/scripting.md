# Dorky Commander — Scripting Reference

> **💡 This is where the fun begins.** You can turn your commander into a smart device that reacts to the car CAN signals. Dive in!

> Scripting is based on Berry - a Python like language: <https://berry-lang.github.io/>

Scripts are `.be` files stored on LittleFS under `/scripts/`. Saving can also
write a `.bep` preprocessed runtime file with DBC names linked to numeric refs.
Each source file can carry metadata comments at the top and must define a `setup()` function.

## Script structure

```berry
# @name My script
# @description What it does
# @bus 0

var my_var = 0

def setup()
  # called when the script is enabled — register callbacks and timers here
  timer_every(1000, def()
    print("tick " .. str(my_var))
    my_var += 1
  end)
end

def teardown()
  # called when the script is disabled (optional)
  print("bye")
end
```

Metadata fields (`@name`, `@description`, `@bus`) appear in the web UI.

---

## CAN signals  *(requires a DBC loaded on the target bus)*

```berry
# Subscribe to a signal — fn is called whenever the value changes.
# First arg is the DBC **message** name, second is the **signal** name within it.
on_can_signal("VCRIGHT_doorStatus", "VCRIGHT_frontHandlePulled", def(sig)
  print("handle: " .. str(sig['value']) .. " prev: " .. str(sig['prev']))
end)

# Read current value synchronously (returns a map or nil).
var m = can_signal_get("DI_speed", "DI_vehicleSpeed")
if m != nil
  print("speed: " .. str(m['value']))
end
```

## Raw CAN frames

```berry
# Send a raw frame: bus (0 or 1), CAN ID (integer), payload (bytes object, max 8).
var b = bytes("01FF")               # from hex string
can_send_raw(0, 0x100, b)

# Using setbits(offset_bits, len_bits, val) — chainable, returns self.
# UI_mirrorFoldRequest at bit 24, 2 bits, value 0x2 (unfold):
can_send_raw(0, 0x273, bytes(-8).setbits(24, 2, 0x2))

# Dequeue the next received frame — returns a list [id, data] or nil when
# the queue is empty. rx[0] = CAN ID (int), rx[1] = payload (bytes).
var rx = can_recv_raw(0)
if rx != nil
  print("got id 0x" .. format("%03X", rx[0]))
end
```

## Encoded messages  *(requires DBC — applies Tesla checksum/counter automatically)*

```berry
# Create a fresh zeroed draft from DBC (name → ID + DLC resolved at compile time)
var msg = can_msg_new("UI_powertrainControl")
# Or with numeric ID, DLC:  can_msg_new(0x313, 8)
can_msg_set(msg, "UI_trackModeRequest", 1)
can_msg_send(0, msg)                           # bus, msg

# Or fetch the latest received frame as a modifiable draft
var msg2 = can_msg_get(0, "UI_powertrainControl")   # bus first, then DBC name
# or by numeric CAN ID: can_msg_get(0, 0x313)
can_msg_set(msg2, "UI_trackModeRequest", 1)
can_msg_send(0, msg2)
```

The draft is a map instance with keys `id` (int), `data` (bytes), `dlc` (int). You can read/write individual signals with `can_msg_set`, or access the raw data buffer directly: `msg["data"]`.

### Low-level bit encoding (used by the preprocessor, exposed for custom work)

```berry
# Extract a raw integer bit field from bytes.
var raw = bit_extract(data, 0, 2, false)   # start_bit=0, len=2, little_endian

# Insert a raw integer into bytes, returns a new bytes copy.
var encoded = bit_insert(data, 0, 2, false, 3)

# Decode a DBC signal: raw bits → physical value (scale + offset).
var val = signal_decode(data, 0, 2, false, false, 1.0, 0.0)

# Encode a physical value into bytes, returns a new copy.
var out = signal_encode(data, 0, 2, false, false, 1.0, 0.0, 42.0)
```

### AI authoring guidance

When writing scripts that inject a vehicle command frame, send only the intended command frame unless the user explicitly asks for a release/reset frame. No need to set up periodic reset - the vehicle will take care of resetting the state back to idle or similar.

---

## Timers

```berry
# Fire once after 500 ms. Returns a handle (int).
var h1 = timer_after(500, def()
  print("fired once")
end)

# Fire every 200 ms. Returns a handle (int) for cancellation.
var h2 = timer_every(200, def()
  print("every 200 ms")
end)

# Cancel by handle (no-op if handle is invalid or already fired).
timer_cancel(h2)
```

---

## Actions  *(appear as tiles on the Events page)*

```berry
action_register("my_action", def()
  print("tile tapped")
  led_set(0, 40, 0)
  timer_after(200, /-> led_off())
end)

action_invoke("my_action")   # call programmatically
```

---

## LED

```berry
led_set(40, 0, 0)   # r, g, b  (0–255 each)
led_off()
```

---

## Persistent storage  *(NVS flash — survives reboot)*

```berry
state_set("my_key", "hello")          # write a string
var v = state_get("my_key", "fallback")  # read, with default
state_remove("my_key")                  # delete key
```

---

## Function reference

### High-level API *(user-facing — write these directly in scripts)*

**Not renamed by the preprocessor** (the call you write is the call that compiles):

| Function | Returns | Description |
|---|---|---|
| `can_msg_new(name)` | draft \| nil | Create zeroed draft from DBC name (name→ID+DLC resolved at compile time) |
| `can_msg_new(id, dlc)` | draft \| nil | Create zeroed draft from numeric ID and DLC |
| `can_msg_get(bus, id \| name)` | draft \| nil | Latest received frame as editable draft — bus is first |
| `can_msg_send(bus, draft)` | — | Transmit the draft on bus (auto-handles checksum/counter) |
| `can_send_raw(bus, id, data)` | — | Transmit a raw CAN frame (max 8 bytes) |
| `can_recv_raw(bus)` | list \| nil | Pop next queued frame → `[id, data]` |
| `timer_after(ms, fn)` | handle | Fire fn once after ms |
| `timer_every(ms, fn)` | handle | Fire fn every ms |
| `timer_cancel(handle)` | — | Cancel a timer by handle |
| `action_register(name, fn)` | — | Register a Dashboard tile |
| `action_invoke(name)` | — | Invoke an action by name |
| `led_set(r, g, b)` | — | On-board RGB LED (0–255 each) |
| `led_off()` | — | Turn off LED |
| `state_set(key, val)` | — | Write string to NVS flash |
| `state_get(key [, default])` | str \| nil | Read string from NVS flash |
| `state_remove(key)` | — | Delete key from NVS flash |
| `millis()` | int | Milliseconds since boot |
| `print(msg)` | — | Log to web UI panel |
| `str(v)` | string | Convert any value to string |
| `format(fmt, ...)` | string | C-style string formatting |

**Renamed or rewritten by the preprocessor** (the call you write is replaced
at compile time — writing them without a DBC loaded will produce errors):

| Source (what you write) | Becomes (after preprocessing) |
|---|---|
| `can_msg_set(draft, "sig", val)` | `draft["data"] = signal_encode(draft["data"], sb, len, be, sg, sc, off, val)` |
| `can_signal_get("msg", "sig" [, bus])` | `__sig_get(msg_id, sb, len, be, sg, sc, off, bus)` |
| `on_can_signal("msg", "sig", fn [, bus])` | `__watch_sig(msg_id, sb, len, be, sg, sc, off, bus, fn)` |

### Low-level API *(generated by the preprocessor — do not write these manually)*

These are emitted automatically when the preprocessor (invoked on save) rewrites
DBC-aware calls. You'll see them if you inspect a `.bep` file, but you should
never type them into a `.be` source file.

| Function | Generated from | Description |
|---|---|---|
| `__sig_get(msg_id, sb, len, be, sg, sc, off, bus)` | `can_signal_get` | Helper that calls `can_msg_get` + `signal_decode` |
| `__watch_sig(msg_id, sb, len, be, sg, sc, off, bus, fn)` | `on_can_signal` | Helper that polls a signal and fires `fn` on change |
| `signal_decode(data, sb, len, be, sg, sc, off)` | `__sig_get` / `__watch_sig` bodies | Decode raw bytes to physical value |
| `signal_encode(data, sb, len, be, sg, sc, off, val)` | `can_msg_set` body | Encode physical value into bytes |
| `bit_extract(data, sb, len, be)` | `signal_decode` body | Extract raw bit field |
| `bit_insert(data, sb, len, be, raw)` | `signal_encode` body | Insert raw integer into bytes |

---

## Berry lang syntax cheat sheet

```berry
var x = 0                        # variable
x += 1                           # compound assignment

def greet(name)                  # function definition
  print("hi " .. name)
end

if x > 10                        # if / elif / else / end
  print("big")
elif x > 5
  print("medium")
else
  print("small")
end

for i: 0..4                      # range loop (inclusive on both ends)
  print(str(i))
end

while x > 0                      # while loop
  x -= 1
end

var fn = /-> print("lambda")     # zero-arg lambda
var fn2 = def(a) a * 2 end       # inline function expression

var b = bytes(-8)                 # fixed 8-byte buffer, zero-filled
b.setbits(24, 2, 2)               # signal at bit 24, 2 bits, value 2 (PRESENT)
var b2 = bytes('deadbeef0011')    # 8 bytes from hex string
b.item(0)                         # read by index
```

## `setbits`

Writes a bit-level value into a `bytes` buffer. Chainable — returns `self`.

Works natively with **Intel (little-endian) unsigned** DBC signals — the most common format. For Motorola (big-endian) or signed signals, convert offset/value before calling.

| Parameter | Unit | Range |
|---|---|---|
| `offset_bits` | bits | `0`+ |
| `len_bits` | bits | `1`..`32` |
| `val` | integer | uses only lower `len_bits` |

```berry
# arbitrary bit offset, no need for byte alignment
can_send_raw(0, 0x273, bytes(-8).setbits(24, 2, 2))
```

More here - [Berry-in-20-minutes](https://berry.readthedocs.io/en/latest/source/en/Berry-in-20-minutes.html)