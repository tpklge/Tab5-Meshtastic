/*
 * Tab5-Meshtastic v2 — LCD/LVGL init (PRD §4, §8).
 *
 * LVGL port is initialised on CPU1 for best frame timing. The 1280×720 landscape
 * view is achieved by a PPA hardware rotation declared in the display driver — NOT
 * by runtime LVGL rotation (which corrupts the render; see PRD §4).
 *
 * Brightness: the BSP initialises LEDC_CHANNEL_1 (LEDC_TIMER_0, 12-bit, low-speed).
 * We can update duty on that same channel without reinitialising.
 */
#include "lcd_tools.h"

#include "lvgl_port.h"
#include "lvgl_port_disp.h"
#include "lvgl_port_touch.h"
#include "m5_tab5_component.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char* TAG = "lcd_tools";

/* LEDC config used by the BSP driver — must match
 * components/m5_tab5_component/src/drivers/lcd_st7123/m5tab5_lcd_st7123.cpp */
static constexpr ledc_mode_t    BACKLIGHT_LEDC_MODE    = LEDC_LOW_SPEED_MODE;
static constexpr ledc_channel_t BACKLIGHT_LEDC_CHANNEL = LEDC_CHANNEL_1;
static constexpr uint32_t       BACKLIGHT_LEDC_RES_BITS = 12;
static constexpr uint32_t       BACKLIGHT_LEDC_MAX_DUTY = (1u << BACKLIGHT_LEDC_RES_BITS) - 1;

esp_err_t app_lcd_lvgl_init(m5::tab5::m5tab5_component& board)
{
    const lvgl_port_cfg_t port_cfg = {
        .task_priority   = 6,
        .task_stack      = 16384,
        .task_affinity   = 1,     /* CPU1, avoids contention with BLE/UART on CPU0 */
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl_port_init failed");

    /* The display sub-driver picks the correct DSI+PPA configuration for the
     * detected variant. The rotation (LV_DISPLAY_ROTATION_90 + use_ppa) is baked
     * in; do NOT override it at runtime. */
    auto& disp_cfg = board.get_display_config();
    ESP_RETURN_ON_ERROR(lvgl_port_add_disp_dsi(&disp_cfg), TAG, "add_disp_dsi failed");

    auto& touch_cfg = board.get_touch_config();
    ESP_RETURN_ON_ERROR(lvgl_port_add_touch(&touch_cfg), TAG, "add_touch failed");

    /* Match touch rotation to display rotation. */
    lvgl_port_set_touch_rotation(LV_DISPLAY_ROTATION_90);

    ESP_LOGI(TAG, "LVGL initialised (1280×720 landscape, CPU1)");
    return ESP_OK;
}

void lcd_set_brightness(uint8_t percent)
{
    if (percent < 5)   percent = 5;
    if (percent > 100) percent = 100;
    uint32_t duty = (BACKLIGHT_LEDC_MAX_DUTY * percent) / 100;
    ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
}
