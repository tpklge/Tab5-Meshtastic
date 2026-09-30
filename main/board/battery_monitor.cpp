#include "battery_monitor.h"
#include "m5_tab5_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <cmath>
namespace {
SemaphoreHandle_t mutex;
battery_snapshot_t state{};
int64_t last_read_us;
void worker(void* arg) {
    auto* board = static_cast<m5::tab5::m5tab5_component*>(arg);
    bool initialized = false;
    for (;;) {
        if (!initialized) initialized = board->ina226_init() == ESP_OK;
        m5::tab5::m5tab5_component::ina226_reading_t reading{};
        bool valid = initialized && board->ina226_read(&reading) == ESP_OK;
        int percent = valid ? battery_estimate_percent(reading.bus_voltage_v) : -1;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (percent >= 0) {
            state = {true, reading.bus_voltage_v, percent};
            last_read_us = esp_timer_get_time();
        } else state.valid = false;
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(initialized ? 5000 : 30000));
    }
}
}
esp_err_t battery_monitor_start(m5::tab5::m5tab5_component* board) {
    if (!board) return ESP_ERR_INVALID_ARG;
    mutex = xSemaphoreCreateMutex();
    if (!mutex) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(worker, "battery", 4096, board, 1, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void battery_monitor_snapshot(battery_snapshot_t* out) {
    *out = {};
    if (!mutex || xSemaphoreTake(mutex, 0) != pdTRUE) return;
    *out = state;
    if (esp_timer_get_time() - last_read_us > 15000000) out->valid = false;
    xSemaphoreGive(mutex);
}
