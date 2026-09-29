#pragma once
#include "lvgl.h"
lv_obj_t* channels_screen_make(lv_obj_t* parent);
void channels_screen_refresh();
void channels_screen_leave();
void channels_screen_key(const char* text, bool backspace, bool enter);
