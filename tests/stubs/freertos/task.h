#pragma once
#include "FreeRTOS.h"
#include <thread>
#include <chrono>
inline int xTaskCreate(void(*f)(void*), const char*, unsigned, void* p, unsigned, void*) { std::thread(f,p).detach(); return pdPASS; }
inline void vTaskDelay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms/10+1)); }
