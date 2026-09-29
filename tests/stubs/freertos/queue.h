#pragma once
#include "FreeRTOS.h"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <cstring>
struct TestQueue { size_t cap, size; std::mutex m; std::condition_variable cv; std::deque<std::vector<unsigned char>> data; };
using QueueHandle_t = TestQueue*;
inline QueueHandle_t xQueueCreate(size_t cap, size_t size) { auto q=new TestQueue; q->cap=cap;q->size=size;return q; }
inline int xQueueSend(QueueHandle_t q, const void* p, uint32_t) {
 std::lock_guard<std::mutex> lock(q->m); if(q->data.size()==q->cap)return pdFALSE;
 auto bytes=static_cast<const unsigned char*>(p);q->data.emplace_back(bytes,bytes+q->size);q->cv.notify_one();return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q, void* p, uint32_t timeout) {
 std::unique_lock<std::mutex> lock(q->m);
 if(timeout==portMAX_DELAY)q->cv.wait(lock,[&]{return !q->data.empty();});
 else if(!q->cv.wait_for(lock,std::chrono::milliseconds(timeout/10+1),[&]{return !q->data.empty();}))return pdFALSE;
 memcpy(p,q->data.front().data(),q->size);q->data.pop_front();return pdTRUE;
}
inline void xQueueReset(QueueHandle_t q) { std::lock_guard<std::mutex> lock(q->m);q->data.clear(); }
