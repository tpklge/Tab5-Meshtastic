#include "../main/storage/settings_store.cpp"
#include <cassert>
#include <cstdio>

int main()
{
    const app_settings_t defaults = s_settings;
    test_nvs["brightness"] = 65;
    test_nvs["transport"] = 0;
    assert(settings_store_init() == ESP_OK);
    assert(settings_store_get()->screen_timeout_s == 120);
    assert(settings_store_get()->brightness == 65 && settings_store_get()->transport == 0);
    for (uint32_t seconds : {0u, 30u, 60u, 120u, 300u, 600u}) {
        assert(settings_store_set_screen_timeout(seconds) == ESP_OK);
        s_settings = defaults; // Simulate reboot with only NVS retaining values.
        assert(settings_store_init() == ESP_OK);
        assert(settings_store_get()->screen_timeout_s == seconds);
        assert(settings_store_get()->brightness == 65 && settings_store_get()->transport == 0);
    }
    assert(settings_store_set_screen_timeout(1) == ESP_ERR_INVALID_ARG);
    assert(settings_store_set_screen_timeout(601) == ESP_ERR_INVALID_ARG);
    assert(settings_store_get()->screen_timeout_s == 600 && test_nvs["screen_timeout"] == 600);
    test_nvs["screen_timeout"] = UINT32_MAX;
    s_settings = defaults;
    assert(settings_store_init() == ESP_OK && settings_store_get()->screen_timeout_s == 120);
    puts("PASS: timeout default, migration preserving settings, persistence of every option and invalid-value handling.");
}
