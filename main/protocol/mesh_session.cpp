#include "channel_service.h"
#include "settings_store.h"
#include "mesh_session.h"
#include "mesh_proto.h"
#include "esp_log.h"
#include "esp_random.h"
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
    if (!m_outbox) m_outbox = xQueueCreate(8, sizeof(OutgoingText));
    if (!m_feedback) m_feedback = xQueueCreate(8, sizeof(TxFeedback));
    if (!m_outbox || !m_feedback) return ESP_ERR_NO_MEM;
    m_running = true;
    m_config_complete = false;
    m_pending_config_id = 0;
    m_started_us = esp_timer_get_time();
    if (xTaskCreatePinnedToCore(session_task, "mesh_session", 4096, this, 5, &m_task, 0) != pdPASS) {
        m_running = false;
        return ESP_ERR_NO_MEM;
    }
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
    constexpr int64_t kRadioWarmupUs = 30000000LL;
    constexpr int64_t kTextGapUs = 2300000LL; // RAK PhoneAPI rate-limits text at 2 s.
    constexpr int64_t kAckGraceUs = 1500000LL; // rate-limit Routing error follows QueueStatus.
    constexpr int64_t kReplyTimeoutUs = 6000000LL;
    while (m_running) {
        int64_t now = esp_timer_get_time();
        if (m_conn == TRANSPORT_CONN_SYNCING && !m_config_complete) {
            if (m_pending_config_id == 0 || now - m_config_sent_us > 5000000LL) {
                if (m_config_attempts++ < 12) do_send_want_config();
                else { m_conn = TRANSPORT_CONN_ERROR; app_state_set_conn(CONN_ERROR, "UART sync timeout"); }
            }
        } else m_config_attempts = 0;

        TxFeedback feedback{};
        while (m_feedback && xQueueReceive(m_feedback, &feedback, 0) == pdTRUE) {
            if (!m_has_pending_text || feedback.id != m_pending_packet_id) continue;
            if (feedback.routing_error || feedback.error != 0) {
                ESP_LOGW(TAG, "text id=%lu rejected by RAK: %s=%d", (unsigned long)feedback.id,
                         feedback.routing_error ? "routing" : "queue", feedback.error);
                m_pending_rejected = true;
                m_retry_same_id = false;
                m_pending_unconfirmed = false;
                m_pending_ack_us = 0;
            } else if (!m_pending_rejected) {
                m_pending_ack_us = now;
            }
        }

        if (m_has_pending_text && m_pending_ack_us && now - m_pending_ack_us >= kAckGraceUs) {
            if (m_pending_text.to_node) {
                /* DM echo already added in send_dm(); just update status */
            } else {
                app_state_add_channel_message(m_pending_text.from, m_pending_text.text, true, now,
                                              m_pending_text.channel, true);
            }
            app_state_set_send_status("Enviado ao radio");
            ESP_LOGI(TAG, "text id=%lu accepted by RAK", (unsigned long)m_pending_packet_id);
            m_has_pending_text = false;
        }
        if (m_has_pending_text && !m_pending_rejected && m_pending_tx_us &&
            now - m_pending_tx_us >= kReplyTimeoutUs) {
            ESP_LOGW(TAG, "text id=%lu got no QueueStatus", (unsigned long)m_pending_packet_id);
            m_pending_rejected = true;
            m_retry_same_id = true; // Same ID avoids duplicating a packet accepted without a reply.
            m_pending_unconfirmed = true;
        }
        if (!m_has_pending_text && m_outbox && xQueueReceive(m_outbox, &m_pending_text, 0) == pdTRUE) {
            m_has_pending_text = true;
            m_pending_rejected = false;
            m_retry_same_id = false;
            m_pending_unconfirmed = false;
            m_pending_attempts = 0;
            m_pending_packet_id = 0;
            m_pending_tx_us = m_pending_ack_us = 0;
        }
        if (m_has_pending_text && m_pending_rejected && m_pending_attempts >= 3) {
            app_state_report_send_failure(m_pending_text.text, m_pending_text.channel,
                m_pending_unconfirmed ? "Sem confirmacao; confira envio" : "Falha no radio; tente novamente");
            ESP_LOGE(TAG, "text failed after 3 attempts");
            m_has_pending_text = false;
        }
        if (m_has_pending_text && m_config_complete && m_conn == TRANSPORT_CONN_READY &&
            now - m_started_us >= kRadioWarmupUs &&
            (!m_last_text_tx_us || now - m_last_text_tx_us >= kTextGapUs) &&
            (!m_pending_tx_us || m_pending_rejected)) {
            uint32_t id = m_retry_same_id ? m_pending_packet_id : 0;
            while (!id) id = esp_random();
            m_retry_same_id = false;
            uint8_t buf[256];
            size_t len = m_pending_text.to_node
                ? mesh_encode_text_to(m_pending_text.text, m_pending_text.to_node,
                                      m_pending_text.channel, id, buf, sizeof(buf))
                : mesh_encode_text_channel_id(m_pending_text.text, m_pending_text.channel,
                                              id, buf, sizeof(buf));
            esp_err_t err = len ? m_transport->send_toproto(buf, len) : ESP_ERR_INVALID_SIZE;
            ++m_pending_attempts;
            m_last_text_tx_us = now;
            m_pending_tx_us = now;
            m_pending_ack_us = 0;
            m_pending_rejected = err != ESP_OK;
            m_pending_packet_id = id;
            if (err != ESP_OK) ESP_LOGW(TAG, "text UART enqueue failed: %s", esp_err_to_name(err));
            else ESP_LOGI(TAG, "text id=%lu queued to RAK (attempt %u)", (unsigned long)id,
                           (unsigned)m_pending_attempts);
            app_state_set_send_status(err == ESP_OK ? "Aguardando confirmacao do radio" : "Reenviando ao radio...");
        }
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
        if (ev.u.text.to != 0xffffffff && ev.u.text.to != 0) {
            app_state_add_dm(ev.u.text.from, ev.u.text.to, ev.u.text.text, false, now_us);
        } else {
            app_state_add_channel_message(ev.u.text.from, ev.u.text.text, false, now_us, ev.u.text.channel, true);
        }
        break;

    case MESH_EV_QUEUE_STATUS:
    case MESH_EV_ROUTING_ERROR:
        if (m_feedback && ev.u.tx_status.id) {
            TxFeedback feedback{ev.u.tx_status.id, ev.u.tx_status.error, ev.kind == MESH_EV_ROUTING_ERROR};
            xQueueSend(m_feedback, &feedback, 0);
        }
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
        m_started_us = esp_timer_get_time();
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
    if (!m_transport || !m_config_complete || !m_outbox || !text || !text[0]) return ESP_ERR_INVALID_STATE;
    app_snapshot_t snap;
    app_state_snapshot(&snap);
    OutgoingText item{};
    strlcpy(item.text, text, sizeof(item.text));
    item.channel  = settings_store_get()->sel_channel;
    item.from     = snap.my_num;
    item.to_node  = 0;
    if (xQueueSend(m_outbox, &item, 0) != pdTRUE) {
        app_state_set_send_status("Fila cheia; aguarde e tente novamente");
        return ESP_ERR_TIMEOUT;
    }
    app_state_set_send_status("Na fila; aguardando radio");
    return ESP_OK;
}

esp_err_t MeshSession::send_dm(const char* text, uint32_t to_node)
{
    if (!to_node || !m_transport || !m_config_complete || !m_outbox || !text || !text[0])
        return ESP_ERR_INVALID_STATE;
    app_snapshot_t snap;
    app_state_snapshot(&snap);
    OutgoingText item{};
    strlcpy(item.text, text, sizeof(item.text));
    item.channel  = settings_store_get()->sel_channel;
    item.from     = snap.my_num;
    item.to_node  = to_node;
    if (xQueueSend(m_outbox, &item, 0) != pdTRUE) {
        app_state_set_send_status("Fila cheia; aguarde e tente novamente");
        return ESP_ERR_TIMEOUT;
    }
    /* Local DM echo so it appears in the DM thread immediately */
    app_state_add_dm(snap.my_num, to_node, text, true, esp_timer_get_time());
    app_state_set_send_status("DM na fila; aguardando radio");
    return ESP_OK;
}
