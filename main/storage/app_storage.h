#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shared microSD mount point.
#define STORAGE_MOUNT_POINT "/sdcard"

// Check storage after sd_storage_init(); no automatic formatting.
esp_err_t app_storage_init(void);

// Returns true if storage is available (mounted successfully).
bool app_storage_available(void);

// Unmount the microSD.
void app_storage_deinit(void);

#ifdef __cplusplus
}
#endif
