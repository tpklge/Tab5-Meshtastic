#include "battery_monitor.h"
#include <cmath>
int battery_estimate_percent(float volts)
{
    if (!std::isfinite(volts) || volts < 4.0f || volts > 9.0f) return -1;
    // Approximate discharge curve for the Tab5's 2-cell NP-F550 battery.
    static const float voltage[] = {6.0f, 6.6f, 7.0f, 7.2f, 7.4f, 7.6f, 7.8f, 8.0f, 8.4f};
    static const int percent[] = {0, 5, 10, 20, 40, 60, 75, 90, 100};
    if (volts <= voltage[0]) return 0;
    for (unsigned i = 1; i < sizeof(voltage)/sizeof(voltage[0]); ++i) {
        if (volts <= voltage[i]) return (int)std::lround(percent[i-1] +
            (volts - voltage[i-1]) * (percent[i] - percent[i-1]) / (voltage[i] - voltage[i-1]));
    }
    return 100;
}
