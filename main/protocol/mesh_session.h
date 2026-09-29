#pragma once
#include "../transport/mesh_transport.h"
#include "../app/app_state.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// MeshSession owns the Meshtastic protocol handshake above a transport.
// It handles: want_config_id send, config_complete_id, and dispatching
// decoded FromRadio events into AppState.
//
// For the BLE transport the existing ble_transport.cpp handles all of this
// internally; MeshSession is used by UartTransport and future transports.
//
// Thread safety: all app_state_* calls are mutex-protected by app_state.
class MeshSession {
public:
    MeshSession();
    ~MeshSession();

    // Attach transport and register our callbacks into it.
    void attach_transport(IMeshTransport* t);

    // Start the session task.
    esp_err_t start();

    // Stop session task (does not stop the transport).
    void stop();

    // Send a broadcast text message.
    esp_err_t send_text(const char* text);

private:
    static void session_task(void* arg);
    void        run_task();
    void        on_fromradio(const uint8_t* data, size_t len);
    void        on_conn_state(transport_conn_t state);
    void        on_error(transport_err_severity_t sev, const char* msg);
    void        do_send_want_config();
    void        handle_config_complete(uint32_t id);
    uint32_t    next_want_config_id();

    IMeshTransport*   m_transport{nullptr};
    transport_conn_t  m_conn{TRANSPORT_CONN_IDLE};
    uint32_t          m_want_config_id{0};
    uint32_t          m_pending_config_id{0};
    bool              m_config_complete{false};
    volatile bool     m_running{false};
    TaskHandle_t      m_task{nullptr};
    SemaphoreHandle_t m_mutex{nullptr};
};
