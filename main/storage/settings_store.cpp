#include "settings_store.h"
#include "app_storage.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "settings_store";
static const char* SETTINGS_PATH = STORAGE_MOUNT_POINT "/settings.bin";

#define SETTINGS_MAGIC 0x54354D01u
#define SETTINGS_VER   1

typedef struct __attribute__((packed)) {
    uint32_t       magic;
    uint8_t        version;
    app_settings_t s;
    uint8_t        _pad[2];
    uint32_t       crc32;
} settings_file_t;

static app_settings_t s_settings = {
    .transport   = 1,   // UART (RAK3172H via Grove)
    .brightness  = 80,
    .notif_en    = 1,
    .notif_vol   = 70,
    .notif_pat   = 1,   // short beep
    .sel_channel = 0,
};

static uint32_t crc_of(const settings_file_t* f)
{
    return esp_crc32_le(0, (const uint8_t*)f, offsetof(settings_file_t, crc32));
}

esp_err_t settings_store_init(void)
{
    if (!app_storage_available()) {
        ESP_LOGW(TAG, "storage unavailable — using defaults");
        return ESP_OK;
    }

    FILE* f = fopen(SETTINGS_PATH, "rb");
    if (!f) {
        // First run: write defaults
        return settings_store_save(&s_settings);
    }

    settings_file_t file = {};
    bool ok = (fread(&file, sizeof(file), 1, f) == 1);
    fclose(f);

    if (!ok || file.magic != SETTINGS_MAGIC || file.version != SETTINGS_VER
            || file.crc32 != crc_of(&file)) {
        ESP_LOGW(TAG, "settings invalid — using defaults");
        return settings_store_save(&s_settings);
    }

    s_settings = file.s;
    ESP_LOGI(TAG, "loaded: transport=%d brightness=%d", s_settings.transport, s_settings.brightness);
    return ESP_OK;
}

const app_settings_t* settings_store_get(void)
{
    return &s_settings;
}

esp_err_t settings_store_save(const app_settings_t* s)
{
    if (s) s_settings = *s;

    if (!app_storage_available()) return ESP_OK;

    settings_file_t file = {};
    file.magic   = SETTINGS_MAGIC;
    file.version = SETTINGS_VER;
    file.s       = s_settings;
    file.crc32   = crc_of(&file);

    // Atomic write via temp file
    const char* tmp = STORAGE_MOUNT_POINT "/settings.tmp";
    FILE* f = fopen(tmp, "wb");
    if (!f) return ESP_FAIL;
    bool ok = (fwrite(&file, sizeof(file), 1, f) == 1);
    fflush(f);
    fclose(f);
    if (!ok) { remove(tmp); return ESP_FAIL; }

    remove(SETTINGS_PATH);
    if (rename(tmp, SETTINGS_PATH) != 0) { remove(tmp); return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t settings_store_set_transport(uint8_t t)  { s_settings.transport = t;  return settings_store_save(nullptr); }
esp_err_t settings_store_set_brightness(uint8_t b) { s_settings.brightness = b; return settings_store_save(nullptr); }
esp_err_t settings_store_set_channel(uint8_t idx)  { s_settings.sel_channel = idx; return settings_store_save(nullptr); }

esp_err_t settings_store_set_notif(uint8_t en, uint8_t vol, uint8_t pat)
{
    s_settings.notif_en  = en;
    s_settings.notif_vol = vol;
    s_settings.notif_pat = pat;
    return settings_store_save(nullptr);
}
