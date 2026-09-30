#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <sys/time.h>
constexpr int ESP_ERR_NVS_NOT_FOUND=0x1102;
inline const char* esp_err_to_name(int) { return "FAKE_ERROR"; }
inline int64_t fake_now=1000000;
inline int64_t esp_timer_get_time() { return fake_now; }
using esp_event_base_t = int;
constexpr int IP_EVENT=1, WIFI_EVENT=2, ESP_EVENT_ANY_ID=-1;
constexpr int WIFI_EVENT_STA_DISCONNECTED=1, WIFI_EVENT_SCAN_DONE=2, IP_EVENT_STA_GOT_IP=3, IP_EVENT_STA_LOST_IP=4;
struct wifi_event_sta_disconnected_t { unsigned reason; };
struct wifi_event_sta_scan_done_t { unsigned status; };
inline int esp_event_loop_create_default() { return ESP_OK; }
inline int esp_event_handler_register(int,int,void(*)(void*,int,int32_t,void*),void*) { return ESP_OK; }
struct esp_netif_t {};
struct esp_netif_ip_info_t { struct { uint32_t addr; } ip; };
inline int esp_netif_init() { return ESP_OK; }
inline esp_netif_t* esp_netif_create_default_wifi_sta() { static esp_netif_t n; return &n; }
inline int esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t*) { return ESP_FAIL; }
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) 1u,2u,3u,4u
inline int esp_hosted_init() { return ESP_OK; }
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_scan_config_t { bool show_hidden; };
struct wifi_ap_record_t { uint8_t ssid[33]; int8_t rssi; int authmode; };
struct wifi_config_t { struct { uint8_t ssid[32],password[64]; struct { int authmode; } threshold; struct { bool capable; } pmf_cfg; int sae_pwe_h2e; } sta; };
constexpr int WIFI_STORAGE_RAM=0, WIFI_MODE_STA=0, WIFI_AUTH_OPEN=0, WIFI_AUTH_WPA2_PSK=2, WPA3_SAE_PWE_BOTH=3, WIFI_IF_STA=0;
inline int fake_scan_starts=0, fake_scan_stops=0;
inline bool fake_scan_block=true;
inline int esp_wifi_init(wifi_init_config_t*) { return ESP_OK; }
inline int esp_wifi_set_storage(int) { return ESP_OK; }
inline int esp_wifi_set_mode(int) { return ESP_OK; }
inline int esp_wifi_start() { return ESP_OK; }
inline int esp_wifi_stop() { return ESP_OK; }
inline int esp_wifi_connect() { return ESP_OK; }
inline int esp_wifi_disconnect() { return ESP_OK; }
inline int esp_wifi_set_config(int,wifi_config_t*) { return ESP_OK; }
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t*) { return ESP_FAIL; }
inline int esp_wifi_scan_start(wifi_scan_config_t*, bool block) { ++fake_scan_starts; fake_scan_block=block; return ESP_OK; }
inline int esp_wifi_scan_stop() { ++fake_scan_stops; return ESP_OK; }
inline int esp_wifi_clear_ap_list() { return ESP_OK; }
inline int esp_wifi_scan_get_ap_num(uint16_t* count) { *count=3; return ESP_OK; }
inline int esp_wifi_scan_get_ap_records(uint16_t* count,wifi_ap_record_t* aps) {
 if (*count != 3) return ESP_FAIL;
 strcpy((char*)aps[0].ssid,"Home"); aps[0].authmode=2;
 strcpy((char*)aps[1].ssid,"Home");
 strcpy((char*)aps[2].ssid,"Guest"); return ESP_OK;
}
struct esp_sntp_config_t {};
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(s) esp_sntp_config_t{}
inline int esp_netif_sntp_init(esp_sntp_config_t*) { return ESP_OK; }
inline void esp_netif_sntp_deinit() {}
inline int esp_netif_sntp_sync_wait(int) { return ESP_FAIL; }
using nvs_handle_t = unsigned;
constexpr int NVS_READWRITE=0;
inline int nvs_open(const char*,int,nvs_handle_t* h) { *h=1; return ESP_OK; }
inline int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*) { return ESP_FAIL; }
inline int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t) { return ESP_OK; }
inline int nvs_get_u8(nvs_handle_t,const char*,uint8_t*) { return ESP_FAIL; }
inline int nvs_set_u8(nvs_handle_t,const char*,uint8_t) { return ESP_OK; }
inline int nvs_erase_key(nvs_handle_t,const char*) { return ESP_OK; }
inline int nvs_commit(nvs_handle_t) { return ESP_OK; }
