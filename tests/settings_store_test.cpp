#include "../main/storage/settings_store.cpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
#include <unistd.h>
#include "../main/storage/sd_kv.cpp"
static bool mounted = true;
bool sd_storage_available() { return mounted; }

int main()
{
    const app_settings_t defaults = s_settings;
    char directory[] = "/tmp/tab5-sd-XXXXXX";
    assert(mkdtemp(directory));
    assert(chdir(directory) == 0);
    sd_kv_handle_t handle;
    assert(sd_kv_open("t5app", SD_KV_READWRITE, &handle) == ESP_OK);
    assert(sd_kv_set_u8(handle, "brightness", 65) == ESP_OK);
    assert(sd_kv_set_u8(handle, "transport", 0) == ESP_OK);
    assert(settings_store_init() == ESP_OK);
    assert(settings_store_get()->screen_timeout_s == 120);
    assert(settings_store_get()->brightness == 65 && settings_store_get()->transport == 0);
    for (uint32_t seconds : {0u, 30u, 60u, 120u, 300u, 600u}) {
        assert(settings_store_set_screen_timeout(seconds) == ESP_OK);
        s_settings = defaults; // Simulate reboot with only SD retaining values.
        assert(settings_store_init() == ESP_OK);
        assert(settings_store_get()->screen_timeout_s == seconds);
        assert(settings_store_get()->brightness == 65 && settings_store_get()->transport == 0);
    }
    assert(settings_store_set_screen_timeout(1) == ESP_ERR_INVALID_ARG);
    assert(settings_store_set_screen_timeout(601) == ESP_ERR_INVALID_ARG);
    assert(settings_store_get()->screen_timeout_s == 600);
    assert(sd_kv_set_u32(handle, "screen_timeout", UINT32_MAX) == ESP_OK);
    s_settings = defaults;
    assert(settings_store_init() == ESP_OK && settings_store_get()->screen_timeout_s == 120);
    // Wrong-sized files must fail without overwriting the caller's value.
    assert(sd_kv_set_u8(handle, "corrupt", 7) == ESP_OK);
    uint32_t untouched = 12345;
    assert(sd_kv_get_u32(handle, "corrupt", &untouched) == ESP_FAIL);
    assert(untouched == 12345);
    assert(sd_kv_erase_key(handle, "corrupt") == ESP_OK);
    assert(sd_kv_erase_key(handle, "corrupt") == ESP_ERR_NOT_FOUND);
    assert(sd_kv_get_u32(handle, "missing", &untouched) == ESP_ERR_NOT_FOUND);
    mounted = false;
    assert(settings_store_set_brightness(80) == ESP_ERR_INVALID_STATE);
    assert(sd_kv_open("t5app", SD_KV_READWRITE, &handle) == ESP_ERR_INVALID_STATE);
    mounted = true;
    for (const char* key : {"transport", "brightness", "notif_en", "notif_vol", "notif_pat", "sel_ch", "screen_timeout"}) {
        assert(sd_kv_erase_key("t5app", key) == ESP_OK);
    }
    assert(chdir("/") == 0);
    assert(rmdir(directory) == 0);
    puts("PASS: timeout default, SD files preserving settings, persistence of every option and invalid-value handling.");
}
