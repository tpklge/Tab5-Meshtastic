#include "serial_framer.h"
#include <string.h>

SerialFramer::SerialFramer(serial_frame_cb_t cb, size_t max_payload)
    : m_cb(std::move(cb)), m_max_payload(max_payload)
{
}

void SerialFramer::reset()
{
    m_state = State::SYNC0;
    m_expected = 0;
    m_received = 0;
    m_frame_start_us = 0;
}

void SerialFramer::push(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        advance_byte(data[i]);
    }
}

void SerialFramer::advance_byte(uint8_t b)
{
    switch (m_state) {
    case State::SYNC0:
        if (b == SERIAL_FRAME_MAGIC0) {
            m_state = State::SYNC1;
        }
        break;

    case State::SYNC1:
        if (b == SERIAL_FRAME_MAGIC1) {
            m_state = State::LEN_HI;
        } else if (b == SERIAL_FRAME_MAGIC0) {
            // Stay in SYNC1 — consecutive 0x94 bytes
        } else {
            m_state = State::SYNC0;
            ++m_stats.resyncs;
        }
        break;

    case State::LEN_HI:
        m_expected = (uint16_t)(b << 8);
        m_state = State::LEN_LO;
        break;

    case State::LEN_LO:
        m_expected |= b;
        if (m_expected == 0) {
            // Zero-length frame: emit immediately
            m_cb(m_buf, 0);
            ++m_stats.frames_ok;
            m_state = State::SYNC0;
        } else if (m_expected > m_max_payload) {
            ++m_stats.oversized;
            ++m_stats.resyncs;
            m_state = State::SYNC0;
        } else {
            m_received = 0;
            m_frame_start_us = 0; // will be set on first payload byte
            m_state = State::PAYLOAD;
        }
        break;

    case State::PAYLOAD:
        if (m_received == 0) {
            // Record when we started collecting payload (caller sets now via tick)
            // We approximate: leave to tick() to manage timeout
        }
        m_buf[m_received++] = b;
        if (m_received == m_expected) {
            m_cb(m_buf, m_received);
            ++m_stats.frames_ok;
            reset();
        }
        break;
    }
}

void SerialFramer::tick(int64_t now_us, int64_t timeout_us)
{
    if (m_state == State::SYNC0 || m_state == State::SYNC1) {
        return;
    }
    // Set frame_start when we first enter a non-sync state
    if (m_frame_start_us == 0) {
        m_frame_start_us = now_us;
        return;
    }
    if ((now_us - m_frame_start_us) > timeout_us) {
        ++m_stats.timeouts;
        ++m_stats.resyncs;
        reset();
    }
}
