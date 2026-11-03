# Agent notes

Repo-specific facts that are easy to get wrong, and where the real docs live.
The READMEs own the detail; this file only points and warns.

## WebUI checks in a fresh worktree

`webui/node_modules` is gitignored and not shared between worktrees.

- `npm test` needs no install — the mocked tier imports no packages.
- `npm run check`, `npm run dev` and `npm run build` install dependencies on
  their first run through their `pre*` hooks (`npm ci`, ~16 s and 100 MB per
  worktree), then do nothing on later runs.
- Never point `webui/node_modules` at another checkout with a junction:
  `npm ci` deletes packages one by one and would delete files in that checkout.

See [webui/README.md — Git worktrees](webui/README.md#git-worktrees).

## Where things are documented

| Area | Doc |
| --- | --- |
| Product, flashing, BLE pairing, scripts, development entry points | [README.md](README.md) |
| WebUI setup, scripts, test tiers, CI | [webui/README.md](webui/README.md) |
| Firmware build, flash, monitor, native tests | [firmware/README.md](firmware/README.md) |
| Hardware spec and schematics | [hardware/README.md](hardware/README.md) |
| Gallery script format | [scripts/README.md](scripts/README.md) |
| BLE protocol, Tesla BLE, scripting API | [docs/ble.md](docs/ble.md), [docs/tesla-ble.md](docs/tesla-ble.md), [docs/scripting.md](docs/scripting.md) |
| Common commands (firmware tests, webui tests, type check) | [Makefile](Makefile) |
