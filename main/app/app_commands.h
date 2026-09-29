#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Route a text message through whichever transport is active (BLE or UART).
 * Called from the UI layer — never call transport APIs directly from UI. */
void app_send_text(const char* text);

#ifdef __cplusplus
}
#endif
