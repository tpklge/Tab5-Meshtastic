#pragma once
#include "meshtastic/apponly.pb.h"
#include <stddef.h>
constexpr size_t CHANNEL_URL_MAX = 2048;
bool channel_settings_valid(const meshtastic_ChannelSettings& s);
bool channel_url_encode(const meshtastic_ChannelSet& set, bool add, char* out, size_t cap);
bool channel_url_decode(const char* url, meshtastic_ChannelSet* out, bool* add);

void channel_settings_resolve_inherited_key(meshtastic_ChannelSettings* settings, const meshtastic_ChannelSettings& primary);
