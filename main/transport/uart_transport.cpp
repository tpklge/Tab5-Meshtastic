#include "uart_transport.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char* TAG = "uart_transport";

UartTransport::UartTransport()
    : m_framer([this](const uint8_t* d, size_t l){ on_frame(d, l); })
{
}

UartTransport::~UartTransport()
{
    stop();
}

void UartTransport::set_callbacks(const transport_callbacks_t& cbs)
{
    m_cbs = cbs;
}

esp_err_t UartTransport::start()
{
    if (m_running) return ESP_OK;

#if GROVE_ENABLE_5V
    // TODO: enable EXT5V via PI4IOE5V6408 expander if carrier supports 5V input.
    // Reference m5_tab5_component expander API.
    ESP_LOGW(TAG, "EXT5V enable not yet wired — set GROVE_ENABLE_5V=0 if unsure");
#endif

    uart_config_t cfg = {
        .baud_rate           = GROVE_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(GROVE_UART_PORT, &cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "uart_param_config: %s", esp_err_to_name(err)); return err; }

    err = uart_set_pin(GROVE_UART_PORT, GROVE_GPIO_TX, GROVE_GPIO_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(err)); return err; }

    err = uart_driver_install(GROVE_UART_PORT, UART_RX_BUF, UART_TX_BUF, 0, nullptr, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err)); return err; }

    m_tx_queue = xQueueCreate(8, sizeof(TxItem));
    if (!m_tx_queue) {
        uart_driver_delete(GROVE_UART_PORT);
        return ESP_ERR_NO_MEM;
    }

    m_running = true;
    m_framer.reset();
    memset(&m_stats, 0, sizeof(m_stats));

    {
        portENTER_CRITICAL(&m_mux);
        m_conn = TRANSPORT_CONN_CONNECTING;
        portEXIT_CRITICAL(&m_mux);
    }
    if (m_cbs.on_conn_state) m_cbs.on_conn_state(TRANSPORT_CONN_CONNECTING);

    xTaskCreatePinnedToCore(uart_task, "uart_transport", 4096, this, 5, &m_task, 0);
    ESP_LOGI(TAG, "started on UART%d TX=%d RX=%d %dbaud", GROVE_UART_PORT,
             GROVE_GPIO_TX, GROVE_GPIO_RX, GROVE_BAUD);
    return ESP_OK;
}

void UartTransport::stop()
{
    if (!m_running) return;
    m_running = false;
    if (m_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        m_task = nullptr;
    }
    uart_driver_delete(GROVE_UART_PORT);
    if (m_tx_queue) { vQueueDelete(m_tx_queue); m_tx_queue = nullptr; }
    {
        portENTER_CRITICAL(&m_mux);
        m_conn = TRANSPORT_CONN_IDLE;
        portEXIT_CRITICAL(&m_mux);
    }
    if (m_cbs.on_conn_state) m_cbs.on_conn_state(TRANSPORT_CONN_IDLE);
    ESP_LOGI(TAG, "stopped");
}

esp_err_t UartTransport::send_toproto(const uint8_t* data, size_t len)
{
    if (!m_running || !m_tx_queue) return ESP_ERR_INVALID_STATE;
    if (len > SERIAL_FRAME_MAX_PAYLOAD) return ESP_ERR_INVALID_SIZE;

    TxItem item;
    item.data[0] = SERIAL_FRAME_MAGIC0;
    item.data[1] = SERIAL_FRAME_MAGIC1;
    item.data[2] = (uint8_t)(len >> 8);
    item.data[3] = (uint8_t)(len & 0xFF);
    memcpy(&item.data[4], data, len);
    item.len = (uint16_t)(4 + len);

    if (xQueueSend(m_tx_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "TX queue full");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

transport_conn_t UartTransport::conn_state() const
{
    transport_conn_t s;
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&m_mux));
    s = m_conn;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&m_mux));
    return s;
}

void UartTransport::get_stats(transport_stats_t& out) const
{
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&m_mux));
    out = m_stats;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&m_mux));
    const auto& fs = m_framer.stats();
    out.rx_frames   = fs.frames_ok;
    out.rx_errors   = fs.oversized + fs.timeouts;
    out.resyncs     = fs.resyncs;
}

void UartTransport::uart_task(void* arg)
{
    static_cast<UartTransport*>(arg)->run_task();
    vTaskDelete(nullptr);
}

void UartTransport::run_task()
{
    static uint8_t rx_buf[512];

    // Drain any boot garbage from the RAK
    vTaskDelay(pdMS_TO_TICKS(200));
    uart_flush_input(GROVE_UART_PORT);

    ESP_LOGI(TAG, "task started — sending want_config");
    // Session will send want_config_id via send_toproto; here we just pump I/O.
    // Signal that hardware is up and session can start
    if (m_cbs.on_conn_state) m_cbs.on_conn_state(TRANSPORT_CONN_SYNCING);
    {
        portENTER_CRITICAL(&m_mux);
        m_conn = TRANSPORT_CONN_SYNCING;
        portEXIT_CRITICAL(&m_mux);
    }

    while (m_running) {
        // Drain TX queue
        TxItem item;
        while (xQueueReceive(m_tx_queue, &item, 0) == pdTRUE) {
            int written = uart_write_bytes(GROVE_UART_PORT, (const char*)item.data, item.len);
            portENTER_CRITICAL(&m_mux);
            m_stats.tx_bytes += (written > 0) ? written : 0;
            m_stats.tx_frames++;
            portEXIT_CRITICAL(&m_mux);
        }

        // Drain RX FIFO
        int available = 0;
        uart_get_buffered_data_len(GROVE_UART_PORT, (size_t*)&available);
        if (available > 0) {
            int n = uart_read_bytes(GROVE_UART_PORT, rx_buf,
                                    (size_t)available < sizeof(rx_buf) ? available : sizeof(rx_buf),
                                    pdMS_TO_TICKS(5));
            if (n > 0) {
                portENTER_CRITICAL(&m_mux);
                m_stats.rx_bytes += n;
                portEXIT_CRITICAL(&m_mux);
                m_framer.push(rx_buf, n);
            }
        }

        // Framer timeout check
        m_framer.tick(esp_timer_get_time());

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void UartTransport::on_frame(const uint8_t* payload, size_t plen)
{
    if (m_cbs.on_fromradio) {
        m_cbs.on_fromradio(payload, plen);
    }
}
