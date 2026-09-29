#include "settings_store.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "settings_store";
static const char* NS  = "t5app";

static app_settings_t s_settings = {
    .transport   = 1,
    .brightness  = 80,
    .notif_en    = 1,
    .notif_vol   = 70,
    .notif_pat   = 1,
    .sel_channel = 0,
};

static nvs_handle_t s_nvs = 0;

static void load_from_nvs(void)
{
    uint8_t v;
#define LOAD8(key, field) if (nvs_get_u8(s_nvs, key, &v) == ESP_OK) s_settings.field = v
    LOAD8("transport",  transport);
    LOAD8("brightness", brightness);
    LOAD8("notif_en",   notif_en);
    LOAD8("notif_vol",  notif_vol);
    LOAD8("notif_pat",  notif_pat);
    LOAD8("sel_ch",     sel_channel);
#undef LOAD8
}

static esp_err_t flush(void)
{
    if (!s_nvs) return ESP_ERR_INVALID_STATE;
    nvs_set_u8(s_nvs, "transport",  s_settings.transport);
    nvs_set_u8(s_nvs, "brightness", s_settings.brightness);
    nvs_set_u8(s_nvs, "notif_en",   s_settings.notif_en);
    nvs_set_u8(s_nvs, "notif_vol",  s_settings.notif_vol);
    nvs_set_u8(s_nvs, "notif_pat",  s_settings.notif_pat);
    nvs_set_u8(s_nvs, "sel_ch",     s_settings.sel_channel);
    return nvs_commit(s_nvs);
}

esp_err_t settings_store_init(void)
{
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        s_nvs = 0;
        return err;
    }
    load_from_nvs();
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
    return flush();
}

esp_err_t settings_store_set_transport(uint8_t t)  { s_settings.transport = t;  return flush(); }
esp_err_t settings_store_set_brightness(uint8_t b) { s_settings.brightness = b; return flush(); }
esp_err_t settings_store_set_channel(uint8_t idx)  { s_settings.sel_channel = idx; return flush(); }

esp_err_t settings_store_set_notif(uint8_t en, uint8_t vol, uint8_t pat)
{
    s_settings.notif_en  = en;
    s_settings.notif_vol = vol;
    s_settings.notif_pat = pat;
    return flush();
}
