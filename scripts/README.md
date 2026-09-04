# Scripts — Gallery contributions

The **Gallery → Scripts** tab in the web UI is built at compile time from this folder. No manual registry — drop a `.be` file here and it appears as a card.

> [!NOTE]
> For now **only Tesla is supported and tested** (see `scripts/tesla/`). Other brand folders are reserved for future use.

## Where to put files

```
scripts/
  tesla/           # Tesla Model 3/Y, S/X, Cybertruck — currently supported
  toyota/          # reserved for future
  hyundai/         # reserved for future
  README.md        # this file
```

- Use `scripts/<brand>/your_script.be` (lowercase brand folder, e.g. `tesla`). The Gallery filters cards by the vehicle picked in the status bar (`app.car.brand` → `examples.ts:11` / `GalleryView.svelte:11-16`) — non-Tesla brands will appear but are not yet validated on Dual-CAN hardware.
- Also indexed: `firmware/test_scripts/*.be` (shown in the `Automations → Load…` dropdown, not Gallery).

Source: `webui/src/examples.ts:7` — `import.meta.glob(['../../scripts/tesla/*.be', '../../firmware/test_scripts/*.be'], { query: '?raw' })`.

## Required header

The first lines must be `# @` comments — they become the Gallery card title, description, and bus badge:

```berry
# @name My feature
# @description What it does, when it triggers, and which bus/signal it uses
# @bus 0
```

- `@name` — card title (fallback: filename)
- `@description` — card subtitle / search text (supports multi-line `@description` continuation)
- `@bus` — `0` or `1` (default `0`) — shown as `bus 0` badge and used for DBC preprocessing examples

See `scripts/tesla/hello_log.be` (minimal, no CAN) and `scripts/tesla/tesla_fold_mirror_tile.be` (two Dashboard tiles) for minimal examples.

## Writing the script

- Implement `def setup()` — called when enabled. Optionally `def teardown()` — called when disabled.
- Use `can_msg_get(bus, "MessageName")` / `msg_sig_get(msg, "SignalName")` with DBC names; they are rewritten at save time to numeric `sb/len/scale` args. See [`docs/scripting.md`](../docs/scripting.md) or the in-app **Automations → Scripting Guide** button for the full Berry API, timers, `action_register`, `led_set`, `state_*`, and syntax cheat sheet.
- Keep scripts focused and well-commented — they run on the ESP32-C6 with limited heap.

## Testing locally

```bash
cd webui
npm install
npm run dev   # http://localhost:5173
```

1. Pick your vehicle in the status bar car icon (or leave unfiltered to see all).
2. Open **Gallery → Scripts** — your card should appear.
3. Click **Install** → verifies it loads into **Automations** → **Save & enable** → check **Log** / **Dashboard**.

No build step — Vite inlines the `.be` contents at dev/build time.

## Tips

- Point external AI agents to [`docs/scripting.md`](../docs/scripting.md) (raw: `https://raw.githubusercontent.com/dzid26/ESP32-DualCAN/main/docs/scripting.md`) — same guide the in-app AI edit tool uses.
- Keep filenames `snake_case.be` — they are saved to the on-device LittleFS `/scripts/` path.
