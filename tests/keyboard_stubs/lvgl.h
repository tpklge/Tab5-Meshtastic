#pragma once
struct lv_timer_t {};
using lv_timer_cb_t = void (*)(lv_timer_t*);
inline lv_timer_cb_t keyboard_test_timer;
inline lv_timer_t* lv_timer_create(lv_timer_cb_t callback, unsigned period, void*) {
    static lv_timer_t timer; keyboard_test_timer=callback; return period==20?&timer:nullptr;
}
