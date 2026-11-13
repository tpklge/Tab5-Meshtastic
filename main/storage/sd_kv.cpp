#include "sd_kv.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if !defined(CONFIG_FATFS_LFN_HEAP) && !defined(CONFIG_FATFS_LFN_STACK)
#error "microSD settings require FAT long filename support (CONFIG_FATFS_LFN_HEAP)"
#endif
#endif
#include "sd_storage.h"
#include "esp_log.h"
#include <cstdio>
#include <cerrno>
#include <mutex>
#include <unistd.h>
#include <sys/stat.h>
namespace {
std::mutex mutex;
esp_err_t io_error(const char* operation, const char* filename) {
    ESP_LOGE("sd_kv", "%s failed for %s (errno=%d)", operation, filename, errno);
    return ESP_FAIL;
}
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
    if (!f && errno == ENOENT) {
        // Recover the previous value if power was lost between the two renames.
        char backup[136];
        snprintf(backup, sizeof(backup), "%s.bak", filename);
        f = fopen(backup, "rb");
    }
    if (!f) return errno == ENOENT ? ESP_ERR_NOT_FOUND : io_error("open", filename);
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
    char filename[128], temporary[136], backup[136];
    if (size && !data) return ESP_ERR_INVALID_ARG;
    if (!path(filename, sizeof(filename), h, key)) return ESP_ERR_INVALID_STATE;
    snprintf(temporary, sizeof(temporary), "%s.tmp", filename);
    snprintf(backup, sizeof(backup), "%s.bak", filename);
    FILE* f = fopen(temporary, "wb");
    if (!f) return io_error("open temporary", filename);
    bool ok = fwrite(data, 1, size, f) == size;
    if (!ok) io_error("write", filename);
    if (fflush(f) != 0) { io_error("flush", filename); ok = false; }
    if (fsync(fileno(f)) != 0) { io_error("sync", filename); ok = false; }
    if (fclose(f) != 0) { io_error("close", filename); ok = false; }
    if (!ok) { remove(temporary); return ESP_FAIL; }

    // FatFs f_rename does not replace an existing destination (EEXIST).
    // Preserve the old value before publishing the fully synced new file.
    struct stat info;
    bool previous = stat(filename, &info) == 0;
    if (!previous && errno != ENOENT) {
        io_error("stat", filename); remove(temporary); return ESP_FAIL;
    }
    if (previous) {
        if (remove(backup) != 0 && errno != ENOENT) {
            io_error("remove backup", filename); remove(temporary); return ESP_FAIL;
        }
        if (rename(filename, backup) != 0) {
            io_error("backup", filename); remove(temporary); return ESP_FAIL;
        }
    }
    if (rename(temporary, filename) != 0) {
        io_error("publish", filename);
        if (previous && rename(backup, filename) != 0) io_error("restore", filename);
        remove(temporary);
        return ESP_FAIL;
    }
    // A leftover backup is harmless: reads prefer the committed file.
    if (remove(backup) != 0 && errno != ENOENT) io_error("cleanup backup", filename);
    return ESP_OK;
}
esp_err_t sd_kv_erase_key(sd_kv_handle_t h, const char* key) {
    std::lock_guard<std::mutex> guard(mutex);
    char filename[128], backup[136];
    if (!path(filename, sizeof(filename), h, key)) return ESP_ERR_INVALID_STATE;
    snprintf(backup, sizeof(backup), "%s.bak", filename);
    bool removed_backup = remove(backup) == 0;
    if (!removed_backup && errno != ENOENT) return io_error("erase backup", filename);
    if (remove(filename) == 0) return ESP_OK;
    if (errno == ENOENT) return removed_backup ? ESP_OK : ESP_ERR_NOT_FOUND;
    return io_error("erase", filename);
}

esp_err_t sd_kv_commit(sd_kv_handle_t h) {
    return h && sd_storage_available() ? ESP_OK : ESP_ERR_INVALID_STATE;
}
