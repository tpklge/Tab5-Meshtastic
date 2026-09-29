#pragma once
#include "channel_url.h"
#include "meshtastic/channel.pb.h"
#include "esp_err.h"
struct channel_snapshot_t {
    meshtastic_Channel channels[8];
    bool known[8];
    bool has_lora;
    meshtastic_Config_LoRaConfig lora;
    bool busy;
    uint32_t generation;
    char status[160];
};
using channel_send_fn = esp_err_t (*)(const uint8_t*, size_t);
esp_err_t channel_service_init(channel_send_fn send);
void channel_service_on_frame(const uint8_t* data, size_t size);
void channel_service_snapshot(channel_snapshot_t* out);
bool channel_service_save(const meshtastic_Channel& channel);
bool channel_service_import(const meshtastic_ChannelSet& set, bool replace, bool apply_lora);
bool channel_service_refresh();
bool channel_service_share(int index, char* url, size_t capacity); // -1 = full set
