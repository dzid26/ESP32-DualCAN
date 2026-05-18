# BOOT button reference

The BOOT button (GPIO 9, active-low) is the only physical control on the
board. It has two actions depending on how long it is held.

## Actions

| Hold duration | Action |
|---|---|
| Short press · release in < 15 s | Open BLE pairing window for 60 s |
| Hold ≥ 15 s | Factory reset |

### Short press — open pairing window

Identical to **Settings → Bluetooth → Open pairing window** in the web UI.
Allows a new phone, tablet, or PC to discover and pair with the device for
60 seconds. Useful when the board is installed behind a dashboard and the web
UI is unavailable.

See [ble.md](ble.md) for the full pairing flow.

### Hold 15 s — factory reset

Wipes the device back to first-run state:

- **NVS erased** — all BLE bonds, WiFi credentials, Anthropic API key,
  Tesla key, bus bitrate settings.
- **LittleFS formatted** — every uploaded script and the `.enabled` list.
- Device reboots. Pairing window opens automatically (no bonds remain).

**Before reconnecting** after a factory reset, remove "Dorky-XXXX" from
your OS Bluetooth settings. The OS still holds the old LTK; the device no
longer recognises it and will reject the re-connect silently. Removing the
old pairing lets the OS start a fresh bond negotiation.

The same reset is also available in the web UI under
**Settings → Danger zone → Erase NVS + scripts**.

## LED countdown during hold

The blue LED gives feedback while BOOT is held so you can abort before the
point of no return:

| Hold time | LED |
|---|---|
| 0 – 3 s | Normal (CAN activity / pairing window indicator) |
| 3 – 10 s | Blinks every 300 ms — warning, release to cancel |
| 10 – 15 s | Blinks every 100 ms — imminent, release to cancel |
| 15 s | Solid — factory reset triggered |
