# ESP32 OBD2 Gauge

A real-time automotive gauge display using the **CrowPanel 1.28" HMI ESP32-S3 Rotary Display** and a **CJMCU-1051 CAN bus transceiver** module. Turn the rotary knob to switch between gauges — only the active gauge polls the CAN bus.

## Hardware

| Component | Description |
|-----------|-------------|
| [CrowPanel 1.28" HMI ESP32 Rotary Display](https://www.elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html) | ESP32-S3, 240x240 IPS round display (GC9A01), rotary encoder, touch |
| [CJMCU-1051](https://www.diymore.cc/products/cjmcu-1051-tja1051-high-speed-low-power-can-transceiver-for-arduino) | TJA1051 high-speed CAN transceiver module |
| OBD2 connector | Standard 16-pin OBD-II plug (CAN on pins 6 & 14) |

## Wiring

### CJMCU-1051 to CrowPanel (FPC Connector)

| CJMCU-1051 Pin | CrowPanel Pin | Notes |
|----------------|---------------|-------|
| VCC | 5V | From USB or external 5V |
| GND | GND | Common ground |
| CTX | GPIO 4 | CAN TX (FPC expansion IO) |
| CRX | GPIO 12 | CAN RX (FPC expansion IO) |
| S | GND | Already pulled low on module via 10K resistor |

### CJMCU-1051 to OBD2 Connector

| CJMCU-1051 Pin | OBD2 Pin | Description |
|----------------|----------|-------------|
| CANH | Pin 6 | CAN High |
| CANL | Pin 14 | CAN Low |

### ⚠️ Voltage Level Warning

The standard TJA1051T operates at 5V logic levels on its CTX/CRX pins, but the ESP32-S3 uses 3.3V GPIOs. Options:

1. **Use the TJA1051T/3 variant** — has a separate VIO pin supporting 3.3V–5V I/O. No level shifter needed.
2. **Add series resistors** (1K–6.8K) on CTX and CRX lines for basic protection.
3. **Use a bidirectional level shifter** for guaranteed safe operation.

Many users run the standard TJA1051 directly with ESP32 without issues, but it's not guaranteed safe long-term.

## Gauges

Turn the rotary encoder knob to cycle through gauges:

| # | Gauge | PID | Formula | Range |
|---|-------|-----|---------|-------|
| 1 | RPM | 0x0C | (256×A + B) / 4 | 0–8000 rpm |
| 2 | Speed | 0x0D | A | 0–255 km/h |
| 3 | Coolant Temp | 0x05 | A − 40 | −40–215 °C |
| 4 | Boost (MAP) | 0x0B | A | 0–255 kPa |
| 5 | Throttle | 0x11 | A × 100/255 | 0–100 % |
| 6 | Engine Load | 0x04 | A × 100/255 | 0–100 % |
| 7 | Intake Air Temp | 0x0F | A − 40 | −40–215 °C |
| 8 | Battery Voltage | 0x42 | (256×A + B) / 1000 | 0–20 V |

## Controls

- **Rotate knob**: Switch between gauges (CW = next, CCW = previous)
- **Press knob**: Cycle display brightness (30% → 50% → 70% → 100%)

## Building

### PlatformIO (Recommended)

```bash
# Install PlatformIO CLI
pip install platformio

# Build
pio run

# Upload (connect via USB, hold BOOT button during power-on)
pio run --target upload

# Monitor serial output
pio device monitor
```

### Arduino IDE

1. Install ESP32 board support (v2.0.14+): Add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to Board Manager URLs
2. Select board: **ESP32S3 Dev Module**
3. Settings: Flash Size = **16MB**, Partition = **Huge APP (3MB No OTA/1MB SPIFFS)**, PSRAM = **OPI PSRAM**
4. Install libraries: **LovyanGFX**, **ESP32-TWAI-CAN**
5. Upload (hold BOOT during power-on)

## Architecture

```
src/
├── main.cpp        # Main setup/loop, gauge switching logic
├── display.cpp     # GC9A01 display driver, gauge rendering with sprites
├── obd2.cpp        # CAN bus communication via TWAI, OBD2 PID requests
└── encoder.cpp     # Rotary encoder ISR handling
include/
├── pins.h          # All hardware pin definitions
├── gauges.h        # Gauge configs, PID definitions, decode formulas
├── display.h       # Display/renderer class declaration
├── obd2.h          # OBD2 class declaration
└── encoder.h       # Encoder class declaration
```

### Key Design Decisions

- **Only the active gauge polls CAN bus** — reduces bus traffic and response latency
- **Sprite-based rendering** — full-screen PSRAM sprite eliminates flicker
- **LovyanGFX** — fast DMA-accelerated SPI, native GC9A01 support
- **ESP32 TWAI** — built-in CAN controller, no MCP2515 SPI overhead
- **ISR-based encoder** — responsive knob input, no polling delay

## OBD2 Protocol Notes

- Standard OBD2 uses **500 kbps CAN** (ISO 15765-4)
- Request CAN ID: **0x7DF** (broadcast)
- Response CAN ID: **0x7E8–0x7EF** (ECU responses)
- Mode 01 (current data) is used for all gauges
- Not all vehicles support all PIDs — unsupported PIDs will show 0

## References

- [CrowPanel 1.28" Wiki](https://www.elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html)
- [Elecrow GitHub (factory code)](https://github.com/Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen)
- [ESP32-TWAI-CAN Library](https://github.com/handmade0octopus/ESP32-TWAI-CAN)
- [OBD2 PID Table](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- [ESP32 TWAI Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html)
- [TJA1051 CAN Transceiver](https://www.circuitstate.com/tutorials/what-is-can-bus-how-to-use-can-interface-with-esp32-and-arduino/)
