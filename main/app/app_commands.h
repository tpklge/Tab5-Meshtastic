#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Route a text message through whichever transport is active (BLE or UART).
 * Called from the UI layer — never call transport APIs directly from UI. */
void app_send_text(const char* text);

/* Stop peripheral use and power the Tab5 off from a dedicated task. */
void app_power_off(void);

#ifdef __cplusplus
}
#endif
