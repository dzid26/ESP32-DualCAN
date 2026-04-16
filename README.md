# ESP32-DualCAN
Dorky Commander - open source alternative to S3XY Commander. 

Uses Molex connector compatible with Enhauto harnesses.

## Development

### Firmware

Requires [PlatformIO](https://platformio.org/).

```bash
cd firmware
pio run                              # build production firmware
pio run -t upload                    # flash to board
pio device monitor                   # serial console
pio test -e tests-native             # run host unit tests (needs gcc)
pio test -e esp32-c6-tests-arduino   # run hardware smoke tests (board connected)
```

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
- ESP32-C6-SuperMini (integrated antenna, 2x TWAI CAN2.0 controllers, RGB LED)
    - alternatively ESP32-C6-Zero 
- 2x CAN transceivers TCAN1044
- TI LV2862 DC/DC converter

## Characteristics
- Size 19.5 x 41.5mm
- 5-58V operating range (e.g. cybertruck)
- Dual CAN
- BLE + WiFi
- USB-C for programming and debugging

## Images
- ESP32-C6-SuperMini chiplet and Molex connector:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/e7944936-6bc0-426c-abbd-16756143bc65" />

- On the other side there are components and optional pads to solder cables with a female [connector](https://duckduckgo.com/?q=MX2.54+cable+6p) to daisy chain with s3xy buttons or a strip:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/49cb72ce-7d53-4655-97a3-350807892f7b" />

- It can accommodate optional female 2.54mm headers for custom extensions:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/4b3c6a97-e744-423b-aed6-bcf8ae97739d" />


## Schematic

<img width="1111"  alt="image" src="https://github.com/user-attachments/assets/97085c71-ebe7-4a6d-bf68-577d57fa121a" />
