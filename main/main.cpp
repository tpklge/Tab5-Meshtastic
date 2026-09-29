/*
 * Tab5-Meshtastic v2 — Milestone 1: BLE transport + sync engine.
 * -------------------------------------------------------------
 * Boot path: NVS + AppState → board bring-up → power the C6 (wlan_power) →
 * display + LVGL shell → esp_hosted (P4<->C6 SDIO) → NimBLE host → BLE
 * transport (scan/bond/sync to the fixed test radio).
 *
 * Ordering is load-bearing (PRD §4): board.begin() + wlan_power(true) must
 * precede esp_hosted_init(), which must precede nimble_port_init(). The shell
 * is built before the radio comes up so the user always sees chrome immediately.
 *
 * Cold-boot discipline (PRD §9.5): any change to this boot path / peripherals /
 * power rails is validated on a REAL cold battery boot — serial resets the P4.
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "m5_tab5_component.h"
#include "esp_hosted.h"
#include "nimble/nimble_port.h"

#include "lcd_tools.h"
#include "ui_shell.h"
#include "keyboard.h"
#include "app_state.h"
#include "settings.h"
#include "ble_transport.h"
#include "uart_transport.h"
#include "mesh_session.h"
#include "app_storage.h"
#include "settings_store.h"
#include "message_store.h"
#include "tab5_audio.h"
#include "app_commands.h"

static UartTransport s_uart_transport;
static MeshSession   s_mesh_session;

static const char* TAG = "tab5-mesh-v2";

static m5::tab5::m5tab5_component s_board;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Tab5-Meshtastic v2 — M1 BLE transport + sync engine");

    app_state_init();

    /* NVS: settings + the NimBLE bond store. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    settings_init();   /* load saved BLE device(s) before transport starts */

    /* Persistent storage via NVS. app_storage (SPIFFS) kept for future use but
     * settings_store and message_store now use NVS directly. */
    app_storage_init();
    settings_store_init();
    message_store_init();

    /* Restore persisted messages into AppState so history shows on first load. */
    {
        static msg_loaded_t hist[30];
        uint32_t n = message_store_load_page(0xFF, 0, hist, 30);
        for (uint32_t i = 0; i < n; i++) {
            bool is_self = (hist[i].direction == 1);
            app_state_add_message(hist[i].from_node, hist[i].text, is_self, hist[i].timestamp_us);
        }
        if (n) ESP_LOGI(TAG, "restored %lu messages from NVS", (unsigned long)n);
    }

    /* Board bring-up, then power the C6 coprocessor BEFORE esp_hosted. */
    m5::tab5::m5tab5_component_config_t board_cfg = {};
    ESP_ERROR_CHECK(s_board.begin(board_cfg));
    ESP_LOGI(TAG, "powering C6 (wlan_power=ON)");
    ESP_ERROR_CHECK(s_board.wlan_power(true));
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Display + LVGL shell first, so chrome is visible while the radio connects. */
    ESP_ERROR_CHECK(app_lcd_lvgl_init(s_board));

    /* Restore saved brightness before the first frame is rendered. */
    lcd_set_brightness(settings_store_get()->brightness);

    ui_start();

    /* Physical Tab5 keyboard (optional accessory) — feeds the chat composer and
     * PIN entry. Best-effort: absence is logged, not fatal. */
    kbd_start();

    /* Audio: uses BSP I2C bus handle (m5tab5_get_sys_i2c_master_bus_handle),
     * no second bus created — safe to init after board.begin(). */
    tab5_audio_init();
    tab5_audio_set_volume(settings_store_get()->notif_vol);

    const uint8_t transport = settings_store_get()->transport;
    ESP_LOGI(TAG, "transport mode: %s", transport == 0 ? "BLE" : "UART");

    if (transport == 0) {
        /* BLE via onboard C6: bring up SDIO tunnel, then NimBLE host. */
        app_state_set_conn(CONN_BOOT, "ESP_HOST");
        ESP_ERROR_CHECK(esp_hosted_init());

        app_state_set_conn(CONN_BOOT, "NIMBLE");
        ret = nimble_port_init();
        if (ret != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(ret)); return; }

        app_state_set_conn(CONN_BOOT, "BLE START");
        ble_transport_start();
    } else {
        /* UART transport (RAK3172H via Grove): no BLE/NimBLE/esp_hosted needed. */
        app_state_set_conn(CONN_BOOT, "UART");
        s_mesh_session.attach_transport(&s_uart_transport);
        ESP_ERROR_CHECK(s_uart_transport.start());
        ESP_ERROR_CHECK(s_mesh_session.start());
    }

    ESP_LOGI(TAG, "init done; transport=%d", transport);
}

extern "C" void app_send_text(const char* text)
{
    if (!text || !text[0]) return;
    if (settings_store_get()->transport == 0) {
        ble_transport_send_text(text);
    } else {
        s_mesh_session.send_text(text);
    }
}
