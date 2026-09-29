#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mount point for the SPIFFS storage partition (label "storage").
#define STORAGE_MOUNT_POINT "/spiffs"
#define STORAGE_PARTITION_LABEL "storage"

// Initialize SPIFFS. Returns ESP_OK if mounted, ESP_ERR_NOT_FOUND if the
// partition doesn't exist, or another error on failure.
// On failure the app continues in RAM-only mode.
esp_err_t app_storage_init(void);

// Returns true if storage is available (mounted successfully).
bool app_storage_available(void);

// Unmount SPIFFS (e.g. before reformat — never called automatically).
void app_storage_deinit(void);

#ifdef __cplusplus
}
#endif
