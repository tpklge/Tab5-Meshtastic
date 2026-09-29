#include "channel_url.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include <cstring>
#include <cstdio>

static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
bool channel_settings_valid(const meshtastic_ChannelSettings& s)
{
    const unsigned n = s.psk.size;
    if (n != 0 && n != 1 && n != 16 && n != 32) return false;
    const size_t len = strnlen(s.name, sizeof(s.name));
    if (len >= sizeof(s.name)) return false;
    // Reject controls and malformed UTF-8; protocol limit is 11 bytes, not characters.
    for (size_t i = 0; i < len;) {
        unsigned c = (unsigned char)s.name[i++];
        if (c < 32 || c == 127) return false;
        if (c < 128) continue;
        unsigned rem, code, min;
        if (c >= 0xC2 && c <= 0xDF) { rem = 1; code = c & 31; min = 128; }
        else if (c >= 0xE0 && c <= 0xEF) { rem = 2; code = c & 15; min = 2048; }
        else if (c >= 0xF0 && c <= 0xF4) { rem = 3; code = c & 7; min = 65536; }
        else return false;
        if (i + rem > len) return false;
        while (rem--) {
            c = (unsigned char)s.name[i++];
            if ((c & 0xC0) != 0x80) return false;
            code = (code << 6) | (c & 63);
        }
        if (code < min || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    }
    return true;
}
bool channel_url_encode(const meshtastic_ChannelSet& set, bool add, char* out, size_t cap)
{
    if (!out || set.settings_count == 0 || set.settings_count > 8) return false;
    for (unsigned i = 0; i < set.settings_count; ++i) if (!channel_settings_valid(set.settings[i])) return false;
    uint8_t raw[1024];
    auto stream = pb_ostream_from_buffer(raw, sizeof(raw));
    if (!pb_encode(&stream, meshtastic_ChannelSet_fields, &set)) return false;
    const char* prefix = add ? "https://meshtastic.org/e/?add=true#" : "https://meshtastic.org/e/#";
    size_t pos = strlen(prefix);
    if (pos + (stream.bytes_written * 8 + 5) / 6 + 1 > cap) return false;
    memcpy(out, prefix, pos);
    unsigned acc = 0, bits = 0;
    for (size_t i = 0; i < stream.bytes_written; ++i) {
        acc = (acc << 8) | raw[i]; bits += 8;
        while (bits >= 6) { bits -= 6; out[pos++] = alphabet[(acc >> bits) & 63]; }
    }
    if (bits) out[pos++] = alphabet[(acc << (6 - bits)) & 63];
    out[pos] = 0;
    return true;
}
bool channel_url_decode(const char* url, meshtastic_ChannelSet* out, bool* add)
{
    if (!url || !out || !add || strnlen(url, CHANNEL_URL_MAX) >= CHANNEL_URL_MAX) return false;
    const char* prefixes[] = {"https://meshtastic.org/e/#", "https://meshtastic.org/e/?add=true#"};
    const char* data = nullptr;
    for (unsigned i = 0; i < 2; ++i) {
        if (strncmp(url, prefixes[i], strlen(prefixes[i])) == 0) { data = url + strlen(prefixes[i]); *add = i == 1; break; }
    }
    if (!data || !*data) return false;
    size_t len = strlen(data), padding = 0;
    while (len && data[len-1] == '=') { --len; ++padding; }
    if (padding > 2 || len % 4 == 1 || (padding && (len + padding) % 4)) return false;
    uint8_t raw[1024]; size_t count = 0; unsigned acc = 0, bits = 0;
    for (size_t i = 0; i < len; ++i) {
        const char* p = strchr(alphabet, data[i]);
        if (!p) return false;
        acc = (acc << 6) | (unsigned)(p - alphabet); bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (count == sizeof(raw)) return false;
            raw[count++] = (acc >> bits) & 255;
        }
    }
    if (bits && (acc & ((1U << bits) - 1))) return false;
    meshtastic_ChannelSet candidate = meshtastic_ChannelSet_init_zero;
    auto stream = pb_istream_from_buffer(raw, count);
    if (!pb_decode(&stream, meshtastic_ChannelSet_fields, &candidate) || !candidate.settings_count || candidate.settings_count > 8) return false;
    for (unsigned i = 0; i < candidate.settings_count; ++i) if (!channel_settings_valid(candidate.settings[i])) return false;
    *out = candidate;
    return true;
}

void channel_settings_resolve_inherited_key(meshtastic_ChannelSettings* settings, const meshtastic_ChannelSettings& primary)
{
    if (settings->psk.size) return;
    settings->psk = primary.psk;
    // Explicit no-encryption shortcut avoids accidentally inheriting a different
    // receiver primary key when importing this as a secondary channel.
    if (!settings->psk.size) { settings->psk.size = 1; settings->psk.bytes[0] = 0; }
}
