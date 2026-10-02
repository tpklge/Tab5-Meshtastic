#include "settings_screen.h"
#include "settings_store.h"
#include "lcd_tools.h"
#include "tab5_audio.h"
#include "app_clock.h"
#include "wifi_settings.h"
#include "position_service.h"
#include <cstring>
#include "../theme.h"

static lv_obj_t* s_brightness_slider = nullptr;
static lv_obj_t* s_brightness_label  = nullptr;
static lv_obj_t* s_notif_sw          = nullptr;
static lv_obj_t* s_vol_slider        = nullptr;
static lv_obj_t* s_vol_label         = nullptr;
static lv_obj_t* s_pat_dd            = nullptr;

static lv_obj_t* s_clock_fields[6] = {};
static lv_obj_t* s_clock_status = nullptr;
static lv_obj_t* s_clock_now = nullptr;

static void clock_save_cb(lv_event_t*)
{
    struct tm t{};
    t.tm_mday = lv_dropdown_get_selected(s_clock_fields[0]) + 1;
    t.tm_mon = lv_dropdown_get_selected(s_clock_fields[1]);
    t.tm_year = lv_dropdown_get_selected(s_clock_fields[2]) + 100;
    t.tm_hour = lv_dropdown_get_selected(s_clock_fields[3]);
    t.tm_min = lv_dropdown_get_selected(s_clock_fields[4]);
    int offset = ((int)lv_dropdown_get_selected(s_clock_fields[5]) - 48) * 15;
    esp_err_t err = app_clock_set(t, offset);
    lv_label_set_text(s_clock_status, err == ESP_OK ? "Data e hora salvas no Tab5." :
        err == ESP_ERR_INVALID_ARG ? "Data invalida. Confira o dia, mes e ano." : "Falha ao salvar o relogio. Tente novamente.");
}

static void make_clock_section(lv_obj_t* panel)
{
    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "DATA E HORA");
    lv_obj_set_style_text_color(title, lv_color_hex(C_DIM), 0);
    lv_obj_t* card = lv_obj_create(panel);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_SURF2), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_clock_now = lv_label_create(card);
    lv_obj_set_style_text_color(s_clock_now, lv_color_hex(C_HI), 0);
    lv_timer_create([](lv_timer_t*) {
        if (s_clock_now) {
            char text[40]; app_clock_format(text, sizeof(text), true);
            if (strcmp(text, lv_label_get_text(s_clock_now))) lv_label_set_text(s_clock_now, text);
        }
    }, 1000, nullptr);
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    const char* names[] = {"Dia", "Mes", "Ano", "Hora", "Minuto", "Fuso UTC"};
    const int counts[] = {31, 12, 100, 24, 60, 105};
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* col = lv_obj_create(row);
        lv_obj_set_size(col, i == 5 ? 170 : 120, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* label = lv_label_create(col);
        lv_label_set_text(label, names[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(C_HI), 0);
        // Serialized by the LVGL lock; do not consume the boot task stack.
        static char options[1600];
        options[0] = 0;
        for (int j = 0; j < counts[i]; ++j) {
            char item[24];
            if (i == 5) {
                int minutes = (j - 48) * 15;
                int magnitude = minutes < 0 ? -minutes : minutes;
                snprintf(item, sizeof(item), "%s%c%02d:%02d", j ? "\n" : "", minutes < 0 ? '-' : '+', magnitude / 60, magnitude % 60);
            } else snprintf(item, sizeof(item), "%s%02d", j ? "\n" : "", j + (i < 2 ? 1 : i == 2 ? 2000 : 0));
            strlcat(options, item, sizeof(options));
        }
        s_clock_fields[i] = lv_dropdown_create(col);
        lv_obj_set_width(s_clock_fields[i], lv_pct(100));
        lv_dropdown_set_options(s_clock_fields[i], options);
    }
    lv_obj_t* save = lv_button_create(card);
    lv_obj_set_size(save, 210, 44);
    lv_obj_add_event_cb(save, clock_save_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* label = lv_label_create(save);
    lv_label_set_text(label, "Salvar data e hora");
    lv_obj_center(label);
    lv_obj_t* timezone = lv_button_create(card);
    lv_obj_set_size(timezone, 260, 44);
    lv_obj_add_event_cb(timezone, [](lv_event_t*) {
        int offset = ((int)lv_dropdown_get_selected(s_clock_fields[5]) - 48) * 15;
        lv_label_set_text(s_clock_status, app_clock_set_offset(offset) == ESP_OK ?
            "Fuso salvo. A hora UTC foi mantida." : "Falha ao salvar o fuso.");
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* tz_label = lv_label_create(timezone); lv_label_set_text(tz_label, "Aplicar somente o fuso"); lv_obj_center(tz_label);
    s_clock_status = lv_label_create(card);
    lv_label_set_text(s_clock_status, "Ajuste manual, sem GPS ou internet. O Tab5 guarda a hora no RTC.");
    lv_obj_set_style_text_color(s_clock_status, lv_color_hex(C_MID), 0);
}

static void brightness_cb(lv_event_t* e)
{
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    if (val < 5)   val = 5;
    if (val > 100) val = 100;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)val);
    if (s_brightness_label) lv_label_set_text(s_brightness_label, buf);

    lcd_set_brightness((uint8_t)val);
    settings_store_set_brightness((uint8_t)val);
}

static void notif_sw_cb(lv_event_t* e)
{
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    const app_settings_t* s = settings_store_get();
    settings_store_set_notif(en ? 1 : 0, s->notif_vol, s->notif_pat);
}

static void vol_cb(lv_event_t* e)
{
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    if (val < 0) val = 0;
    if (val > 100) val = 100;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)val);
    if (s_vol_label) lv_label_set_text(s_vol_label, buf);

    tab5_audio_set_volume((uint8_t)val);
    const app_settings_t* s = settings_store_get();
    settings_store_set_notif(s->notif_en, (uint8_t)val, s->notif_pat);
}

static void pat_cb(lv_event_t* e)
{
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    const app_settings_t* s = settings_store_get();
    settings_store_set_notif(s->notif_en, s->notif_vol, (uint8_t)sel);
}

static void test_sound_cb(lv_event_t* e)
{
    (void)e;
    const app_settings_t* s = settings_store_get();
    if (s->notif_en) {
        tab5_audio_beep((audio_pattern_t)s->notif_pat);
    }
}

lv_obj_t* settings_screen_make(lv_obj_t* parent)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(panel, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 16, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // --- Screen section ---
    lv_obj_t* sec1 = lv_label_create(panel);
    lv_label_set_text(sec1, "DISPLAY");
    lv_obj_set_style_text_color(sec1, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_text_font(sec1, &lv_font_montserrat_14, 0);

    // Brightness row
    lv_obj_t* row1 = lv_obj_create(panel);
    lv_obj_set_size(row1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row1, lv_color_hex(C_SURF2), 0);
    lv_obj_set_style_radius(row1, M_RAD_M, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 12, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row1, 8, 0);

    lv_obj_t* rl1 = lv_obj_create(row1);
    lv_obj_set_size(rl1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rl1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rl1, 0, 0);
    lv_obj_set_style_pad_all(rl1, 0, 0);
    lv_obj_set_flex_flow(rl1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rl1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* bl = lv_label_create(rl1);
    lv_label_set_text(bl, "Brightness");
    lv_obj_set_style_text_color(bl, lv_color_hex(C_HI), 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);

    s_brightness_label = lv_label_create(rl1);
    lv_obj_set_style_text_color(s_brightness_label, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_text_font(s_brightness_label, &lv_font_montserrat_16, 0);

    s_brightness_slider = lv_slider_create(row1);
    lv_obj_set_width(s_brightness_slider, lv_pct(100));
    lv_slider_set_range(s_brightness_slider, 5, 100);
    lv_obj_add_event_cb(s_brightness_slider, brightness_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Reset brightness button
    lv_obj_t* rst_btn = lv_btn_create(row1);
    lv_obj_set_size(rst_btn, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_bg_color(rst_btn, lv_color_hex(C_SURF), 0);
    lv_obj_add_event_cb(rst_btn, [](lv_event_t* e){
        (void)e;
        lv_slider_set_value(s_brightness_slider, 80, LV_ANIM_OFF);
        lcd_set_brightness(80);
        settings_store_set_brightness(80);
        lv_label_set_text(s_brightness_label, "80%");
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* rst_lbl = lv_label_create(rst_btn);
    lv_label_set_text(rst_lbl, "Reset to default");
    lv_obj_center(rst_lbl);

    // --- Audio section ---
    lv_obj_t* sec2 = lv_label_create(panel);
    lv_label_set_text(sec2, "NOTIFICATIONS");
    lv_obj_set_style_text_color(sec2, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_text_font(sec2, &lv_font_montserrat_14, 0);

    lv_obj_t* row2 = lv_obj_create(panel);
    lv_obj_set_size(row2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row2, lv_color_hex(C_SURF2), 0);
    lv_obj_set_style_radius(row2, M_RAD_M, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 12, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row2, 12, 0);

    // Enable toggle
    lv_obj_t* nr_en = lv_obj_create(row2);
    lv_obj_set_size(nr_en, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(nr_en, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nr_en, 0, 0);
    lv_obj_set_style_pad_all(nr_en, 0, 0);
    lv_obj_set_flex_flow(nr_en, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nr_en, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* enl = lv_label_create(nr_en);
    lv_label_set_text(enl, "Sound notifications");
    lv_obj_set_style_text_color(enl, lv_color_hex(C_HI), 0);
    lv_obj_set_style_text_font(enl, &lv_font_montserrat_16, 0);
    s_notif_sw = lv_switch_create(nr_en);
    lv_obj_add_event_cb(s_notif_sw, notif_sw_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Volume row
    lv_obj_t* nr_vol = lv_obj_create(row2);
    lv_obj_set_size(nr_vol, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(nr_vol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nr_vol, 0, 0);
    lv_obj_set_style_pad_all(nr_vol, 0, 0);
    lv_obj_set_flex_flow(nr_vol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(nr_vol, 6, 0);

    lv_obj_t* nr_vol_row = lv_obj_create(nr_vol);
    lv_obj_set_size(nr_vol_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(nr_vol_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nr_vol_row, 0, 0);
    lv_obj_set_style_pad_all(nr_vol_row, 0, 0);
    lv_obj_set_flex_flow(nr_vol_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nr_vol_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* voll = lv_label_create(nr_vol_row);
    lv_label_set_text(voll, "Volume");
    lv_obj_set_style_text_color(voll, lv_color_hex(C_HI), 0);
    lv_obj_set_style_text_font(voll, &lv_font_montserrat_16, 0);
    s_vol_label = lv_label_create(nr_vol_row);
    lv_obj_set_style_text_color(s_vol_label, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_text_font(s_vol_label, &lv_font_montserrat_16, 0);
    s_vol_slider = lv_slider_create(nr_vol);
    lv_obj_set_width(s_vol_slider, lv_pct(100));
    lv_slider_set_range(s_vol_slider, 0, 100);
    lv_obj_add_event_cb(s_vol_slider, vol_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Pattern dropdown
    lv_obj_t* pat_row = lv_obj_create(row2);
    lv_obj_set_size(pat_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pat_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pat_row, 0, 0);
    lv_obj_set_style_pad_all(pat_row, 0, 0);
    lv_obj_set_flex_flow(pat_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pat_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* patl = lv_label_create(pat_row);
    lv_label_set_text(patl, "Pattern");
    lv_obj_set_style_text_color(patl, lv_color_hex(C_HI), 0);
    lv_obj_set_style_text_font(patl, &lv_font_montserrat_16, 0);
    s_pat_dd = lv_dropdown_create(pat_row);
    lv_dropdown_set_options(s_pat_dd, "Silent\nShort beep\nDouble beep\nTriple beep");
    lv_obj_set_width(s_pat_dd, 180);
    lv_obj_add_event_cb(s_pat_dd, pat_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Test button
    lv_obj_t* test_btn = lv_btn_create(row2);
    lv_obj_set_size(test_btn, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_bg_color(test_btn, lv_color_hex(C_GREEN), 0);
    lv_obj_add_event_cb(test_btn, test_sound_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* test_lbl = lv_label_create(test_btn);
    lv_label_set_text(test_lbl, "Test sound");
    lv_obj_set_style_text_color(test_lbl, lv_color_hex(C_BG), 0);
    lv_obj_center(test_lbl);

    wifi_settings_make(panel);
    make_clock_section(panel);

    /* Position via Wi-Fi */
    {
        lv_obj_t* pos_row = lv_obj_create(panel);
        lv_obj_set_size(pos_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(pos_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pos_row, 0, 0);
        lv_obj_set_style_pad_all(pos_row, 8, 0);
        lv_obj_t* pos_btn = lv_button_create(pos_row);
        lv_obj_set_size(pos_btn, LV_SIZE_CONTENT, 44);
        lv_obj_set_style_bg_color(pos_btn, lv_color_hex(C_GREEN), 0);
        lv_obj_add_event_cb(pos_btn, [](lv_event_t*) {
            position_service_fetch_and_send();
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* pos_lbl = lv_label_create(pos_btn);
        lv_label_set_text(pos_lbl, LV_SYMBOL_GPS " Enviar posicao via Wi-Fi");
        lv_obj_set_style_text_color(pos_lbl, lv_color_hex(C_INK), 0);
        lv_obj_set_style_text_font(pos_lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(pos_lbl);
    }

    settings_screen_refresh(panel);
    return panel;
}

void settings_screen_refresh(lv_obj_t* panel)
{
    (void)panel;
    if (s_clock_fields[0]) {
        struct tm local{};
        if (!app_clock_local(&local)) { local.tm_year = 126; local.tm_mon = 0; local.tm_mday = 1; }
        lv_dropdown_set_selected(s_clock_fields[0], local.tm_mday - 1);
        lv_dropdown_set_selected(s_clock_fields[1], local.tm_mon);
        lv_dropdown_set_selected(s_clock_fields[2], local.tm_year - 100);
        lv_dropdown_set_selected(s_clock_fields[3], local.tm_hour);
        lv_dropdown_set_selected(s_clock_fields[4], local.tm_min);
        lv_dropdown_set_selected(s_clock_fields[5], app_clock_offset_minutes() / 15 + 48);
        char text[40]; app_clock_format(text, sizeof(text), true);
        lv_label_set_text(s_clock_now, text);
    }
    const app_settings_t* s = settings_store_get();

    if (s_brightness_slider) {
        lv_slider_set_value(s_brightness_slider, s->brightness, LV_ANIM_OFF);
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", s->brightness);
        if (s_brightness_label) lv_label_set_text(s_brightness_label, buf);
    }
    if (s_notif_sw) {
        if (s->notif_en) lv_obj_add_state(s_notif_sw, LV_STATE_CHECKED);
        else             lv_obj_clear_state(s_notif_sw, LV_STATE_CHECKED);
    }
    if (s_vol_slider) {
        lv_slider_set_value(s_vol_slider, s->notif_vol, LV_ANIM_OFF);
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", s->notif_vol);
        if (s_vol_label) lv_label_set_text(s_vol_label, buf);
    }
    if (s_pat_dd) {
        lv_dropdown_set_selected(s_pat_dd, s->notif_pat);
    }
}
