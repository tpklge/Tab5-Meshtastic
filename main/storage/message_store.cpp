#include "message_store.h"
#include "sd_storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "msg_store";

/* Ring buffer backed by a single file on the microSD. 1000 slots × 120 bytes
 * ≈ 120 KB — well under the SD capacity and ~30× what the NVS backend held.
 * Keeping the on-disk record (packed, 120 bytes) binary-compatible with the
 * old NVS layout so that any pre-existing data could be imported later. */
#define MSG_SLOTS      1000
#define MSG_TEXT_MAX   106
#define MSG_FILE_MAGIC 0x4D534754u   /* "MSGT" */
#define MSG_PATH       SD_MOUNT_POINT "/t5msg.bin"

typedef struct __attribute__((packed)) {
    uint8_t  direction;      /* bit 0 = is_tx; bits 1..3 = channel_idx */
    uint32_t from_node;
    int64_t  timestamp_us;
    uint8_t  text_len;
    char     text[MSG_TEXT_MAX];
} msg_rec_t;                 /* 120 bytes (1 + 4 + 8 + 1 + 106) */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t head;           /* next slot to write (0..MSG_SLOTS-1)   */
    uint32_t count;          /* records stored    (0..MSG_SLOTS)      */
    uint32_t total;          /* monotonic append counter (lifetime)   */
    uint32_t reserved[4];
} msg_file_hdr_t;            /* 32 bytes */

static_assert(sizeof(msg_rec_t) == 120, "msg_rec_t must be 120 bytes");
static_assert(sizeof(msg_file_hdr_t) == 32, "msg_file_hdr_t must be 32 bytes");

static bool     s_persistent = false;   /* SD-backed file is usable */
static uint32_t s_head  = 0;
static uint32_t s_count = 0;
static uint32_t s_total = 0;

/* RAM mirror of recent messages, kept whether the backend is SD or RAM-only.
 * Enables fast load_page() without re-reading the SD on every refresh. */
/* The 120 KB mirror must not consume internal RAM needed by FreeRTOS
 * queues, lwIP and the C6 transport. Tab5 provides external PSRAM. */
static msg_rec_t* s_ring = nullptr;

static inline off_t slot_offset(uint32_t slot)
{
    return (off_t)sizeof(msg_file_hdr_t) + (off_t)slot * (off_t)sizeof(msg_rec_t);
}

static esp_err_t create_empty_file(void)
{
    FILE* f = fopen(MSG_PATH, "wb");
    if (!f) return ESP_FAIL;
    msg_file_hdr_t hdr = { MSG_FILE_MAGIC, 0, 0, 0, {0,0,0,0} };
    fwrite(&hdr, 1, sizeof(hdr), f);
    /* Pre-size the file to the full ring so later seeks never grow it. */
    msg_rec_t zero = {};
    for (uint32_t i = 0; i < MSG_SLOTS; i++) fwrite(&zero, 1, sizeof(zero), f);
    fclose(f);
    return ESP_OK;
}

esp_err_t message_store_init(void)
{
    s_head = s_count = s_total = 0;
    s_persistent = false;
    if (!s_ring) {
        s_ring = static_cast<msg_rec_t*>(heap_caps_calloc(
            MSG_SLOTS, sizeof(msg_rec_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!s_ring) {
        ESP_LOGE(TAG, "message mirror PSRAM allocation failed");
        return ESP_ERR_NO_MEM;
    }
    memset(s_ring, 0, MSG_SLOTS * sizeof(msg_rec_t));
    ESP_LOGI(TAG, "message mirror in PSRAM (%u bytes); internal heap free=%u, largest=%u",
             (unsigned)(MSG_SLOTS * sizeof(msg_rec_t)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    if (!sd_storage_available()) {
        ESP_LOGW(TAG, "SD not available — running in RAM-only mode (no persistence)");
        return ESP_OK;
    }

    FILE* f = fopen(MSG_PATH, "rb");
    if (!f) {
        /* First boot on this card — initialize the file. */
        if (create_empty_file() != ESP_OK) {
            ESP_LOGE(TAG, "failed to create %s — RAM-only mode", MSG_PATH);
            return ESP_OK;
        }
        f = fopen(MSG_PATH, "rb");
        if (!f) {
            ESP_LOGE(TAG, "re-open after create failed — RAM-only mode");
            return ESP_OK;
        }
    }

    msg_file_hdr_t hdr = {};
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) || hdr.magic != MSG_FILE_MAGIC) {
        /* Corrupt or legacy file — rebuild empty. */
        fclose(f);
        ESP_LOGW(TAG, "bad/legacy msg file — reinitializing");
        if (create_empty_file() != ESP_OK) return ESP_OK;
        s_persistent = true;
        return ESP_OK;
    }

    if (hdr.head >= MSG_SLOTS) hdr.head = 0;
    if (hdr.count > MSG_SLOTS) hdr.count = MSG_SLOTS;
    s_head  = hdr.head;
    s_count = hdr.count;
    s_total = hdr.total;

    /* Preload the entire ring into RAM so load_page() never touches the SD. */
    fseek(f, (long)sizeof(msg_file_hdr_t), SEEK_SET);
    size_t n = fread(s_ring, sizeof(msg_rec_t), MSG_SLOTS, f);
    fclose(f);
    (void)n;

    s_persistent = true;
    ESP_LOGI(TAG, "loaded from SD: %lu messages (total ever=%lu, path=%s)",
             (unsigned long)s_count, (unsigned long)s_total, MSG_PATH);
    return ESP_OK;
}

esp_err_t message_store_append(uint32_t from_node, uint32_t /*to_node*/,
                                uint32_t /*packet_id*/, uint8_t channel_idx,
                                bool /*is_broadcast*/, bool is_tx,
                                int64_t timestamp_us, int64_t /*abs_time_s*/,
                                int16_t /*snr*/, int16_t /*rssi*/,
                                const char* text)
{
    if (!s_ring) return ESP_ERR_INVALID_STATE;
    msg_rec_t msg = {};
    msg.direction    = (is_tx ? 1 : 0) | ((channel_idx & 7) << 1);
    msg.from_node    = from_node;
    msg.timestamp_us = timestamp_us ? timestamp_us : esp_timer_get_time();
    if (text) {
        size_t len = strnlen(text, MSG_TEXT_MAX);
        msg.text_len = (uint8_t)len;
        memcpy(msg.text, text, len);
    }

    /* RAM ring always updated — the UI reads from here. */
    s_ring[s_head] = msg;
    uint32_t slot = s_head;
    s_head = (s_head + 1) % MSG_SLOTS;
    if (s_count < MSG_SLOTS) s_count++;
    s_total++;

    if (!s_persistent) return ESP_OK;

    FILE* f = fopen(MSG_PATH, "r+b");
    if (!f) {
        ESP_LOGW(TAG, "fopen(%s) failed on append", MSG_PATH);
        return ESP_FAIL;
    }
    if (fseek(f, slot_offset(slot), SEEK_SET) != 0 ||
        fwrite(&msg, 1, sizeof(msg), f) != sizeof(msg)) {
        fclose(f);
        ESP_LOGW(TAG, "record write failed at slot %lu", (unsigned long)slot);
        return ESP_FAIL;
    }
    /* Header rewrite is cheap (32 bytes, same page) and keeps head/count/total
     * durable across a sudden power loss. */
    fseek(f, 0, SEEK_SET);
    msg_file_hdr_t hdr = { MSG_FILE_MAGIC, s_head, s_count, s_total, {0,0,0,0} };
    fwrite(&hdr, 1, sizeof(hdr), f);
    fflush(f);
    fclose(f);
    return ESP_OK;
}

uint32_t message_store_load_page(uint8_t /*channel_idx*/, uint32_t page,
                                  msg_loaded_t* out, uint32_t count)
{
    if (!out || count == 0 || s_count == 0) return 0;

    uint32_t skip    = page * count;
    uint32_t loaded  = 0;
    uint32_t matched = 0;

    for (uint32_t i = 0; i < s_count && loaded < count; i++) {
        uint32_t slot = (s_head + MSG_SLOTS - s_count + i) % MSG_SLOTS;
        const msg_rec_t* msg = &s_ring[slot];
        if (matched++ < skip) continue;

        msg_loaded_t* dst = &out[loaded++];
        memset(dst, 0, sizeof(*dst));
        dst->seq          = s_total - s_count + matched;
        dst->direction    = msg->direction & 1;
        dst->channel_idx  = (msg->direction >> 1) & 7;
        dst->from_node    = msg->from_node;
        dst->timestamp_us = msg->timestamp_us;
        uint8_t tlen = msg->text_len < (uint8_t)(sizeof(dst->text) - 1)
                           ? msg->text_len
                           : (uint8_t)(sizeof(dst->text) - 1);
        memcpy(dst->text, msg->text, tlen);
        dst->text[tlen] = '\0';
    }
    return loaded;
}

uint32_t message_store_total(void) { return s_total; }

esp_err_t message_store_clear(void)
{
    if (!s_ring) return ESP_ERR_INVALID_STATE;
    memset(s_ring, 0, MSG_SLOTS * sizeof(msg_rec_t));
    s_head = s_count = s_total = 0;
    if (s_persistent) {
        if (create_empty_file() != ESP_OK) {
            ESP_LOGW(TAG, "clear: failed to rewrite file");
        }
    }
    ESP_LOGI(TAG, "cleared");
    return ESP_OK;
}
