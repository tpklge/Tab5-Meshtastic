#include "settings_store.h"
#include "esp_log.h"
#include "sd_kv.h"
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
    .screen_timeout_s = 120,
};

static sd_kv_handle_t s_storage = 0;

static void load_from_sd(void)
{
    uint8_t v;
#define LOAD8(key, field) if (sd_kv_get_u8(s_storage, key, &v) == ESP_OK) s_settings.field = v
    LOAD8("transport",  transport);
    LOAD8("brightness", brightness);
    LOAD8("notif_en",   notif_en);
    LOAD8("notif_vol",  notif_vol);
    LOAD8("notif_pat",  notif_pat);
    LOAD8("sel_ch",     sel_channel);
#undef LOAD8
    uint32_t timeout;
    if (sd_kv_get_u32(s_storage, "screen_timeout", &timeout) == ESP_OK &&
        (timeout == 0 || (timeout >= 30 && timeout <= 600))) {
        s_settings.screen_timeout_s = timeout;
    }
}

static esp_err_t flush(void)
{
    if (!s_storage) return ESP_ERR_INVALID_STATE;
    esp_err_t err;
#define SAVE(call) do { err = (call); if (err != ESP_OK) return err; } while (0)
    SAVE(sd_kv_set_u8(s_storage, "transport", s_settings.transport));
    SAVE(sd_kv_set_u8(s_storage, "brightness", s_settings.brightness));
    SAVE(sd_kv_set_u8(s_storage, "notif_en", s_settings.notif_en));
    SAVE(sd_kv_set_u8(s_storage, "notif_vol", s_settings.notif_vol));
    SAVE(sd_kv_set_u8(s_storage, "notif_pat", s_settings.notif_pat));
    SAVE(sd_kv_set_u8(s_storage, "sel_ch", s_settings.sel_channel));
    SAVE(sd_kv_set_u32(s_storage, "screen_timeout", s_settings.screen_timeout_s));
#undef SAVE
    return sd_kv_commit(s_storage);
}

esp_err_t settings_store_init(void)
{
    esp_err_t err = sd_kv_open(NS, SD_KV_READWRITE, &s_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sd_kv_open: %s", esp_err_to_name(err));
        s_storage = 0;
        return err;
    }
    load_from_sd();
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

esp_err_t settings_store_set_screen_timeout(uint32_t seconds)
{
    if (seconds != 0 && (seconds < 30 || seconds > 600)) return ESP_ERR_INVALID_ARG;
    s_settings.screen_timeout_s = seconds;
    return flush();
}

esp_err_t settings_store_set_notif(uint8_t en, uint8_t vol, uint8_t pat)
{
    s_settings.notif_en  = en;
    s_settings.notif_vol = vol;
    s_settings.notif_pat = pat;
    return flush();
}
