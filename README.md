# ESP32 OBD2 Gauge

A real-time automotive gauge display for **CAN bus / OBD2** vehicles. Supports two hardware targets from a single codebase:

| Target | Display | Gauges | Input |
|--------|---------|--------|-------|
| **CrowPanel 1.28"** (ESP32-S3) | 240x240 round IPS (GC9A01) | 1 at a time | Rotary encoder |
| **M5Stack Tab5** (ESP32-P4) | 1280x720 IPS (MIPI-DSI) | 4 in a 2x2 grid | Touchscreen |

Both targets feature a **neon bloom glow** aesthetic — 10-layer graduated arcs on the sweep, needle, and center ring with smooth falloff from near-black to a hot orange/yellow core.

## Features

- **9 configurable gauges** including Ford-specific enhanced PIDs (Mode 0x22)
- **Live gauge editing** — long-press any gauge to edit name, PID, ranges, formula, and more
- **14 decode formulas** — selectable per gauge (raw, percent, temp C/F, RPM, speed, voltage, boost PSI, etc.)
- **DTC diagnostics** — scan and clear trouble codes on both targets
- **CSV data logging** — log all gauge data to SD card (Tab5)
- **Peak/min hold** — track peak and minimum values per gauge
- **Fullscreen mode** — double-tap a gauge to expand to full 720px (Tab5)
- **PID auto-discovery** — scan which Mode 01 PIDs your vehicle supports
- **Configurable CAN speed** — toggle between 250/500 kbps, saved to NVS
- **NVS persistence** — all gauge configs and settings survive reboot
- **Auto-sleep** — display turns off after 30s of no CAN data

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
| [M5Stack Mini CAN Unit](https://shop.m5stack.com/products/mini-can-unit-tja1051t-3) | TJA1051T/3 CAN transceiver — connect via Grove Port A |
| OBD2 connector | Standard 16-pin OBD-II plug (CAN on pins 6 & 14) |
| MicroSD card (optional) | For CSV data logging |

## Wiring

### CrowPanel — M5Stack Mini CAN to UART Header

Connect the M5Stack Mini CAN Unit to the CrowPanel's 4-pin UART header (1.25mm) on the back:

| Grove Wire Color | M5Stack Pin | CrowPanel Pin | Notes |
|------------------|-------------|---------------|-------|
| Red | 5V | 5V | Power (or from OBD2 pin 16) |
| Black | GND | GND | Common ground |
| White | CAN_TX (TXD) | GPIO 43 | CAN TX |
| Yellow | CAN_RX (RXD) | GPIO 44 | CAN RX |

### Tab5 — M5Stack Mini CAN to Port A

Connect the M5Stack Mini CAN Unit to the Tab5's **Port A** (Grove HY2.0-4P):

| Signal | GPIO | Notes |
|--------|------|-------|
| CAN TX | GPIO 53 | Port A pin 1 |
| CAN RX | GPIO 54 | Port A pin 2 |

To use different pins, edit `include/pins_tab5.h`.

### M5Stack Mini CAN to OBD2 Connector

The M5Stack Mini CAN has screw terminals for CANH/CANL:

| M5Stack Terminal | OBD2 Pin | Description |
|------------------|----------|-------------|
| CANH | Pin 6 | CAN High |
| CANL | Pin 14 | CAN Low |

**Note:** The M5Stack Mini CAN has a built-in 120 ohm termination resistor, DC-DC isolation, and ESD protection. No level shifter is needed — the TJA1051T/3 supports 3.3V-5V I/O natively.

## Gauges

9 default gauges, all fully editable at runtime:

| # | Gauge | PID | Mode | Range | Units | Decode Formula |
|---|-------|-----|------|-------|-------|----------------|
| 1 | RPM | 0x0C | 0x01 | 0-8,000 | rpm | (256A+B)/4 |
| 2 | Speed | 0x0D | 0x01 | 0-160 | mph | A * 0.621 |
| 3 | Coolant Temp | 0x05 | 0x01 | 100-260 | F | A-40 (F) |
| 4 | Boost (TIP) | 0x033E | 0x22 | -15-30 | psi | kPa->PSI |
| 5 | Throttle | 0x11 | 0x01 | 0-100 | % | A*100/255 |
| 6 | Engine Load | 0x04 | 0x01 | 0-100 | % | A*100/255 |
| 7 | Intake Air Temp | 0x0F | 0x01 | 0-200 | F | A-40 (F) |
| 8 | Charge Air Temp | 0xF40F | 0x22 | 0-300 | F | A-40 (F) |
| 9 | Battery Voltage | 0x42 | 0x01 | 8-16 | V | (256A+B)/1000 |

### Decode Formulas

Each gauge has a selectable decode formula for converting raw OBD2 bytes to display values:

| # | Formula | Description |
|---|---------|-------------|
| 0 | Raw A | Single byte (A) |
| 1 | Raw 256A+B | Two bytes combined |
| 2 | A*100/255 % | Percentage (0-100%) |
| 3 | A-40 (C) | Temperature in Celsius |
| 4 | A-40 (F) | Temperature in Fahrenheit |
| 5 | (256A+B)/4 RPM | Engine RPM |
| 6 | A km/h | Speed in km/h |
| 7 | A*0.621 mph | Speed in mph |
| 8 | (256A+B)/1000 V | Voltage |
| 9 | kPa->PSI boost | Boost pressure (gauge PSI) |
| 10 | (256A+B)/128 | Generic divide by 128 |
| 11 | (256A+B)/4 | Generic divide by 4 |
| 12 | (256A+B)/100 | Generic divide by 100 |
| 13 | (256A+B)/1000 | Generic divide by 1000 |

## Controls

### CrowPanel 1.28"

- **Rotate knob** - switch between gauges (CW = next, CCW = previous)
- **Press knob** - open DTC diagnostics menu (scan codes, clear codes)
- **In DTC results** - rotate to scroll if more than 5 codes

### M5Stack Tab5

- **Tap a gauge** - cycle it to the next PID
- **Double-tap a gauge** - enter fullscreen mode (720px gauge), tap anywhere to exit
- **Long-press a gauge (1 sec)** - open the gauge editor
- **DTC button** (center of grid) - open the tools & diagnostics menu
- All 4 gauges poll simultaneously (round-robin, ~2.5 Hz each)

### Gauge Editor (Tab5)

Long-press any gauge to open the full editor. All 14 fields are editable via an on-screen QWERTY keyboard:

| Field | Description |
|-------|-------------|
| NAME | Display name (up to 11 chars) |
| UNITS | Unit string shown below value |
| LABEL | Scale label (e.g. "x1000r/min") |
| MODE | OBD2 mode (hex: 01 standard, 22 enhanced) |
| PID | OBD2 PID (hex: 0C, 0D, 033E, F40F, etc.) |
| MIN | Minimum display value |
| MAX | Maximum display value |
| WARN | Warning threshold |
| DANGER | Danger threshold (red zone starts here) |
| SCALE DIV | Divisor for dial numbers (e.g. 1000 for RPM) |
| DIVISIONS | Number of major tick marks |
| DATA BYTES | Response bytes (1 or 2) |
| DECIMALS | Decimal places for numeric readout |
| FORMULA | Decode formula (opens picker) |

Bottom row buttons:
- **SAVE** - apply changes and persist to NVS
- **RESET** - restore gauge to factory default
- **CANCEL** - discard changes
- **CAN 500k/250k** - toggle CAN bus speed (saved to NVS)

### Tools & Diagnostics Menu (Tab5)

Tap the DTC button at the center of the 2x2 grid to access:

| Item | Description |
|------|-------------|
| **SCAN CODES** | Read stored DTCs (Mode 0x03), scroll through results |
| **CLEAR CODES** | Clear DTCs and reset MIL (Mode 0x04) |
| **LOG TO SD** | Start/stop CSV data logging to SD card |
| **PEAK HOLD** | Toggle peak/min value tracking (shown in fullscreen mode) |
| **SCAN SUPPORTED PIDS** | Auto-discover which Mode 01 PIDs the vehicle supports |
| **BACK** | Return to gauge view |

### DTC Diagnostics (CrowPanel)

Press the encoder knob to access:

- **Scan Codes** - reads Mode 03 (stored DTCs), displays up to 5 at a time with scroll support
- **Clear Codes** - sends Mode 04 to clear DTCs and reset the MIL (check engine light)
- **Back** - return to gauge view

### CSV Data Logging (Tab5)

When an SD card is inserted, the data logger writes gauge readings to CSV files:

- Files saved to `/obd2_log/` on the SD card
- Format: `timestamp_ms, slot0_name, slot0_value, slot1_name, slot1_value, ...`
- Flushes to disk every 10 seconds
- Toggle logging on/off via the tools menu
- Status indicator shows green when actively logging

### Auto-Sleep (Both Targets)

The display turns off after **30 seconds** of no CAN bus data. It wakes instantly on:
- Encoder rotation or button press (CrowPanel)
- Screen tap (Tab5)
- CAN data resuming

The timeout is configurable via `SLEEP_TIMEOUT_MS` in `main.cpp`.

## Pre-built Firmware

Pre-built binaries are available in the `firmware/` directory:

- `firmware/crowpanel_128_firmware.bin` - CrowPanel 1.28"
- `firmware/m5stack_tab5_firmware.bin` - M5Stack Tab5

### Flashing with esptool

```bash
# Install esptool
pip install esptool

# Flash CrowPanel (hold BOOT during power-on, then release)
esptool --chip esp32s3 --baud 921600 write_flash 0x10000 firmware/crowpanel_128_firmware.bin

# Flash Tab5
esptool --chip esp32p4 --baud 1500000 write_flash 0x10000 firmware/m5stack_tab5_firmware.bin
```

## Building from Source

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

**VS Code:** Open the project folder, install the PlatformIO extension, and use `Ctrl+Shift+P` > "PlatformIO: Build" to build. Select the target environment from the status bar.

### Arduino IDE - CrowPanel

1. Install ESP32 board support (v2.0.14+): Add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to Board Manager URLs
2. Select board: **ESP32S3 Dev Module**
3. Settings: Flash Size = **16MB**, Partition = **Huge APP (3MB No OTA/1MB SPIFFS)**, PSRAM = **OPI PSRAM**
4. Install libraries: **LovyanGFX**, **ESP32-TWAI-CAN**
5. Add `-DTARGET_CROWPANEL` to build flags
6. Upload (hold BOOT during power-on)

### Arduino IDE - Tab5

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
├── display.cpp        # Display, touch, menus, editor, keypad, fullscreen
├── gauge_config.cpp   # NVS persistence for gauge configs and CAN speed
├── datalog.cpp        # CSV data logger for SD card (Tab5 only)
├── obd2.cpp           # CAN bus via TWAI, OBD2 PID request/response
└── encoder.cpp        # Rotary encoder with quadrature decoding (CrowPanel only)

include/
├── pins.h             # Auto-selects pins_crowpanel.h or pins_tab5.h
├── pins_crowpanel.h   # CrowPanel GPIO definitions
├── pins_tab5.h        # Tab5 GPIO definitions (CAN on Port A: GPIO 53/54)
├── gauge_render.h     # GaugeLayout struct + rendering API
├── gauges.h           # GaugeConfig struct, decode formulas, default gauges
├── display.h          # Display class (LGFX for CrowPanel, M5Unified for Tab5)
├── datalog.h          # DataLogger class (Tab5 SD card logging)
├── obd2.h             # OBD2 class declaration
└── encoder.h          # Encoder class declaration

firmware/
├── crowpanel_128_firmware.bin   # Pre-built CrowPanel binary
└── m5stack_tab5_firmware.bin    # Pre-built Tab5 binary
```

### Key Design Decisions

- **Resolution-independent renderer** - `GaugeLayout::fromSize(px)` derives all dimensions from a single radius value. Works at 240px (CrowPanel), 320px (Tab5 quadrants), or 720px (Tab5 fullscreen).
- **10-layer neon bloom glow** - sweep arc, needle, and center ring each use 10 graduated color layers from near-black to full brightness, with a hot orange/yellow core.
- **Selectable decode formulas** - each gauge has an independent `DecodeFormula` enum instead of PID-based hardcoded decoding, so custom PIDs decode correctly.
- **Full gauge editor with NVS** - all 14 config fields editable via touchscreen QWERTY keyboard, persisted as binary blobs in ESP32 NVS flash.
- **Only active gauge(s) poll CAN bus** - CrowPanel polls 1 PID at 10 Hz; Tab5 round-robins 4 PIDs at ~2.5 Hz each.
- **Sprite-based rendering** - PSRAM-backed sprites eliminate flicker on both targets.
- **Single codebase** - `#ifdef TARGET_CROWPANEL` / `TARGET_TAB5` and `build_src_filter` keep platform differences isolated.
- **ESP32 TWAI** - built-in CAN controller on both ESP32-S3 and ESP32-P4, no external MCP2515 needed.

## OBD2 Protocol Notes

- CAN speed: **500 kbps** default (configurable to 250 kbps via editor)
- Request CAN ID: **0x7DF** (broadcast)
- Response CAN ID: **0x7E8-0x7EF** (ECU responses)
- Mode 0x01: Standard current data PIDs
- Mode 0x22: Enhanced/manufacturer-specific PIDs (Ford Boost TIP, Charge Air Temp)
- Mode 0x03: Read stored DTCs
- Mode 0x04: Clear DTCs and reset MIL
- Not all vehicles support all PIDs - use the PID discovery scan to check

## References

- [CrowPanel 1.28" Wiki](https://www.elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html)
- [Elecrow GitHub (factory code)](https://github.com/Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen)
- [M5Stack Tab5 Docs](https://docs.m5stack.com/en/core/Tab5)
- [M5Stack Mini CAN Unit](https://docs.m5stack.com/en/unit/Unit-Mini%20CAN)
- [ESP32-TWAI-CAN Library](https://github.com/handmade0octopus/ESP32-TWAI-CAN)
- [M5Unified Library](https://github.com/m5stack/M5Unified)
- [M5GFX Library](https://github.com/m5stack/M5GFX)
- [LovyanGFX Library](https://github.com/lovyan03/LovyanGFX)
- [OBD2 PID Table](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- [ESP32 TWAI Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html)
