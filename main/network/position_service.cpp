#include "position_service.h"
#include "app_commands.h"
#include "wifi_service.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

static const char* TAG = "pos_svc";

static char s_response[256];
static int  s_resp_len = 0;

static esp_err_t http_event(esp_http_client_event_t* evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int copy = evt->data_len;
        if (s_resp_len + copy >= (int)sizeof(s_response) - 1)
            copy = (int)sizeof(s_response) - 1 - s_resp_len;
        memcpy(s_response + s_resp_len, evt->data, copy);
        s_resp_len += copy;
        s_response[s_resp_len] = 0;
    }
    return ESP_OK;
}

static void fetch_task(void*)
{
    s_resp_len = 0; s_response[0] = 0;
    esp_http_client_config_t cfg{};
    cfg.url            = "http://ip-api.com/json/?fields=lat,lon";
    cfg.event_handler  = http_event;
    cfg.timeout_ms     = 8000;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || s_resp_len < 10) {
        ESP_LOGW(TAG, "geolocation request failed: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    float lat = 0, lon = 0;
    const char* p = strstr(s_response, "\"lat\":");
    if (p) lat = strtof(p + 6, nullptr);
    p = strstr(s_response, "\"lon\":");
    if (p) lon = strtof(p + 6, nullptr);
    if (lat == 0.0f && lon == 0.0f) {
        ESP_LOGW(TAG, "geolocation returned 0,0 — check Wi-Fi");
        vTaskDelete(nullptr);
        return;
    }
    int32_t lat_i = (int32_t)(lat * 1e7f);
    int32_t lon_i = (int32_t)(lon * 1e7f);
    ESP_LOGI(TAG, "position from IP: %.5f, %.5f → sending to mesh", (double)lat, (double)lon);
    app_send_position(lat_i, lon_i);
    vTaskDelete(nullptr);
}

esp_err_t position_service_fetch_and_send(void)
{
    wifi_snapshot_t ws; wifi_service_snapshot(&ws);
    if (!ws.connected) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(fetch_task, "pos_fetch", 6144, nullptr, 3, nullptr) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
