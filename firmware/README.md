# Dorky Commander — Firmware

Open-source alternative to the S3XY Commander. ESP32-C6 with dual CAN, Berry scripting, and a web UI over BLE.

---

## What you need

- **Dorky Commander board** (ESP32-C6-SuperMini + dual TCAN1044 transceivers)
- **USB-C cable** for first flash
- **Chrome or Chromium** (required for Web Bluetooth on all platforms)
- A Tesla (or any vehicle with a supported DBC)

---

## Step 1 — Flash the firmware

### Option A: Download prebuilt binary (recommended)

1. Download the latest `dorky-commander-vX.Y.Z.bin` from [GitHub Releases](https://github.com/dzid26/ESP32-DualCAN/releases).
2. Connect the board via USB-C.
3. Flash with `esptool.py`:

```bash
pip install esptool
esptool.py --chip esp32c6 write_flash 0x10000 dorky-commander-vX.Y.Z.bin
```

Or use the [Espressif Web Flasher](https://espressif.github.io/esptool-js/) in Chrome — no install needed.

### Option B: Build from source

```bash
# Requires Python + PlatformIO
pip install platformio
cd firmware
pio run -e esp32-c6          # build
pio run -e esp32-c6 -t upload # build + flash
```

---

## Step 2 — Connect over BLE

1. Open the [Dorky Commander web UI](https://dzid26.github.io/ESP32-DualCAN/) in Chrome.
   *(Or install it as an app: Settings → Install app → Add to home screen.)*
2. Click **Connect** in the status bar.
3. Select **Dorky Commander** from the Bluetooth pairing dialog.
4. The status bar turns green and shows the firmware version.

> **Tip:** The board advertises as `Dorky Commander`. If you don't see it, hold the board's BOOT button for 3 s to reset BLE state.

---

## Step 3 — Load a DBC

The device needs a compiled DBC to decode signals by name.

1. Go to **DBC** in the left rail.
2. Paste or load a `.dbc` file. The Tesla Model 3/Y vehicle DBC is bundled — click **Load example**.
3. Select the target bus (0 = vehicle CAN, 1 = chassis CAN on Tesla).
4. Click **Upload to device**. The binary blob is sent over BLE and stored in flash.

---

## Step 4 — Write your first script

1. Go to **Scripts**.
2. Click **Load example…** → select `hello_log.be`.
3. Click **Save**, then toggle the **Enable** switch.
4. Open the **Log** panel (bottom right) — you should see `hello_log: setup` followed by heartbeat lines every 5 seconds.

The script runs on the device. `print()` output streams over BLE to the log panel.

---

## Step 5 — Run an action

1. Go to **Events** in the left rail.
2. Click **+ Add event** — this loads the `tiles_demo.be` example into the Scripts editor. Save and enable it.
3. Back on the **Events** page, four tiles appear: `blip_red`, `blip_green`, `blip_blue`, `rainbow`.
4. Tap a tile — the onboard LED blinks.

This proves the full path: BLE → firmware → Berry → hardware.

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

| Function | Description |
|---|---|
| `on_can_signal(msg, sig, fn)` | Subscribe to signal changes; `fn(sig)` receives `sig['value']`, `sig['prev']` |
| `can_signal_get(msg, sig)` | Read current value (returns map or nil) |
| `can_send_raw(bus, id, bytes)` | Send raw CAN frame |
| `can_recv_raw(bus)` | Receive next queued frame (bytes or nil) |
| `can_msg_get(id | name [, bus])` | Get encodable message draft (requires DBC) |
| `can_msg_new(name [, bus])` | Create zeroed encodable draft from DBC name *(name → ID+DLC resolved at compile time)* |
| `can_msg_new(id, bus, dlc)` | Create zeroed encodable draft from numeric ID |
| `can_msg_set(msg, sig, val)` | Set signal in message |
| `can_msg_send(msg)` | Transmit with auto checksum/counter |
| `action_register(name, fn)` | Register a Dashboard tile |
| `timer_after(ms, fn)` | One-shot timer |
| `timer_every(ms, fn)` | Repeating timer |
| `timer_cancel(fn)` | Cancel a timer |
| `led_set(r, g, b)` | Set onboard RGB LED (0–255 each) |
| `led_off()` | Turn off LED |
| `state_set(key, val)` / `state_get(key)` | Persist values to NVS flash |
| `millis()` | Milliseconds since boot |
| `print(msg)` | Log to the web UI log panel |

### Example: blink LED when speed exceeds 100 km/h

```berry
# @name Speed LED
# @description Green LED above 100 km/h, off below.
# Requires Tesla Model 3/Y DBC on bus 0.

var fast = false

def setup()
  on_can_signal("DI_speed", "DI_vehicleSpeed", def(sig)
    var over = sig['value'] > 100
    if over != fast
      fast = over
      if fast
        led_set(0, 40, 0)
      else
        led_off()
      end
    end
  end)
end
```

---

## OTA firmware update

You can update the firmware without USB:

1. Go to **Settings → Firmware**.
2. Click **Check GitHub** to fetch the latest release, then **Download & flash**.
   Or click **Upload .bin** to flash a file from disk.
3. The progress bar shows transfer status. The device reboots automatically when done.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Board not showing in Bluetooth dialog | Make sure Chrome is used (not Firefox/Safari). Check board is powered. |
| Script errors in log panel | Syntax error in Berry code — check the `error` field in Scripts list. |
| DBC upload fails | Try a smaller DBC first. Signals are limited by flash — typical DBCs work fine. |
| Device unresponsive after bad script | Power-cycle the board. Bad scripts are isolated; others still run. |
| OTA stuck at 0% | BLE MTU negotiation can take a few seconds — wait 10 s before aborting. |

---

## Development

```bash
cd firmware
pio run -e esp32-c6              # build
pio run -e esp32-c6 -t upload    # flash
pio device monitor               # serial console (115200 baud)
pio test -e tests-native         # host unit tests (gcc required)
```

See [`../README.md`](../README.md) for hardware schematic and pin assignments.
