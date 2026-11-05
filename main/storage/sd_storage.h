#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mount point for the microSD card (SDSPI on SPI3, CLK=43, MOSI=44, MISO=39, CS=42).
#define SD_MOUNT_POINT "/sdcard"

// Attempt to initialize and mount the microSD card. Returns ESP_OK if mounted,
// ESP_ERR_NOT_FOUND if no card detected (so callers can gracefully fall back
// to RAM-only mode), or another error code if the mount failed.
esp_err_t sd_storage_init(void);

// True iff the SD card is mounted and ready for I/O.
bool sd_storage_available(void);

// Unmount the SD card. Safe to call even if init failed.
void sd_storage_deinit(void);

#ifdef __cplusplus
}
#endif
