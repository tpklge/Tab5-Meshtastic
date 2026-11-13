#include "../main/network/wifi_netif_guard.cpp"
#include <cassert>
#include <cstdio>
static unsigned starts = 0, stops = 0;
static bool listed = false;
extern "C" void __real_esp_netif_action_start(void*, esp_event_base_t, int32_t, void*) {
    assert(!listed); // Reproduces lwIP's "netif already added" assertion.
    listed = true;
    ++starts;
}
extern "C" void __real_esp_netif_action_stop(void*, esp_event_base_t, int32_t, void*) {
    listed = false;
    ++stops;
}
int main() {
    esp_netif_t sta;
    __wrap_esp_netif_action_start(&sta, WIFI_EVENT, WIFI_EVENT_STA_START, nullptr);
    __wrap_esp_netif_action_start(&sta, WIFI_EVENT, WIFI_EVENT_STA_START, nullptr);
    // Disconnection does not remove the netif: another START must still be ignored.
    assert(starts == 1 && listed);
    __wrap_esp_netif_action_stop(&sta, WIFI_EVENT, WIFI_EVENT_STA_STOP, nullptr);
    assert(stops == 1 && !listed);
    __wrap_esp_netif_action_start(&sta, WIFI_EVENT, WIFI_EVENT_STA_START, nullptr);
    assert(starts == 2 && listed);
    __wrap_esp_netif_action_start(&sta, WIFI_EVENT, WIFI_EVENT_STA_START, nullptr);
    assert(starts == 2);
    puts("PASS: duplicate STA_START suppressed; STA_STOP allows a fresh start.");
}
