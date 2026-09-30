#include "battery_monitor.h"
#include "m5_tab5_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <cmath>
namespace {
SemaphoreHandle_t mutex;
battery_snapshot_t state{false, 0, -1, ESP_ERR_INVALID_STATE};
int64_t last_read_us;
void worker(void* arg) {
    auto* board = static_cast<m5::tab5::m5tab5_component*>(arg);
    bool initialized = false;
    for (;;) {
        esp_err_t error = ESP_OK;
        if (!initialized) {
            error = board->ina226_init();
            initialized = error == ESP_OK;
            // 16 averaged bus/shunt conversions need ~35 ms after reset.
            if (initialized) vTaskDelay(pdMS_TO_TICKS(100));
        }
        float voltage = 0;
        if (initialized) error = board->ina226_read_voltage(&voltage);
        bool valid_voltage = error == ESP_OK && std::isfinite(voltage) && voltage >= 0 && voltage <= 36;
        if (error == ESP_OK && !valid_voltage) error = ESP_ERR_INVALID_RESPONSE;
        xSemaphoreTake(mutex, portMAX_DELAY);
        state.error = error;
        if (valid_voltage) {
            state.valid = true;
            state.voltage = voltage;
            state.percent = battery_estimate_percent(voltage);
            last_read_us = esp_timer_get_time();
        }
        // Keep a recent good sample through a transient I2C failure. The reader
        // rejects it after 15 seconds; the diagnostic always shows the last error.
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(initialized ? 2000 : 5000));
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
    *out = {false, 0, -1, ESP_ERR_INVALID_STATE};
    if (!mutex || xSemaphoreTake(mutex, 0) != pdTRUE) return;
    *out = state;
    if (esp_timer_get_time() - last_read_us > 15000000) out->valid = false;
    xSemaphoreGive(mutex);
}
