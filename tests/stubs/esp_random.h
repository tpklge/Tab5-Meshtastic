#pragma once
#include <atomic>
#include <cstdint>
inline uint32_t esp_random() { static std::atomic<uint32_t> n{100}; return ++n; }
