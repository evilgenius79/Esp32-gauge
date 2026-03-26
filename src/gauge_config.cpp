#include "gauges.h"
#include <Preferences.h>
#include <cstring>

// Mutable gauge array — initialized from defaults, then overwritten by NVS
GaugeConfig GAUGES[NUM_GAUGES];

// CAN bus speed (kbps) — default 500
uint32_t canBusSpeed = 500;

static Preferences prefs;
static const char* NVS_NAMESPACE = "gauges";

void loadGaugeConfigs() {
    // Start with defaults
    memcpy(GAUGES, DEFAULT_GAUGES, sizeof(GAUGES));

    // Try to load saved configs from NVS
    if (prefs.begin(NVS_NAMESPACE, true)) {  // read-only
        for (int i = 0; i < NUM_GAUGES; i++) {
            char key[8];
            snprintf(key, sizeof(key), "g%d", i);
            size_t len = prefs.getBytesLength(key);
            if (len == sizeof(GaugeConfig)) {
                prefs.getBytes(key, &GAUGES[i], sizeof(GaugeConfig));
            }
        }
        canBusSpeed = prefs.getUInt("canspd", 500);
        if (canBusSpeed != 250 && canBusSpeed != 500) canBusSpeed = 500;
        prefs.end();
        Serial.printf("[NVS] Loaded configs, CAN speed: %lu kbps\n", canBusSpeed);
    } else {
        Serial.println("[NVS] No saved configs, using defaults");
    }
}

void saveGaugeConfig(int index) {
    if (index < 0 || index >= NUM_GAUGES) return;

    if (prefs.begin(NVS_NAMESPACE, false)) {  // read-write
        char key[8];
        snprintf(key, sizeof(key), "g%d", index);
        prefs.putBytes(key, &GAUGES[index], sizeof(GaugeConfig));
        prefs.end();
        Serial.printf("[NVS] Saved gauge %d: %s\n", index, GAUGES[index].name);
    }
}

void resetGaugeConfig(int index) {
    if (index < 0 || index >= NUM_GAUGES) return;
    memcpy(&GAUGES[index], &DEFAULT_GAUGES[index], sizeof(GaugeConfig));
    saveGaugeConfig(index);
    Serial.printf("[NVS] Reset gauge %d to default\n", index);
}

void saveCanSpeed() {
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUInt("canspd", canBusSpeed);
        prefs.end();
        Serial.printf("[NVS] Saved CAN speed: %lu kbps\n", canBusSpeed);
    }
}
