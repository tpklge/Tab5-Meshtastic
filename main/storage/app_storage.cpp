#include "app_storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"

static const char* TAG = "app_storage";
static bool s_available = false;

esp_err_t app_storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = STORAGE_MOUNT_POINT,
        .partition_label        = STORAGE_PARTITION_LABEL,
        .max_files              = 8,
        .format_if_mount_failed = false,  // never auto-format
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "storage partition not found — running in RAM-only mode");
        s_available = false;
        return err;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s — RAM-only mode", esp_err_to_name(err));
        s_available = false;
        return err;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(STORAGE_PARTITION_LABEL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %zu/%zu bytes used", used, total);
    s_available = true;
    return ESP_OK;
}

bool app_storage_available(void)
{
    return s_available;
}

void app_storage_deinit(void)
{
    if (s_available) {
        esp_vfs_spiffs_unregister(STORAGE_PARTITION_LABEL);
        s_available = false;
    }
}
