#include "channel_url.h"
#include "mesh_proto.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/admin.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "quirc.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero, decoded{};
    bool add = false; char url[CHANNEL_URL_MAX];
    assert(channel_url_decode("https://meshtastic.org/e/#CgMSAQE", &decoded, &add));
    assert(!add && decoded.settings_count == 1 && decoded.settings[0].psk.size == 1 && decoded.settings[0].psk.bytes[0] == 1);
    for (unsigned count = 1; count <= 8; ++count) {
        set.settings_count = count;
        for (unsigned i = 0; i < count; ++i) {
            snprintf(set.settings[i].name, sizeof(set.settings[i].name), "Canal%u", i);
            set.settings[i].psk.size = 32;
            for (unsigned j = 0; j < 32; ++j) set.settings[i].psk.bytes[j] = i * 32 + j;
        }
        set.has_lora_config = true; set.lora_config.use_preset = true;
        set.lora_config.region = meshtastic_Config_LoRaConfig_RegionCode_ANZ; set.lora_config.channel_num = 20;
        assert(channel_url_encode(set, true, url, sizeof(url)));
        assert(channel_url_decode(url, &decoded, &add) && add);
        assert(decoded.settings_count == count && decoded.lora_config.channel_num == 20);
        for (unsigned i = 0; i < count; ++i) assert(!memcmp(decoded.settings[i].psk.bytes, set.settings[i].psk.bytes, 32));
        std::string padded(url); while ((padded.size() - padded.find('#') - 1) % 4) padded += '=';
        assert(channel_url_decode(padded.c_str(), &decoded, &add));
    }
    FILE* official = fopen("tests/fixtures/channels-eight.url", "r"); assert(official);
    char reference[CHANNEL_URL_MAX]; assert(fgets(reference, sizeof(reference), official)); fclose(official);
    reference[strcspn(reference, "\r\n")] = 0;
    assert(!strcmp(reference, url)); // Independent official Google protobuf serialization.
    FILE* out = fopen(argv[1], "w"); assert(out); fprintf(out, "%s\n", url); fclose(out);
    assert(!channel_url_encode(set, false, url, 10));
    const char* invalid[] = {"", "https://evil.example/e/#CgMSAQE", "https://meshtastic.org/e/#", "https://meshtastic.org/e/#A", "https://meshtastic.org/e/#AA$", "https://meshtastic.org/e/#CgMSAQF", "https://meshtastic.org/e/#CgMSAQE===", "https://meshtastic.org/e/#CgQSAgoK"};
    for (auto s : invalid) assert(!channel_url_decode(s, &decoded, &add));
    auto c = set.settings[0];
    for (unsigned n = 0; n <= 32; ++n) { c.psk.size = n; assert(channel_settings_valid(c) == (n == 0 || n == 1 || n == 16 || n == 32)); }
    c.psk.size = 32; strcpy(c.name, "Ol\xc3\xa1"); assert(channel_settings_valid(c));
    strcpy(c.name, "\xc0\x80"); assert(!channel_settings_valid(c));
    memset(c.name, 'x', sizeof(c.name)); assert(!channel_settings_valid(c));
    auto inherited = set.settings[0]; inherited.psk.size = 0;
    channel_settings_resolve_inherited_key(&inherited, set.settings[0]);
    assert(inherited.psk.size == 32 && !memcmp(inherited.psk.bytes, set.settings[0].psk.bytes, 32));
    inherited.psk.size = 0; auto plain = inherited;
    channel_settings_resolve_inherited_key(&inherited, plain);
    assert(inherited.psk.size == 1 && inherited.psk.bytes[0] == 0);
    uint8_t bytes[512];
    for (unsigned i = 0; i < 8; ++i) {
        size_t n = mesh_encode_text_channel("teste", i, bytes, sizeof(bytes)); assert(n);
        meshtastic_ToRadio radio{}; auto stream = pb_istream_from_buffer(bytes, n);
        assert(pb_decode(&stream, meshtastic_ToRadio_fields, &radio));
        assert(radio.packet.channel == i && radio.packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP);
    }
    assert(!mesh_encode_text_channel("bad", 8, bytes, sizeof(bytes)));
    {
        size_t n = mesh_encode_text_channel_id("primeira", 0, 0x12345678, bytes, sizeof(bytes));
        assert(n);
        meshtastic_ToRadio radio{}; auto stream = pb_istream_from_buffer(bytes, n);
        assert(pb_decode(&stream, meshtastic_ToRadio_fields, &radio));
        assert(radio.packet.id == 0x12345678 && radio.packet.decoded.payload.size == 8);

        meshtastic_FromRadio status = meshtastic_FromRadio_init_zero;
        status.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
        status.queueStatus.mesh_packet_id = radio.packet.id;
        status.queueStatus.res = 34;
        auto encoded = pb_ostream_from_buffer(bytes, sizeof(bytes));
        assert(pb_encode(&encoded, meshtastic_FromRadio_fields, &status));
        mesh_event_t ev{};
        assert(mesh_decode_fromradio(bytes, encoded.bytes_written, &ev));
        assert(ev.kind == MESH_EV_QUEUE_STATUS && ev.u.tx_status.id == radio.packet.id && ev.u.tx_status.error == 34);

        meshtastic_Routing routing = meshtastic_Routing_init_zero;
        routing.which_variant = meshtastic_Routing_error_reason_tag;
        routing.error_reason = meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED;
        status = meshtastic_FromRadio_init_zero;
        status.which_payload_variant = meshtastic_FromRadio_packet_tag;
        status.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        status.packet.decoded.portnum = meshtastic_PortNum_ROUTING_APP;
        status.packet.decoded.request_id = radio.packet.id;
        auto payload = pb_ostream_from_buffer(status.packet.decoded.payload.bytes,
                                              sizeof(status.packet.decoded.payload.bytes));
        assert(pb_encode(&payload, meshtastic_Routing_fields, &routing));
        status.packet.decoded.payload.size = payload.bytes_written;
        encoded = pb_ostream_from_buffer(bytes, sizeof(bytes));
        assert(pb_encode(&encoded, meshtastic_FromRadio_fields, &status));
        assert(mesh_decode_fromradio(bytes, encoded.bytes_written, &ev));
        assert(ev.kind == MESH_EV_ROUTING_ERROR && ev.u.tx_status.id == radio.packet.id &&
               ev.u.tx_status.error == meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED);
    }
    // Decode an independently generated QR image (Python qrcode encoder).
    if (argc > 2) {
        FILE* image = fopen(argv[2], "rb"); assert(image);
        int w, h, max; assert(fscanf(image, "P5\n%d %d\n%d\n", &w, &h, &max) == 3 && max == 255);
        quirc* q = quirc_new(); assert(q && quirc_resize(q, w, h) == 0);
        auto pixels = quirc_begin(q, nullptr, nullptr); assert(fread(pixels, 1, w*h, image) == (size_t)(w*h)); fclose(image);
        quirc_end(q); assert(quirc_count(q) == 1);
        quirc_code code{}; quirc_data data{}; quirc_extract(q, 0, &code); assert(quirc_decode(&code, &data) == QUIRC_SUCCESS);
        data.payload[data.payload_len] = 0;
        assert(channel_url_decode(reinterpret_cast<char*>(data.payload), &decoded, &add));
        assert(decoded.settings_count == 8); quirc_destroy(q);
    }
    printf("PASS: URL fixtures, 1..8 channels, PSK/UTF-8 validation, malformed input, channel routing%s.\n", argc > 2 ? ", QR decode" : "");
    printf("sizeof FromRadio=%zu AdminMessage=%zu\n", sizeof(meshtastic_FromRadio), sizeof(meshtastic_AdminMessage));
}
