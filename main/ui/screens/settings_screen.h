#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build the settings panel (brightness + audio notifications).
// Returns the root container; attach to parent or swap as needed.
lv_obj_t* settings_screen_make(lv_obj_t* parent);

// Refresh settings panel values from storage (call on panel show).
void settings_screen_refresh(lv_obj_t* panel);

#ifdef __cplusplus
}
#endif
