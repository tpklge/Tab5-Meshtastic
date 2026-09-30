#include "drivers/ina226/m5tab5_ina226.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
using namespace m5::tab5;
static uint16_t registers[256];
static int fail_register = -1;
static bool ignore_config = false;
i2c_bus_device_handle_t i2c_bus_device_create(i2c_bus_handle_t, uint8_t address, uint32_t) { assert(address == 0x41); return registers; }
esp_err_t i2c_bus_device_delete(i2c_bus_device_handle_t* dev) { *dev = nullptr; return ESP_OK; }
esp_err_t i2c_bus_read_bytes(i2c_bus_device_handle_t, uint8_t reg, uint8_t len, uint8_t* data) {
    assert(len == 2); if (reg == fail_register) return ESP_FAIL;
    data[0] = registers[reg] >> 8; data[1] = registers[reg]; return ESP_OK;
}
esp_err_t i2c_bus_write_bytes(i2c_bus_device_handle_t, uint8_t reg, uint8_t len, const uint8_t* data) {
    assert(len == 2); if (ignore_config && reg == INA226_REG_CONFIG) return ESP_OK; registers[reg] = (data[0] << 8) | data[1]; return ESP_OK;
}
int main() {
    m5tab5_ina226_config_t config{}; config.bus = registers;
    registers[0xFE] = 0x5449;
    for (uint16_t revision : {0x2260, 0x2261, 0x226F}) {
        registers[0xFF] = revision;
        m5tab5_ina226_t sensor{};
        assert(m5tab5_ina226_init(&config, &sensor) == ESP_OK);
        registers[INA226_REG_BUS_V] = 6000; // 7.5 V, big-endian at 1.25 mV/LSB
        fail_register = INA226_REG_CURRENT;
        float voltage = 0;
        assert(m5tab5_ina226_read_bus_voltage(&sensor, &voltage) == ESP_OK);
        assert(std::fabs(voltage - 7.5f) < 0.001f);
        float current;
        assert(m5tab5_ina226_read_current(&sensor, &current) == ESP_FAIL);
        fail_register = -1;
        m5tab5_ina226_deinit(&sensor);
    }
    m5tab5_ina226_t sensor{};
    registers[0xFF] = 0x2270; assert(m5tab5_ina226_init(&config, &sensor) != ESP_OK);
    registers[0xFF] = 0x2261; registers[0xFE] = 0; assert(m5tab5_ina226_init(&config, &sensor) != ESP_OK);
    config.allow_voltage_compatible = true;
    registers[0xFE] = 0; registers[0xFF] = 0; registers[INA226_REG_CONFIG] = 0x4127;
    assert(m5tab5_ina226_init(&config, &sensor) == ESP_OK);
    float voltage; assert(m5tab5_ina226_read_bus_voltage(&sensor, &voltage) == ESP_OK);
    assert(std::fabs(voltage - 7.5f) < 0.001f);
    assert(sensor.manufacturer_id == 0 && sensor.die_id == 0);
    float current; assert(m5tab5_ina226_read_current(&sensor, &current) == ESP_ERR_NOT_SUPPORTED);
    m5tab5_ina226_deinit(&sensor);
    ignore_config = true; registers[INA226_REG_CONFIG] = 0;
    assert(m5tab5_ina226_init(&config, &sensor) == ESP_ERR_INVALID_RESPONSE);
    ignore_config = false; fail_register = INA226_REG_CONFIG;
    assert(m5tab5_ina226_init(&config, &sensor) == ESP_FAIL);
    puts("PASS: INA226 revisions 0/1/F, reject wrong chip/manufacturer, big-endian voltage and independence from current read failure; compatible voltage mode and configuration readback failures.");
}
