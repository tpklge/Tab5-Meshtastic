#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
// Each key is a file in /sdcard. Namespace and key are application constants.
using sd_kv_handle_t = const char*;
constexpr int SD_KV_READWRITE = 0;
esp_err_t sd_kv_open(const char*, int, sd_kv_handle_t*);
esp_err_t sd_kv_get_blob(sd_kv_handle_t, const char*, void*, size_t*);
esp_err_t sd_kv_set_blob(sd_kv_handle_t, const char*, const void*, size_t);
esp_err_t sd_kv_erase_key(sd_kv_handle_t, const char*);
esp_err_t sd_kv_commit(sd_kv_handle_t);
#define SD_KV_SCALAR(type, suffix) \
inline esp_err_t sd_kv_get_##suffix(sd_kv_handle_t h, const char* k, type* v) { size_t n = sizeof(*v); return sd_kv_get_blob(h, k, v, &n); } \
inline esp_err_t sd_kv_set_##suffix(sd_kv_handle_t h, const char* k, type v) { return sd_kv_set_blob(h, k, &v, sizeof(v)); }
SD_KV_SCALAR(uint8_t, u8)
SD_KV_SCALAR(uint32_t, u32)
SD_KV_SCALAR(int16_t, i16)
#undef SD_KV_SCALAR
