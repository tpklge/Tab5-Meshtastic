#include "message_store.h"
#include "app_storage.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "msg_store";
static const char* MSG_PATH = STORAGE_MOUNT_POINT "/messages.bin";

#define FILE_MAGIC   0x54354D02u
#define RECORD_MAGIC 0x4D534754u
#define FILE_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  _pad[3];
    uint32_t count;
    uint32_t crc32;
} file_header_t;

static uint32_t s_seq_counter = 0;
static uint32_t s_total = 0;

static uint32_t crc32_buf(const void* buf, size_t len)
{
    return esp_crc32_le(0, (const uint8_t*)buf, len);
}

static bool write_file_header(FILE* f, uint32_t count)
{
    file_header_t hdr = {};
    hdr.magic   = FILE_MAGIC;
    hdr.version = FILE_VERSION;
    hdr.count   = count;
    hdr.crc32   = crc32_buf(&hdr, offsetof(file_header_t, crc32));
    rewind(f);
    return fwrite(&hdr, sizeof(hdr), 1, f) == 1;
}

esp_err_t message_store_init(void)
{
    if (!app_storage_available()) return ESP_ERR_NOT_SUPPORTED;

    FILE* f = fopen(MSG_PATH, "rb");
    if (!f) {
        // Create new file with header
        f = fopen(MSG_PATH, "wb");
        if (!f) { ESP_LOGE(TAG, "cannot create %s", MSG_PATH); return ESP_FAIL; }
        write_file_header(f, 0);
        fclose(f);
        ESP_LOGI(TAG, "created new message store");
        return ESP_OK;
    }

    // Read and validate existing header
    file_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f); goto recreate;
    }
    if (hdr.magic != FILE_MAGIC || hdr.version != FILE_VERSION) {
        fclose(f); goto recreate;
    }
    {
        uint32_t expected = crc32_buf(&hdr, offsetof(file_header_t, crc32));
        if (hdr.crc32 != expected) { fclose(f); goto recreate; }
    }

    // Scan forward to find highest seq
    {
        msg_record_hdr_t rec;
        while (fread(&rec, sizeof(rec), 1, f) == 1) {
            if (rec.magic != RECORD_MAGIC || rec.version != FILE_VERSION) break;
            if (rec.seq > s_seq_counter) s_seq_counter = rec.seq;
            s_total++;
            // Skip text payload
            if (rec.text_len > 0) {
                fseek(f, rec.text_len, SEEK_CUR);
            }
        }
        ESP_LOGI(TAG, "loaded: %lu records, max_seq=%lu", (unsigned long)s_total,
                 (unsigned long)s_seq_counter);
    }
    fclose(f);
    return ESP_OK;

recreate:
    ESP_LOGW(TAG, "invalid store — reinitialising");
    s_seq_counter = 0;
    s_total = 0;
    f = fopen(MSG_PATH, "wb");
    if (!f) return ESP_FAIL;
    write_file_header(f, 0);
    fclose(f);
    return ESP_OK;
}

esp_err_t message_store_append(uint32_t from_node, uint32_t to_node,
                                uint32_t packet_id, uint8_t channel_idx,
                                bool is_broadcast, bool is_tx,
                                int64_t timestamp_us, int64_t abs_time_s,
                                int16_t snr, int16_t rssi,
                                const char* text)
{
    if (!app_storage_available()) return ESP_OK;

    size_t text_len = text ? strnlen(text, 255) : 0;

    // Build record
    msg_record_hdr_t rec = {};
    rec.magic        = RECORD_MAGIC;
    rec.seq          = ++s_seq_counter;
    rec.version      = FILE_VERSION;
    rec.direction    = is_tx ? 1 : 0;
    rec.channel_idx  = channel_idx;
    rec.is_broadcast = is_broadcast ? 1 : 0;
    rec.from_node    = from_node;
    rec.to_node      = to_node;
    rec.packet_id    = packet_id;
    rec.timestamp_us = timestamp_us;
    rec.abs_time_s   = abs_time_s;
    rec.snr          = snr;
    rec.rssi         = rssi;
    rec.text_len     = (uint16_t)text_len;
    rec.crc32        = crc32_buf(&rec, offsetof(msg_record_hdr_t, crc32));

    FILE* f = fopen(MSG_PATH, "ab");
    if (!f) { ESP_LOGE(TAG, "cannot open for append"); return ESP_FAIL; }

    bool ok = (fwrite(&rec, sizeof(rec), 1, f) == 1);
    if (ok && text_len > 0) {
        ok = (fwrite(text, 1, text_len, f) == text_len);
    }
    fflush(f);
    fclose(f);

    if (ok) {
        s_total++;
        if (s_total > MSG_STORE_MAX_RECORDS) {
            ESP_LOGW(TAG, "record limit reached — compaction needed");
            // TODO: implement compaction (Phase 3 follow-up)
        }
    }
    return ok ? ESP_OK : ESP_FAIL;
}

uint32_t message_store_load_page(uint8_t channel_idx, uint32_t page,
                                  msg_loaded_t* out, uint32_t count)
{
    if (!app_storage_available() || !out || count == 0) return 0;

    FILE* f = fopen(MSG_PATH, "rb");
    if (!f) return 0;

    // Skip file header
    fseek(f, sizeof(file_header_t), SEEK_SET);

    // Collect matching records into a temp array (simple linear scan)
    // For large files a proper index would be needed; for now acceptable.
    uint32_t skip = page * count;
    uint32_t loaded = 0;
    uint32_t matched = 0;

    msg_record_hdr_t rec;
    char text_buf[256];

    while (fread(&rec, sizeof(rec), 1, f) == 1) {
        if (rec.magic != RECORD_MAGIC || rec.version != FILE_VERSION) break;

        bool match = (channel_idx == 0xFF) || (rec.channel_idx == channel_idx);
        if (rec.text_len > 0) {
            size_t r = fread(text_buf, 1, rec.text_len < 255 ? rec.text_len : 255, f);
            text_buf[r] = '\0';
            if (r < rec.text_len) fseek(f, rec.text_len - r, SEEK_CUR);
        } else {
            text_buf[0] = '\0';
        }

        if (!match) continue;
        if (matched++ < skip) continue;
        if (loaded >= count) break;

        msg_loaded_t* dst = &out[loaded++];
        dst->seq          = rec.seq;
        dst->direction    = rec.direction;
        dst->channel_idx  = rec.channel_idx;
        dst->is_broadcast = rec.is_broadcast;
        dst->from_node    = rec.from_node;
        dst->to_node      = rec.to_node;
        dst->packet_id    = rec.packet_id;
        dst->timestamp_us = rec.timestamp_us;
        dst->snr          = rec.snr;
        dst->rssi         = rec.rssi;
        strncpy(dst->text, text_buf, sizeof(dst->text) - 1);
        dst->text[sizeof(dst->text)-1] = '\0';
    }

    fclose(f);
    return loaded;
}

uint32_t message_store_total(void)
{
    return s_total;
}

esp_err_t message_store_clear(void)
{
    if (!app_storage_available()) return ESP_OK;
    FILE* f = fopen(MSG_PATH, "wb");
    if (!f) return ESP_FAIL;
    write_file_header(f, 0);
    fclose(f);
    s_seq_counter = 0;
    s_total = 0;
    ESP_LOGI(TAG, "message store cleared");
    return ESP_OK;
}
