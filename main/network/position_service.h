#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Fetch approximate position from ip-api.com over Wi-Fi and send it to the mesh.
 * Non-blocking: launches a task. Returns ESP_ERR_INVALID_STATE if Wi-Fi not connected. */
esp_err_t position_service_fetch_and_send(void);
#ifdef __cplusplus
}
#endif
