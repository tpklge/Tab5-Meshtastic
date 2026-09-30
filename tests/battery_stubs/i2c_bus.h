#pragma once
#include <stdint.h>
#include "esp_err.h"
using i2c_bus_handle_t = void*;
using i2c_bus_device_handle_t = void*;
i2c_bus_device_handle_t i2c_bus_device_create(i2c_bus_handle_t, uint8_t, uint32_t);
esp_err_t i2c_bus_device_delete(i2c_bus_device_handle_t*);
esp_err_t i2c_bus_read_bytes(i2c_bus_device_handle_t, uint8_t, uint8_t, uint8_t*);
esp_err_t i2c_bus_write_bytes(i2c_bus_device_handle_t, uint8_t, uint8_t, const uint8_t*);
