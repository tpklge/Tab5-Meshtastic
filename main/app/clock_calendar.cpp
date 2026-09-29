#include "app_clock.h"
#include <stdint.h>

bool app_clock_to_epoch(struct tm local, int offset_minutes, time_t* epoch)
{
    if (!epoch || offset_minutes < -720 || offset_minutes > 840 ||
        local.tm_year < 100 || local.tm_year > 199 || local.tm_mon < 0 || local.tm_mon > 11 ||
        local.tm_mday < 1 || local.tm_mday > 31 || local.tm_hour < 0 || local.tm_hour > 23 ||
        local.tm_min < 0 || local.tm_min > 59 || local.tm_sec < 0 || local.tm_sec > 59) return false;
    // UTC-only conversion; never mutates process-wide TZ.
    int year = local.tm_year + 1900;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (local.tm_mday > days[local.tm_mon] + (local.tm_mon == 1 && leap)) return false;
    int64_t total_days = 0;
    for (int y = 1970; y < year; ++y) total_days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    for (int m = 0; m < local.tm_mon; ++m) total_days += days[m] + (m == 1 && leap);
    total_days += local.tm_mday - 1;
    *epoch = (time_t)(total_days * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec - offset_minutes * 60);
    struct tm utc{}; gmtime_r(epoch, &utc);
    return utc.tm_year >= 100 && utc.tm_year <= 199;
}
