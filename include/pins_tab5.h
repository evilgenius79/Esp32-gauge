#pragma once

// =============================================================================
// M5Stack Tab5 (ESP32-P4) - Pin Definitions
//
// Display & touch are handled by M5Unified — no pin defines needed.
// CAN Bus: connect M5Stack Mini CAN Unit to Port A (Grove HY2.0-4P)
// or GPIO_EXT header. Adjust pins below to match your wiring.
// =============================================================================

// ---- CAN Bus (via Port A Grove HY2.0-4P) ----
#define PIN_CAN_TX      53
#define PIN_CAN_RX      54
