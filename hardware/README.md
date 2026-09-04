# Hardware — ESP32-DualCAN

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/e7944936-6bc0-426c-abbd-16756143bc65" /> 

## Characteristics
- 5-58 V operating range (e.g. Cybertruck)
- Dual CAN
- integrated 2.4 GHz antenna
- RGB LED
- USB-C - USB CDC (virtual COM, debugging)  

## Case
Designed in FreeCAD
- size 23.5 x 42.8 x 12.3 mm

## ICs
- ESP32-C6-Zero (ESP32-C6FH8, 8MB, BLE + WiFi, 2x TWAI CAN2.0 controllers)
- 2x CAN transceivers TCAN1044
- TI LV2862 DC/DC converter

## PCB
Designed in Kicad
- size 19.5 x 41.5 mm

## Images
- ESP32-C6-Zero chiplet and Molex connector:

<img width="555" alt="image" src="https://github.com/user-attachments/assets/bf56de74-3c79-44e5-afe4-fdb7394c0679" />

- On the other side there are components and optional pads to solder cables with a female [connector](https://duckduckgo.com/?q=MX2.54+cable+6p) to daisy chain with s3xy buttons or a strip:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/49cb72ce-7d53-4655-97a3-350807892f7b" />

- It can accommodate optional female 2.54mm headers for custom extensions:

<img width="555"  alt="image" src="https://github.com/user-attachments/assets/4b3c6a97-e744-423b-aed6-bcf8ae97739d" />


## Schematic

<img width="1111" alt="image" src="https://github.com/user-attachments/assets/b76eb95f-d02d-41a8-978b-3b63b6831206" />

Source files: [`ESP32-DualCAN.kicad_sch`](ESP32-DualCAN.kicad_sch), [`ESP32-DualCAN.kicad_pcb`](ESP32-DualCAN.kicad_pcb) — see [KiCad-Waveshare-ESP32/](KiCad-Waveshare-ESP32/) for the ESP32-C6-Zero module footprint.
