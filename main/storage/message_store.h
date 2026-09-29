#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of records kept before compaction.
#define MSG_STORE_MAX_RECORDS 10000
// Maximum file size before compaction (leave headroom in 1MB partition).
#define MSG_STORE_MAX_BYTES   (800 * 1024)
// Records per page for paginated loads.
#define MSG_STORE_PAGE_SIZE   200

// On-disk message record (matches STORAGE_SCHEMA.md v1).
// Do NOT change field order or size — bump the version and add migration instead.
typedef struct __attribute__((packed)) {
    uint32_t magic;        // 0x4D534754
    uint32_t seq;
    uint8_t  version;      // 1
    uint8_t  direction;    // 0=rx, 1=tx
    uint8_t  channel_idx;
    uint8_t  is_broadcast;
    uint32_t from_node;
    uint32_t to_node;
    uint32_t packet_id;
    int64_t  timestamp_us;
    int64_t  abs_time_s;
    int16_t  snr;
    int16_t  rssi;
    uint16_t text_len;
    uint8_t  _pad[2];
    uint32_t crc32;
    // followed by text_len bytes of UTF-8 text (no null terminator)
} msg_record_hdr_t;

// In-RAM representation after loading.
typedef struct {
    uint32_t seq;
    uint8_t  direction;
    uint8_t  channel_idx;
    uint8_t  is_broadcast;
    uint32_t from_node;
    uint32_t to_node;
    uint32_t packet_id;
    int64_t  timestamp_us;
    int16_t  snr;
    int16_t  rssi;
    char     text[256];
} msg_loaded_t;

// Initialize message store (call after app_storage_init).
esp_err_t message_store_init(void);

// Append a message. No-op (returns ESP_OK) if storage unavailable.
// Deduplicates by (from_node, packet_id, channel_idx) — returns ESP_OK
// for duplicates without writing.
esp_err_t message_store_append(uint32_t from_node, uint32_t to_node,
                                uint32_t packet_id, uint8_t channel_idx,
                                bool is_broadcast, bool is_tx,
                                int64_t timestamp_us, int64_t abs_time_s,
                                int16_t snr, int16_t rssi,
                                const char* text);

// Load the most-recent `count` records for the given channel into out[].
// Pass channel_idx = 0xFF to load all channels.
// Returns number of records loaded.
uint32_t message_store_load_page(uint8_t channel_idx, uint32_t page,
                                  msg_loaded_t* out, uint32_t count);

// Total records ever appended (monotonic, survives app restart).
uint32_t message_store_total(void);

// Clear all stored messages (shows confirmation dialog before calling).
esp_err_t message_store_clear(void);

#ifdef __cplusplus
}
#endif
