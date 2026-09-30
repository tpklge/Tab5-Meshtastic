#pragma once
#include "../../stubs/freertos/task.h"
using TaskHandle_t = void*;
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*,int) { return pdPASS; }
