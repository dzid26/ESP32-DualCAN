# ESP32-DualCAN
Dorky Commander - open source alternative to S3XY Commander. 

Uses Molex connector compatible with Enhauto harnesses.

See **[docs/scripting.md](docs/scripting.md)** for the Berry scripting API reference.

See **[firmware/README.md](firmware/README.md)** for firmware Development (build/flash from source).

## Step 1 — Flash the firmware

### Option A: Web UI (wireless OTA update)

1. Connect via BLE (see [Step 2 — Connect over BLE](#step-2--connect-over-ble)).
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

## Step 2 — Connect over BLE

1. Open the [Dorky Commander web UI](https://dzid26.github.io/ESP32-DualCAN/) in Chrome.
   *(Or install it as an app: Settings → Install app → Add to home screen.)*
2. Click **Connect** in the status bar.
3. Select **Dorky Commander** from the Bluetooth pairing dialog.
4. The status bar turns green and shows the firmware version.

> [!NOTE]
> The board advertises as `Dorky Commander`. Once paired, the pairing window stays closed for security — new devices cannot pair until you re-open it with a short-press of the B button or from the web UI on a bonded device (**Settings → Bluetooth → Open pairing window**). 

> [!TIP]
> If the board will be hidden behind car trim, pair with at least two devices (e.g., phone + laptop) — this avoids needing to reach the button, in case you loose a bond with the device.

See [docs/ble.md](docs/ble.md) for the full flow and troubleshooting.


### B button — Bluetooth / Reset / Boot

| Press | Action |
|---|---|
| Short press — release within 15 s | Open BLE pairing window for 60 s |
| Hold ≥15 s | Factory reset — wipes BLE bonds, credentials, and scripts |

See [docs/ble.md](docs/ble.md) for LED feedback and full pairing details.

---

## Step 3 — Load a DBC

The device needs a compiled DBC to decode signals by name.

1. Go to **DBC** in the left rail.
2. Paste or load a `.dbc` file. The Tesla Model 3/Y vehicle DBC is bundled — click **Load example**.
3. Select the target bus (0 = vehicle CAN, 1 = chassis CAN on Tesla).
4. Click **Upload to device**. The binary blob is sent over BLE and stored in flash.

---

## Step 4 — Write your first script

1. Go to **Scripts**.
2. Click **Load example…** → select `hello_log.be`.
3. Click **Save**, then toggle the **Enable** switch.
4. Open the **Log** panel (bottom right) — you should see `hello_log: setup` followed by heartbeat lines every 5 seconds.

The script runs on the device. `print()` output streams over BLE to the log panel.

---

## Step 5 — Run an action

1. Go to **Events** in the left rail.
2. Click **+ Add event** — this loads the `tiles_demo.be` example into the Scripts editor. Save and enable it.
3. Back on the **Events** page, four tiles appear: `blip_red`, `blip_green`, `blip_blue`, `rainbow`.
4. Tap a tile — the onboard LED blinks.

This proves the full path: BLE → firmware → Berry → hardware.

---

## Development

### Firmware

See [firmware/README.md — Development](firmware/README.md#development) for build, flash, and test instructions. Recommended: [PlatformIO IDE extension for VS Code](https://platformio.org/install).

### Web UI

Requires [Node.js](https://nodejs.org/).

```bash
cd webui
npm install
npm run dev       # dev server at http://localhost:5173
npm run build     # production build to dist/
```

Open in Chrome (required for Web Bluetooth). The DBC upload/parse works offline. BLE connect requires the board powered and flashed.

## ICs
- ESP32-C6-Zero (ESP32-C6FH8, 8MB, BLE + WiFi, 2x TWAI CAN2.0 controllers)
- 2x CAN transceivers TCAN1044
- TI LV2862 DC/DC converter

## Characteristics
- Size 19.5 x 41.5mm
- 5-58V operating range (e.g. cybertruck)
- Dual CAN
- integrated 2.4GHz antenna
- RGB LED
- USB-C for programming and debugging

## Images
- ESP32-C6-Zero chiplet and Molex connector:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/e7944936-6bc0-426c-abbd-16756143bc65" />

- On the other side there are components and optional pads to solder cables with a female [connector](https://duckduckgo.com/?q=MX2.54+cable+6p) to daisy chain with s3xy buttons or a strip:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/49cb72ce-7d53-4655-97a3-350807892f7b" />

- It can accommodate optional female 2.54mm headers for custom extensions:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/4b3c6a97-e744-423b-aed6-bcf8ae97739d" />


## Schematic

<img width="1111" alt="image" src="https://github.com/user-attachments/assets/b76eb95f-d02d-41a8-978b-3b63b6831206" />