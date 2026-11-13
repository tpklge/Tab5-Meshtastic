#include "sd_kv.h"
#include "sd_storage.h"
#include <cstdio>
#include <cerrno>
#include <mutex>
#include <unistd.h>
namespace {
std::mutex mutex;
bool path(char* out, size_t size, sd_kv_handle_t h, const char* key) {
    if (!h || !key || !sd_storage_available()) return false;
    int n = snprintf(out, size, SD_MOUNT_POINT "/%s_%s.bin", h, key);
    return n > 0 && static_cast<size_t>(n) < size;
}
}
esp_err_t sd_kv_open(const char* ns, int, sd_kv_handle_t* out) {
    if (!ns || !out) return ESP_ERR_INVALID_ARG;
    *out = nullptr;
    if (!sd_storage_available()) return ESP_ERR_INVALID_STATE;
    *out = ns;
    return ESP_OK;
}
esp_err_t sd_kv_get_blob(sd_kv_handle_t h, const char* key, void* data, size_t* size) {
    std::lock_guard<std::mutex> guard(mutex);
    char filename[128];
    if (!data || !size) return ESP_ERR_INVALID_ARG;
    if (!path(filename, sizeof(filename), h, key)) return ESP_ERR_INVALID_STATE;
    FILE* f = fopen(filename, "rb");
    if (!f) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    bool ok = fseek(f, 0, SEEK_END) == 0;
    long length = ok ? ftell(f) : -1;
    ok = length >= 0 && static_cast<size_t>(length) == *size && fseek(f, 0, SEEK_SET) == 0;
    if (ok) ok = fread(data, 1, *size, f) == *size;
    ok = (fgetc(f) == EOF && !ferror(f)) && ok;
    if (fclose(f) != 0) ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}
esp_err_t sd_kv_set_blob(sd_kv_handle_t h, const char* key, const void* data, size_t size) {
    std::lock_guard<std::mutex> guard(mutex);
    char filename[128], temporary[136];
    if (size && !data) return ESP_ERR_INVALID_ARG;
    if (!path(filename, sizeof(filename), h, key)) return ESP_ERR_INVALID_STATE;
    snprintf(temporary, sizeof(temporary), "%s.tmp", filename);
    FILE* f = fopen(temporary, "wb");
    if (!f) return ESP_FAIL;
    bool ok = fwrite(data, 1, size, f) == size;
    if (fflush(f) != 0) ok = false;
    if (fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (ok && rename(temporary, filename) == 0) return ESP_OK;
    remove(temporary);
    return ESP_FAIL;
}
esp_err_t sd_kv_erase_key(sd_kv_handle_t h, const char* key) {
    std::lock_guard<std::mutex> guard(mutex);
    char filename[128];
    if (!path(filename, sizeof(filename), h, key)) return ESP_ERR_INVALID_STATE;
    if (remove(filename) == 0) return ESP_OK;
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}
esp_err_t sd_kv_commit(sd_kv_handle_t h) {
    return h && sd_storage_available() ? ESP_OK : ESP_ERR_INVALID_STATE;
}
