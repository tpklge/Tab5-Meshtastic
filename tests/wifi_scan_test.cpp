#include "../main/network/wifi_service.cpp"
#include <cassert>
#include <cstdio>
esp_err_t app_clock_sync(time_t) { return ESP_OK; }
int main() {
    assert(wifi_service_init() == ESP_OK);
    Command command{}; command.action = SCAN;
    handle(command);
    assert(scan_deadline && !fake_scan_block && fake_scan_starts == 1);
    wifi_snapshot_t view{};
    assert(wifi_service_snapshot(&view) && view.busy);
    // UI must return immediately when another task owns the snapshot lock.
    std::atomic<bool> holding{false}, release{false};
    std::thread holder([&] { mutex->lock(); holding=true; while (!release) std::this_thread::yield(); mutex->unlock(); });
    while (!holding) std::this_thread::yield();
    assert(!wifi_service_snapshot(&view)); release=true; holder.join();
    handle(command); assert(fake_scan_starts == 1); // ignore repeated scan
    poll_scan(fake_now); assert(scan_deadline);
    wifi_event_sta_scan_done_t done{};
    event(nullptr,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&done);
    poll_scan(fake_now);
    assert(!scan_deadline && wifi_service_snapshot(&view) && !view.busy);
    assert(view.count == 2 && !strcmp(view.networks[0].ssid,"Home") && !strcmp(view.networks[1].ssid,"Guest"));
    handle(command); fake_now += 16000000; poll_scan(fake_now);
    assert(!scan_deadline && fake_scan_stops == 1 && wifi_service_snapshot(&view) && !view.busy);
    handle(command); command.action = DISCONNECT; handle(command);
    assert(!scan_deadline && fake_scan_stops == 2 && wifi_service_snapshot(&view) && !view.busy);
    event(nullptr,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&done); poll_scan(fake_now); // late event ignored
    assert(!scan_deadline);
    puts("PASS: asynchronous Wi-Fi scan, busy snapshot, nonblocking UI read, deduplication, timeout, cancellation and late completion.");
}
