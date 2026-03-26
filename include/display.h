#pragma once

#ifdef TARGET_TAB5
#include <M5GFX.h>
#else
#include <LovyanGFX.hpp>
#endif
#include "pins.h"
#include "gauges.h"
#include "obd2.h"
#include "gauge_render.h"

#ifdef TARGET_TAB5
#include <M5Unified.h>
#endif

// =============================================================================
// CrowPanel 1.28" GC9A01 Display Driver (LovyanGFX)
// Only compiled for the CrowPanel target
// =============================================================================

#ifndef TARGET_TAB5

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

#endif // !TARGET_TAB5

// =============================================================================
// Gauge Display — adapts to CrowPanel (single gauge) or Tab5 (multi-gauge)
// =============================================================================

class GaugeDisplay {
public:
    void begin();
    void setBrightness(uint8_t percent);
    void clearScreen();  // Full black fill — call on state transitions

#ifdef TARGET_TAB5
    // --- Tab5: 4-gauge dashboard ---
    void drawAllGauges(const int indices[4], const float values[4], bool forceRedraw);
    void drawSingleGauge(int slot, int gaugeIdx, float value, bool forceRedraw);
    void drawDTCButton();  // Draw DTC button at center of grid
    void invalidateSlot(int slot);  // Force full redraw of slot (after config edit)

    // Touch input — call once per loop, returns action
    enum TouchAction {
        TOUCH_NONE = 0,
        TOUCH_GAUGE_TAP,      // Quick tap on a gauge (slot in touchSlot)
        TOUCH_GAUGE_LONGPRESS, // Long press on a gauge (slot in touchSlot)
        TOUCH_DTC_BUTTON,     // Tap on DTC button
    };
    TouchAction pollTouch();  // Call once per loop iteration
    int touchSlot = -1;       // Which slot was touched (0-3)

    // DTC & tools menu (fullscreen overlay)
    void drawDTCMenuTab5(int selectedItem, bool obdEnabled = false, bool logging = false, bool peakHold = false);
    int  dtcMenuTapped();  // Returns tapped menu item 0-6, or -1
    void drawDTCScanningTab5();
    void drawDTCResultsTab5(const DTC* dtcs, int count, int scrollOffset = 0);
    int  dtcResultsScrollOrBack(); // Returns: -2=back, -1=scroll up, 1=scroll down, 0=none
    void drawDTCClearingTab5();
    void drawDTCClearedTab5(bool success);
    bool dtcBackTapped();  // Generic "tap anywhere to go back"

    // PID discovery
    void drawPIDScanningTab5();
    void drawPIDResultsTab5(const uint8_t* pidBitmap, int count);

    // Fullscreen single gauge (double-tap to enter, any tap to exit)
    void drawFullscreenGauge(int gaugeIdx, float value, float peakVal, bool forceRedraw);

    // Gauge editor
    void drawGaugeEditor(int slot, const GaugeConfig& gauge, int selectedField);
    int  editorFieldTapped();  // Returns field index 0-13 or -1, 99=save, 98=reset, 97=cancel, 96=CAN speed
    void drawEditorKeypad(const char* title, const char* currentValue);
    int  keypadTapped(char* buffer, int bufLen);  // Returns: 0=key, 1=done, -1=none

    // Formula picker
    void drawFormulaPicker(int currentFormula);
    int  formulaPickerTapped();  // Returns formula index 0-13, or -1

#else
    // --- CrowPanel: single gauge + DTC menus ---
    void drawGauge(const GaugeConfig& gauge, float value, bool forceRedraw);
    void drawNoCanStatus();

    // DTC menu screens
    void drawDTCMenu(int selectedItem);
    void drawDTCScanning();
    void drawDTCResults(const DTC* dtcs, int count, int scrollOffset = 0);
    void drawDTCClearing();
    void drawDTCCleared(bool success);
#endif

private:
    GaugeLayout _layout;

#ifdef TARGET_TAB5
    LGFX_Sprite _sprites[4];
    LGFX_Sprite _fullSprite;    // Fullscreen gauge sprite (created on demand)
    GaugeLayout _fullLayout;    // Layout for fullscreen gauge
    float _prevValues[4]  = {-99999, -99999, -99999, -99999};
    int   _prevGaugeIdx[4] = {-1, -1, -1, -1};
    float _prevFullValue = -99999;
    int   _prevFullIdx = -1;
    uint32_t _lastTouchTime = 0;

    // Long-press tracking
    bool     _touching = false;
    int      _touchQuadrant = -1;
    uint32_t _touchStartTime = 0;
    bool     _longPressTriggered = false;
    static constexpr uint32_t LONG_PRESS_MS = 1000;

    // 2x2 grid layout: each cell 640x360, gauge 320x320 centered
    static constexpr int SCREEN_W   = 1280;
    static constexpr int SCREEN_H   = 720;
    static constexpr int CELL_W     = 640;
    static constexpr int CELL_H     = 360;
    static constexpr int GAUGE_SIZE = 320;  // Sprite size per gauge

    int slotX(int slot) const { return (slot % 2) * CELL_W + (CELL_W - GAUGE_SIZE) / 2; }
    int slotY(int slot) const { return (slot / 2) * CELL_H + (CELL_H - GAUGE_SIZE) / 2; }

    // DTC button at center intersection — 80x80 rounded rect
    static constexpr int DTC_BTN_W = 80;
    static constexpr int DTC_BTN_H = 80;
    static constexpr int DTC_BTN_X = (SCREEN_W - DTC_BTN_W) / 2;
    static constexpr int DTC_BTN_Y = (SCREEN_H - DTC_BTN_H) / 2;

#else
    LGFX        _tft;
    LGFX_Sprite _sprite;
    float _prevValue    = -99999.0f;
    int   _prevGaugeIdx = -1;
#endif
};
