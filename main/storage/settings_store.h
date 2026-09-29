#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Application settings persisted to SPIFFS (settings.bin).
// NVS is used only for NimBLE bonds and BLE device list (settings.cpp).
typedef struct {
    uint8_t transport;    // 0=BLE, 1=UART
    uint8_t brightness;   // 5–100 %
    uint8_t notif_en;     // 0/1
    uint8_t notif_vol;    // 0–100
    uint8_t notif_pat;    // 0=silent,1=short,2=double,3=triple
    uint8_t sel_channel;  // 0–7 selected chat channel index
} app_settings_t;

// Initialize (load from file or create with defaults).
// Returns ESP_OK even if storage is unavailable (uses RAM defaults).
esp_err_t settings_store_init(void);

// Get current settings (always valid, falls back to defaults).
const app_settings_t* settings_store_get(void);

// Update and persist. Individual fields may be set before calling.
esp_err_t settings_store_save(const app_settings_t* s);

// Convenience setters — saves immediately.
esp_err_t settings_store_set_transport(uint8_t transport);
esp_err_t settings_store_set_brightness(uint8_t brightness);
esp_err_t settings_store_set_notif(uint8_t en, uint8_t vol, uint8_t pat);
esp_err_t settings_store_set_channel(uint8_t idx);

#ifdef __cplusplus
}
#endif
