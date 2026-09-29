#include "app_clock.h"
#include "m5tab5_rtc.h"
#include "nvs.h"
#include <sys/time.h>
#include <stdio.h>

using namespace m5::tab5;
namespace {
bool valid = false;
int16_t offset = -240; // Default UTC-4; user can change in SET.
nvs_handle_t storage = 0;
}
void app_clock_init()
{
    if (nvs_open("t5clock", NVS_READWRITE, &storage) == ESP_OK) {
        nvs_get_i16(storage, "utc_offset", &offset);
        if (offset < -720 || offset > 840) offset = -240;
    }
    m5tab5_rtc_datetime_t dt;
    if (m5tab5_rtc_init() != ESP_OK || m5tab5_rtc_get_datetime(&dt) != ESP_OK) return;
    time_t epoch;
    if (!app_clock_to_epoch(dt.to_tm(), 0, &epoch)) return;
    struct timeval tv{epoch, 0};
    valid = settimeofday(&tv, nullptr) == 0;
}
bool app_clock_local(struct tm* out)
{
    if (!valid || !out) return false;
    time_t now = time(nullptr) + offset * 60;
    return gmtime_r(&now, out) != nullptr;
}
int app_clock_offset_minutes() { return offset; }
esp_err_t app_clock_set(struct tm local, int offset_minutes)
{
    time_t epoch;
    if (!app_clock_to_epoch(local, offset_minutes, &epoch)) return ESP_ERR_INVALID_ARG;
    if (!storage) return ESP_ERR_INVALID_STATE;
    esp_err_t err = m5tab5_rtc_init();
    if (err != ESP_OK) return err;
    struct tm utc{}; gmtime_r(&epoch, &utc);
    m5tab5_rtc_datetime_t dt(utc);
    err = m5tab5_rtc_set_datetime(&dt);
    if (err != ESP_OK) return err;
    // Verify the RTC write before reporting success (one-second rollover allowed).
    m5tab5_rtc_datetime_t check;
    time_t readback;
    err = m5tab5_rtc_get_datetime(&check);
    if (err != ESP_OK) return err;
    if (!app_clock_to_epoch(check.to_tm(), 0, &readback) || readback < epoch || readback > epoch + 1) return ESP_FAIL;
    err = nvs_set_i16(storage, "utc_offset", offset_minutes);
    if (err == ESP_OK) err = nvs_commit(storage);
    if (err != ESP_OK) return err;
    struct timeval tv{readback, 0};
    if (settimeofday(&tv, nullptr) != 0) return ESP_FAIL;
    offset = offset_minutes;
    valid = true;
    return ESP_OK;
}
void app_clock_format(char* out, size_t capacity, bool date)
{
    struct tm local;
    if (!app_clock_local(&local)) { snprintf(out, capacity, "%s", date ? "Hora nao ajustada" : "--:--"); return; }
    strftime(out, capacity, date ? "%d/%m/%Y %H:%M:%S" : "%H:%M", &local);
}
