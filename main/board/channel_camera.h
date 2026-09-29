#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
constexpr int QR_PREVIEW_W = 320;
constexpr int QR_PREVIEW_H = 180;
esp_err_t channel_camera_start();
void channel_camera_stop();
bool channel_camera_poll(char* result, size_t cap, char* status, size_t status_cap,
                         uint16_t* preview, uint32_t* generation);
