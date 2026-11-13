#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"

// ESP-Hosted can deliver repeated STA_START notifications. In IDF 5.4,
// esp_netif_action_start unconditionally adds the interface to lwIP, which
// asserts on a second add. These actions run serially on the default event
// loop. Track membership until STA_STOP, including while disconnected (down
// does not mean removed). Keep the SDK's MAC/RX/DHCP handlers intact.
namespace {
void* active_station = nullptr;
}
extern "C" void __real_esp_netif_action_start(void*, esp_event_base_t, int32_t, void*);
extern "C" void __real_esp_netif_action_stop(void*, esp_event_base_t, int32_t, void*);

extern "C" void __wrap_esp_netif_action_start(void* netif, esp_event_base_t base, int32_t id, void* data)
{
    const bool station = netif && base == WIFI_EVENT && id == WIFI_EVENT_STA_START;
    if (station && active_station == netif) {
        ESP_LOGW("wifi_netif", "Ignoring duplicate STA_START for %p", netif);
        return;
    }
    __real_esp_netif_action_start(netif, base, id, data);
    if (station) {
        active_station = netif;
        ESP_LOGI("wifi_netif", "Station interface started: %p", netif);
    }
}

extern "C" void __wrap_esp_netif_action_stop(void* netif, esp_event_base_t base, int32_t id, void* data)
{
    __real_esp_netif_action_stop(netif, base, id, data);
    if (netif && active_station == netif && base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        active_station = nullptr;
        ESP_LOGI("wifi_netif", "Station interface stopped: %p", netif);
    }
}
