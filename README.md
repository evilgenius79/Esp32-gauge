# ESP32 OBD2 Gauge

A real-time automotive gauge display for **CAN bus / OBD2** vehicles. Supports two hardware targets from a single codebase:

| Target | Display | Gauges | Input |
|--------|---------|--------|-------|
| **CrowPanel 1.28"** (ESP32-S3) | 240x240 round IPS (GC9A01) | 1 at a time | Rotary encoder |
| **M5Stack Tab5** (ESP32-P4) | 1280x720 IPS (MIPI-DSI) | 4 in a 2x2 grid | Touchscreen |

Both targets feature a **neon bloom glow** aesthetic — 10-layer graduated arcs on the sweep, needle, and center ring with smooth falloff from near-black to a hot orange/yellow core.

## Hardware

### CrowPanel 1.28" Build

| Component | Description |
|-----------|-------------|
| [CrowPanel 1.28" HMI ESP32 Rotary Display](https://www.elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html) | ESP32-S3, 240x240 IPS round display (GC9A01), rotary encoder, touch |
| [M5Stack Mini CAN Unit](https://shop.m5stack.com/products/mini-can-unit-tja1051t-3) | TJA1051T/3 CAN transceiver, 3.3V I/O compatible, Grove connector |
| OBD2 connector | Standard 16-pin OBD-II plug (CAN on pins 6 & 14) |

### M5Stack Tab5 Build

| Component | Description |
|-----------|-------------|
| [M5Stack Tab5](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4) | ESP32-P4 (400MHz RISC-V), 5" 1280x720 IPS touchscreen, Wi-Fi 6 via ESP32-C6 |
| [M5Stack Mini CAN Unit](https://shop.m5stack.com/products/mini-can-unit-tja1051t-3) | TJA1051T/3 CAN transceiver — connect via Grove Port A or GPIO_EXT header |
| OBD2 connector | Standard 16-pin OBD-II plug (CAN on pins 6 & 14) |

## Wiring

### CrowPanel — M5Stack Mini CAN to UART Header

Connect the M5Stack Mini CAN Unit to the CrowPanel's 4-pin UART header (1.25mm) on the back:

| Grove Wire Color | M5Stack Pin | CrowPanel Pin | Notes |
|------------------|-------------|---------------|-------|
| Red | 5V | 5V | Power (or from OBD2 pin 16) |
| Black | GND | GND | Common ground |
| White | CAN_TX (TXD) | GPIO 43 | CAN TX |
| Yellow | CAN_RX (RXD) | GPIO 44 | CAN RX |

### Tab5 — M5Stack Mini CAN to Grove/GPIO_EXT

Connect the M5Stack Mini CAN Unit to the Tab5's Port A (Grove HY2.0-4P) or GPIO_EXT header. Default pin assignments in `pins_tab5.h`:

| Signal | Default GPIO | Notes |
|--------|-------------|-------|
| CAN TX | GPIO 19 | Adjust in `pins_tab5.h` to match your wiring |
| CAN RX | GPIO 20 | Adjust in `pins_tab5.h` to match your wiring |

### M5Stack Mini CAN to OBD2 Connector

The M5Stack Mini CAN has screw terminals for CANH/CANL:

| M5Stack Terminal | OBD2 Pin | Description |
|------------------|----------|-------------|
| CANH | Pin 6 | CAN High |
| CANL | Pin 14 | CAN Low |

**Note:** The M5Stack Mini CAN has a built-in 120 ohm termination resistor, DC-DC isolation, and ESD protection. No level shifter is needed — the TJA1051T/3 supports 3.3V–5V I/O natively.

## Gauges

9 gauges available, including Ford-specific enhanced PIDs (Mode 0x22):

| # | Gauge | PID | Mode | Range | Units |
|---|-------|-----|------|-------|-------|
| 1 | RPM | 0x0C | 0x01 | 0–8,000 | rpm |
| 2 | Speed | 0x0D | 0x01 | 0–160 | mph |
| 3 | Coolant Temp | 0x05 | 0x01 | 100–260 | °F |
| 4 | Boost (TIP) | 0x033E | 0x22 | -15–30 | psi |
| 5 | Throttle | 0x11 | 0x01 | 0–100 | % |
| 6 | Engine Load | 0x04 | 0x01 | 0–100 | % |
| 7 | Intake Air Temp | 0x0F | 0x01 | 0–200 | °F |
| 8 | Charge Air Temp | 0xF40F | 0x22 | 0–300 | °F |
| 9 | Battery Voltage | 0x42 | 0x01 | 8–16 | V |

Each gauge has configurable warning/danger thresholds — ticks and arc turn red in the danger zone.

## Controls

### CrowPanel 1.28"

- **Rotate knob** — switch between gauges (CW = next, CCW = previous)
- **Press knob** — open DTC diagnostics menu (scan codes, clear codes)
- **In DTC results** — rotate to scroll if more than 5 codes

### M5Stack Tab5

- **Tap a gauge** — cycle it to the next PID
- All 4 gauges poll simultaneously (round-robin, ~2.5 Hz each)

### Auto-Sleep (Both Targets)

The display turns off after **30 seconds** of no CAN bus data. It wakes instantly on:
- Encoder rotation or button press (CrowPanel)
- Screen tap (Tab5)
- CAN data resuming

The timeout is configurable via `SLEEP_TIMEOUT_MS` in `main.cpp`.

## Building

### PlatformIO (Recommended)

```bash
# Install PlatformIO CLI
pip install platformio

# Build for CrowPanel 1.28"
pio run -e crowpanel_128

# Build for M5Stack Tab5
pio run -e m5stack_tab5

# Upload (connect via USB)
pio run -e crowpanel_128 --target upload
pio run -e m5stack_tab5 --target upload

# Monitor serial output
pio device monitor
```

### Arduino IDE — CrowPanel

1. Install ESP32 board support (v2.0.14+): Add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to Board Manager URLs
2. Select board: **ESP32S3 Dev Module**
3. Settings: Flash Size = **16MB**, Partition = **Huge APP (3MB No OTA/1MB SPIFFS)**, PSRAM = **OPI PSRAM**
4. Install libraries: **LovyanGFX**, **ESP32-TWAI-CAN**
5. Add `-DTARGET_CROWPANEL` to build flags
6. Upload (hold BOOT during power-on)

### Arduino IDE — Tab5

1. Install ESP32 board support with ESP32-P4 support (pioarduino or Espressif v3.x+)
2. Select board: **ESP32P4 Dev Module**
3. Settings: Flash Size = **16MB**, PSRAM = **Enabled**
4. Install libraries: **M5Unified**, **M5GFX**, **ESP32-TWAI-CAN**
5. Add `-DTARGET_TAB5` to build flags
6. Upload via USB-C

## Architecture

```
src/
├── main.cpp           # Setup/loop with #ifdef for CrowPanel vs Tab5
├── gauge_render.cpp   # Resolution-independent gauge renderer (neon bloom glow)
├── display.cpp        # Display management — single gauge (CrowPanel) or 2x2 grid (Tab5)
├── obd2.cpp           # CAN bus via TWAI, OBD2 PID request/response
└── encoder.cpp        # Rotary encoder with quadrature decoding (CrowPanel only)

include/
├── pins.h             # Auto-selects pins_crowpanel.h or pins_tab5.h
├── pins_crowpanel.h   # CrowPanel GPIO definitions
├── pins_tab5.h        # Tab5 GPIO definitions (CAN pins — adjust to your wiring)
├── gauge_render.h     # GaugeLayout struct + rendering API
├── gauges.h           # Gauge configs, PID definitions, decode formulas
├── display.h          # Display class (LGFX driver for CrowPanel, M5Unified for Tab5)
├── obd2.h             # OBD2 class declaration
└── encoder.h          # Encoder class declaration
```

### Key Design Decisions

- **Resolution-independent renderer** — `GaugeLayout::fromSize(px)` derives all dimensions from a single radius value. Works at any size: 240px (CrowPanel), 320px (Tab5 cells), or anything else.
- **10-layer neon bloom glow** — sweep arc, needle, and center ring each use 10 graduated color layers from near-black to full brightness, with a hot orange/yellow core. Bloom width scales proportionally with gauge size.
- **Only active gauge(s) poll CAN bus** — CrowPanel polls 1 PID at 10 Hz; Tab5 round-robins 4 PIDs at ~2.5 Hz each.
- **Sprite-based rendering** — PSRAM-backed sprites eliminate flicker on both targets.
- **Auto-sleep** — backlight turns off after 30s of no CAN data, wakes on input or data.
- **Single codebase** — `#ifdef TARGET_CROWPANEL` / `TARGET_TAB5` and `build_src_filter` keep platform differences isolated.
- **ESP32 TWAI** — built-in CAN controller on both ESP32-S3 and ESP32-P4, no external MCP2515 needed.

## DTC Diagnostics (CrowPanel Only)

Press the encoder knob to access the diagnostics menu:

- **Scan Codes** — reads Mode 03 (stored DTCs), displays up to 5 at a time with scroll support
- **Clear Codes** — sends Mode 04 to clear DTCs and reset the MIL (check engine light)
- **Back** — return to gauge view

## OBD2 Protocol Notes

- Standard OBD2 uses **500 kbps CAN** (ISO 15765-4)
- Request CAN ID: **0x7DF** (broadcast)
- Response CAN ID: **0x7E8–0x7EF** (ECU responses)
- Mode 0x01: Standard current data PIDs
- Mode 0x22: Enhanced/manufacturer-specific PIDs (Boost TIP, Charge Air Temp)
- Mode 0x03: Read stored DTCs
- Mode 0x04: Clear DTCs and reset MIL
- Not all vehicles support all PIDs — unsupported PIDs will show 0

## References

- [CrowPanel 1.28" Wiki](https://www.elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html)
- [Elecrow GitHub (factory code)](https://github.com/Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen)
- [M5Stack Tab5 Docs](https://docs.m5stack.com/en/core/Tab5)
- [M5Stack Mini CAN Unit](https://docs.m5stack.com/en/unit/Unit-Mini%20CAN)
- [ESP32-TWAI-CAN Library](https://github.com/handmade0octopus/ESP32-TWAI-CAN)
- [M5Unified Library](https://github.com/m5stack/M5Unified)
- [M5GFX Library](https://github.com/m5stack/M5GFX)
- [OBD2 PID Table](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- [ESP32 TWAI Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html)
