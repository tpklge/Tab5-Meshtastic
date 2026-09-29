#pragma once
#include <stdint.h>
#include "esp_err.h"
struct wifi_network_t { char ssid[33]; int8_t rssi; bool secured; };
struct wifi_snapshot_t {
    bool connected;
    bool busy;
    bool saved;
    uint8_t count;
    uint32_t generation;
    char ssid[33];
    char saved_ssid[33];
    char ip[16];
    char status[128];
    char time_status[96];
    wifi_network_t networks[20];
};
esp_err_t wifi_service_init(); // NVS, mutex and queue only; before UI.
esp_err_t wifi_service_start(); // Worker; after radio transport initialization.
void wifi_service_snapshot(wifi_snapshot_t* out);
bool wifi_service_scan();
bool wifi_service_connect(const char* ssid, const char* password, bool use_saved);
bool wifi_service_disconnect(bool forget);
bool wifi_service_sync_time();
