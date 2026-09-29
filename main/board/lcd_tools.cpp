/*
 * Tab5-Meshtastic v2 — LCD/LVGL init (PRD §4, §8).
 *
 * Physical panel: 720(H)×1280(V) portrait DSI. The lvgl_port DSI driver uses
 * PPA hardware rotation (sw_rotate + use_ppa) to present a 1280×720 landscape
 * logical frame to LVGL. Pass hres=720, vres=1280 (physical) and set
 * sw_rotation=LV_DISPLAY_ROTATION_90 + use_ppa=1 so the driver allocates the
 * correctly-sized rotated buffers and LVGL ends up with a 1280×720 coordinate
 * space — which is what the shell layout expects (PRD §4).
 *
 * Do NOT call lv_display_set_rotation() at runtime — that path corrupts the
 * render (PRD §4, hardware constraint).
 *
 * Brightness: BSP initialises LEDC_CHANNEL_1 (LEDC_TIMER_0, 12-bit, low-speed).
 */
#include "lcd_tools.h"

#include "lvgl_port.h"
#include "lvgl_port_disp.h"
#include "lvgl_port_touch.h"
#include "m5_tab5_component.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/ledc.h"

static const char* TAG = "lcd_tools";

/* Physical panel dimensions (portrait). PPA rotation makes LVGL see 1280×720. */
static constexpr uint32_t PANEL_H_RES = 720;
static constexpr uint32_t PANEL_V_RES = 1280;

static constexpr ledc_mode_t    BACKLIGHT_LEDC_MODE     = LEDC_LOW_SPEED_MODE;
static constexpr ledc_channel_t BACKLIGHT_LEDC_CHANNEL  = LEDC_CHANNEL_1;
static constexpr uint32_t       BACKLIGHT_LEDC_RES_BITS = 12;
static constexpr uint32_t       BACKLIGHT_LEDC_MAX_DUTY = (1u << BACKLIGHT_LEDC_RES_BITS) - 1;

esp_err_t app_lcd_lvgl_init(m5::tab5::m5tab5_component& board)
{
    esp_lcd_panel_handle_t panel = board.lcd_panel();
    if (!panel) {
        ESP_LOGE(TAG, "lcd_panel() null — call board.begin() first");
        return ESP_ERR_INVALID_STATE;
    }

    lvgl_port_cfg_t port_cfg = lvgl_PORT_INIT_CONFIG();
    port_cfg.task_stack    = 16384;
    port_cfg.task_affinity = 1;   /* CPU1: avoids BLE/UART contention on CPU0 */
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl_port_init failed");

    /* Physical portrait dimensions + PPA 90° rotation → LVGL sees 1280×720. */
    const lvgl_disp_cfg_t disp_cfg = {
        .io_handle      = nullptr,
        .panel_handle   = panel,
        .control_handle = nullptr,
        .buffer_size    = PANEL_H_RES * PANEL_V_RES,
        .double_buffer  = true,
        .trans_size     = 0,
        .hres           = PANEL_H_RES,
        .vres           = PANEL_V_RES,
        .monochrome     = false,
        .rotation       = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
        .color_format   = LV_COLOR_FORMAT_RGB565,
        .flags          = {
            .buff_dma    = 0,
            .buff_spiram = 0,
            .sw_rotate   = 1,   /* enable PPA-backed rotation */
            .swap_bytes  = 0,
            .full_refresh = 0,
            .direct_mode = 1,
        },
    };
    const lvgl_disp_dsi_cfg_t dsi_cfg = {
        .sw_rotation = LV_DISPLAY_ROTATION_90,
        .flags = {.avoid_tearing = 1, .use_ppa = 1},
    };

    lv_display_t* disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_dsi failed");
        return ESP_FAIL;
    }

    /* Register touch. PPA handles display rotation but does NOT set LVGL's
     * display rotation, so the touch driver can't infer it. We must explicitly
     * set the touch rotation to match the PPA rotation (90°). */
    esp_lcd_touch_handle_t tp = board.touch_panel();
    if (tp) {
        const lvgl_touch_cfg_t touch_cfg = {
            .disp   = disp,
            .handle = tp,
            .scale  = {.x = 1.0f, .y = 1.0f},
        };
        lv_indev_t* touch_indev = lvgl_port_add_touch(&touch_cfg);
        if (touch_indev) {
            lvgl_port_set_touch_rotation(touch_indev, LV_DISPLAY_ROTATION_90);
        }
    }

    ESP_LOGI(TAG, "LVGL init: panel %" PRIu32 "x%" PRIu32 " + PPA 90deg -> 1280x720 landscape",
             PANEL_H_RES, PANEL_V_RES);
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
