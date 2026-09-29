#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>

// Meshtastic serial framing: 0x94 0xC3 [uint16_be length] [protobuf bytes]
// Maximum protobuf payload size enforced at SERIAL_FRAME_MAX_PAYLOAD.

static constexpr size_t SERIAL_FRAME_MAGIC0    = 0x94;
static constexpr size_t SERIAL_FRAME_MAGIC1    = 0xC3;
static constexpr size_t SERIAL_FRAME_HEADER_LEN = 4;
static constexpr size_t SERIAL_FRAME_MAX_PAYLOAD = 512;

// Callback invoked with a complete, validated frame payload.
using serial_frame_cb_t = std::function<void(const uint8_t* payload, size_t len)>;

// Non-blocking state-machine frame parser.
// Feed bytes with push(). Thread-safe only if external locking is applied.
// Resynchronises automatically on bad magic/oversized frames.
class SerialFramer {
public:
    explicit SerialFramer(serial_frame_cb_t cb, size_t max_payload = SERIAL_FRAME_MAX_PAYLOAD);

    // Feed a chunk of received bytes.
    void push(const uint8_t* data, size_t len);

    // Call periodically to detect frame timeout (microseconds clock).
    // Resets framer to SYNC state if a frame has been partially received
    // for longer than timeout_us.
    void tick(int64_t now_us, int64_t timeout_us = 3'000'000);

    // Reset to initial sync state.
    void reset();

    struct Stats {
        uint32_t frames_ok;
        uint32_t resyncs;
        uint32_t oversized;
        uint32_t timeouts;
    };
    const Stats& stats() const { return m_stats; }

private:
    enum class State { SYNC0, SYNC1, LEN_HI, LEN_LO, PAYLOAD };

    serial_frame_cb_t m_cb;
    size_t            m_max_payload;
    State             m_state{State::SYNC0};
    uint16_t          m_expected{0};
    uint16_t          m_received{0};
    uint8_t           m_buf[SERIAL_FRAME_MAX_PAYLOAD];
    int64_t           m_frame_start_us{0};
    Stats             m_stats{};

    void advance_byte(uint8_t b);
};
