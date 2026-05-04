# Dorky Commander — Firmware Architecture

Open source alternative to S3XY Commander. ESP32-C6 based dual-CAN node for Tesla (and other CAN-equipped vehicles) that runs user-authored automations against CAN signals, with a browser-based configuration and scripting UI.

This document captures the intended design. It is a living spec — update it when decisions change.

---

## 1. Goals

- Run user automations that react to CAN signals and send CAN messages.
- Be scriptable in a small, embedded-friendly language without flashing firmware.
- Provide a browser-based UI for editing scripts, watching signals, and reverse-engineering unknown messages.
- Work without a proprietary mobile app.
- Support OTA firmware updates.
- Ship useful defaults (DBC files, example automations) but let users replace them.

### Non-goals (for now)

- Physical buttons / HMI devices. The first release is automations only.
- Interop with commercial S3XY Buttons hardware. Their BLE protocol is proprietary; reverse engineering is out of scope.
- Mobile native apps (iOS/Android). The web UI is the only client.
- Cloud connectivity. Everything runs locally or just hosted for convenience.

---

## 2. Hardware

Existing, designed and fabricated:

- **MCU**: ESP32-C6-SuperMini (single RISC-V core @ 160 MHz, 320 KB SRAM, 512 KB ROM, 4 MB flash, BLE 5 + WiFi 6, 2× TWAI CAN 2.0 controllers, integrated antenna, RGB LED).
- **Transceivers**: 2× TCAN1044 (CAN FD capable, we use CAN 2.0).
- **Power**: TI LV2862 DC/DC, 5–58 V input.
- **Connector**: Molex, compatible with Enhauto Tesla harnesses.
- **USB-C** for programming and debugging.

Pin assignments (from `TempFirmwareTest/src/main.cpp`):

| Function | GPIO |
|---|---|
| CAN0 TX | 2 |
| CAN0 RX | 1 |
| CAN0 STB | 19 |
| CAN1 TX | 4 |
| CAN1 RX | 3 |
| CAN1 STB | 18 |

Bus assignment (convention, user-configurable per preset) — by default for Tesla:

- **CAN0** → Tesla VehicleCAN
- **CAN1** → Tesla ChassisCAN

---

## 3. Software stack

- **Framework**: ESP-IDF via PlatformIO.
- **RTOS**: FreeRTOS (shipped with ESP-IDF).
- **Filesystem**: LittleFS for scripts, DBCs, web UI assets, logs.
- **Config store**: ESP-IDF NVS for key/value state (per-script).
- **Scripting language**: [Berry](https://github.com/berry-lang/berry) (MIT), vendored into `lib/berry/`. Chosen over Lua and MicroPython for memory footprint (~40 KB vs 150+ KB), object-orientation that maps well to CAN messages/signals, and proven embedded track record (Tasmota).
- **Web UI**: Svelte 5 + Vite + TypeScript, built as a static SPA. Monaco editor for script editing. uPlot for signal graphs.
- **Transport to UI**: BLE GATT (primary) and HTTP + WebSocket (fallback over WiFi). Same UI code via a transport adapter.
- **Firmware license**: GPLv3 (`firmware/LICENSE`). Hardware stays CERN-OHL-W (`LICENSE.txt` at repo root).

---

## 4. Process / task model

FreeRTOS tasks, rough priority high → low:

1. **CAN RX task (per bus)** — pops frames from the TWAI driver queue, runs DBC decode, updates the signal cache, dispatches events to subscribers. Highest priority so the rx queue never overflows.
2. **CAN TX task (per bus)** — drains an application-level TX queue into the TWAI driver, enforces per-message-ID rate limiting, handles periodic transmit cycles.
3. **Scripting task** — single task runs the Berry VM. It drains a work queue fed by three event sources: CAN rx events (from the rx tasks), timer events (`timer.after` / `timer.every`), and UI commands. Callbacks run serially, so scripts never see concurrent invocation. Berry is not thread-safe; all VM access is confined to this task. There is no implicit "main loop" tick — scripts are purely reactive.
4. **Connectivity task(s)** — BLE GATT server and/or WiFi + HTTP/WebSocket server. Lower priority than CAN.
5. **Tesla-BLE task** — optional, talks to the car over BLE for things CAN can't easily express (wake-on-command, charging when asleep). Runs independently of the scripting task but exposes an API to it.
6. **Housekeeping** — watchdog, OTA, logging flush.

CAN work is always prioritized over scripting. A slow script cannot delay rx or tx.

---

## 5. CAN subsystem

### 5.1 Signal cache

Every decoded signal is kept in an in-memory table keyed by (bus, message, signal) with:

- current value (typed: int, float, bool, enum-string)
- previous value
- last-rx timestamp (monotonic ms)
- subscriber list (Berry callbacks)

When a frame arrives, the rx task decodes it using the per-bus DBC, updates the cache, and enqueues change events into the scripting task's work queue. If a signal's value equals its previous value, the event is still enqueued but marked `changed=false` — scripts can filter.

### 5.2 Message mirror

For every known TX-capable message, the most recent decoded frame is kept so that `can.message("NAME")` returns the full current state. When a script modifies a single signal and calls `.send()`, the framework re-encodes using the mirrored bytes, replacing only the bits that belong to the edited signal. Other signals ride along unchanged, except the checksum which is recalculated. This is the "modify one, preserve the rest" semantic.

### 5.3 Checksums and counters

Detected by signal name suffix:

- `*_Checksum` — computed automatically on send. Default algorithm is the [Tesla checksum](https://github.com/commaai/opendbc/blob/901c138423c16f24445732c57432cc5ff73f7e19/opendbc/car/tesla/teslacan.py#L54-L60):

  ```python
  def tesla_checksum(address, checksum_byte_index, payload):
      s = (address & 0xFF) + ((address >> 8) & 0xFF)
      for i, b in enumerate(payload):
          if i != checksum_byte_index:
              s += b
      return s & 0xFF
  ```

- `*_Counter` — auto-incremented (mod 2^width) on each send of that message.

Other algorithms can be registered per message if needed. A manual helper `can.tesla_checksum(addr, start_bit, bytes)` is also exposed.

### 5.4 Raw access

Always available, regardless of DBC:

```python
can.raw.on_frame(bus, id, fn)   # fn(frame) where frame.data is bytes
can.raw.send(bus, id, data)     # data is bytes
```

Used for reverse engineering and for messages not present in any loaded DBC.

### 5.5 Rate limiting

Hard per-ID cap, default 100 Hz, configurable in the message's transmit policy. If a script exceeds the cap, excess frames are dropped and a warning is logged. This is a safety guard against runaway loops, not a general QoS mechanism.

### 5.6 Simulation mode

Global toggle exposed via UI. When on, all `msg.send()` / `can.raw.send()` calls are routed to a log stream instead of the TWAI driver. The UI can also inject fake signal values into the cache, triggering the same callbacks as real frames. Used for developing and testing scripts without being in the car.

---

## 6. DBC handling

### 6.1 Parsing happens in the browser

DBC files are parsed client-side in the web UI (e.g. with a JS/TS DBC parser). The UI compiles them to a compact binary format and uploads that to the ESP32. The ESP32 never sees raw DBC text. This keeps the firmware parser-free and saves RAM.

### 6.2 Binary format (sketch)

Per-bus file on LittleFS, e.g. `/dbc/bus0.bin`:

```
Header          { magic, version, bus_id, msg_count, string_table_offset }
Messages[]      { id, name_idx, dlc, tx_period_ms, flags, signal_count, signals[] }
Signals[]       { name_idx, start_bit, length, endianness, signedness, mux_type,
                  mux_val, scale, offset, min, max, unit_idx, value_table_idx }
StringTable     (deduped UTF-8 strings)
ValueTables     (for enum signals)
```

Multiplexed signals are supported via `mux_type` (none / multiplexor / multiplexed) and `mux_val`. Decoding respects the current multiplexor value.

### 6.3 Per-bus DBCs

Each bus loads its own DBC. A signal name may exist on both buses with different definitions; the API is always qualified by bus when disambiguation is needed. The unqualified `can.signal("NAME")` searches the active (default) bus first, then the other; if ambiguous it raises an error.

### 6.4 No-DBC operation

If no DBC is loaded for a bus, only `can.raw.*` is usable on it. The UI shows a banner indicating that decoded signal access is unavailable.

---

## 7. Scripting engine (Berry)

### 7.1 One script per file

Scripts live in `/scripts/*.be` on LittleFS. Each is a self-contained automation. The framework loads all scripts marked enabled at boot, plus any enabled at runtime via the UI.

Required shape:

```python
# @name Window drop on long handle pull
# @description Lowers the driver window when the door is open and the handle
#              is held for 2+ seconds.
# @bus 0

def setup()
  # register signal subscriptions, timers, actions
end

def teardown()
  # optional cleanup; called on disable
end
```

The framework calls `setup()` when the script is enabled and `teardown()` when disabled. Handler unregistration is automatic based on what `setup()` registered (each registration is tagged with the current script id and revoked on disable).

**Scripts are purely reactive.** There is no `loop()` and no implicit tick. Periodic behavior uses `timer.every(ms, fn)`. Event-driven behavior uses `can.on(signal, fn)`. On-demand behavior uses Actions (§7.8).

Shared helpers go in `/scripts/lib/*.be`, imported with `import lib.helper`.

### 7.1.1 Script isolation

Berry has a single global namespace. To stop scripts trampling each other:

1. Each script is assigned a unique numeric `script_id` when loaded.
2. The script's source is executed in a fresh-globals state — after running it, the framework captures references to `setup` / `teardown` (and any actions/handlers it registered) and clears those globals so the next script loads cleanly.
3. Native bindings (`can.on`, `timer.every`, `actions.register`, …) tag every registration with the current `script_id`. On `script_loader_disable`, the framework first calls the captured `teardown` ref (if any), then revokes every registration tagged with that `script_id`.
4. Per-script state (`state.*`) uses an NVS namespace derived from the script filename, surviving disable/enable.

### 7.2 Enable/disable UI

The web UI lists all scripts with a checkbox per script. Toggling performs `setup`/`teardown`. A global kill switch disables all scripts at once.

### 7.3 Persistent state

Per-script NVS namespace:

```python
state.set("windows_lowered", true)
var flag = state.get("windows_lowered", false)
```

Hard-capped at ~4 KB per script. Survives reboot, OTA, script edit.

### 7.4 Crash isolation

If a script throws an uncaught error in `setup()` or any callback, the framework disables that single script, records the error, and shows it in the UI. Other scripts keep running. A repeated crash loop disables the script until manually re-enabled.

### 7.5 Timers

```python
timer.after(2000, fn)         # one-shot, ms
var h = timer.every(1000, fn) # periodic
timer.cancel(h)
```

Backed by FreeRTOS software timers. Callbacks run in the scripting task.

### 7.6 Logging

```python
log("message", level)   # level: debug / info / warn / error
```

Logs go to a ring buffer and stream to the UI console over the active transport. Also mirrored to UART for wired debugging.

### 7.8 Actions (named callable units)

Actions are named, parameterless (or single-arg) callables exposed by scripts. They serve two purposes:

- **UI tiles**: the web UI's Dashboard renders one button per registered action. Tapping calls `actions.invoke(name)` over the transport. Use case: short-honk, fold-mirrors, lock-doors, summon-window-down.
- **Cross-script calls**: a reactive handler in script A can invoke an action defined in script B. Example: a "drive-mode-changed" subscriber invokes the "track-mode-prep" action.

An action is just a Berry function with a stable name:

```python
def setup()
  actions.register("short_honk", def()
    var msg = can.message("VCFRONT_buzzerCmd")
    msg.set("VCFRONT_buzzerRequest", 1)
    msg.send()
    timer.after(150, def() msg.set("VCFRONT_buzzerRequest", 0); msg.send() end)
  end)
end
```

Actions are revoked when their script is disabled (same lifecycle as `can.on`).

### 7.9 Scripting API reference (initial surface)

```python
# ---- Signals (decoded) ----
can.signal(name [, bus])            # -> Signal
  .value                            # current decoded value
  .prev                             # previous value
  .age_ms                           # ms since last rx

can.on(name, fn [, bus])            # fn(sig) called when value changes
                                    # use sig.prev inside fn for edge detection

# ---- Messages (encode + send) ----
can.message(name [, bus])           # -> Message (a draft from latest rx mirror)
  .set(signal_name, value)          # modify one signal in the draft
  .get(signal_name)                 # read a signal from the draft
  .send([bus])                      # transmit once; checksum/counter auto-handled

# Periodic transmission uses timer.every + msg.send(). No dedicated API.

# ---- Raw ----
can.raw.on_frame(bus, id, fn)       # fn(frame) with frame.id, frame.data, frame.ts
can.raw.send(bus, id, data)

# ---- Timers ----
timer.after(ms, fn)
timer.every(ms, fn)
timer.cancel(handle)

# ---- State (NVS) ----
state.get(key [, default])
state.set(key, value)
state.remove(key)

# ---- Logging ----
log(msg [, level])

# ---- LED ----
led.set(r, g, b)
led.off()

# ---- Actions (UI tiles + cross-script) ----
actions.register(name, fn)          # tiles fn for UI button + cross-script invoke
actions.invoke(name [, arg])        # call an action by name (returns its result)
actions.list()                      # -> [name, …] (for introspection)

# ---- Checksum helper ----
can.tesla_checksum(addr, checksum_byte_index, data)
```

The exact names and shapes may evolve during phase 1 implementation.

---

## 8. Connectivity

### 8.1 Dual transport, one UI

The web UI speaks to the firmware over an abstract transport with three primitives: `connect()`, `request(cmd, payload)`, `subscribe(topic, cb)`. Two concrete implementations:

- **BLE GATT** — primary. One custom service with characteristics for request/response and notifications. Used when the UI is loaded from GitHub Pages or a local file and connects via Web Bluetooth.
- **WebSocket over HTTP** — fallback. Used when the UI is served from the ESP32's own HTTP server over WiFi.

Same UI bundle in both cases. The transport is chosen at runtime: WS if the page was served from the ESP32, BLE if hosted externally. Users can override.

### 8.2 WiFi modes

- **Station mode**: ESP32 joins a configured WiFi network (home WiFi, phone hotspot). Preferred.
- **SoftAP**: ESP32 hosts its own hotspot. Available as fallback when no station credentials are configured.

No captive portal for now; the user reaches the UI at a documented address or via mDNS (`http://dorky.local`).

### 8.3 Web Bluetooth availability

Works natively on Chrome/Edge on Android, macOS, Linux, Windows, ChromeOS. Does **not** work on iOS Safari. iOS users can use the Bluefy browser, or use WiFi + WebSocket.

### 8.4 Protocol framing

Messages between UI and firmware use CBOR (compact, schema-light). One schema shared between transports. Topics include:

- `script.list`, `script.read`, `script.write`, `script.enable`, `script.disable`, `script.delete`
- `action.list`, `action.invoke`
- `dbc.upload`, `dbc.status`
- `signal.subscribe`, `signal.unsubscribe`, `signal.inject`
- `trace.start`, `trace.stop`, `trace.frame` (notification)
- `log` (notification)
- `sim.enable`, `sim.disable`
- `ota.begin`, `ota.chunk`, `ota.finish`
- `system.info`, `system.reboot`

---

## 9. Web UI

Independent Svelte project at repo root (`webui/`), built into a static bundle. Two distribution paths from a single build:

- **Embedded**: bundle is copied into `firmware/data/webui/` by a build script and flashed to LittleFS. Served by the ESP32's HTTP server.
- **GitHub Pages**: same bundle published from `webui/dist/` by a CI workflow. Users load it once and it talks to any Dorky Commander over BLE.

The two copies are identical and chosen at runtime based on transport availability.

### 9.1 Views

- **Dashboard** — signal watch list, live values, simple graphs (uPlot), and **action tiles** (one button per registered Action).
- **Scripts** — list with enable toggles, Monaco editor, error console.
- **DBC** — upload, per-bus assignment, signal browser.
- **Trace** — live CAN frame log (both buses), filter, pause, export.
- **Diff / capture** — event-triggered capture, baseline vs. post-action diff.
- **Settings** — WiFi, presets, bus speeds, simulation mode, OTA.
- **Tesla-BLE** — pairing flow, status (optional, hidden until enabled).

### 9.2 Editor

Monaco with a custom Berry tokenizer (~100 lines) and a static completion provider generated from a TypeScript description of the scripting API. Hover docs from the same source. Compile errors shown inline, produced either by a Berry WASM build or by a round-trip to the firmware.

### 9.3 Example script gallery

`firmware/scripts_examples/` holds example `.be` files with frontmatter metadata (`@name`, `@description`, `@bus`). The web UI fetches this directory from the GitHub repo at runtime and renders it as a one-click install gallery. No separate "store" infrastructure.

---

## 10. Reverse engineering tools

Not an afterthought — must be first-class because the Tesla DBC is incomplete and many useful messages are unknown.

- **Live trace**: streaming CAN log, both buses, filters by id / signal / bus / text, pause, export as CSV or candump format.
- **Signal diff**: watch a set of signals, highlight only the ones that changed in the last N seconds. Use while performing the action you want to RE.
- **Event-triggered capture**: start recording when signal X meets condition Y, capture for N seconds, save.
- **Baseline diff**: snapshot state A, perform action, snapshot state B, show id/signal-level diff.
- **Replay**: load a capture, replay on the bus or in simulation mode.

All of these are firmware features exposed over the transport to the UI. The UI renders them.

---

## 11. Tesla-BLE integration

Optional secondary channel for the car's BLE command API. Complements CAN, does not replace it. Useful for:

- Sending commands when the car is asleep (CAN is not active then).
- Unlock / charge control actions that aren't easily expressible on CAN.

Implementation:

- A dedicated task running a Tesla-BLE client library (e.g. `tesla_ble_ctrl` or equivalent C++ library).
- Pairing flow walked through in the UI: generate keypair → request "add key" → user confirms with an existing authorized key. Documented limits: the user needs an existing phone key or keycard to approve.
- Private key stored in NVS with ESP32 flash encryption enabled.
- Exposed to scripts via a small `tesla.*` API surface (`tesla.wake()`, `tesla.charge_start()`, etc).

Deferred: not part of phase 0 or phase 1.

---

## 12. OTA

Two separate update paths:

- **Firmware OTA** — ESP-IDF native dual-partition OTA, rollback on boot failure. Upload path: HTTP(S) download from a release URL or direct file upload via the UI over WiFi. BLE OTA is possible but slow (~10+ minutes for 1 MB) and comes later.
- **Scripts / DBCs / config** — not OTA, just LittleFS file writes via the UI. Live-reload on write, no reboot required.

Partition table (draft, `partitions.csv`):

```
nvs,        data, nvs,     0x9000,  0x6000
otadata,    data, ota,     0xf000,  0x2000
app0,       app,  ota_0,   0x10000, 0x1C0000
app1,       app,  ota_1,   ,        0x1C0000
littlefs,   data, littlefs, ,        0xC0000
```

Sizes are a starting point; tune after first real build.

---

## 13. Safety guardrails

- **Per-ID TX rate limit** (hard cap, default 100 Hz).
- **Script crash watchdog** — a crashing script is isolated, not fatal.
- **Simulation mode** — routes all sends to a log; toggled from the UI.
- **Global kill switch** — disables all scripts at once. 
- **Startup delay** — scripts don't start until the CAN buses have been observed alive for a short window, so a faulty script can't send into a dead bus.
- **Flash encryption** — enabled when storing Tesla-BLE keys.
- Enable CAN transceivers standby mode

---

## 14. Testing

The goal is to make the inner dev loop flash-free. Most logic should be testable on the host machine in under a second per run. Hardware flashing is reserved for integration checks and tests that touch real peripherals.

### 14.1 Three layers

1. **Host unit tests (fast, most of the coverage)** — pure logic compiled for the dev machine. No ESP-IDF, no FreeRTOS, no TWAI driver. Run via `pio test -e native` or plain `cmake + ctest`. Covers:
   - DBC binary loader and signal decode (including multiplexed signals, endianness, scale/offset, signedness).
   - Signal cache update semantics (value / prev / age / changed flag).
   - Message mirror + encode path — "set one signal, preserve the rest".
   - Auto checksum + counter insertion (Tesla algorithm test vectors).
   - Per-ID rate limiter timing.
   - CBOR framing of the transport protocol (encode / decode round-trip).
   - Berry VM integration: load a `.be` script, inject fake signal updates, assert that the expected native binding calls were made. Berry compiles for host, so this is genuinely just unit-test speed.

2. **Target tests on real hardware (integration)** — run via `pio test -e esp32-c6-devkitm-1`, flashed and executed on the board, output captured over UART. Covers:
   - TWAI rx/tx smoke test (CAN0→CAN1 bridged loopback).
   - LittleFS mount, read, write.
   - NVS read/write.
   - BLE GATT server advertises and accepts a connection.
   - WiFi station connect + HTTP server up.
   These are slow; keep the suite small and focused on "the peripheral actually works on this silicon with this config".

3. **Web UI tests** — Vitest for unit tests of Svelte components and the transport adapter (BLE/WS mocked). Playwright for E2E against a full mock firmware that speaks the CBOR protocol over an in-process transport. No physical board required.

### 14.2 Mockable boundaries

To make host tests possible, a few interfaces must be abstracted from day one:

- **TWAI driver**: a thin internal API (`can_bus_t` with `tx`, `rx`, `init` function pointers). Real implementation uses ESP-IDF `twai_*`. Test implementation is an in-memory queue with controllable timing.
- **Clock**: anywhere we compare timestamps (rate limiter, signal age, timers), go through a `now_ms()` function that tests can fast-forward.
- **Storage**: abstract `kv_get/set` and `file_read/write` so tests run against stubs, not LittleFS/NVS.

### 14.3 Test layout

```
firmware/
├── test/
│   ├── native/                    # host unit tests (pio env: native)
│   │   ├── test_dbc_decode/
│   │   ├── test_signal_cache/
│   │   ├── test_message_encode/
│   │   ├── test_checksum/
│   │   ├── test_rate_limiter/
│   │   ├── test_cbor_protocol/
│   │   └── test_berry_bindings/
│   └── embedded/                  # on-target tests (pio env: esp32-c6)
│       ├── test_twai_loopback/
│       ├── test_littlefs/
│       └── test_ble_advertise/
webui/
├── src/
└── tests/
    ├── unit/                      # Vitest
    └── e2e/                       # Playwright
```

### 14.4 CI

GitHub Actions on every push and PR:

- `host-tests`: `pio test -e native` — runs the host suite on Linux.
- `firmware-build`: `pio run -e esp32-c6-devkitm-1` — confirms the firmware still compiles (no hardware executed).
- `webui-tests`: `npm run test` (Vitest) and optionally `npm run test:e2e` (Playwright headless).
- `webui-build`: `npm run build` — confirms the UI still bundles.

Target (on-device) tests are not part of CI because they require hardware. A manual `pio test -e esp32-c6-devkitm-1` is run locally before cutting releases. Eventually a self-hosted runner with a board attached could automate this.

### 14.5 Test-first discipline

- Every new Berry binding lands with at least one host test that loads a script and asserts behavior.
- Every new CBOR topic lands with a round-trip encode/decode test.
- Every bug fix starts with a failing host test reproducing the bug.
- Target tests are added only when the failure can't be reproduced on host.

---

## 15. Licensing

- Hardware (`hardware/LICENSE.txt` at repo root): **CERN-OHL-W v2**, already in place.
- Firmware (`firmware/LICENSE`): **GPLv3**. Matches the ethos of an open alternative to a closed commercial product. Compatible with Berry (MIT), ESP-IDF (Apache 2.0 + others), and typical ESP32 component licenses.
- Web UI (`webui/LICENSE`): **GPLv3** to match firmware, or MIT if we want the UI reusable by others. To be decided.

---

## 15. Repository layout

### Current (phase 0)

```
ESP32-DualCAN/
├── README.md
├── LICENSE.txt                     # CERN-OHL-W (hardware)
├── .gitmodules                     # Berry submodule (optional, for updating)
├── hardware/                       # KiCad project (PCB + footprints)
└── firmware/
    ├── ARCHITECTURE.md             # this file
    ├── ROADMAP.md                  # phased plan
    ├── platformio.ini              # 4 envs: esp32-c6, esp32-c6-debug,
    │                               #   esp32-c6-tests-arduino, tests-native
    ├── CMakeLists.txt              # ESP-IDF project-level cmake
    ├── sdkconfig.defaults          # USB-Serial-JTAG console, etc.
    ├── .gitignore
    ├── src/
    │   ├── CMakeLists.txt          # main component (REQUIRES driver berry)
    │   ├── main.c                  # app_main: CAN init, Berry REPL
    │   └── can/
    │       ├── can_bus.h           # thin TWAI v2 wrapper API
    │       └── can_bus.c
    ├── components/
    │   └── berry/                  # Berry scripting engine
    │       ├── CMakeLists.txt      # ESP-IDF component
    │       ├── LICENSE.berry       # MIT (upstream)
    │       ├── src/                # vendored upstream berry/src/ (38 .c files)
    │       ├── generate/           # coc-generated headers (23 files, committed)
    │       ├── port/
    │       │   ├── berry_conf.h    # our ESP32-C6 config
    │       │   ├── be_port.c       # print→ESP_LOGI, FS stubs
    │       │   └── be_modtab.c     # module table
    │       ├── scripts/
    │       │   └── update_berry.sh # regenerate from submodule
    │       └── berry/              # optional git submodule (not needed to build)
    ├── dbc/
    │   ├── tesla_model3_vehicle.dbc
    │   └── tesla_model3_y_dedup.dbc
    └── test/
        ├── unity_config.{h,cpp}        # HWCDC serial fix for test output
        ├── test_hw_led/test_led.cpp    # Arduino: RGB + GPIO15 LED smoke test
        ├── test_hw_loopback/           # Arduino: dual-TWAI loopback
        │   └── test_loopback.cpp
        └── test_native_checksum/       # host: Tesla CAN checksum algorithm
            └── test_checksum.c
```

### Planned additions (phase 1+)

```
firmware/
    ├── src/
    │   ├── dbc/                 # binary DBC loader, signal decode/encode
    │   ├── scripting/           # Berry native bindings (can, led, timer, state)
    │   ├── ble/                 # GATT server, transport
    │   ├── http/                # WiFi, HTTP server, WebSocket
    │   ├── storage/             # LittleFS, NVS helpers
    │   ├── tesla_ble/           # optional, phase 3
    │   └── ota/
    ├── partitions.csv           # OTA + LittleFS layout
    ├── scripts_examples/        # .be example automations
    └── data/                    # populated by build, flashed to LittleFS
        └── webui/               # copied from webui/dist
webui/                           # Svelte app (independent project)
    ├── package.json
    ├── src/
    └── dist/                    # build output (gitignored)
scripts/
    └── build-webui.sh           # npm build + copy into firmware/data/webui
.github/workflows/
    ├── firmware.yml             # pio build + host tests
    ├── webui.yml                # npm build + GH Pages deploy
    └── release.yml              # firmware binaries to releases
```

---

## 16. Open questions

- Final CBOR schema for the transport protocol.
- Whether the Berry compiler runs in the browser (WASM build) or on device for inline error reporting in Monaco.
- Merge strategy for the two DBC files — which one becomes canonical per bus, and whether we maintain our own curated Tesla DBC fork.
- Whether `webui/` uses GPLv3 or MIT.
- Tesla-BLE library selection.

---

## 17. Out of scope (explicit)

- S3XY Buttons interop (proprietary BLE protocol).
- Native mobile apps.
- Cloud backend.
- Physical buttons / HMI in the v1 hardware.
- Full DBC parsing on device.
