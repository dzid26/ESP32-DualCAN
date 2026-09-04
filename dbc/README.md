# DBCs — Gallery definitions

> [!NOTE]
> For now **only Tesla is supported and tested** on Dual-CAN hardware (Model 3/Y — see below; Model S/X and Cybertruck community DBCs are bundled but not yet validated).

The **Gallery → DBCs** tab in the web UI is driven by `webui/src/lib/sampleData.ts:GALLERY_DBCS`. Each card is a pointer to a `.dbc` file plus metadata (bus, brands, size, source).

## Bundled vs. referenced

- **Bundled in this repo** (`dbc/`): Tesla Model 3/Y — `Model3_VEH.dbc` (bus 0, 687 msgs), `Model3_CH.dbc` (bus 1, 303 msgs), `Model3_PARTY.dbc` (46 msgs). Served by Vite at `BASE_URL/dbc/<file>` and parsed in-browser via `DbcView.svelte` / `parser.ts` worker. Selecting a Tesla vehicle in the status bar auto-loads the matching bus DBC(s). Other Tesla variants (Model S/X, Cybertruck) are also bundled as community DBCs.

- **Referenced from `commaai/opendbc`** (`source: 'opendbc'` in `sampleData.ts`): Toyota, Honda, Hyundai EV, etc. (e.g., `toyota_rav4_2019_pt.dbc`, `honda_civic_2022_can_generated.dbc`). Listed in Gallery for future use with an external link to `https://github.com/commaai/opendbc/blob/master/opendbc/dbc/<file>` — not vendored in `dbc/` and **not yet supported/tested** on this hardware.

## Adding a new DBC

Drop the `.dbc` in `dbc/<YourModel>.dbc` and add a card in `webui/src/lib/sampleData.ts:GALLERY_DBCS` with `n`, `bus`, `desc`, `file`, `brands`, `source: 'community'`, and `url: '${import.meta.env.BASE_URL}dbc/<file>'`.

Test with `cd webui && npm run dev` → pick the car in the status bar → **Gallery → DBCs** should show and **Load** the new card → verify signals appear in **DBC** view.

## Future plan — reference `commaai/opendbc` directly

We intend to stop vendoring DBCs and reference `commaai/opendbc` as the upstream source — ideally as a git submodule or a build-time fetch from `https://raw.githubusercontent.com/commaai/opendbc/master/opendbc/dbc/<file>` — so `dbc/` stays thin and tracks upstream fixes nightly (see `webui/src/lib/sampleData.ts:DBC_SOURCE` and the `GALLERY_DBCS` `opendbc` entries). Community Tesla DBCs that are not in opendbc will remain bundled in `dbc/` until upstreamed.

See [`docs/scripting.md`](../docs/scripting.md) for how the browser uses the loaded DBC for autocomplete and for preprocessing `msg_sig_get` / `msg_sig_set` signal names into numeric IDs at save time.
