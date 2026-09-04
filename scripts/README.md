# Scripts — Gallery contributions

The **Gallery → Scripts** tab in the web UI is built at compile time from this folder. No manual registry — drop a `.be` file here and it appears as a card. For the full Berry API, see [`docs/scripting.md`](../docs/scripting.md) (also opened via **Automations → Scripting Guide**).

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

Minimal skeleton (from `firmware/README.md`):

```berry
# @name My script
# @description What it does

def setup()
  # called when the script is enabled
  timer_every(1000, def()
    print("tick")
  end)
end

def teardown()
  # called when the script is disabled (optional)
end
```

See `scripts/tesla/hello_log.be` (minimal, no CAN) and `scripts/tesla/tesla_fold_mirror_tile.be` (two Dashboard tiles) for minimal examples.

## Writing the script

- The runtime invokes `setup()` on enable and `teardown()` on disable when present — a typical script registers timers, CAN callbacks, or Dashboard actions in `setup()` and releases them in `teardown()`.
- Use `can_msg_get(bus, "MessageName")` / `msg_sig_get(msg, "SignalName")` with DBC names; they are rewritten at save time to numeric `sb/len/scale` args. See [`docs/scripting.md`](../docs/scripting.md) or the in-app **Automations → Scripting Guide** button for the full Berry API, timers, `action_register`, `led_set`, `state_*`, and syntax cheat sheet.
- Don't do too crazy stuff in the scripts — they run on the ESP32-C6 with limited heap and CPU. Despite protections it's possible to get locked out from the BLE webui and need USB connection to factory reset in worst case.

New scripts will appear in the gallery. No store update needed — Vite automatically pulls `.be` contents at dev/build time.

## Tips

- Point external AI agents to [`docs/scripting.md`](../docs/scripting.md) (raw: `https://raw.githubusercontent.com/dzid26/ESP32-DualCAN/main/docs/scripting.md`) — same guide the in-app AI edit tool uses.
- Keep filenames `snake_case.be` — they are saved to the on-device LittleFS `/scripts/` path.
