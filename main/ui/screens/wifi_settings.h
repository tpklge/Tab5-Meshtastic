#pragma once
#include "lvgl.h"
void wifi_settings_make(lv_obj_t* parent);
void wifi_settings_refresh();
void wifi_settings_leave();
void wifi_settings_key(const char* text, bool backspace, bool submit);
