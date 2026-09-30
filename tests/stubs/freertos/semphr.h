#pragma once
#include "FreeRTOS.h"
#include <mutex>
using SemaphoreHandle_t = std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
inline int xSemaphoreTake(SemaphoreHandle_t m, uint32_t timeout) { if (!timeout) return m->try_lock() ? pdTRUE : pdFALSE; m->lock(); return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t m) { m->unlock(); }
