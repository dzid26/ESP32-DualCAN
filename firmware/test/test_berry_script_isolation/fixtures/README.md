# 0x334 flood fixtures

Runtime copies of the two scripts that were enabled together when the Tesla
chassis bus flooded with `0x334` (route `ef274e718d23a289/000001c7--07e54db434`).

- `pedal_stopping_mode.bep` is the runtime file that was on the device. It
  mirrors every new `UI_powertrainControl` (0x334) and keeps a top-level
  `last_counter` guard.
- `track_mode_on_full_throttle.be` is the device version (it also declares
  `last_counter`). The gallery copy in `scripts/tesla/` does not have that
  guard, so the fixture keeps the device source next to its runtime file.
- The `.bep` files are preprocessed output from `webui/src/lib/preprocessor.ts`
  with the repo DBCs. `pedal_stopping_mode.bep` is the exact file recovered
  from the device; `track_mode_on_full_throttle.bep` was regenerated from the
  source above.

The host suite installs each pair as `<name>.be` + `<name>.bep` in
`berry_test_scripts/`, then runs the real `script_loader_scan/enable` and timer
loop. The `.be` placeholder only exists so the scan finds the script; enable
loads the `.bep`.
