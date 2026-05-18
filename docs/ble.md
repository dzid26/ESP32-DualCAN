# BLE connection process

How a Dorky Commander device gets discovered, paired, bonded, and
re-connected over BLE — and what to do when it doesn't.

## Identity

- **Stack:** NimBLE (the only BLE stack supported on ESP32-C6).
- **Address:** public Bluetooth address from the eFuse MAC (fixed per board).
- **Advertised name:** `Dorky-XXXX` where `XXXX` is the low two MAC bytes in
  hex, so multiple boards on a bench (or in an OS Bluetooth list) are
  distinguishable at a glance.
- **GATT service:** custom UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
  (Nordic UART Service convention) with two characteristics:
  - **RX** (`…0002…`) — writable; the WebUI sends commands here.
  - **TX** (`…0003…`) — notifiable; the device sends responses/events here.

## Security model

- **Pairing method:** Just Works (no PIN; `sm_io_cap = NO_INPUT_OUTPUT`).
- **Secure Connections only.** Legacy SMP is compiled out (see
  `CONFIG_BT_NIMBLE_SM_LEGACY` in `firmware/sdkconfig.defaults`). Every BLE
  4.2+ central does SC; pre-2014 BLE 4.0 centrals cannot pair.
- **Bonding** — LTK + IRK are exchanged on first pair and persisted to NVS
  via NimBLE's config store. Subsequent connections from the same central
  encrypt with the stored LTK without re-pairing.
- **Pairing window** is gated by `sm_bonding`:
  - **OPEN** — `sm_bonding=1`; any device may connect and complete bonding.
  - **LOCKED** — `sm_bonding=0`; connections from unbonded peers are
    terminated 100 ms after they land. Bonded peers connect normally and
    encrypt with their stored LTK.
- The window opens on boot when there are zero bonds (held until a bond
  succeeds), via a short BOOT button press (60 s; see [button.md](button.md)),
  via the web UI's "Open pairing window" button (60 s), or implicitly after
  `ble.reset_pairs` (held until a bond succeeds, same as boot).

## Single-client kicking

The firmware tracks only one active session (`s_conn_handle`). When a
**second authorized** device connects:

1. The new connection is accepted.
2. The old connection is terminated with `BLE_ERR_REM_USER_CONN_TERM`.
3. The web UI on the booted-out client sees its `gattserverdisconnected`
   event and shows a toast.

A **second unauthorized** device (no bond, pairing window closed) is held
silently for 100 ms then terminated with `BLE_ERR_AUTH_FAIL`; the existing
session is untouched.

## Advertising

Connectable undirected advertising at the controller's default interval.
The device **keeps advertising while connected** so a second client can find
it and (if authorized) kick the first — the "swap PC for phone" flow. On
this controller, `ble_gap_adv_start` sometimes returns `ENOMEM` while a
connection is active; in that case a single ERROR is logged and the
advertising **watchdog** (every 10 s) retries. After 60 s of unrecoverable
adv failure, the device reboots.

## End-to-end flow

### First-time pair

1. Boot. Device advertises as `Dorky-XXXX`, pairing window OPEN (no bonds).
2. User opens the WebUI in Chrome / Edge, clicks Connect, picks `Dorky-XXXX`
   from the browser's BLE picker.
3. GATT connect → `BLE_GAP_EVENT_CONNECT` fires; firmware authorizes the
   session and calls `ble_gap_security_initiate()` (this is what triggers
   the OS-level pairing dialog on Android).
4. SMP exchange completes (`pairing_complete: status=0`,
   `enc_change ... bonded=1`).
5. Pairing window closes early on bond success; bond is written to NVS.

### Subsequent reconnect (already bonded)

1. Device boots, scans bond store → ≥ 1 bond → window stays LOCKED.
2. Central reconnects, link encrypts immediately with the cached LTK.
3. `enc_change ... bonded=1` confirms.

### Failed reconnect (mismatched bond)

If the device's bond store was wiped (`ble.reset_pairs` or first flash with
clean NVS) but the OS still has the old LTK, the central will try to encrypt
with that stale key, the firmware will have nothing matching, and the link
drops with `reason=517` (HCI `0x05` AUTH_FAIL). **No fresh SMP is attempted**
— BLE spec disallows silent re-bonding because it would be a MITM hole.

The cure is OS-side: remove the device from the OS Bluetooth settings, then
reconnect. The device's pairing window auto-reopens after `reset_pairs` or
after a failed reconnect with zero bonds, so first contact then completes
fresh SMP and writes a new bond.

## Troubleshooting matrix

| Symptom | Probable cause | Fix |
|---|---|---|
| `Disconnected (reason=517)` on reconnect | OS has stale LTK after device-side wipe | Remove "Dorky-XXXX" from OS Bluetooth, reconnect |
| Quick connect → quick disconnect, never bonds | Pairing window closed and peer not yet bonded | Press BOOT (60 s) or use Settings → Bluetooth → Open pairing window |
| Boot shows `0 bond(s)` after a known-good pair | `ble_store_config_init()` not called, or full chip erase wiped NVS | Verify the call is present; ensure flash is `pio run -t upload` not `erase upload` |
| `adv start failed: 6` during a connection | Controller refuses concurrent adv+conn (ENOMEM). Watchdog retries; one log per cycle is normal | None — informational |
| Boot bootloops or BLE goes silent | `.pio/build/<env>` shadowed an old sdkconfig | `rm -rf .pio/build/<env> sdkconfig.<env>` and rebuild |
| Chrome shows old name "Dorky" after rename | OS cached the name at first bond time | Remove + re-pair to refresh |
| Cannot pair from very old laptop / OBD scanner | Pre-2014 BLE 4.0 only — no Secure Connections | Re-enable `CONFIG_BT_NIMBLE_SM_LEGACY=y` (downgrade attack surface) |

## Related code

- `firmware/src/ble/ble_transport.c` — NimBLE init, GAP events, pairing
  window, kick logic, advertising watchdog.
- `firmware/src/ble/ble_transport.h` — public API
  (`dorky_ble_notify`, `dorky_ble_unlock_pairing`, `dorky_ble_reset_pairings`,
  `dorky_ble_bond_count`).
- `firmware/sdkconfig.defaults` — `BT_NIMBLE_*` flags.
- `firmware/src/protocol/protocol.c` — `ble.status`, `ble.unlock_pairing`,
  `ble.reset_pairs` op handlers.
- `webui/src/transport/ble.ts` — Web Bluetooth client.
- `webui/src/components/views/SettingsView.svelte` — Bluetooth panel + the
  Wipe Bonds AlertDialog.
