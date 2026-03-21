# ESP32 OBD2 Gauge

A real-time automotive gauge display using the **CrowPanel 1.28" HMI ESP32-S3 Rotary Display** and an **M5Stack Mini CAN Unit (TJA1051T/3)**. Turn the rotary knob to switch between gauges — only the active gauge polls the CAN bus.

## Hardware

| Component | Description |
|-----------|-------------|
| [CrowPanel 1.28" HMI ESP32 Rotary Display](https://www.elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html) | ESP32-S3, 240x240 IPS round display (GC9A01), rotary encoder, touch |
| [M5Stack Mini CAN Unit](https://shop.m5stack.com/products/mini-can-unit-tja1051t-3) | TJA1051T/3 CAN transceiver, 3.3V I/O compatible, Grove connector |
| OBD2 connector | Standard 16-pin OBD-II plug (CAN on pins 6 & 14) |

## Wiring

### M5Stack Mini CAN to CrowPanel (FPC Connector)

The M5Stack Mini CAN has a **Grove (HY2.0-4P) connector**. Cut a Grove cable or use jumper wires to connect to the CrowPanel FPC expansion pads:

| Grove Wire Color | M5Stack Pin | CrowPanel Pin | Notes |
|------------------|-------------|---------------|-------|
| Red | 5V | 5V | Power (or from OBD2 pin 16) |
| Black | GND | GND | Common ground |
| White | CAN_TX (TXD) | GPIO 4 | CAN TX — FPC expansion IO |
| Yellow | CAN_RX (RXD) | GPIO 12 | CAN RX — FPC expansion IO |

### M5Stack Mini CAN to OBD2 Connector

The M5Stack Mini CAN has screw terminals for CANH/CANL:

| M5Stack Terminal | OBD2 Pin | Description |
|------------------|----------|-------------|
| CANH | Pin 6 | CAN High |
| CANL | Pin 14 | CAN Low |

**Note:** The M5Stack Mini CAN has a built-in 120Ω termination resistor. If your vehicle's OBD2 port already has termination, this is fine for a two-node bus. The unit also has built-in DC-DC isolation and ESD protection.

### No Level Shifter Needed

The M5Stack Mini CAN uses the **TJA1051T/3** variant which supports 3.3V–5V I/O natively. It connects directly to the ESP32-S3's 3.3V GPIO pins without any level shifting.

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
└── encoder.cpp     # Rotary encoder with quadrature decoding & debounce
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
- **Quadrature encoder decoding** — lookup-table-based state machine with 4-step detent threshold eliminates bouncing and false triggers

## OBD2 Protocol Notes

- Standard OBD2 uses **500 kbps CAN** (ISO 15765-4)
- Request CAN ID: **0x7DF** (broadcast)
- Response CAN ID: **0x7E8–0x7EF** (ECU responses)
- Mode 01 (current data) is used for all gauges
- Not all vehicles support all PIDs — unsupported PIDs will show 0

## References

- [CrowPanel 1.28" Wiki](https://www.elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html)
- [Elecrow GitHub (factory code)](https://github.com/Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen)
- [M5Stack Mini CAN Unit](https://docs.m5stack.com/en/unit/Unit-Mini%20CAN)
- [ESP32-TWAI-CAN Library](https://github.com/handmade0octopus/ESP32-TWAI-CAN)
- [OBD2 PID Table](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- [ESP32 TWAI Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html)
