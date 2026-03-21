#pragma once

// =============================================================================
// CrowPanel 1.28" HMI ESP32-S3 Rotary Display - Pin Definitions
// =============================================================================

// ---- Display (GC9A01 via SPI) ----
#define PIN_SPI_SCLK    10
#define PIN_SPI_MOSI    11
#define PIN_SPI_MISO    -1
#define PIN_TFT_DC       3
#define PIN_TFT_CS       9
#define PIN_TFT_RST     14
#define PIN_TFT_BL      46

// ---- LCD Power Enable ----
#define PIN_LCD_PWR_EN1  1
#define PIN_LCD_PWR_EN2  2

// ---- Touch Screen (CST816D via I2C on Wire1) ----
#define PIN_TP_SDA       6
#define PIN_TP_SCL       7
#define PIN_TP_INT       5
#define PIN_TP_RST      13

// ---- Rotary Encoder ----
#define PIN_ENC_A       45
#define PIN_ENC_B       42
#define PIN_ENC_SW      41

// ---- RGB LEDs (WS2812) ----
#define PIN_RGB_LED     48
#define NUM_RGB_LEDS     5

// ---- Power Indicator ----
#define PIN_PWR_LIGHT   40

// ---- CAN Bus (M5Stack Mini CAN Unit - TJA1051T/3) ----
// Grove connector: White wire = TX, Yellow wire = RX
// Connected to the two available expansion GPIOs on FPC connector
#define PIN_CAN_TX       4
#define PIN_CAN_RX      12
