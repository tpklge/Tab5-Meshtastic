// Headless LVGL integration test: production widgets and rendering paths.
#include "../main/ui/ui_shell.cpp"
#include "../main/ui/screens/wifi_settings.cpp"
#include "../main/ui/screens/settings_screen.cpp"
#include "message_store.h"
#include <cassert>

static app_settings_t preferences{};
static channel_snapshot_t channel_state{};
static int beep_count;
extern "C" esp_err_t settings_store_set_brightness(uint8_t) { return ESP_OK; }
extern "C" esp_err_t settings_store_set_notif(uint8_t, uint8_t, uint8_t) { return ESP_OK; }
extern "C" void tab5_audio_set_volume(uint8_t) {}
bool app_clock_local(struct tm*) { return false; }
int app_clock_offset_minutes() { return -240; }
void app_clock_format(char* out, size_t capacity, bool) { snprintf(out, capacity, "--:--"); }
esp_err_t app_clock_set(struct tm, int) { return ESP_OK; }
esp_err_t app_clock_set_offset(int) { return ESP_OK; }
static wifi_snapshot_t wifi_model{};
static char requested_ssid[33], requested_password[65];
bool wifi_service_snapshot(wifi_snapshot_t* out) { *out = wifi_model; return true; }
bool wifi_service_scan() { return true; }
bool wifi_service_disconnect(bool) { return true; }
bool wifi_service_sync_time() { return true; }
bool wifi_service_connect(const char* ssid, const char* password, bool) {
    strlcpy(requested_ssid, ssid, sizeof(requested_ssid));
    strlcpy(requested_password, password, sizeof(requested_password));
    return true;
}
extern "C" const app_settings_t* settings_store_get() { return &preferences; }
extern "C" esp_err_t settings_store_set_channel(uint8_t channel) { preferences.sel_channel = channel; return ESP_OK; }
void channel_service_snapshot(channel_snapshot_t* out) { *out = channel_state; }
extern "C" void tab5_audio_beep(audio_pattern_t) { ++beep_count; }
extern "C" int app_send_text(const char*) { return ESP_OK; }
extern "C" esp_err_t message_store_append(uint32_t, uint32_t, uint32_t, uint8_t, bool, bool, int64_t, int64_t, int16_t, int16_t, const char*) { return ESP_OK; }

int main() {
    lv_init();
    lv_display_t* display = lv_display_create(1280, 720);
    app_state_init(); app_state_set_conn(CONN_READY, "READY");
    for (int i = 0; i < 2; ++i) {
        channel_state.known[i] = true;
        channel_state.channels[i].role = i ? meshtastic_Channel_Role_SECONDARY : meshtastic_Channel_Role_PRIMARY;
        strcpy(channel_state.channels[i].settings.name, i ? "Privado" : "LongFast");
    }
    lv_obj_t* panel = make_chat_panel(lv_screen_active());
    for (int i = 0; i < 40; ++i) app_state_add_channel_message(42, "Message for scrolling", false, i, i % 2, false);
    refresh_chat_channels(); append_messages(); lv_obj_update_layout(panel);
    assert(lv_obj_get_child_count(S.chat_list) == 20);
    assert(lv_obj_get_height(S.chat_list) > 400 && lv_obj_get_height(S.chat_list) < 650);
    assert(lv_obj_has_flag(S.chat_list, LV_OBJ_FLAG_CLICKABLE));
    assert(lv_obj_get_scroll_top(S.chat_list) > 100);
    lv_obj_scroll_to_y(S.chat_list, 50, LV_ANIM_OFF);
    assert(lv_obj_get_scroll_y(S.chat_list) == 50);
    app_state_add_channel_message(42, "Incoming while reading", false, 40, 0, false);
    append_messages(); assert(lv_obj_get_scroll_y(S.chat_list) == 50);
    app_state_add_channel_message(42, "Other channel", false, 41, 1, false);
    append_messages(); assert(lv_obj_get_child_count(S.chat_list) == 21);
    assert(lv_obj_get_scroll_y(S.chat_list) == 50);
    lv_textarea_set_text(S.chat_input, "draft LongFast");
    preferences.sel_channel = 1; refresh_chat_channels(); append_messages();
    assert(lv_obj_get_child_count(S.chat_list) == 21);
    assert(!lv_textarea_get_text(S.chat_input)[0]);
    lv_textarea_set_text(S.chat_input, "draft private");
    preferences.sel_channel = 0; refresh_chat_channels(); append_messages();
    assert(!strcmp(lv_textarea_get_text(S.chat_input), "draft LongFast"));
    preferences.sel_channel = 1; append_messages();
    assert(!strcmp(lv_textarea_get_text(S.chat_input), "draft private"));
    for (int i = 42; i < 180; ++i) {
        app_state_add_channel_message(42, "Ring wrapped", false, i, i % 2, false); append_messages();
    }
    assert(lv_obj_get_child_count(S.chat_list) == APP_MAX_MSGS / 2);
    // Large bursts must remain bounded and never underflow the notification loop.
    preferences.notif_en = 1;
    for (int i = 180; i < 400; ++i) app_state_add_channel_message(42, "Burst", false, i, i % 2, false);
    append_messages(); assert(lv_obj_get_child_count(S.chat_list) == APP_MAX_MSGS / 2);
    assert(beep_count == 1);
    // Exercise actual Wi-Fi editor layout and physical-keyboard routing.
    wifi_model.count = 1; wifi_model.generation = 1;
    strcpy(wifi_model.networks[0].ssid, "Minha rede");
    settings_screen_make(lv_screen_active());
    assert(lv_dropdown_get_option_count(s_clock_fields[2]) == 100);
    assert(lv_dropdown_get_option_count(s_clock_fields[5]) == 105);
    configure(nullptr); lv_obj_update_layout(modal);
    assert(!strcmp(lv_textarea_get_text(ssid_input), "Minha rede"));
    assert(lv_textarea_get_password_mode(password_input));
    lv_area_t bounds; lv_obj_get_coords(keyboard, &bounds);
    assert(bounds.y2 < 720);
    wifi_settings_key("secret123", false, false);
    wifi_settings_key(nullptr, false, true);
    assert(!modal && !password_input);
    assert(!strcmp(requested_ssid, "Minha rede") && !strcmp(requested_password, "secret123"));
    configure(nullptr); wifi_settings_leave(); assert(!modal && !focused);
    lv_display_delete(display);
    puts("PASS: headless LVGL chat: scroll range, viewport preservation, channel filtering, drafts, bounded history, burst notifications and full settings/clock/Wi-Fi construction.");
}
