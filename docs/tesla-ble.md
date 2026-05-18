# Tesla BLE

Local control of a Tesla via the car's BLE VCSEC (Vehicle Control Security)
service — wake, unlock, charge control, etc. — without going through Tesla's
cloud API. The protocol is the same one Tesla's phone app and the official
[teslamotors/vehicle-command](https://github.com/teslamotors/vehicle-command)
Go library use.

## Status

| Phase | Scope | Status |
|---|---|---|
| **1** | P-256 keypair generation + NVS persistence + protocol ops | ✅ landed |
| **2** | NimBLE central role + VCSEC handshake + signed command sending | 🚧 next |
| **3** | Berry script API (`tesla.wake`, `tesla.unlock`, `tesla.charge_start`…) | ⏸️ depends on Phase 2 |

## Phase 1 — what's in the tree today

The device can generate, persist, and surface a NIST P-256 keypair. This is
the *identity* Tesla's VCSEC will check signatures against, so it's the
prerequisite for everything else.

- **Firmware module:** [firmware/src/tesla_ble/](../firmware/src/tesla_ble/)
  - `tesla_ble.h` — C API: `tesla_ble_init`, `tesla_ble_has_key`,
    `tesla_ble_generate_key`, `tesla_ble_get_public_key`, `tesla_ble_reset`.
  - `tesla_ble.cpp` — thin glue over `TeslaBLE::Client::create_private_key` /
    `load_private_key`. Private key is persisted to the `tesla` NVS
    namespace as PEM (the format `mbedtls_pk_parse_key` accepts under the
    hood). Public key derived on demand via mbedtls because the library
    doesn't expose `Client::public_key` publicly.
- **Protocol ops** in [firmware/src/protocol/protocol.c](../firmware/src/protocol/protocol.c):
  - `tesla.status` → `{ has_key, public_key_hex? }`
  - `tesla.gen_key` → generates a fresh keypair, returns new status
  - `tesla.reset` → wipes the keypair
- **WebUI:** [webui/src/components/views/TeslaView.svelte](../webui/src/components/views/TeslaView.svelte)
  shows key status, lets the user generate / regenerate / wipe / copy the
  public key as hex.

### Option A vs Option B (resolved)

The Phase 2 plan below originally floated two integration shapes — own the
keygen ourselves (B) vs. let the library own it (A). **Phase 1 landed
Option A** because:

- The library already does mbedtls-backed P-256 keygen the same way our
  earlier raw-mbedtls code did.
- Going through `Client::load_private_key` keeps the in-memory `Client`
  singleton aligned with what's on disk — no risk of session state desync
  between "what we persisted" and "what the library has".
- Less code we own, less to keep in sync with upstream.

The downside (coupling our NVS format to whatever PEM the library accepts)
is acceptable: PEM is a stable mbedtls format, and the library's API
contract is narrow.

### Security notes

- Private scalar stays in firmware RAM + NVS only; never returned through
  any API.
- NVS isn't encrypted by default on this build. Flash encryption + secure
  boot are listed as a Phase 3 prerequisite in
  [firmware/ROADMAP.md](../firmware/ROADMAP.md).
- Stored as PEM (~227 bytes) rather than raw 32-byte scalar — small NVS
  cost in exchange for using the library's parsing path unchanged.

## Phase 2 — the missing handshake

The actual command path uses Tesla's VCSEC service over BLE:

1. ESP32-C6 (us) acts as a **BLE central**, scans for advertisements from the
   Tesla VCSEC service UUID, connects to the car.
2. Sends a "whitelist add key" request signed with the P-256 private key
   from Phase 1.
3. User confirms inside the car using an existing phone key or keycard.
4. After approval, every subsequent command is wrapped in a signed protobuf
   envelope (ECDH session key, sliding counter, MAC).

This is a non-trivial chunk of crypto + protobuf + NimBLE central work that
**we should not roll ourselves**. We integrate
[yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) — an MIT-licensed
C++ port of the official vehicle-command library, already used in
production by [esphome-tesla-ble](https://github.com/yoziru/esphome-tesla-ble).
It's vendored as a git submodule under
[firmware/components/tesla-ble/](../firmware/components/tesla-ble/) and
exposed as a normal IDF component (see
[CMakeLists.txt](../firmware/components/tesla-ble/CMakeLists.txt) and
[.gitmodules](../.gitmodules)).

### What the library does NOT provide

The library is transport-agnostic: it builds + parses protobuf-wrapped
signed messages and manages session state, but **the caller has to do
the BLE plumbing** (scan, connect, GATT discovery, write to RX char,
subscribe to TX char). The interface is `TeslaBLE::BleAdapter` (in
[adapters.h](../firmware/components/tesla-ble/tesla-ble/include/adapters.h)):

```c++
class BleAdapter {
  virtual void connect(const std::string &address) = 0;
  virtual void disconnect() = 0;
  virtual bool write(const std::vector<uint8_t> &data) = 0;
  // RX side: the platform calls Vehicle::on_rx_data(bytes) when a
  // notification arrives on the TX characteristic.
};
class StorageAdapter {
  virtual bool load(const std::string &key, std::vector<uint8_t> &buf) = 0;
  virtual bool save(const std::string &key, const std::vector<uint8_t> &buf) = 0;
  virtual bool remove(const std::string &key) = 0;
};
```

Phase 2 work is implementing those two adapters on top of:

- **BleAdapter** — NimBLE central role API (`ble_gap_connect`,
  `ble_gattc_disc_svc_by_uuid`, `ble_gattc_write_flat`, indicate
  subscription on the TX char). The Tesla VCSEC service UUID and char
  UUIDs are well-known from the upstream `esphome-tesla-ble` reference.
- **StorageAdapter** — thin wrapper over our existing `state.h` NVS
  helpers, namespace `tesla`. The library will store session epoch +
  counter + shared secret under each `key` it asks for.

Then `TeslaBLE::Vehicle` (in
[vehicle.h](../firmware/components/tesla-ble/tesla-ble/include/vehicle.h))
is constructed with `shared_ptr<BleAdapter>` + `shared_ptr<StorageAdapter>`
and gives us: `pair()`, `wake()`, `unlock()`, `lock()`, `set_charging_state(bool)`,
`set_charging_limit(int)`, `flash_lights()`, `honk_horn()`, full vehicle
data polls, etc. `Vehicle::loop()` is the pump — must be called
periodically from a task (it drives the command queue, retries, and
timeouts).

### NimBLE config changes Phase 2 will need

- Enable BLE central role (currently peripheral-only):
  `CONFIG_BT_NIMBLE_ROLE_CENTRAL=y`. NimBLE supports peripheral + central
  simultaneously; the existing peripheral path (WebUI BLE pairing) must
  keep working.
- Likely raise `BT_NIMBLE_HOST_TASK_STACK_SIZE` (currently 16 KB) once
  protobuf decode + crypto land in the same task.
- Confirm `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` ≥ 2 (one for the WebUI peer,
  one for the car).

### Pairing UX (Phase 2)

The WebUI's Tesla view will grow:

- A "Pair with vehicle" button that triggers a scan + connect + add-key flow.
- Status feedback during pairing (scanning / connecting / waiting for user
  to approve in car / paired).
- VIN field once we can read it via VCSEC.

### Phase 2 commit breakdown (rough)

1. NimBLE central role flag + verify peripheral still works.
2. NVS-backed `StorageAdapter` (testable in isolation).
3. `BleAdapter` scaffold — scan only, prints what it sees.
4. Connect + discover Tesla VCSEC service + write/notify wiring.
5. Wire `Vehicle` into our tesla_ble module, expose `tesla_ble_pair()`.
6. Add `tesla.pair` protocol op + WebUI button.
7. Iterate on the command set in WebUI (unlock, charge, etc.).

Steps 3+ each need a real Tesla nearby to validate; not safe to land
speculatively.

## Phase 3 — Berry script API

Once Phase 2 lands:

```berry
# Example automation: charge during off-peak hours
def maybe_charge()
  if hour() >= 23 || hour() < 7
    tesla.charge_start()
  else
    tesla.charge_stop()
  end
end
```

Wake / unlock / lock / honk / flash / climate_on / climate_off / trunk_open
/ trunk_close — basically the full
[vehicle-command CLI command set](https://github.com/teslamotors/vehicle-command/tree/main/cmd/tesla-control).

## References

- [teslamotors/vehicle-command](https://github.com/teslamotors/vehicle-command) — official Go library, the protocol source of truth.
- [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) — C++ port we plan to integrate.
- [yoziru/esphome-tesla-ble](https://github.com/yoziru/esphome-tesla-ble) — proven ESP32 use case.
- Protocol overview in the
  [vehicle-command pkg README](https://pkg.go.dev/github.com/teslamotors/vehicle-command/pkg/connector/ble).
