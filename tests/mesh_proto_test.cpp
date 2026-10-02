/* Integration tests for mesh_proto encode/decode round-trips.
 * Covers broadcast text, DM (to field), and position. Compiled off-device. */
#include "mesh_proto.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include <cassert>
#include <cstring>
#include <cstdio>

static bool decode_toproto(const uint8_t* buf, size_t len, meshtastic_ToRadio* out)
{
    *out = meshtastic_ToRadio_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, len);
    return pb_decode(&is, meshtastic_ToRadio_fields, out);
}

int main(void)
{
    uint8_t buf[256];

    /* --- 1. broadcast text, channel 3 --- */
    {
        size_t n = mesh_encode_text_channel("hello mesh", 3, buf, sizeof(buf));
        assert(n > 0);
        meshtastic_ToRadio tr{};
        assert(decode_toproto(buf, n, &tr));
        assert(tr.which_payload_variant == meshtastic_ToRadio_packet_tag);
        auto& p = tr.packet;
        assert(p.to == 0xffffffff);
        assert(p.channel == 3);
        assert(p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP);
        assert(p.decoded.payload.size == 10);
        assert(memcmp(p.decoded.payload.bytes, "hello mesh", 10) == 0);
        printf("PASS: broadcast text ch3\n");
    }

    /* --- 2. DM (mesh_encode_text_to with to_node != 0) --- */
    {
        uint32_t peer = 0xdeadbeef;
        size_t n = mesh_encode_text_to("direct msg", peer, 0, 0xabc, buf, sizeof(buf));
        assert(n > 0);
        meshtastic_ToRadio tr{};
        assert(decode_toproto(buf, n, &tr));
        auto& p = tr.packet;
        assert(p.to == peer);
        assert(p.id == 0xabc);
        assert(p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP);
        assert(p.decoded.payload.size == 10);
        printf("PASS: DM to=0x%08x\n", (unsigned)peer);
    }

    /* --- 3. want_config id increments --- */
    {
        size_t n1 = mesh_encode_want_config(1, buf, sizeof(buf));
        assert(n1 > 0);
        meshtastic_ToRadio tr{};
        assert(decode_toproto(buf, n1, &tr));
        assert(tr.which_payload_variant == meshtastic_ToRadio_want_config_id_tag);
        assert(tr.want_config_id == 1);
        printf("PASS: want_config id=1\n");
    }

    /* --- 4. decode text preserves to field --- */
    {
        uint32_t peer = 0x11223344;
        size_t n = mesh_encode_text_to("dm test", peer, 2, 77, buf, sizeof(buf));
        assert(n > 0);
        meshtastic_ToRadio tr{};
        assert(decode_toproto(buf, n, &tr));
        assert(tr.packet.to == peer);
        assert(tr.packet.channel == 2);
        printf("PASS: to field preserved ch2 id=77\n");
    }

    printf("All mesh_proto tests passed.\n");
    return 0;
}
