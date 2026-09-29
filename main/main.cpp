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
#include "app_storage.h"
#include "settings_store.h"
#include "message_store.h"
#include "lcd_tools.h"
#include "tab5_audio.h"

/* transport_drv.h is private to esp_hosted; forward-declare the readiness
 * probe so we can poll before calling nimble_port_init(). */
extern "C" uint8_t is_transport_tx_ready(void);

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

    /* Persistent SPIFFS storage (messages, settings). Non-fatal if absent. */
    app_storage_init();
    settings_store_init();
    message_store_init();

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

    /* Audio: non-fatal if ES8388/I2S unavailable. */
    tab5_audio_init();
    tab5_audio_set_volume(settings_store_get()->notif_vol);

    /* P4<->C6 transport, then the NimBLE host, then our BLE transport.
     * Stage labels are shown on-screen (status chip) so we can see where
     * a cold-boot hangs without attaching serial (which resets the P4). */
    app_state_set_conn(CONN_BOOT, "ESP_HOST");
    ESP_LOGI(TAG, "esp_hosted_init()");
    ESP_ERROR_CHECK(esp_hosted_init());

    /* esp_hosted_init() starts the SDIO handshake asynchronously and returns
     * before the transport is TX-ready.  ble_transport_ll_init() (called from
     * nimble_port_init()) will reset the C6 and retry 1000×1 s if called while
     * the transport is still coming up.  Poll until TX-ready (max 10 s). */
    app_state_set_conn(CONN_BOOT, "WAIT C6");
    {
        const int kMaxMs   = 30000;
        const int kLogEvery = 5000;
        int elapsed = 0;
        int next_log = kLogEvery;
        while (!is_transport_tx_ready() && elapsed < kMaxMs) {
            vTaskDelay(pdMS_TO_TICKS(50));
            elapsed += 50;
            if (elapsed >= next_log) {
                ESP_LOGI(TAG, "SDIO not TX-ready yet (%d ms)…", elapsed);
                next_log += kLogEvery;
            }
        }
        if (!is_transport_tx_ready()) {
            ESP_LOGE(TAG, "SDIO transport not TX-ready after %d ms — continuing anyway", kMaxMs);
        } else {
            ESP_LOGI(TAG, "SDIO transport TX-ready after %d ms", elapsed);
        }
    }

    app_state_set_conn(CONN_BOOT, "NIMBLE");
    ret = nimble_port_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(ret)); return; }

    app_state_set_conn(CONN_BOOT, "BLE START");
    ble_transport_start();

    ESP_LOGI(TAG, "init done; transport=%d", settings_store_get()->transport);
}
