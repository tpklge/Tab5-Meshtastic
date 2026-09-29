#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>

// Transport capability flags
enum transport_caps_t : uint32_t {
    TRANSPORT_CAP_BLE  = (1u << 0),
    TRANSPORT_CAP_UART = (1u << 1),
};

// Connection state reported by the transport layer
enum transport_conn_t {
    TRANSPORT_CONN_IDLE,
    TRANSPORT_CONN_CONNECTING,
    TRANSPORT_CONN_SYNCING,
    TRANSPORT_CONN_READY,
    TRANSPORT_CONN_ERROR,
    TRANSPORT_CONN_DISCONNECTING,
};

// Error severity
enum transport_err_severity_t {
    TRANSPORT_ERR_RECOVERABLE,
    TRANSPORT_ERR_FATAL,
};

struct transport_stats_t {
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_errors;
    uint32_t resyncs;
    uint32_t proto_errors;
    char     last_error[64];
};

// Callbacks set by MeshSession; transport never calls LVGL directly.
struct transport_callbacks_t {
    // Raw FromRadio protobuf received (no framing bytes).
    std::function<void(const uint8_t* data, size_t len)> on_fromradio;
    // Transport connection state changed.
    std::function<void(transport_conn_t state)> on_conn_state;
    // Error notification.
    std::function<void(transport_err_severity_t sev, const char* msg)> on_error;
};

// Abstract transport interface. Only one transport is active at a time.
// All methods are called from non-LVGL tasks; implementations must be
// thread-safe with respect to each other and the callback invocations.
class IMeshTransport {
public:
    virtual ~IMeshTransport() = default;

    // Configure callbacks before calling start().
    virtual void set_callbacks(const transport_callbacks_t& cbs) = 0;

    // Start the transport (allocate resources, begin connection).
    virtual esp_err_t start() = 0;

    // Stop and release all resources. Safe to call multiple times.
    virtual void stop() = 0;

    // Send a raw ToRadio protobuf buffer (transport adds framing if needed).
    // Returns ESP_OK if enqueued/sent. Never blocks the caller.
    virtual esp_err_t send_toproto(const uint8_t* data, size_t len) = 0;

    // Poll / tick (called periodically from the session's poll timer).
    // Transports that are purely interrupt-driven may leave this a no-op.
    virtual void poll() = 0;

    // Return current connection state.
    virtual transport_conn_t conn_state() const = 0;

    // Return transport capabilities.
    virtual transport_caps_t capabilities() const = 0;

    // Fill diagnostics struct.
    virtual void get_stats(transport_stats_t& out) const = 0;

    // Human-readable name for UI display (e.g. "BLE", "RAK3172H UART").
    virtual const char* name() const = 0;
};
