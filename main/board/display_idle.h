#pragma once
#include <stdint.h>

// Call on the LVGL task or with its lock held. Zero disables automatic blanking.
void display_idle_init(uint32_t timeout_seconds);
void display_idle_set_timeout(uint32_t timeout_seconds);

// Record user input. Returns true when this input woke the screen (consume it).
bool display_idle_activity();

// Returns whether a touch sample may reach LVGL. Consumes the entire wake touch.
bool display_idle_touch(bool pressed);
