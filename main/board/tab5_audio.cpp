/*
 * ES8388 audio for M5Stack Tab5.
 *
 * Uses the SYS I2C bus owned by the BSP via m5tab5_get_sys_i2c_bus() and the
 * espressif/i2c_bus component — same API the BSP drivers use, no mixing of
 * old/new I2C APIs on the same port.
 *
 * SPK_EN is PI4IOE5V6408 addr=0x43, OUT_STATE reg 0x05, bit1.
 */
#include "tab5_audio.h"
#include "m5tab5_pinmap.h"
#include "m5tab5_driver_common.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <atomic>
#include <math.h>
#include <string.h>

static const char* TAG = "tab5_audio";

#define ES8388_I2C_ADDR   0x10
#define PI4IO_ADDR_LOW    0x43
#define PI4IO_REG_OUT     0x05
#define SPK_EN_MASK      (1U << 1)

#define BEEP_SAMPLE_RATE 16000
#define BEEP_FREQ_HZ     1000
#define BEEP_AMPLITUDE   8000
#define BEEP_SHORT_MS    100
#define BEEP_GAP_MS      80

static bool             s_available = false;
static std::atomic<uint8_t> s_volume{70};
static i2s_chan_handle_t s_tx_chan   = nullptr;
static QueueHandle_t    s_queue     = nullptr;
static i2c_bus_device_handle_t s_codec = nullptr;
static i2c_bus_device_handle_t s_extio = nullptr;

static esp_err_t codec_write(uint8_t reg, uint8_t val)
{
    return i2c_bus_write_byte(s_codec, reg, val);
}

static esp_err_t spk_enable(bool on)
{
    if (!s_extio) return ESP_ERR_INVALID_STATE;
    uint8_t state;
    esp_err_t err = i2c_bus_read_byte(s_extio, PI4IO_REG_OUT, &state);
    if (err != ESP_OK) return err;
    // Preserve display, touch, antenna and external-power outputs.
    state = on ? (state | SPK_EN_MASK) : (state & ~SPK_EN_MASK);
    return i2c_bus_write_byte(s_extio, PI4IO_REG_OUT, state);
}

static esp_err_t codec_init(void)
{
    // ES8388 DAC-only path, matching esp_codec_dev's ES8388 register map.
    // Reference: espressif/esp-adf components/esp_codec_dev/device/es8388/.
    // P4 supplies Philips I2S, 16-bit stereo, MCLK = 256 * sample rate.
    static const uint8_t seq[][2] = {
        {0x00, 0x80}, // Reset, including state retained across a P4 warm reboot
        {0x00, 0x12}, // Reference enabled, normal playback mode
        {0x19, 0x04}, // Mute DAC during configuration
        {0x01, 0x50}, // Analog power/reference management
        {0x02, 0x00}, // Power up digital blocks (old 0xF3 kept them down)
        {0x03, 0xFF}, // ADC unused
        {0x04, 0xC0}, // DAC/outputs off until routing is configured
        {0x35, 0xA0}, // Internal DLL configuration for low sample rates
        {0x37, 0xD0},
        {0x39, 0xD0},
        {0x08, 0x00}, // Codec is I2S slave
        {0x17, 0x18}, // DAC: 16-bit Philips I2S
        {0x18, 0x02}, // Single speed, MCLK/LRCK = 256
        {0x1A, 0x00}, // Left digital attenuation: 0 dB
        {0x1B, 0x00}, // Right digital attenuation: 0 dB
        {0x26, 0x00}, // No analog bypass
        {0x27, 0x90}, // Left DAC -> left mixer (NOT a volume register)
        {0x2A, 0x90}, // Right DAC -> right mixer
        {0x2B, 0x80}, // DAC clock enabled
        {0x2D, 0x00}, // Output resistance configuration
        {0x2E, 0x1E}, // LOUT1: 0 dB
        {0x2F, 0x1E}, // ROUT1: 0 dB
        {0x30, 0x1E}, // LOUT2: 0 dB
        {0x31, 0x1E}, // ROUT2: 0 dB
        {0x02, 0xF0}, // Restart codec state machine with configured clocks
        {0x02, 0x00},
        {0x04, 0x3C}, // Enable DAC and all output pairs
        {0x19, 0x00}, // Unmute DAC
    };
    for (size_t i = 0; i < sizeof(seq)/sizeof(seq[0]); i++) {
        esp_err_t e = codec_write(seq[i][0], seq[i][1]);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "codec reg 0x%02X write failed: %s", seq[i][0], esp_err_to_name(e));
            return e;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    // Read back the critical route/format/power registers: an I2C ACK alone
    // does not establish that the requested playback configuration is active.
    static const uint8_t expected[][2] = {
        {0x02, 0x00}, {0x04, 0x3C}, {0x17, 0x18}, {0x18, 0x02},
        {0x19, 0x00}, {0x27, 0x90}, {0x2A, 0x90}, {0x2B, 0x80},
    };
    for (const auto& reg : expected) {
        uint8_t value = 0;
        esp_err_t err = i2c_bus_read_byte(s_codec, reg[0], &value);
        if (err != ESP_OK) return err;
        if (value != reg[1]) {
            ESP_LOGE(TAG, "codec reg 0x%02X: read 0x%02X, expected 0x%02X",
                     reg[0], value, reg[1]);
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

static bool write_samples(const int16_t* data, size_t bytes)
{
    size_t written = 0;
    // This API takes milliseconds, not FreeRTOS ticks.
    esp_err_t err = i2s_channel_write(s_tx_chan, data, bytes, &written, 200);
    if (err != ESP_OK || written != bytes) {
        ESP_LOGE(TAG, "I2S write: %s, %u/%u bytes", esp_err_to_name(err),
                 (unsigned)written, (unsigned)bytes);
        return false;
    }
    return true;
}

static bool play_tone_ms(uint32_t ms)
{
    static int16_t buf[640];
    uint32_t total = (BEEP_SAMPLE_RATE * ms) / 1000;
    float phase = 0.0f;
    const float step = 2.0f * (float)M_PI * BEEP_FREQ_HZ / BEEP_SAMPLE_RATE;
    while (total > 0) {
        uint32_t chunk = total < 320 ? total : 320;
        for (uint32_t i = 0; i < chunk; i++) {
            int16_t s = (int16_t)(sinf(phase) * BEEP_AMPLITUDE * s_volume.load() / 100);
            buf[i*2] = buf[i*2+1] = s;
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        if (!write_samples(buf, chunk * 4)) return false;
        total -= chunk;
    }
    return true;
}

static bool silence_ms(uint32_t ms)
{
    static int16_t buf[640];
    memset(buf, 0, sizeof(buf));
    uint32_t total = (BEEP_SAMPLE_RATE * ms) / 1000;
    while (total > 0) {
        uint32_t chunk = total < 320 ? total : 320;
        if (!write_samples(buf, chunk * 4)) return false;
        total -= chunk;
    }
    return true;
}

static void beep_task(void*)
{
    uint8_t pat;
    while (true) {
        if (xQueueReceive(s_queue, &pat, portMAX_DELAY) != pdTRUE) continue;
        if (pat == (uint8_t)AUDIO_PAT_SILENT) continue;
        esp_err_t err = spk_enable(true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPK_EN failed: %s", esp_err_to_name(err));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // Amplifier startup
        bool ok = true;
        for (uint8_t i = 0; i < pat && ok; ++i) {
            if (i) ok = silence_ms(BEEP_GAP_MS);
            if (ok) ok = play_tone_ms(BEEP_SHORT_MS);
        }
        // DMA holds at most 4 * 160 frames = 40 ms. Queue more silence
        // than that so the final tone reaches the codec before disabling PA.
        if (ok) ok = silence_ms(60);
        err = spk_enable(false);
        if (err != ESP_OK) ESP_LOGE(TAG, "SPK_EN off failed: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "beep pattern=%u volume=%u: %s", pat,
                 (unsigned)s_volume.load(), ok && err == ESP_OK ? "I2S sent" : "failed");
    }
}

esp_err_t tab5_audio_init(void)
{
    if (s_available) return ESP_OK;
    bool tx_enabled = false;
    auto fail = [&tx_enabled](esp_err_t err) {
        ESP_LOGE(TAG, "audio init failed: %s", esp_err_to_name(err));
        if (s_extio) spk_enable(false);
        if (s_tx_chan) {
            if (tx_enabled) i2s_channel_disable(s_tx_chan);
            i2s_del_channel(s_tx_chan);
            s_tx_chan = nullptr;
        }
        if (s_queue) { vQueueDelete(s_queue); s_queue = nullptr; }
        if (s_codec) i2c_bus_device_delete(&s_codec);
        if (s_extio) i2c_bus_device_delete(&s_extio);
        return err;
    };
    i2c_bus_handle_t bus = m5::tab5::m5tab5_get_sys_i2c_bus();
    if (!bus) {
        ESP_LOGW(TAG, "SYS I2C bus not ready — audio unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    s_codec = i2c_bus_device_create(bus, ES8388_I2C_ADDR, 100000);
    if (!s_codec) return fail(ESP_ERR_NO_MEM);
    s_extio = i2c_bus_device_create(bus, PI4IO_ADDR_LOW, 400000);
    if (!s_extio) return fail(ESP_ERR_NO_MEM);

    esp_err_t err = spk_enable(false);
    if (err == ESP_OK) err = codec_init();
    if (err != ESP_OK) return fail(err);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 160;
    chan_cfg.auto_clear = true;
    err = i2s_new_channel(&chan_cfg, &s_tx_chan, nullptr);
    if (err != ESP_OK) return fail(err);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(BEEP_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)m5::tab5::M5TAB5_PIN_I2S_MCLK,
            .bclk = (gpio_num_t)m5::tab5::M5TAB5_PIN_I2S_SCLK,
            .ws   = (gpio_num_t)m5::tab5::M5TAB5_PIN_I2S_LCLK,
            .dout = (gpio_num_t)m5::tab5::M5TAB5_PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {},
        },
    };
    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) return fail(err);
    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) return fail(err);
    tx_enabled = true;

    s_queue = xQueueCreate(4, 1);
    if (!s_queue || xTaskCreatePinnedToCore(beep_task, "tab5_beep", 3072,
                                          nullptr, 2, nullptr, 0) != pdPASS) {
        return fail(ESP_ERR_NO_MEM);
    }

    s_available = true;
    ESP_LOGI(TAG, "audio ready (ES8388 via i2c_bus, SPK_EN via PI4IO)");
    return ESP_OK;
}

void tab5_audio_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    s_volume = vol;
    // Scale PCM only; never change the DAC mixer routing for volume.
}

void tab5_audio_beep(audio_pattern_t pat)
{
    if (!s_available || !s_queue || pat <= AUDIO_PAT_SILENT || pat > AUDIO_PAT_TRIPLE) return;
    uint8_t p = (uint8_t)pat;
    xQueueSend(s_queue, &p, 0);
}

bool tab5_audio_available(void) { return s_available; }
