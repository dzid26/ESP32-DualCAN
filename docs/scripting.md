# Dorky Commander — Berry Scripting Reference

Scripts are `.be` files stored on LittleFS under `/scripts/`. Saving can also
write a `.bep` preprocessed runtime file with DBC names linked to numeric refs.
Each source file can carry
metadata comments at the top and must define a `setup()` function.

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
# Send a raw frame: bus (0 or 1), CAN ID (integer), payload (bytes object).
var b = bytes()
b.add(0x01)
b.add(0xFF)
can_send_raw(0, 0x100, b)

# Dequeue the next received frame — returns bytes or nil when queue is empty.
var rx = can_recv_raw(0)
if rx != nil
  print("got id 0x" .. format("%03X", rx.item(0)))
end
```

## Encoded messages  *(requires DBC — applies Tesla checksum/counter automatically)*

```berry
# Create a fresh zeroed draft from DBC (name → ID + DLC resolved at compile time)
var msg = can_msg_new("UI_powertrainControl")
# Or with numeric ID, bus, DLC:  can_msg_new(0x313, 0, 8)
can_msg_set(msg, "UI_trackModeRequest", 1)
can_msg_send(msg)
```

### AI authoring guidance

When writing scripts that inject a vehicle command frame, send only the intended command frame unless the user explicitly asks for a release/reset frame. No need to set up periodic reset - the vehicle will take care of resetting the state back to idle or similar.

---

## Timers

```berry
timer_after(500, /-> print("once after 500 ms"))

var t = def() print("every 200 ms") end
timer_every(200, t)

timer_cancel(t)   # cancel by function reference
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
state_set("my_key", 42)
var v = state_get("my_key")   # nil if not set
state_remove("my_key")
```

---

## Utilities

| Function | Returns | Description |
|---|---|---|
| `millis()` | integer | milliseconds since boot |
| `print(msg)` | — | logs to the web UI log panel |
| `str(v)` | string | converts any value to string |
| `format(fmt, ...)` | string | C-style string formatting |

---

## Berry syntax cheat sheet

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

var b = bytes()                  # byte buffer
b.add(0xFF)                      # append byte
b.set(0, 0x01)                   # set by index
b.item(0)                        # read by index
```
