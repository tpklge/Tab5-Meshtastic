#include "display_idle.h"
#include "lcd_tools.h"
#include "settings_store.h"
#include "lvgl.h"

namespace {
uint32_t timeout_ms;
uint32_t last_activity;
bool asleep;
bool consuming_touch;
lv_timer_t* timer;

void check_idle(lv_timer_t*)
{
    if (!asleep && timeout_ms && lv_tick_elaps(last_activity) >= timeout_ms) {
        asleep = true;
        lcd_set_brightness(0);
    }
}
}

void display_idle_init(uint32_t timeout_seconds)
{
    display_idle_set_timeout(timeout_seconds);
    if (!timer) timer = lv_timer_create(check_idle, 250, nullptr);
}

void display_idle_set_timeout(uint32_t timeout_seconds)
{
    timeout_ms = timeout_seconds * 1000u;
    display_idle_activity();
}

bool display_idle_activity()
{
    last_activity = lv_tick_get();
    if (!asleep) return false;
    asleep = false;
    lcd_set_brightness(settings_store_get()->brightness);
    return true;
}

bool display_idle_touch(bool pressed)
{
    if (pressed) {
        if (display_idle_activity()) consuming_touch = true;
        return !consuming_touch;
    }
    if (consuming_touch) {
        consuming_touch = false;
        display_idle_activity();
        return false;
    }
    return true;
}
