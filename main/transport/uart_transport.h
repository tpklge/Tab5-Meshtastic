#pragma once
#include "mesh_transport.h"
#include "serial_framer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

// UART numbers and GPIO assignments for Grove port on M5Stack Tab5.
// Confirmed from Tab5 schematic: Grove pin3=GPIO53, pin4=GPIO54.
// TX/RX polarity: GPIO53=TX (→RAK PB7/U1RX), GPIO54=RX (←RAK PB6/U1TX).
// Verify with oscilloscope before first use.
static constexpr uart_port_t GROVE_UART_PORT = UART_NUM_2;
static constexpr int         GROVE_GPIO_TX   = 53;
static constexpr int         GROVE_GPIO_RX   = 54;
static constexpr int         GROVE_BAUD      = 115200;
static constexpr size_t      UART_RX_BUF     = 2048;
static constexpr size_t      UART_TX_BUF     = 1024;

// Set to 1 if your RAK3172H carrier board declares 5V input.
// When 0, the EXT5V Grove rail is NOT enabled by this transport.
#ifndef GROVE_ENABLE_5V
#define GROVE_ENABLE_5V 0
#endif

class UartTransport : public IMeshTransport {
public:
    UartTransport();
    ~UartTransport() override;

    void           set_callbacks(const transport_callbacks_t& cbs) override;
    esp_err_t      start() override;
    void           stop() override;
    esp_err_t      send_toproto(const uint8_t* data, size_t len) override;
    void           poll() override {}  // interrupt-driven
    transport_conn_t conn_state() const override;
    transport_caps_t capabilities() const override { return TRANSPORT_CAP_UART; }
    void           get_stats(transport_stats_t& out) const override;
    const char*    name() const override { return "RAK3172H UART"; }

private:
    static void uart_task(void* arg);
    void        run_task();
    void        send_frame(const uint8_t* proto, size_t len);
    void        on_frame(const uint8_t* payload, size_t plen);

    transport_callbacks_t m_cbs;
    transport_conn_t      m_conn{TRANSPORT_CONN_IDLE};
    SerialFramer          m_framer;
    transport_stats_t     m_stats{};
    TaskHandle_t          m_task{nullptr};
    QueueHandle_t         m_tx_queue{nullptr};
    volatile bool         m_running{false};
    mutable portMUX_TYPE  m_mux = portMUX_INITIALIZER_UNLOCKED;

    struct TxItem {
        uint8_t  data[SERIAL_FRAME_MAX_PAYLOAD + SERIAL_FRAME_HEADER_LEN];
        uint16_t len;
    };
};
