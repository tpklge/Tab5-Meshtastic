#pragma once
#include <stddef.h>
#include <time.h>
#include "esp_err.h"

// Called after board/NVS init. RTC stores UTC; offset is persisted separately.
void app_clock_init();
bool app_clock_local(struct tm* out);
int app_clock_offset_minutes();
esp_err_t app_clock_set(struct tm local, int offset_minutes);
void app_clock_format(char* out, size_t capacity, bool date);
// Pure calendar conversion, rejects normalized/invalid dates.
bool app_clock_to_epoch(struct tm local, int offset_minutes, time_t* epoch);
