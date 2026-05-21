# BLE — connecting to Dorky Commander

## Can't connect?

Start here before reading anything else.

1. **Is the device powered?** Check the blue LED — it should be on or blinking.
2. **Remove the old pairing from your OS.** Go to OS Bluetooth settings, find
   "Dorky-XXXX", click Forget / Remove. Then try connecting again. This fixes
   the majority of failed reconnect cases.
3. **Open the pairing window.** If the device has at least one existing bond,
   new devices are blocked. Press the BOOT button (short press, release within
   15 s) — the blue LED goes solid for 60 s while the window is open. Or use
   **Settings → Bluetooth → Open pairing window** if you still have a working
   BLE connection from another device.
4. **Still stuck?** See the [Troubleshooting scenarios](#troubleshooting-scenarios)
   below for your specific symptom.

---

## Troubleshooting scenarios

### Can't reconnect after a device wipe or factory reset

Symptom: connected before, now the browser gets an immediate disconnect.
The serial log says `reason=517` (HCI AUTH_FAIL).

The OS is trying to re-use a saved encryption key (LTK) that no longer exists
on the device.

1. On your phone/PC, open **OS Bluetooth settings**.
2. Find **Dorky-XXXX**, tap **Forget** / **Remove device**.
3. Reconnect from the WebUI — a fresh pairing handshake will run.

The device's pairing window re-opens automatically after any bond wipe, so
you don't need to press BOOT.

### New device can't pair — connects then drops immediately

The pairing window is locked (another bond already exists on the device).

1. On a device that *is* already bonded, open the WebUI → **Settings →
   Bluetooth → Open pairing window** (60 s).
2. Or press the **BOOT button** (short press) to open the window.
3. Connect from the new device within 60 s.

### Chrome shows the wrong name ("Dorky" instead of "Dorky-XXXX")

The OS cached the name from before the device was renamed. This is cosmetic
only — the connection still works.

Fix: Forget the device from OS Bluetooth settings and re-pair.

### Device not visible in the picker at all

- Confirm the device is powered (blue LED).
- Try moving closer — BLE range through metal trim is limited.
- If a different device is already connected, the device still advertises but
  some browsers / OS versions filter out already-connected devices. Disconnect
  the other session first, or use a different browser profile.

---

## BOOT button

The BOOT button (GPIO 9, active-low) is the only physical control on the board.

| Hold duration | Action |
|---|---|
| Short press — release within 15 s | Open BLE pairing window for 60 s |
| Hold ≥ 15 s | Factory reset (wipe NVS + scripts, reboot) |

### Short press — open pairing window

Equivalent to **Settings → Bluetooth → Open pairing window**.
The blue LED goes solid while the window is open.
Use this when the web UI is unreachable and you need to pair a new device.

### Hold 15 s — factory reset

Wipes the device back to first-run state:

- **NVS erased** — BLE bonds, WiFi credentials, Anthropic API key, Tesla key,
  bus bitrate settings.
- **LittleFS formatted** — every uploaded script and the `.enabled` list.
- Device reboots. Pairing window opens automatically (no bonds remain).

**After a factory reset,** remove "Dorky-XXXX" from your OS Bluetooth settings
before reconnecting — see [Can't reconnect after wipe](#cant-reconnect-after-a-device-wipe-or-factory-reset).

### LED countdown during hold

| Hold time | Blue LED |
|---|---|
| 0 – 3 s | Normal (pairing window solid / CAN blink) |
| 3 – 10 s | Blinks every 300 ms — warning, release to cancel |
| 10 – 15 s | Blinks every 100 ms — imminent, release to cancel |
| ≥ 15 s | Solid — factory reset triggered |

---

## Device is hidden — USB cable recovery

If the board is behind a trim panel and the BOOT button is out of reach,
a USB-C cable is enough. The ESP32-C6 enters download mode automatically
over its USB-JTAG port — no BOOT+RESET pin dance required.

All commands run from the `firmware/` directory. Replace `AUTO` with a
specific port (e.g. `/dev/ttyACM0`, `COM3`) if you have multiple devices.

### Simplest - nuke everything and reflash

```bash
cd firmware
pio run -e esp32-c6-debug -t erase   # wipes all 4 MB
pio run -e esp32-c6-debug -t upload  # reflash current firmware
```


---

## Technical reference

### Identity

- **Stack:** NimBLE (the only BLE stack supported on ESP32-C6).
- **Address:** public Bluetooth address from the eFuse MAC (fixed per board).
- **Advertised name:** `Dorky-XXXX` where `XXXX` is the low two MAC bytes in
  hex — multiple boards are distinguishable at a glance.
- **GATT service:** custom UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
  (Nordic UART Service convention):
  - **RX** (`…0002…`) — writable; WebUI sends commands here.
  - **TX** (`…0003…`) — notifiable; device sends responses and events here.

### Security model

- **Pairing:** Just Works (no PIN; `sm_io_cap = NO_INPUT_OUTPUT`).
- **Secure Connections only.** Legacy SMP is compiled out
  (`CONFIG_BT_NIMBLE_SM_LEGACY` disabled in `sdkconfig.defaults`).
  BLE 4.2+ devices pair normally; pre-2014 BLE 4.0 hardware cannot pair.
- **Bonding** — LTK + IRK exchanged on first pair, persisted to NVS.
  Subsequent connections encrypt immediately with the stored LTK.
- **Pairing window** (`sm_bonding` flag):
  - **OPEN** — any device may connect and complete bonding.
  - **LOCKED** — unbonded peers are terminated 100 ms after connecting;
    bonded peers encrypt normally.
  - Opens on boot when no bonds exist (held indefinitely until a bond forms),
    for 60 s after a BOOT button short press or `ble.unlock_pairing` op,
    and indefinitely after `ble.reset_pairs`.

### Single-client kicking

The firmware tracks one active session. When a second **bonded** device
connects, the existing connection is terminated gracefully
(`BLE_ERR_REM_USER_CONN_TERM`). An **unbonded** device with the window locked
is silently dropped after 100 ms — the active session is untouched.

### Advertising

The device advertises while connected so a second authorized client can
discover it ("swap PC for phone" flow). On this controller,
`ble_gap_adv_start` occasionally returns `ENOMEM` while a connection is
active; the advertising watchdog (runs every 10 s) retries silently. After
60 s of unrecoverable failure the device reboots.

### Troubleshooting matrix (developer)

| Symptom | Cause | Fix |
|---|---|---|
| `reason=517` on reconnect | OS has stale LTK after device-side wipe | Forget device in OS Bluetooth, reconnect |
| Quick connect → drop, never bonds | Pairing window closed | Short-press BOOT or use Settings → Bluetooth |
| `adv start failed: 6` during a connection | Controller ENOMEM (normal) | Informational — watchdog handles it |
| Bootloop / BLE silent after sdkconfig change | PIO cache shadowed old sdkconfig | `rm -rf .pio/build/<env> sdkconfig.<env>` |
| OS shows old name "Dorky" | OS cached name at bond time | Forget and re-pair |
| Pre-2014 device can't pair | BLE 4.0, no Secure Connections | Re-enable `CONFIG_BT_NIMBLE_SM_LEGACY=y` |

### Related code

- `firmware/src/ble/ble_transport.c` — NimBLE init, GAP events, pairing
  window, kick logic, advertising watchdog.
- `firmware/sdkconfig.defaults` — `BT_NIMBLE_*` flags.
- `firmware/src/protocol/protocol.c` — `ble.status`, `ble.unlock_pairing`,
  `ble.reset_pairs` op handlers.
- `webui/src/transport/ble.ts` — Web Bluetooth client.
- `webui/src/components/views/SettingsView.svelte` — Bluetooth panel.
