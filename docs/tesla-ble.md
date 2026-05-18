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
  - `tesla_ble.c` — mbedTLS-backed P-256 keygen, hex-encoded private key
    persisted to the `tesla` NVS namespace.
- **Protocol ops** in [firmware/src/protocol/protocol.c](../firmware/src/protocol/protocol.c):
  - `tesla.status` → `{ has_key, public_key_hex? }`
  - `tesla.gen_key` → generates a fresh keypair, returns new status
  - `tesla.reset` → wipes the keypair
- **WebUI:** [webui/src/components/views/TeslaView.svelte](../webui/src/components/views/TeslaView.svelte)
  shows key status, lets the user generate / regenerate / wipe / copy the
  public key as hex.

### Security notes

- Private scalar stays in firmware RAM + NVS only; never returned through
  any API.
- NVS isn't encrypted by default on this build. Flash encryption + secure
  boot are listed as a Phase 3 prerequisite in
  [firmware/ROADMAP.md](../firmware/ROADMAP.md).
- Hex encoding doubles storage size but matches the format Tesla's official
  tooling accepts when pasting a public key.

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
**we should not roll ourselves**. The plan is to integrate
[yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) — an MIT-licensed
C++ port of the official vehicle-command library, already used in
production by [esphome-tesla-ble](https://github.com/yoziru/esphome-tesla-ble).

### Integration sketch

**Distribution: git submodule**, matching the existing Berry pattern
(`firmware/components/berry/berry`). Submodule preferred over the
IDF Component Manager because:

- Matches the existing convention in this repo (Berry).
- Version is pinned visibly in `.gitmodules`.
- No build-time network call (initial clone only).
- The IDF Component Manager path was attempted and failed in our PIO env
  with a `uv pip install` timeout for `tool-esptoolpy` — likely a
  NixOS-specific tooling flake. Submodule sidesteps that pipeline.

Steps for Phase 2:

```bash
git submodule add -b main https://github.com/yoziru/tesla-ble.git \
    firmware/components/tesla-ble/upstream
git submodule add -b master https://github.com/nanopb/nanopb.git \
    firmware/components/nanopb/upstream
```

Then write a thin `firmware/components/tesla-ble/CMakeLists.txt`
mirroring the Berry wrapper — registers the submodule's `src/*.cpp`
and `generated/src/*.pb.c` as an IDF component, exposes its `include/`,
REQUIRES `nanopb mbedtls`. Add `tesla-ble` to `firmware/src/CMakeLists.txt`
REQUIRES so our app can include `<client.h>`.

The library brings its own keypair management via
`TeslaBLE::Client::create_private_key()` / `load_private_key()`. We have
two options:

- **Option A** — let the library own the key. Remove our `tesla_ble.c`
  keygen, swap `tesla.status` / `tesla.gen_key` to thin C wrappers over
  the library's API. Cleaner, less code, but couples our protocol op
  semantics to the library's lifecycle.
- **Option B** — keep our keygen as the source of truth, hand the
  decoded private bytes to `Client::load_private_key()` at init. Lets us
  swap libraries later without breaking the protocol API. Slightly more
  code, slightly more flexible.

Recommend Option B for the first integration. Once the library is proven
on our hardware, Option A becomes attractive.

### NimBLE config changes Phase 2 will need

- Enable BLE central role (currently peripheral-only):
  `CONFIG_BT_NIMBLE_ROLE_CENTRAL=y`
- Likely raise `BT_NIMBLE_HOST_TASK_STACK_SIZE` (currently 16 KB) once
  protobuf decode + crypto land in the same task.
- The library uses C++ STL (`std::string`, `std::unique_ptr`, etc.) — verify
  IDF's default C++ build flags don't strip exceptions/RTTI in a way that
  breaks it.

### Pairing UX (Phase 2)

The WebUI's Tesla view will grow:

- A "Pair with vehicle" button that triggers a scan + connect + add-key flow.
- Status feedback during pairing (scanning / connecting / waiting for user
  to approve in car / paired).
- VIN field once we can read it via VCSEC.

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
