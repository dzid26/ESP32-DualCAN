# Dorky Commander — Roadmap

Phased plan from current state (hardware done, loopback test) to a shippable v1. Each phase ends with something demonstrable. Earlier phases are specified in more detail; later phases are sketches that will be refined as we learn.

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for the design this plan implements.

---

## Phase 0 — Project skeleton and Berry loopback demo

**Goal**: prove the whole stack (ESP-IDF build + dual TWAI + Berry VM + native bindings + LED) works end-to-end with a tiny demo script. No BLE, no WiFi, no DBC, no web UI.

**Demo at end of phase**: a Berry script running on the board does this: every 1 s, TX a known frame on CAN0. When the same frame is received on CAN1 (buses bridged with a jumper and 120 Ω terminator), light the LED green for 100 ms. On timeout, light red. Additionally, a second script forwards all CAN0 frames to CAN1 unchanged (a pass-through bridge).

### Tasks

1. Rename `Firmware/` → `firmware/`, move DBCs into `firmware/dbc/`, add `firmware/LICENSE` (GPLv3).
2. Create ESP-IDF PlatformIO project skeleton in `firmware/`:
   - `platformio.ini` targeting `esp32-c6-devkitm-1` with `framework = espidf`.
   - `sdkconfig.defaults` with FreeRTOS tick, TWAI enabled, LittleFS enabled.
   - Draft `partitions.csv` (2× OTA app slots + NVS + LittleFS).
3. Port `TempFirmwareTest/src/main.cpp` TWAI bringup into `src/can/`:
   - `can_bus_init(bus_id, tx_pin, rx_pin, stb_pin, bitrate)`
   - FreeRTOS rx task per bus that pushes frames into a FreeRTOS queue consumed by the scripting task.
   - `can_send(bus_id, id, data, len)` enqueues into a per-bus TX queue; TX task drains it.
4. Vendor Berry into `lib/berry/` from upstream, add to the build via a CMakeLists.
5. Start the Berry VM in a dedicated FreeRTOS task; wire a work queue so CAN rx events and timer expiries become Berry callback invocations.
6. Implement minimal native bindings:
   - `can.raw.on_frame(bus, id, fn)`
   - `can.raw.send(bus, id, data)`
   - `led.set(r, g, b)`, `led.off()`
   - `log(msg)` → UART
   - `timer.after(ms, fn)`, `timer.every(ms, fn)`
7. Hardcode a demo script compiled into flash (no LittleFS yet) that implements the loopback LED demo and the bridge.
8. Document the physical test jig (bridge CAN0 H/L to CAN1 H/L with one 120 Ω across).
9. **Testing infrastructure set up from day one**:
   - `test/native/` and `test/embedded/` folders, `[env:native]` in `platformio.ini`.
   - Abstract the TWAI driver behind a `can_bus_t` interface so a host mock can be injected.
   - Abstract `now_ms()` so tests can control time.
   - First host tests: Tesla checksum against known vectors, rate limiter timing with fake clock, a trivial Berry binding test (load a script, fire a mock frame, assert a log call).
   - GitHub Actions workflow `host-tests.yml` running `pio test -e native` on every push.

### Exit criteria

- `pio run` builds clean.
- Board flashes, boots, TWAI rx/tx both buses healthy (TEC stays 0, rx counters increment).
- LED blinks green on received loopback frames; red on timeout.
- Forwarding works — external CAN traffic injected on CAN0 reappears on CAN1.
- Build is reproducible with a one-line command.

---

## Phase 1 — DBC signals, BLE transport, minimal web UI, first real automation

**Goal**: get from "raw frame loopback" to "user edits a Berry script in a browser, flashes it over BLE, and the car does something".

**Demo at end of phase**: user opens the web UI on a phone, connects to the board over BLE, uploads a compiled DBC for bus 0, pastes the window-drop-on-long-handle-pull script, enables it, and it works in the car.

### Tasks

1. **Binary DBC loader** (firmware side):
   - Define the binary format from the spec.
   - Loader reads from `/dbc/bus0.bin`, `/dbc/bus1.bin` on LittleFS.
   - Signal decode including multiplexed signals.
   - Signal cache with prev/value/age_ms/changed.
2. **Message mirror and encode path**:
   - Keep latest rx bytes per known message id.
   - `can.message(name)` returns a draft; `.set()` modifies bits for one signal; `.send()` emits with auto checksum/counter (name-suffix based, Tesla algorithm).
3. **Berry bindings for decoded signals**:
   - `can.signal(name [, bus])`, `can.on(name, fn [, bus])`.
   - `can.message(name [, bus]).set().get().send()`.
   - `can.tesla_checksum(...)` helper.
4. **LittleFS script loader**:
   - Scripts under `/scripts/*.be`, each with `@name`, `@description`, `@bus` frontmatter and `setup()` / `teardown()`.
   - Enable flags in NVS or a `/scripts/enabled.json`.
   - Crash isolation: one script failing doesn't take down others.
5. **NVS-backed state**:
   - `state.get`, `state.set`, `state.remove` per-script namespace.
6. **BLE GATT transport**:
   - Single custom service, request/response and notification characteristics.
   - CBOR framing.
   - Topics needed for phase 1: `system.info`, `script.list`, `script.get`, `script.put`, `script.enable`, `script.disable`, `dbc.upload`, `log` (notify), `signal.subscribe`, `signal.value` (notify).
7. **Web UI v0**:
   - Svelte 5 + Vite + TS project at `webui/`.
   - Transport abstraction with BLE adapter only (WS later).
   - CodeMirror 6 editor with a basic Berry mode and static completion from the API.
   - Three views: Scripts (edit/enable), DBC (upload), Dashboard (signal watch).
   - GH Pages deployment workflow.
8. **Browser-side DBC compiler**:
   - JS/TS parser for DBC text.
   - Emits the binary format defined in the spec.
   - Uploads via `dbc.upload`.
9. **Safety**:
   - Per-ID TX rate limiter (default 100 Hz).
   - Startup delay (wait for rx activity before enabling user scripts).
   - Simulation mode toggle that diverts sends to a log.
10. **First real automation** (`scripts/tesla/window_drop_on_long_handle.be`):
    - Subscribe to door open and handle pull signals on bus 0.
    - On door-open + handle-pulled-sustained-2s, send window-down command.
    - Includes the actual Tesla signal and message names once we identify them in the DBC.
11. **Tests for phase 1 features**:
    - Host: DBC binary decode vs. a set of reference frames (multiplexed signals included), message encode preserving untouched bits, auto checksum + counter on send, CBOR round-trip for every new topic.
    - Host: Berry script test that loads `window_drop_on_long_handle.be` from test_scripts, fakes signal updates, asserts the correct TX message is produced — all without flashing.
    - Embedded: LittleFS mount + script read, NVS round-trip, BLE advertise smoke test.
    - Web UI: Vitest unit tests for the BLE transport adapter against a mocked GATT layer.

### Exit criteria

- Web UI on Chrome/Android connects to the board over Web Bluetooth.
- User can upload a DBC, edit/enable a script, see log output, see live signal values.
- Window-drop automation triggers correctly in the car (or in simulation mode with injected signals).
- Bad scripts don't brick the device; crash is logged and isolated.

---

## Phase 2 — Reverse engineering toolkit, PWA install, second automation

**Goal**: make the board useful for discovering unknown messages. Ship the trace/diff/capture RE toolkit, add PWA installability so users can pin the GH Pages app to their home screen or desktop, and ship the track-mode automation as a second example.

**Note**: WiFi + HTTP server dropped — users install the GH Pages web app as a PWA instead. BLE is the transport; no WebSocket layer needed.

**Demo at end of phase**: user pins the web app to their phone home screen, connects to the board over BLE, performs an action in the car, and the trace/diff view highlights exactly which signals changed.

### Tasks

1. **PWA install button**:
   - `manifest.json` with name, icons, `display: standalone`, `theme_color`.
   - Minimal service worker (enables installability; cache-first optional later).
   - Capture `beforeinstallprompt` in app state; expose "Add to home screen" button in Settings.
   - Detect already-installed (standalone mode) to hide the button.
2. **Live trace view** *(largely done — wired in TraceView.svelte)*:
   - Firmware: `trace.start` / `trace.stop` topics ✓, push `{type:'trace'}` ✓.
   - UI: table, filters, pause, export to candump/CSV ✓.
3. **Signal diff mode**: UI-side, subscribe to a set of signals and highlight changes within a sliding window.
4. **Event-triggered capture**: UI-driven, starts/stops trace based on a signal predicate, saves to a file.
5. **Baseline diff tool**: UI-side, snapshot A / perform action / snapshot B / render diff.
6. **Replay**: load a saved capture, send frames in order (respects timestamps or at fixed rate), works in simulation mode too.
7. **Second automation** (`test_scripts/track_mode_on_full_throttle.be`):
   - Uses `UI_trackModeRequest` signal from the larger DBC.
   - Holds full-throttle detection and edge debouncing.
8. **Signal graphs on the Dashboard** *(uPlot — partially scaffolded in DashboardView.svelte)*.
9. **Tests for phase 2 features**:
   - Host: trace buffer behavior under load, event-capture trigger evaluation, replay frame ordering with fake timestamps.

### Exit criteria

- User can install the web app from GH Pages on Android / desktop and use it fully over BLE.
- iOS user on Bluefy can fully use the tool.
- A previously unknown message can be identified end-to-end using the RE tools alone.
- Track-mode script works on a Performance/Plaid car.

---

## Phase 3 — OTA, polish, Tesla-BLE, v1 release

**Goal**: ship a v1.0.0 release people can install without reading source.

### Tasks

1. **Firmware OTA** ✓:
   - ESP-IDF dual-partition OTA ✓, BLE OTA ✓ (`ota.begin/write/end/abort` + `streamFirmware`).
   - UI ✓ — upload `.bin` from disk or fetch from GitHub releases; progress bar, abort, reboot toggle.
2. **Example script gallery** ✓ — UI + bundled scripts wired; Install loads script into editor.
3. **Settings view** — WiFi ✓, sim mode ✓, OTA ✓; bitrate/rate-limit overrides need new protocol ops.
4. **Tesla-BLE integration** (optional, can slip to v1.1):
   - Dedicated FreeRTOS task running a Tesla-BLE client library.
   - Pairing flow in the UI.
   - Private key in NVS with flash encryption enabled.
   - Minimal `tesla.*` script API (`wake`, `charge_start`, `charge_stop`).
5. **Release plumbing** ✓:
   - `firmware.yml` — builds + host tests on every firmware push ✓.
   - `release.yml` — v* tag → build → GitHub release with `.bin` ✓.
   - `webui.yml` — test + type-check + GH Pages deploy on main push ✓.
6. **AI assistant** ✓ — BYOK Claude chat; streams responses; code blocks become installable scripts.
7. **Documentation** — user-facing `firmware/README.md` with flash + first-use walkthrough.

### Exit criteria

- A person who never saw the repo can flash the prebuilt firmware, open the GH Pages UI, pair over BLE, upload a DBC, enable an example automation, and have it work — with only `firmware/README.md` as documentation.
- v1.0.0 tag cut, release assets published.

---

## Phase 4 — Post-v1 gimmicks

**Goal**: the fun stuff that has no business being in v1 but is too good to forget. Everything here is optional, low-priority, and allowed to be janky.

### Tasks

1. **Browser-rendered engine sound emulation**. ESP32-C6 has no BT Classic, so on-device A2DP is out. Instead, the web UI streams `{speed, torque, rpm_est}` from the firmware over WS/BLE at ~50 Hz and synthesizes engine audio in the browser via Web Audio API (AudioWorklet granular playback of sample loops, pitch-shifted by a derived rpm, load/coast layers crossfaded by torque sign). The phone's existing A2DP pairing to the car stereo plays it — no extra BT stack on the device. Engine profiles (V8, I4 turbo, EV whine) are JSON + sample packs hot-loaded in the UI. Accuracy doesn't matter; it just has to feel dumb and fun. Known caveats to punt on: iOS Safari background audio suspension (workaround: PWA + `MediaSession` + silent keep-alive), WS latency over WiFi (fine), phone screen-on drain (user problem).

2. 
- **AI-assisted script editor** (client-side only). Chat panel in the web UI where the user describes an automation in plain English; an LLM (Claude API, user's own key stored in localStorage) generates the Berry script. System prompt includes the Berry API reference, the user's loaded DBC signals, and example scripts as few-shot context. Error logs fed back automatically for self-correction. No cloud backend — the API call happens in the browser. Berry's small, purpose-built API surface makes this a well-constrained code-gen problem. Depends on: stable Berry API, DBC signal browser, example gallery, web UI script deployment.
---

## Out of roadmap (parking lot)

- Blockly block-based script editor generating Berry code.
- S3XY Buttons interop (would require reversing their BLE protocol).
- Support for other vehicles beyond Tesla (structurally supported, but no presets shipped).
- Cloud relay / remote access.
- Native mobile apps.
- On-device Berry compile-error reporting via a Berry WASM build (nice-to-have for better editor integration).
