#include "message_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "msg_store";
static const char* NS  = "t5msgs";

/* Ring buffer: 30 slots, each 120 bytes stored as an NVS blob.
 * Compact on-disk struct — only what the UI needs. */
#define MSG_SLOTS 30
#define MSG_TEXT_MAX 107

typedef struct __attribute__((packed)) {
    uint8_t  direction;     /* 0=rx, 1=tx */
    uint32_t from_node;
    int64_t  timestamp_us;
    uint8_t  text_len;
    char     text[MSG_TEXT_MAX];
} nvs_msg_t;                /* 120 bytes */

static nvs_handle_t s_nvs   = 0;
static uint16_t     s_head  = 0;   /* next slot to write (0..MSG_SLOTS-1) */
static uint16_t     s_count = 0;   /* messages stored (0..MSG_SLOTS) */
static uint32_t     s_total = 0;   /* monotonic append counter */

static void slot_key(char* buf, uint16_t idx)
{
    snprintf(buf, 8, "m%02u", (unsigned)idx);
}

esp_err_t message_store_init(void)
{
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        s_nvs = 0;
        return err;
    }

    uint16_t head = 0, count = 0;
    uint32_t total = 0;
    nvs_get_u16(s_nvs, "head",  &head);
    nvs_get_u16(s_nvs, "count", &count);
    nvs_get_u32(s_nvs, "total", &total);

    if (head >= MSG_SLOTS) head = 0;
    if (count > MSG_SLOTS) count = MSG_SLOTS;

    s_head  = head;
    s_count = count;
    s_total = total;
    ESP_LOGI(TAG, "loaded: %u messages (total ever=%lu)", (unsigned)s_count, (unsigned long)s_total);
    return ESP_OK;
}

esp_err_t message_store_append(uint32_t from_node, uint32_t /*to_node*/,
                                uint32_t /*packet_id*/, uint8_t /*channel_idx*/,
                                bool /*is_broadcast*/, bool is_tx,
                                int64_t timestamp_us, int64_t /*abs_time_s*/,
                                int16_t /*snr*/, int16_t /*rssi*/,
                                const char* text)
{
    if (!s_nvs) return ESP_OK;  /* silent no-op — already logged at init */

    nvs_msg_t msg = {};
    msg.direction    = is_tx ? 1 : 0;
    msg.from_node    = from_node;
    msg.timestamp_us = timestamp_us ? timestamp_us : esp_timer_get_time();
    if (text) {
        size_t len = strnlen(text, MSG_TEXT_MAX);
        msg.text_len = (uint8_t)len;
        memcpy(msg.text, text, len);
    }

    char key[8];
    slot_key(key, s_head);
    esp_err_t err = nvs_set_blob(s_nvs, key, &msg, sizeof(msg));
    if (err != ESP_OK) { ESP_LOGW(TAG, "nvs_set_blob(%s): %s", key, esp_err_to_name(err)); return err; }

    s_head = (s_head + 1) % MSG_SLOTS;
    if (s_count < MSG_SLOTS) s_count++;
    s_total++;

    nvs_set_u16(s_nvs, "head",  s_head);
    nvs_set_u16(s_nvs, "count", s_count);
    nvs_set_u32(s_nvs, "total", s_total);
    nvs_commit(s_nvs);
    return ESP_OK;
}

uint32_t message_store_load_page(uint8_t /*channel_idx*/, uint32_t page,
                                  msg_loaded_t* out, uint32_t count)
{
    if (!s_nvs || !out || count == 0 || s_count == 0) return 0;

    /* Walk the ring oldest→newest. Oldest slot: (head - count + MSG_SLOTS) % MSG_SLOTS */
    uint32_t skip    = page * count;
    uint32_t loaded  = 0;
    uint32_t matched = 0;

    for (uint16_t i = 0; i < s_count && loaded < count; i++) {
        uint16_t slot = (uint16_t)((s_head - s_count + i + MSG_SLOTS) % MSG_SLOTS);
        char key[8];
        slot_key(key, slot);

        nvs_msg_t msg = {};
        size_t sz = sizeof(msg);
        if (nvs_get_blob(s_nvs, key, &msg, &sz) != ESP_OK) continue;

        if (matched++ < skip) continue;

        msg_loaded_t* dst = &out[loaded++];
        memset(dst, 0, sizeof(*dst));
        dst->seq          = s_total - s_count + matched;
        dst->direction    = msg.direction;
        dst->from_node    = msg.from_node;
        dst->timestamp_us = msg.timestamp_us;
        uint8_t tlen = msg.text_len < (uint8_t)(sizeof(dst->text) - 1) ? msg.text_len : (uint8_t)(sizeof(dst->text) - 1);
        memcpy(dst->text, msg.text, tlen);
        dst->text[tlen] = '\0';
    }
    return loaded;
}

uint32_t message_store_total(void) { return s_total; }

esp_err_t message_store_clear(void)
{
    if (!s_nvs) return ESP_OK;
    for (uint16_t i = 0; i < MSG_SLOTS; i++) {
        char key[8]; slot_key(key, i);
        nvs_erase_key(s_nvs, key);
    }
    s_head = 0; s_count = 0; s_total = 0;
    nvs_set_u16(s_nvs, "head",  0);
    nvs_set_u16(s_nvs, "count", 0);
    nvs_set_u32(s_nvs, "total", 0);
    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "cleared");
    return ESP_OK;
}
