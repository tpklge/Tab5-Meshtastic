#include "channel_service.h"
#include "settings_store.h"
#include "mesh_session.h"
#include "mesh_proto.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "mesh_session";

MeshSession::MeshSession()
{
    m_mutex = xSemaphoreCreateMutex();
}

MeshSession::~MeshSession()
{
    stop();
    if (m_mutex) vSemaphoreDelete(m_mutex);
}

void MeshSession::attach_transport(IMeshTransport* t)
{
    m_transport = t;
    transport_callbacks_t cbs;
    cbs.on_fromradio  = [this](const uint8_t* d, size_t l){ on_fromradio(d, l); };
    cbs.on_conn_state = [this](transport_conn_t s){ on_conn_state(s); };
    cbs.on_error      = [this](transport_err_severity_t sv, const char* m){ on_error(sv, m); };
    t->set_callbacks(cbs);
}

esp_err_t MeshSession::start()
{
    if (m_running) return ESP_OK;
    m_running = true;
    m_config_complete = false;
    m_pending_config_id = 0;
    xTaskCreatePinnedToCore(session_task, "mesh_session", 4096, this, 5, &m_task, 0);
    return ESP_OK;
}

void MeshSession::stop()
{
    m_running = false;
    if (m_task) { vTaskDelay(pdMS_TO_TICKS(100)); m_task = nullptr; }
}

void MeshSession::session_task(void* arg)
{
    static_cast<MeshSession*>(arg)->run_task();
    vTaskDelete(nullptr);
}

void MeshSession::run_task()
{
    while (m_running) {
        if (m_conn == TRANSPORT_CONN_SYNCING && !m_config_complete) {
            if (m_pending_config_id == 0 || esp_timer_get_time() - m_config_sent_us > 5000000LL) {
                if (m_config_attempts++ < 12) do_send_want_config();
                else { m_conn = TRANSPORT_CONN_ERROR; app_state_set_conn(CONN_ERROR, "UART sync timeout"); }
            }
        } else m_config_attempts = 0;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

uint32_t MeshSession::next_want_config_id()
{
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        m_want_config_id++;
        if (m_want_config_id == 0) m_want_config_id = 1;
        uint32_t id = m_want_config_id;
        xSemaphoreGive(m_mutex);
        return id;
    }
    return ++m_want_config_id;
}

void MeshSession::do_send_want_config()
{
    if (!m_transport) return;
    uint32_t id = next_want_config_id();
    m_pending_config_id = id;
    m_config_sent_us = esp_timer_get_time();

    uint8_t buf[64];
    size_t  len = mesh_encode_want_config(id, buf, sizeof(buf));
    if (len > 0) {
        m_transport->send_toproto(buf, len);
        ESP_LOGI(TAG, "sent want_config_id=%lu", (unsigned long)id);
    }
}

void MeshSession::handle_config_complete(uint32_t id)
{
    if (id != m_pending_config_id) return;
    ESP_LOGI(TAG, "config complete");
    m_config_complete = true;
    m_pending_config_id = 0;
    m_conn = TRANSPORT_CONN_READY;
    app_state_set_conn(CONN_READY, "Ready");
}

void MeshSession::on_fromradio(const uint8_t* data, size_t len)
{
    if (len > UINT16_MAX) return;
    channel_service_on_frame(data, len);
    mesh_event_t ev;
    if (!mesh_decode_fromradio(data, (uint16_t)len, &ev)) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    // A large config/node stream can take longer than the timeout; only retry
    // after a lack of progress, not while valid configuration is arriving.
    if (m_conn == TRANSPORT_CONN_SYNCING) m_config_sent_us = now_us;

    switch (ev.kind) {
    case MESH_EV_MY_INFO:
        // my_num only; short_name unknown at this point
        app_state_set_myinfo(ev.u.my_num, "", "");
        break;

    case MESH_EV_NODE_INFO: {
        const mesh_node_t& n = ev.u.node;
        app_state_upsert_node(n.num, n.long_name, n.short_name,
                              n.snr, n.hops, n.hops_valid, n.has_user, now_us);
        if (n.has_position) {
            app_state_set_node_position(n.num, &n.position, now_us);
        }
        if (n.has_metrics) {
            app_state_set_node_metrics(n.num, &n.metrics, now_us);
        }
        break;
    }

    case MESH_EV_REBOOTED:
        on_conn_state(TRANSPORT_CONN_SYNCING);
        break;

    case MESH_EV_CONFIG_COMPLETE:
        handle_config_complete(ev.u.config_complete_id);
        break;

    case MESH_EV_CHANNEL:
        // Channel records — stored in app_state when support is added (Phase 4)
        ESP_LOGD(TAG, "channel record received (not yet stored)");
        break;

    case MESH_EV_TEXT:
        app_state_add_channel_message(ev.u.text.from, ev.u.text.text, /*is_self=*/false, now_us, ev.u.text.channel, true);
        break;

    case MESH_EV_POSITION:
        app_state_set_node_position(ev.u.position.from, &ev.u.position.pos, now_us);
        break;

    case MESH_EV_TELEMETRY:
        app_state_set_node_metrics(ev.u.telemetry.from, &ev.u.telemetry.metrics, now_us);
        break;

    default:
        break;
    }
}

void MeshSession::on_conn_state(transport_conn_t state)
{
    m_conn = state;
    switch (state) {
    case TRANSPORT_CONN_IDLE:
        app_state_set_conn(CONN_DISCONNECTED, "Idle");
        break;
    case TRANSPORT_CONN_CONNECTING:
        app_state_set_conn(CONN_CONNECTING, "Connecting...");
        break;
    case TRANSPORT_CONN_SYNCING:
        app_state_set_conn(CONN_SYNCING, "Syncing...");
        m_config_complete = false;
        m_pending_config_id = 0;
        app_state_clear_nodes();
        break;
    case TRANSPORT_CONN_READY:
        app_state_set_conn(CONN_READY, "Ready");
        break;
    case TRANSPORT_CONN_ERROR:
        app_state_set_conn(CONN_ERROR, "Transport error");
        break;
    default:
        break;
    }
}

void MeshSession::on_error(transport_err_severity_t sev, const char* msg)
{
    ESP_LOGE(TAG, "[%s] %s", sev == TRANSPORT_ERR_FATAL ? "FATAL" : "WARN", msg);
    if (sev == TRANSPORT_ERR_FATAL) {
        app_state_set_conn(CONN_ERROR, msg);
    }
}

esp_err_t MeshSession::send_text(const char* text)
{
    if (!m_transport || !m_config_complete) return ESP_ERR_INVALID_STATE;

    uint8_t buf[256];
    size_t len = mesh_encode_text_channel(text, settings_store_get()->sel_channel, buf, sizeof(buf));
    if (len == 0) return ESP_ERR_INVALID_SIZE;

    // Local echo
    app_snapshot_t snap;
    app_state_snapshot(&snap);
    app_state_add_channel_message(snap.my_num, text, /*is_self=*/true, esp_timer_get_time(), settings_store_get()->sel_channel, true);

    return m_transport->send_toproto(buf, len);
}
