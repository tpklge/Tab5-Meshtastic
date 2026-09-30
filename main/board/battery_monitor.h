#pragma once
#include "esp_err.h"
namespace m5::tab5 { class m5tab5_component; }
struct battery_snapshot_t { bool valid; float voltage; int percent; };
esp_err_t battery_monitor_start(m5::tab5::m5tab5_component* board);
void battery_monitor_snapshot(battery_snapshot_t* out);
// Approximate 2S Li-ion state of charge, not a coulomb counter.
int battery_estimate_percent(float volts);
