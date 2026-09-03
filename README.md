# ESP32-DualCAN
Dorky Commander - open source alternative to S3XY Commander. 

Uses Molex connector compatible with Enhauto harnesses.

See **[firmware/README.md](firmware/README.md)** for the user-facing install guide and first-use walkthrough.

See **[docs/scripting.md](docs/scripting.md)** for the Berry scripting API reference.

## Flashing

### Option A: Web UI (wireless OTA update)

1. Connect via BLE (see [firmware/README.md — Step 2](firmware/README.md#step-2--connect-over-ble)).
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

To build from source, see [firmware/README.md — Step 1](firmware/README.md#step-1--flash-the-firmware) (PlatformIO extension recommended).

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

### Bluetooth pairing

The device requires bonded pairing — first-time setup needs an open pairing
window (boot defaults to OPEN until first bond, BOOT button or web UI re-opens
later). Pairing uses **Secure Connections only**; very old centrals (BLE 4.0,
pre-2014) won't pair. See [docs/ble.md](docs/ble.md) for the full connection
flow and troubleshooting.

### BOOT button

| Press | Action |
|---|---|
| Short press | Open BLE pairing window for 60 s |
| Hold 15 s | Factory reset — wipes bonds, credentials, and scripts |

See [docs/ble.md](docs/ble.md) for LED feedback and full pairing details.

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