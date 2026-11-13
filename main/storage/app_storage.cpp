#include "app_storage.h"
#include "sd_storage.h"
esp_err_t app_storage_init(void) { return sd_storage_available() ? ESP_OK : ESP_ERR_INVALID_STATE; }
bool app_storage_available(void) { return sd_storage_available(); }
void app_storage_deinit(void) { sd_storage_deinit(); }
