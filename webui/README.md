# Dorky Commander WebUI

Svelte 5 + Vite PWA (`webui/`) that talks to the firmware over BLE. This is the
app published at <https://dzid26.github.io/ESP32-DualCAN/>. See the
[repo README](../README.md) for what the product does.

## Quick start

```bash
cd webui
npm install
npm run dev        # http://localhost:5173
```

Requires Node.js 22+ (CI pins 22).

## Scripts

| Script | What it does |
| --- | --- |
| `npm run dev` | Vite dev server on <http://localhost:5173>. |
| `npm run host` | Same, bound to the LAN (`vite --host`) so a phone can reach it. |
| `npm run tunnel` | Exposes port 5173 publicly with [tunnelmole](https://github.com/robbie-cahill/tunnelmole-client). Not a dependency — install it first (`npm i -g tunnelmole`). |
| `npm run build` | Production build into `webui/dist/`. |
| `npm run preview` | Serves the built `dist/` locally. |
| `npm run check` | `svelte-check` type check (`tsconfig.app.json`). |
| `npm test` | Mocked BLE tier — `node --test tests/*.test.ts`, no browser, no hardware. |
| `npm run test:ble:real` | Real-hardware Web Bluetooth tier (headful Playwright). |
| `npm run test:ble:real:install` | Forced download of Playwright's Chromium (normally automatic, see below). |

Notes:

- Web Bluetooth needs a secure context. `localhost` is trusted, but a plain-HTTP
  LAN IP is not — use `npm run tunnel` (public HTTPS) or the deployed site to
  exercise BLE from a phone.
- `npm run build` honors `VITE_BASE` (e.g. `VITE_BASE=/ESP32-DualCAN/`) for
  sub-path hosting; CI sets it for GitHub Pages.
- The dev server and the build serve repo-root assets: `dbc/` at `/dbc` and
  `docs/` at `/docs`, copied into `dist/dbc` and `dist/docs` at build time
  (see `vite.config.ts`).
- `public/recovery.html` is a standalone panic-recovery page (open
  `/recovery.html`): it connects without subscribing to notifications, lists
  every script on the device, and disables them — for when a runaway `print()`
  has flooded BLE.
- `capacitor.config.ts` holds native app-shell metadata (`appId`, `webDir`); no
  Capacitor CLI/platform packages are installed, so there is no native build
  step yet.

## Tests

### Mocked tier (default)

```bash
npm test              # equivalent to: node --test tests/*.test.ts
```

Plain Node — no browser and no radio. It covers the protocol round-trip, DBC
preprocessing, capture, examples, and the BLE transport/store optimistic and
reconnect logic against a mocked `BleClient` (device discovery, GATT connect,
disconnect classification, backoff with the delay injected so nothing waits on
the real timer). Test-only shims live in `tests/helpers/`: `test-globals.ts`
fakes `localStorage` and Svelte 5 runes, and `register-ts-resolver.ts` resolves
Vite-style extensionless imports for `node --test`.

### Real-hardware BLE tier (not part of `npm test`)

`tests/e2e/ble-real-device.spec.ts` is a Playwright test that drives the native
Web Bluetooth chooser over CDP against **real hardware** — a Dorky
(`Dorky-XXXX`, matched case-insensitively on the `dorky` prefix).

```bash
# run against hardware (default; a visible Chromium window opens)
npm run test:ble:real
```

Playwright's Chromium is downloaded automatically on the first run if missing
(`globalSetup` in `playwright.ble.config.ts`; skipped in an automated context).
`npm run test:ble:real:install` still forces the download explicitly.

- **Headful, and not your daily Chrome.** Playwright launches its own bundled
  Chromium with a visible window (`playwright.ble.config.ts` sets
  `headless: false`, which the native chooser requires). The mocked tier opens
  no browser at all.
- **Runs by default** for a developer, and **self-skips** when no device
  matching the prefix is discovered within `BLE_REAL_SCAN_MS`: the prompt is
  cancelled and the run reports skipped. No hardware is not a failure — a hard
  failure is reserved for genuine assertions once a device has been selected
  and connected (e.g. `requestDevice()` resolves but GATT fails).
- **Skipped automatically** when `CI` or `BLE_SKIP_REAL_DEVICE` is truthy, before
  any browser launches.

Force-run when something exports `CI`:

```powershell
# PowerShell
Remove-Item Env:CI -ErrorAction SilentlyContinue
$env:BLE_SKIP_REAL_DEVICE = '0'
npm run test:ble:real
```

```bash
# POSIX
env -u CI BLE_SKIP_REAL_DEVICE=0 npm run test:ble:real
```

Force-skip (reports skipped; launches no browser):

```powershell
# PowerShell
$env:BLE_SKIP_REAL_DEVICE = '1'
npm run test:ble:real
```

```bash
# POSIX
BLE_SKIP_REAL_DEVICE=1 npm run test:ble:real
```

| Env var | Default | Purpose |
| --- | --- | --- |
| `BLE_SKIP_REAL_DEVICE` | unset | Truthy → skip the tier (force-skip). |
| `CI` | unset | Truthy → skip the tier (automatic in CI). |
| `BLE_REAL_MATCH` | `dorky` | Case-insensitive advertised-name prefix to authorize. |
| `BLE_REAL_SCAN_MS` | `30000` | Chooser scan timeout before a no-hardware skip. |
| `BLE_REAL_REBOOT_MS` | `5000` | Wait before the simulated-reboot reconnect. |

**Required Chromium launch args.** Playwright's Chromium must be launched with
`--enable-features=WebBluetooth,WebBluetoothNewPermissionsBackend`. The
`WebBluetoothNewPermissionsBackend` flag is required for
`navigator.bluetooth.getDevices()` — the silent-reconnect primitive — which is
otherwise `undefined` on Chromium 153. It is set in `playwright.ble.config.ts`.

**What it covers:** connect via the native chooser → GATT → drop → silent
reconnect with `navigator.bluetooth.getDevices()` + `gatt.connect()` (no
chooser; the same path the app's auto-reconnect uses), and the same flow after a
simulated reboot delay. Guardrails: only the authorized prefix is selected
(never a fallback), an ambiguous match cancels the prompt and lists candidates,
and OS Bluetooth/adapter state is never modified.

**Limitations.** The mocks cannot cover the real radio stack: chooser and
permission behaviour, OS bonds, stale keys, GATT MTU/timeouts, or actual
re-advertise timing. The real tier speaks raw Web Bluetooth rather than the
Capacitor plugin to stay app-build-free, and drives the real native chooser
because emulated `gatt.connect()` does not reliably settle on Chromium 153.

## CI

`.github/workflows/webui.yml` runs when `webui/**` (or the workflow) changes:

- **test** job: `npm ci`, `npm run check`, `npm test`, with
  `BLE_SKIP_REAL_DEVICE: "1"` as defense in depth. `npm test` globs
  `tests/*.test.ts`, so `tests/e2e/*.spec.ts` is never collected.
- **deploy** job (push to `main` only): `npm run build` with `VITE_BASE`, then
  publishes `webui/dist` to GitHub Pages.

There is no committed hook framework (`.git/config` points `core.hooksPath` at
`scripts/git-hooks`, which does not exist; there is no `.husky/` or
`lefthook.yml`). If hooks are added, point their test step at `npm test` — do
**not** wire `npm run test:ble:real` into a hook or CI.
