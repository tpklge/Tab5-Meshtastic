#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <map>
#include <string>

using nvs_handle_t = unsigned;
constexpr int NVS_READWRITE = 1;
inline std::map<std::string, uint32_t> test_nvs;
inline esp_err_t nvs_open(const char*, int, nvs_handle_t* handle) { *handle = 1; return ESP_OK; }
inline esp_err_t nvs_get_u32(nvs_handle_t, const char* key, uint32_t* value) {
    auto entry = test_nvs.find(key);
    if (entry == test_nvs.end()) return ESP_ERR_INVALID_STATE;
    *value = entry->second;
    return ESP_OK;
}
inline esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* value) {
    uint32_t stored;
    esp_err_t err = nvs_get_u32(handle, key, &stored);
    if (err == ESP_OK) *value = stored;
    return err;
}
inline esp_err_t nvs_set_u32(nvs_handle_t, const char* key, uint32_t value) {
    test_nvs[key] = value;
    return ESP_OK;
}
inline esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
    return nvs_set_u32(handle, key, value);
}
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
