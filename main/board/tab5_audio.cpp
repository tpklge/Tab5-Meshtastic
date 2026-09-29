/*
 * ES8388 audio for M5Stack Tab5.
 *
 * Hardware:
 *   I2S: MCLK=30, BCLK=27, WS=29, DOUT=26 (from m5tab5_pinmap.h)
 *   ES8388 on system I2C bus (SDA=31, SCL=32, addr=0x10) — BSP owns this bus
 *   SPK_EN: PI4IOE5V6408 (addr=0x43, pin1) — no public BSP API exposed yet
 *
 * LIMITATION: The m5_tab5_component BSP does not currently expose:
 *   - A handle to the SYS I2C bus (needed for ES8388 register writes)
 *   - A method to toggle SPK_EN on the PI4IOE5V6408 expander
 *
 * Until those are exposed, this module uses a NEW i2c_master_bus for the codec
 * on the SAME pins. This works only if the BSP does not claim an exclusive lock
 * on those pins. If it does, build with CONFIG_TAB5_AUDIO_DISABLED=y to skip.
 *
 * The proper fix is to add to m5_tab5_component:
 *   esp_err_t spk_enable(bool en);
 *   i2c_master_bus_handle_t get_sys_i2c_bus();
 *
 * Audio runs in a dedicated task — never blocks LVGL, BLE or UART.
 */
#include "tab5_audio.h"
#include "m5tab5_pinmap.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include <string.h>

static const char* TAG = "tab5_audio";

// ES8388 I2C address
#define ES8388_I2C_ADDR  0x10

// I2S config
#define BEEP_SAMPLE_RATE 16000
#define BEEP_FREQ_HZ     1000
#define BEEP_AMPLITUDE   8000
#define BEEP_SHORT_MS    100
#define BEEP_GAP_MS      80

static bool             s_available  = false;
static uint8_t          s_volume     = 70;
static i2s_chan_handle_t s_tx_chan    = nullptr;
static QueueHandle_t    s_queue      = nullptr;
static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static i2c_master_dev_handle_t s_codec   = nullptr;

static esp_err_t codec_write(uint8_t reg, uint8_t val)
{
    if (!s_codec) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_codec, buf, 2, pdMS_TO_TICKS(20));
}

static esp_err_t codec_init(void)
{
    // Minimal ES8388 init for DAC playback (16-bit I2S, stereo).
    // Full register map: https://github.com/espressif/esp-idf-lib/blob/master/components/es8388/
    static const uint8_t init_seq[][2] = {
        {0x00, 0x80}, // Chip reset
        {0x00, 0x00},
        {0x01, 0x58}, // Power management 1: VMID/VREF on, ADC/DAC powered
        {0x02, 0xF3}, // Power management 2: power on DAC L+R, ADC L+R
        {0x03, 0x00}, // ADC input selection
        {0x04, 0x0C}, // ADC control: LIN1/RIN1
        {0x08, 0x00}, // Master clock divider
        {0x09, 0x00}, // ADC control 5
        {0x0A, 0x00}, // ADC control 6
        {0x0B, 0x02}, // DAC control 1: 16-bit I2S format
        {0x0C, 0x00}, // DAC control 2
        {0x26, 0x00}, // DAC control 23: volume
        {0x27, 0x00}, // LOUT1 volume: 0dB
        {0x2A, 0x00}, // ROUT1 volume: 0dB
        {0x2E, 0x1E}, // LOUT2 volume (headphone): 0dB
        {0x2F, 0x1E}, // ROUT2 volume
        {0x30, 0x07}, // Enable LOUT1/ROUT1 and LOUT2/ROUT2
        {0x38, 0x09}, // I2S clock: MCLK / 256 = 16kHz * 256 * 2
    };
    for (size_t i = 0; i < sizeof(init_seq)/sizeof(init_seq[0]); i++) {
        esp_err_t e = codec_write(init_seq[i][0], init_seq[i][1]);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "codec reg 0x%02X write failed: %s", init_seq[i][0], esp_err_to_name(e));
            return e;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

static void set_volume_hw(uint8_t vol)
{
    uint8_t hw = (uint8_t)((vol * 30) / 100); // 0–30 for ES8388 LOUT1/ROUT1
    codec_write(0x27, hw);
    codec_write(0x2A, hw);
}

static void play_tone_ms(uint32_t ms)
{
    static int16_t buf[640]; // 20ms at 16kHz stereo
    uint32_t total_samples = (BEEP_SAMPLE_RATE * ms) / 1000;
    float phase = 0.0f;
    const float step = 2.0f * (float)M_PI * BEEP_FREQ_HZ / BEEP_SAMPLE_RATE;

    while (total_samples > 0) {
        uint32_t chunk = total_samples < 320 ? total_samples : 320;
        for (uint32_t i = 0; i < chunk; i++) {
            int16_t s = (int16_t)(sinf(phase) * BEEP_AMPLITUDE * s_volume / 100);
            buf[i*2] = buf[i*2+1] = s;
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        size_t written = 0;
        i2s_channel_write(s_tx_chan, buf, chunk * 4, &written, pdMS_TO_TICKS(200));
        total_samples -= chunk;
    }
}

static void silence_ms(uint32_t ms)
{
    static int16_t buf[640];
    memset(buf, 0, sizeof(buf));
    uint32_t total_samples = (BEEP_SAMPLE_RATE * ms) / 1000;
    while (total_samples > 0) {
        uint32_t chunk = total_samples < 320 ? total_samples : 320;
        size_t written = 0;
        i2s_channel_write(s_tx_chan, buf, chunk * 4, &written, pdMS_TO_TICKS(200));
        total_samples -= chunk;
    }
}

static void beep_task(void*)
{
    uint8_t pat;
    while (true) {
        if (xQueueReceive(s_queue, &pat, portMAX_DELAY) != pdTRUE) continue;
        switch ((audio_pattern_t)pat) {
        case AUDIO_PAT_SILENT: break;
        case AUDIO_PAT_SHORT:
            play_tone_ms(BEEP_SHORT_MS);
            break;
        case AUDIO_PAT_DOUBLE:
            play_tone_ms(BEEP_SHORT_MS); silence_ms(BEEP_GAP_MS);
            play_tone_ms(BEEP_SHORT_MS);
            break;
        case AUDIO_PAT_TRIPLE:
            play_tone_ms(BEEP_SHORT_MS); silence_ms(BEEP_GAP_MS);
            play_tone_ms(BEEP_SHORT_MS); silence_ms(BEEP_GAP_MS);
            play_tone_ms(BEEP_SHORT_MS);
            break;
        }
    }
}

esp_err_t tab5_audio_init(void)
{
#ifdef CONFIG_TAB5_AUDIO_DISABLED
    ESP_LOGW(TAG, "audio disabled by config");
    return ESP_OK;
#endif

    // Try to create I2C master bus on SYS I2C pins.
    // If the BSP already owns these pins this will fail gracefully.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port    = I2C_NUM_0,
        .sda_io_num  = m5::tab5::M5TAB5_PIN_SYS_I2C_SDA,
        .scl_io_num  = m5::tab5::M5TAB5_PIN_SYS_I2C_SCL,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = false },
    };
    // Use i2c_new_master_bus; if port already exists, try to get handle.
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed (%s) — audio unavailable", esp_err_to_name(err));
        s_available = false;
        return ESP_OK; // non-fatal
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8388_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_codec);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8388 device add failed — audio unavailable");
        i2c_del_master_bus(s_i2c_bus); s_i2c_bus = nullptr;
        return ESP_OK;
    }

    err = codec_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8388 init failed — audio unavailable");
        i2c_master_bus_rm_device(s_codec); s_codec = nullptr;
        i2c_del_master_bus(s_i2c_bus); s_i2c_bus = nullptr;
        return ESP_OK;
    }

    // Init I2S
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    err = i2s_new_channel(&chan_cfg, &s_tx_chan, nullptr);
    if (err != ESP_OK) { ESP_LOGW(TAG, "i2s_new_channel failed"); return ESP_OK; }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(BEEP_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
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
    if (err != ESP_OK) { i2s_del_channel(s_tx_chan); s_tx_chan = nullptr; return ESP_OK; }
    i2s_channel_enable(s_tx_chan);

    set_volume_hw(s_volume);

    s_queue = xQueueCreate(4, 1);
    xTaskCreatePinnedToCore(beep_task, "tab5_beep", 3072, nullptr, 3, nullptr, 0);

    s_available = true;
    ESP_LOGI(TAG, "audio ready (ES8388, I2S port 0)");
    return ESP_OK;
}

void tab5_audio_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    s_volume = vol;
    if (s_available) set_volume_hw(vol);
}

void tab5_audio_beep(audio_pattern_t pat)
{
    if (!s_available || !s_queue || pat == AUDIO_PAT_SILENT) return;
    uint8_t p = (uint8_t)pat;
    xQueueSend(s_queue, &p, 0);
}

bool tab5_audio_available(void) { return s_available; }
