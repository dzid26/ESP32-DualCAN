# Dorky Commander — Firmware

Open-source alternative to the S3XY Commander based on ESP32-C6 with dual CAN. Provides BLE transport for webui, scripting VM, CAN caching, wifi file server, OTA, TeslaBLE.


---

## What you need

- **Dorky Commander board** (ESP32-C6-SuperMini + dual TCAN1044 transceivers)
- **USB-C cable** for first flash
- **Chrome or Chromium** (required for Web Bluetooth on all platforms)
- A Tesla (or any vehicle with a supported DBC) for CANbus interaction

See [../README.md](../README.md) for the first-use walkthrough (flashing, BLE connection, DBC, scripts, and actions).

---

## Berry scripting reference

Scripts live on LittleFS under `/scripts/*.be`. Each script must define `setup()`:

```berry
# @name My script
# @description What it does

def setup()
  # called when the script is enabled
  timer_every(1000, def()
    print("tick")
  end)
end

def teardown()
  # called when the script is disabled (optional)
end
```

For the full API reference with examples, see [scripting.md](../docs/scripting.md).

### Available functions (quick reference)

**High-level (user-facing):**

Functions you write directly. Those marked "preprocessed" are rewritten at compile
time — the call you write is replaced with resolved numeric arguments, a different
function name, or inline code.

| Function | Returns | Preprocessed? | Description |
|---|---|---|---|
| `can_msg_new(name)` | draft \| nil | arg resolve | Zeroed draft from DBC name *(ID+DLC baked in)* |
| `can_msg_new(id, dlc)` | draft \| nil | no | Zeroed draft from numeric ID |
| `can_msg_get(bus, id \| name)` | draft \| nil | arg resolve | Latest rx frame as a draft
| `msg_sig_set(draft, sig, val)` | — | **rewritten** | Becomes `msg_sig_set(draft, sb, len, be, signed, scale, offset, val)` |
| `can_msg_send(bus, draft)` | — | no | Transmit draft (auto checksum/counter) |
| `msg_sig_get(draft, sig)` | int | **inlined** | Becomes `msg_sig_get(draft, sb, len, be, signed, scale, offset)` |
| `can_send_raw(bus, id, data)` | — | no | Send raw CAN frame (no DBC) |
| `can_recv_raw(bus, msg_id [, timeout])` | bytes \| nil | no | Last payload for a CAN ID. Default 1 s timeout blocks for initial data. |
| `timer_after(ms, fn)` | handle | no | One-shot timer |
| `timer_every(ms, fn)` | handle | no | Repeating timer |
| `timer_cancel(handle)` | — | no | Cancel a timer |
| `action_register(name, fn)` | — | no | Register a Dashboard tile |
| `action_invoke(name)` | — | no | Invoke action programmatically |
| `led_set(r, g, b)` | — | no | On-board RGB LED (0–255 each) |
| `led_off()` | — | no | Turn off LED |
| `state_set/state_get/state_remove` | — | no | NVS flash storage |
| `millis()` | int | no | Milliseconds since boot |
| `print(msg)` | — | no | Log to web UI |

For the full functions reference with examples, see [scripting.md](../docs/scripting.md).

### Example: blink LED when speed exceeds 100 km/h

```berry
# @name Speed LED
# @description Green LED above 100 km/h, off below.
# Requires Tesla Model 3/Y DBC on bus 0.

var fast = false

def poll()
  var msg = can_msg_get(0, "DI_speed")
  if msg == nil return end
  var speed = msg_sig_get(msg, "DI_vehicleSpeed")
  var over = speed > 100
  if over != fast
    fast = over
    if fast
      led_set(0, 40, 0)
    else
      led_off()
    end
  end
end

def setup()
  timer_every(250, poll)
end
```

---

## Development

Clone with submodules (required — the BLE and Berry components are git submodules):

```bash
git clone https://github.com/dzid26/ESP32-DualCAN.git --recurse-submodules
```

Or update submodules later:

```bash
git submodule update --init --recursive
```

### Build & Flash

Recommended: [PlatformIO IDE extension for VS Code](https://platformio.org/install) — open the `firmware` folder in VS Code and use **Build** / **Upload** from the sidebar.

CLI alternative (requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)):

```bash
cd firmware
pio run -e esp32-c6              # build
pio run -e esp32-c6 -t upload    # flash
```

### Monitor & Test

```bash
cd firmware
pio device monitor               # serial console (115200 baud)
pio test -e tests-native         # host unit tests (gcc required)
pio test -e esp32-c6-tests-arduino  # hardware smoke tests (board connected)
```

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Board not showing in Bluetooth dialog | Make sure Chrome is used (not Firefox/Safari). Check board is powered. |
| Script errors in log panel | Syntax error in Berry code — check the `error` field in Scripts list. |
| DBC upload fails | Try a smaller DBC first. Signals are limited by flash — typical DBCs work fine. |
| Device unresponsive after bad script | Power-cycle the board. Bad scripts are isolated; others still run. |
| OTA stuck at 0% | BLE MTU negotiation can take a few seconds — wait 10 s before aborting. |


See [`../README.md`](../README.md) for hardware schematic and pin assignments.
