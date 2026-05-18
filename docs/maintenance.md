# Maintenance — recovery via USB cable

Use these when the device is inaccessible (tucked under a trim panel, no
way to reach the BOOT button) and you have a USB-C cable but no working
BLE connection.

> **Quick reminder — what the BOOT button does when you *can* reach it:**
> | Action | Effect |
> |--------|--------|
> | Short press (< 15 s) | Opens BLE pairing window |
> | Hold ≥ 15 s | Full factory reset (NVS + LittleFS + reboot) |

All `esptool` commands below are invoked through PlatformIO so you don't
need a separate esptool install.  The device auto-enters download mode on
the ESP32-C6 USB-JTAG port; no physical BOOT+RESET dance required.

---

## Partition map (reference)

```
0x009000  nvs       16 KB   WiFi creds, Tesla key, BLE bonds, settings
0x00d000  otadata    8 KB   OTA boot slot pointer
0x00f000  phy_init   4 KB   RF calibration data
0x010000  app0     1.75 MB  OTA slot 0 (firmware)
0x1d0000  app1     1.75 MB  OTA slot 1 (firmware)
0x390000  littlefs  448 KB  Scripts, DBC files, .enabled list
```

---

## Recipes

### 1. Wipe only BLE bonds (keep everything else)

BLE bonds live in a sub-namespace inside the NVS `"ble_sec"` namespace.
The cleanest approach without touching anything else is:

**Option A — via WebUI** (if you still have a BLE connection):
```
Settings → BLE → Reset pairings
```

**Option B — erase the whole NVS partition** (safe, ~1 s):

```bash
cd firmware
pio run -e esp32-c6-debug -t upload   # optional: ensure firmware is current first
python -m esptool --chip esp32c6 --port AUTO erase_region 0x9000 0x4000
```

After reboot NVS is blank, which means:
- BLE bonds gone → pairing window opens automatically on next connect
- Tesla keypair gone → regenerate from WebUI
- WiFi credentials gone → re-enter if used
- Settings gone → defaults

If you only want bonds gone and nothing else, flash your VIN/key/settings
back via WebUI immediately after reconnecting.

### 2. Wipe scripts and DBC files (LittleFS)

Erases all user scripts, DBC overrides, and the enabled-scripts list.
Firmware and NVS (bonds, key, WiFi) are untouched.

```bash
cd firmware
python -m esptool --chip esp32c6 --port AUTO erase_region 0x390000 0x70000
```

On next boot LittleFS is reformatted automatically and starts empty.

### 3. Full factory reset (NVS + LittleFS)

Equivalent to holding BOOT for 15 s.  Wipes everything user-written;
firmware OTA slots are preserved so the device boots straight into the
current firmware.

```bash
cd firmware
python -m esptool --chip esp32c6 --port AUTO erase_region 0x9000 0x4000
python -m esptool --chip esp32c6 --port AUTO erase_region 0x390000 0x70000
```

Or as a one-liner (erases both in a single connection):

```bash
cd firmware
python -m esptool --chip esp32c6 --port AUTO \
  erase_region 0x9000 0x4000 \
  erase_region 0x390000 0x70000
```

### 4. Nuke everything and reflash from scratch

Erases the entire 4 MB flash then re-flashes the current firmware.
Use this if the device is completely unresponsive or you suspect flash
corruption.

```bash
cd firmware
pio run -e esp32-c6-debug -t erase    # wipes all 4 MB
pio run -e esp32-c6-debug -t upload   # reflash firmware
```

---

## Finding the serial port automatically

`esptool` accepts `--port AUTO` which scans for a connected ESP32.  If
you have multiple devices plugged in, list ports first:

```bash
python -m esptool --port AUTO chip_id   # identifies the chip on each port
# or manually:
ls /dev/ttyACM* /dev/ttyUSB*            # Linux
ls /dev/cu.*                            # macOS
```

Then replace `AUTO` with the specific port, e.g. `--port /dev/ttyACM0`.

---

## Notes

- The ESP32-C6 enters download mode automatically over its USB-JTAG/CDC
  port — no physical BOOT pin manipulation needed for `esptool` commands.
- `erase_region` erases in 4 KB sector granularity; the sizes above are
  already aligned.
- After erasing NVS the device will log `"no keypair in NVS"` and
  `"no VIN in NVS"` at boot — that's expected.
- OTA slot selection (`otadata`) is untouched by all recipes above; the
  device will boot from whichever slot was active before.
