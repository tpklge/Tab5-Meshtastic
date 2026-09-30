#include "app_state.h"
#include "app_clock.h"
#include "battery_monitor.h"
#include <limits>
#include "message_store.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" esp_err_t message_store_append(uint32_t, uint32_t, uint32_t, uint8_t, bool, bool,
                                          int64_t, int64_t, int16_t, int16_t, const char*) { return ESP_OK; }
int main()
{
    app_state_init();
    app_state_set_myinfo(42, "", "");
    app_state_upsert_node(42, "Meu radio", "ME", 0, 0, false, true, 1);
    app_snapshot_t s; app_state_snapshot(&s);
    assert(!strcmp(s.my_long, "Meu radio") && !strcmp(s.my_short, "ME"));
    // Resync must recover identity even when MyInfo arrives after NodeInfo.
    app_state_set_myinfo(42, "", ""); app_state_snapshot(&s);
    assert(!strcmp(s.my_long, "Meu radio"));
    app_state_set_myinfo(43, nullptr, nullptr); app_state_snapshot(&s);
    assert(!s.my_long[0] && !s.my_short[0]);
    app_state_upsert_node(44, "Outro radio", "OT", 0, 0, false, true, 1);
    app_state_set_myinfo(44, "", ""); app_state_snapshot(&s);
    assert(!strcmp(s.my_long, "Outro radio"));

    std::thread writer([] {
        for (int i = 0; i < 2000; ++i) {
            char text[24]; snprintf(text, sizeof(text), "%d", i);
            app_state_add_channel_message(42, text, false, i, i % 8, false);
        }
    });
    for (int i = 0; i < 2000; ++i) {
        msg_rec_t messages[APP_MAX_MSGS]; uint32_t total;
        uint32_t n = app_state_copy_messages_snapshot(messages, APP_MAX_MSGS, &total);
        assert(n <= APP_MAX_MSGS && n <= total);
        for (uint32_t j = 0; j < n; ++j) {
            assert(messages[j].recv_us == total - n + j);
            assert(messages[j].channel == (total - n + j) % 8);
        }
    }
    writer.join();
    struct tm t{}; t.tm_year = 124; t.tm_mon = 1; t.tm_mday = 29; t.tm_hour = 23; t.tm_min = 45;
    time_t epoch;
    assert(app_clock_to_epoch(t, -240, &epoch));
    struct tm utc; gmtime_r(&epoch, &utc);
    assert(utc.tm_mon == 2 && utc.tm_mday == 1 && utc.tm_hour == 3 && utc.tm_min == 45);
    t.tm_year = 125; assert(!app_clock_to_epoch(t, 0, &epoch));
    t.tm_year = 124; t.tm_mday = 30; assert(!app_clock_to_epoch(t, 0, &epoch));
    t.tm_mday = 29; t.tm_hour = 24; assert(!app_clock_to_epoch(t, 0, &epoch));
    t.tm_hour = 0; assert(!app_clock_to_epoch(t, 841, &epoch));
    t.tm_mon = 0; t.tm_mday = 1; t.tm_min = 0;
    assert(app_clock_to_epoch(t, 345, &epoch)); // UTC+5:45, previous date
    gmtime_r(&epoch, &utc);
    assert(utc.tm_year == 123 && utc.tm_mon == 11 && utc.tm_mday == 31 && utc.tm_hour == 18 && utc.tm_min == 15);
    t.tm_year = 100; assert(app_clock_to_epoch(t, 0, &epoch)); assert(epoch == 946684800);
    assert(!app_clock_to_epoch(t, 60, &epoch)); // outside RTC year range
    assert(battery_estimate_percent(0) == -1);
    assert(battery_estimate_percent(std::numeric_limits<float>::quiet_NaN()) == -1);
    assert(battery_estimate_percent(6.0f) == 0);
    assert(battery_estimate_percent(8.4f) == 100);
    assert(battery_estimate_percent(7.4f) == 40);
    int previous = 0;
    for (int mv = 6000; mv <= 8400; mv += 10) {
        int percent = battery_estimate_percent(mv / 1000.0f);
        assert(percent >= previous && percent >= 0 && percent <= 100); previous = percent;
    }
    puts("PASS: own-radio identity ordering; concurrent history snapshots and channel indices; calendar, leap years, UTC offsets RTC limits and battery estimates.");
}
