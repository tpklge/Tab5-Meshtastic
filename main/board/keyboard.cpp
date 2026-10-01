/*
 * Tab5-Meshtastic v2 — physical keyboard driver glue. See keyboard.h.
 *
 * The M5Tab5Keyboard driver's poll task calls key_cb() off the UI thread, so we
 * hand events to a queue drained by an LVGL timer. UI edits, sending and
 * history storage then run on the UI task, never the small keyboard task. We use POLLING (not the hardware INT) deliberately: it
 * needs no GPIO ISR and so can't perturb the esp_hosted/SDIO interrupt path that
 * the cold-boot gate guards (PRD §4). A 20 ms poll is imperceptible for typing.
 */

#include "keyboard.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "m5_tab5_keyboard.h"
#include "ui_shell.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char* TAG = "kbd";

static m5::M5Tab5Keyboard s_kb;
static QueueHandle_t s_queue = nullptr;
static bool s_keyboard_ready = false;

/* Driver poll-task context. Keep it short: just enqueue the decoded string. */
static void key_cb(m5_tab5_key_event_t ev, void* /*arg*/)
{
    if (ev.type != M5_TAB5_KB_MODE_STRING || ev.str_len == 0) return;
    if (s_queue) xQueueSend(s_queue, &ev, 0);
}

static void kbd_timer(lv_timer_t* /*timer*/)
{
    m5_tab5_key_event_t ev;
    // Bound work per UI tick; preserve FIFO order without waiting for input.
    for (unsigned i = 0; i < 4; ++i) {
        if (xQueueReceive(s_queue, &ev, 0) != pdTRUE) break;
        ui_kbd_feed(ev.str_data, ev.str_len, ev.str_modifier);
    }
}

void kbd_start(void)
{
    s_queue = xQueueCreate(16, sizeof(m5_tab5_key_event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "event queue alloc failed; physical keyboard disabled");
        return;
    }

    /* Self-created master bus on I2C_NUM_1 (the keyboard's dedicated pins),
     * polling mode — independent of the board's system I2C. */
    m5_tab5_kb_err_t err =
        s_kb.begin(I2C_NUM_1, M5_TAB5_KB_DEFAULT_ADDR, M5_TAB5_KB_DEFAULT_SDA, M5_TAB5_KB_DEFAULT_SCL,
                   M5_TAB5_KB_I2C_FREQ_400K, M5_TAB5_KB_INT_MODE_POLLING);
    if (err != M5_TAB5_KB_OK) {
        ESP_LOGW(TAG, "keyboard not detected (err=%d); using on-screen input only", err);
        return;  /* non-fatal — touch OSK still works */
    }

    s_kb.setInterruptMode(M5_TAB5_KB_INT_MODE_POLLING, 20);  /* 20 ms poll */
    if (s_kb.enableStringMode(key_cb, nullptr) != M5_TAB5_KB_OK) {
        ESP_LOGE(TAG, "cannot enable keyboard string mode");
        s_kb.end();
        return;
    }
    s_keyboard_ready = true;

    uint8_t ver = 0;
    s_kb.getVersion(&ver);
    ESP_LOGI(TAG, "physical keyboard ready (FW 0x%02X)", ver);

    // ui_start() has already initialized LVGL. Timer callbacks run on its
    // 16 KB task; the recursive LVGL lock in ui_kbd_feed remains valid there.
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "cannot register keyboard UI timer");
        return;
    }
    if (!lv_timer_create(kbd_timer, 20, nullptr))
        ESP_LOGE(TAG, "keyboard UI timer allocation failed");
    lvgl_port_unlock();
}

void kbd_prepare_exit(void)
{
    if (!s_keyboard_ready) return;
    m5_tab5_kb_err_t stop_err = s_kb.setInterruptMode(M5_TAB5_KB_INT_MODE_DISABLED);
    if (stop_err != M5_TAB5_KB_OK)
        ESP_LOGW(TAG, "cannot stop keyboard polling (err=%d)", stop_err);
    m5_tab5_kb_err_t err = s_kb.enableNormalMode();
    if (err != M5_TAB5_KB_OK)
        ESP_LOGW(TAG, "cannot restore keyboard normal mode (err=%d)", err);
    s_kb.end();
    s_keyboard_ready = false;
}
