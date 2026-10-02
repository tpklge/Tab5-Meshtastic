#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Route a text message through whichever transport is active (BLE or UART).
 * Called from the UI layer — never call transport APIs directly from UI. */
/* Returns ESP_OK only when the request was accepted into the outgoing queue. */
int app_send_text(const char* text);

/* Send a direct message to a specific node (to_node != 0). */
int app_send_dm(const char* text, uint32_t to_node);

/* Stop peripheral use and power the Tab5 off from a dedicated task. */
void app_power_off(void);

/* Send our own GPS position to the mesh (lat/lon in 1e-7 degrees). */
int app_send_position(int32_t lat_i, int32_t lon_i);

#ifdef __cplusplus
}
#endif
