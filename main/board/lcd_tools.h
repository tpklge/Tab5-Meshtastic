/*
 * Tab5-Meshtastic v2 — display bring-up.
 *
 * Initializes LVGL on the Tab5 panel in landscape 1280x720 using PPA HARDWARE
 * rotation in the display init (PRD §4). Runtime LVGL rotation corrupts the
 * render and must not be used.
 */
#ifndef LCD_TOOLS_H
#define LCD_TOOLS_H

#include <esp_err.h>

namespace m5 {
namespace tab5 {
class m5tab5_component;
}
}  // namespace m5

// Initialize LVGL using the native LCD/touch handles from m5_tab5_component.
// Call after board.begin() has populated the panel handle.
esp_err_t app_lcd_lvgl_init(m5::tab5::m5tab5_component& board);

// Set backlight brightness (5–100 %). Uses LEDC channel already initialized
// by the BSP — safe to call after app_lcd_lvgl_init(). Does NOT reinitialize
// the display or LEDC timer.
void lcd_set_brightness(uint8_t percent);

#endif  // LCD_TOOLS_H
