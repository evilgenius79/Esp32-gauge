#pragma once

#include <LovyanGFX.hpp>
#include "pins.h"
#include "gauges.h"

// =============================================================================
// Display Driver - CrowPanel 1.28" GC9A01 (LovyanGFX)
// =============================================================================

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI      _bus;

public:
    LGFX(void) {
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 80000000;
            cfg.freq_read   = 20000000;
            cfg.spi_3wire   = true;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_SPI_SCLK;
            cfg.pin_mosi    = PIN_SPI_MOSI;
            cfg.pin_miso    = PIN_SPI_MISO;
            cfg.pin_dc      = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs           = PIN_TFT_CS;
            cfg.pin_rst          = PIN_TFT_RST;
            cfg.pin_busy         = -1;
            cfg.memory_width     = 240;
            cfg.memory_height    = 240;
            cfg.panel_width      = 240;
            cfg.panel_height     = 240;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

// =============================================================================
// Gauge Renderer
// =============================================================================

class GaugeDisplay {
public:
    void begin();
    void drawGauge(const GaugeConfig& gauge, float value, bool forceRedraw);
    void drawConnectionStatus(bool connected);
    void setBrightness(uint8_t percent);

private:
    LGFX       _tft;
    LGFX_Sprite _sprite;   // Full-screen sprite for flicker-free drawing

    int   _prevGaugeIdx = -1;
    float _prevValue    = -99999.0f;
    bool  _prevConnected = true;

    // Drawing helpers
    void drawArc(int cx, int cy, int r_outer, int r_inner,
                 float startAngle, float endAngle, uint16_t color);
    void drawNeedle(int cx, int cy, float angle, int length, uint16_t color);
    void drawScaleMarks(int cx, int cy, int radius,
                        float minVal, float maxVal,
                        float startAngle, float sweepAngle);
    uint16_t valueToColor(const GaugeConfig& gauge, float value);
    float valueToAngle(const GaugeConfig& gauge, float value);
};
