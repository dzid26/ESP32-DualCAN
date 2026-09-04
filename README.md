# ESP32-DualCAN - aka Dorky Commander
Open source alternative to S3XY Commander with Molex connector compatible with Enhauto harnesses.

ESP32-C6 functions: 
- Dual CAN
- BLE for WebUI, OTA and TeslaBLE
- WiFi (file browser)
- USB-C for development (flashing, debugging)

<img width="555" alt="dorky" src="https://github.com/user-attachments/assets/3a2bcd91-30e8-4d55-b7af-4633fedc148c" />

See [`hardware/README.md`](hardware/README.md) for detailed spec and schematics.


## Flash the firmware

### Option A: Web UI (wireless OTA update)

1. Connect via BLE (see [Connect over BLE](#connect-over-ble)).
2. Go to **Settings → Firmware** in the [Dorky Commander web UI](https://dzid26.github.io/ESP32-DualCAN/).
3. Click **Upload .bin** and select a firmware `.bin` from disk (download from [GitHub Releases](https://github.com/dzid26/ESP32-DualCAN/releases) if needed).
4. The progress bar shows transfer status. The device reboots automatically when done.

### Option B: USB

**Download prebuilt binary:**

1. Download the latest `dorky-commander-vX.Y.Z.bin` from [GitHub Releases](https://github.com/dzid26/ESP32-DualCAN/releases).
2. Connect the board via USB-C. Easiest: open the [Espressif Web Flasher](https://espressif.github.io/esptool-js/) in Chrome and flash the `.bin` — no install needed.

   CLI alternative:

   ```bash
   pip install esptool
   esptool.py --chip esp32c6 write_flash 0x10000 dorky-commander-vX.Y.Z.bin
   ```

To build from source, see [firmware/README.md — Development — Build & Flash](firmware/README.md#build--flash) (PlatformIO extension recommended).

---

## Connect over BLE

1. Open the [Dorky Commander web UI](https://dzid26.github.io/ESP32-DualCAN/) in Chrome.
   *(Or install it as an app: Settings → Install app → Add to home screen.)*
2. Click **Connect** in the status bar.
3. Select **Dorky Commander** from the Bluetooth pairing dialog.
4. The status bar turns green and shows the firmware version.

> [!NOTE]
> The board advertises as `Dorky Commander`. Once paired, the pairing window stays closed for security — new devices cannot pair until you re-open it with a short-press of the B button or from the web UI on a bonded device (**Settings → Bluetooth → Open pairing window**). 


### "B" button — Bluetooth / Factory Reset

| Press | Action |
|---|---|
| Short press — release within 15 s | Open BLE pairing window for 60 s - see below |
| Hold ≥15 s | Factory reset — wipes BLE bonds, credentials, and scripts |
| Hold on Reset | ESP32 boot mode (advanced) |

## "R" button - Reset

ESP32 reset.


> [!TIP]
> If the board will be hidden behind car trim, pair with at least two devices (e.g., phone + laptop) — this avoids needing to reach the button, in case you loose a bond with the device.

See [docs/ble.md](docs/ble.md) for the full flow and troubleshooting.



---

## Choose your car

The board needs signal definitions (CAN dbc) to interpret signal `DI_vehicleSpeed` to raw IDs.

- Click the **car icon** in the top status bar and pick your vehicle (e.g., Tesla Model 3/Y). The matching DBC(s) load automatically.
- Or open **Gallery → DBCs** and click **Load** on the card for your car (choose **Bus 0** or **Bus 1** with the selector on the card if needed).
- For a custom file, open **DBC** in the left rail and click **Upload .dbc** — use the **Bus 0 / Bus 1** tabs at the top to view what's loaded for each bus.

The browser keeps the DBC locally in the browser for autocomplete and for preprocessing signal names into numeric IDs when you save a script. Verify with the search box in the DBC view.

---

## Install a script

1. Open **Gallery** in the left rail → **Scripts** tab.
2. Find **Hello log** and click **Install** — it opens **Automations** with the file preloaded (or use **Automations → Load… → Hello log**).
3. Click **Save & enable** (or **Save** then toggle). The file appears in the **Installed** list on the left; the switch shows enabled/disabled. Use **Revert** to discard edits.
4. With the script enabled, open **Log** (left rail, bottom) — you should see `hello_log: setup` followed by `heartbeat` every 5 s. `print()` from the board streams there over BLE; no CAN connection required.

5. Next, try more useful script from the **Gallery** — browse **Gallery → Scripts** for real-car automations like *Easy entry window drop*, or *Light flash → horn beep*, or pick a DBC in **Gallery → DBCs**.

---

## Use action tiles

1. Open **Gallery** in the left rail → **Scripts** tab.
2. Find **Tesla fold mirrors** and click **Install** — it opens **Automations** with `tesla_fold_mirror_tile.be` preloaded. Click **Save & enable**.
3. Return to **Dashboard**: two tiles appear — `fold_mirrors` and `unfold_mirrors`.
4. Tap a tile — the mirrors fold/unfold and the LED blinks blue. Requires Tesla Model 3/Y DBC on Bus 0 and the car awake.

## Use AI edit to write simple scripts

No coding needed — describe what you want in plain English.

1. One-time setup: Open **Settings → AI assistant** and paste your Anthropic API key — **BYOK** (Bring Your Own Key, billed to your Anthropic account). The key is stored locally in your browser and, if you click **Save to device**, also on the Dorky Commander non-volatile-storage - so any paired browser can reuse it. The browser calls Anthropic directly (`api.anthropic.com`); the key is never shared - it stays with you.

2. **Option A — from a script:** Open **Automations**, then click **AI edit** (sparkle icon) on any script. It opens **AI assistant** with that file attached as context — just type what to change.
   **Option B — from scratch:** Open **AI assistant** in the left rail directly and type a request, e.g., `Print battery temperature to log panel`

3. Press **Send** (`Ctrl/Cmd+Enter`). The assistant streams a `berry` code block (knowing your loaded DBC signals) with a proper `# @name` and `def setup()`.

4. Click **Install & enable** on the code card — it saves as `ai_script_*.be` and enables it. Check **Log** for `print()` output or **Dashboard** if it registered an action tile.

> [!TIP]
> Load a CAN definitions first (Choose your car) — the AI sees the first ~80 signal names from the browser and will use exact `message_name` / `signal_name` in `msg_sig_get`.

---

## Write your own scripts

Ready to go beyond examples?

- In **Automations**, click **Scripting Guide** (top bar, next to **New** / **Load…**) — it opens `docs/scripting.md` inside the UI with the full Berry API, CAN/DBC, timers, actions, LED, and `state_*` storage, plus a syntax cheat sheet. No tab switching needed. Point your AI agents to `docs/scripting.md` to help you out.

Start small: copy a Gallery script, tweak it in the editor, **Save & enable**, and watch **Log**.

---

## Development

### Brand scripting — adding scripts to the Gallery

See [`scripts/README.md`](scripts/README.md) for the full guide — Gallery pulls from `scripts/<brand>/*.be` at npm build time (`import.meta.glob`). Create `scripts/<brand>/your_script.be` with `# @name` / `# @description` / `# @bus` file header, implement `def setup()`, and verify in **Gallery → Scripts → Install**.

### Firmware

See [firmware/README.md — Development](firmware/README.md#development) for build, flash, and test instructions. Recommended: [PlatformIO IDE extension for VS Code](https://platformio.org/install).

### Web UI

First time
```bash
cd webui
npm install
```
Then
```
npm run dev   # open http://localhost:5173
```

Requires [Node.js](https://nodejs.org/).
