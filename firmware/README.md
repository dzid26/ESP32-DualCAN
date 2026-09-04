# Dorky Commander — Firmware

Open-source alternative to the S3XY Commander based on ESP32-C6 with dual CAN. Provides BLE transport for webui, scripting VM, CAN caching, wifi file server, OTA, TeslaBLE.



### Berry scripting

The firmware runs a Berry VM with LittleFS (`/scripts/*.be`) managed by `script_loader` — on boot it scans the directory, defers enabling until the CAN buses are alive (or a timeout), restores the persisted enabled set, and invokes `setup()` on enable and `teardown()` on disable when present. Scripts typically register timers, and Dashboard actions in `setup()` and optionally clean up in `teardown()`. See [`../scripts/README.md`](../scripts/README.md) for the Gallery header/skeleton and [`../docs/scripting.md`](../docs/scripting.md) for the full API.

---

## What you need

- **Dorky Commander board** (ESP32-C6-SuperMini + dual TCAN1044 transceivers)
- **USB-C cable** for first flash
- **Chrome or Chromium** (required for Web Bluetooth on all platforms)
- A Tesla (or any vehicle with a supported DBC) for CANbus interaction

See [../README.md](../README.md) for the first-use walkthrough (flashing, BLE connection, DBC, scripts, and actions).

---

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
| Board not showing in Bluetooth dialog | Use Chrome, check board is powered. |
| Script errors in log panel | Check the `error` field in Scripts list. |

| Device unresponsive after bad script | Power-cycle the board (or wait for the car to sleep). Bad scripts are isolated; others still run. |


See [`../README.md`](../README.md) for hardware schematic and pin assignments.
