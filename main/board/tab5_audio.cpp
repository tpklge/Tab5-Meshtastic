/*
 * ES8388 audio for M5Stack Tab5.
 *
 * Uses the SYS I2C bus owned by the BSP (via m5tab5_get_sys_i2c_master_bus_handle)
 * — does NOT create a second I2C master. SPK_EN (PI4IOE5V6408 addr=0x43 pin P1)
 * is set HIGH via I2C to the expander before playing.
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

/* BSP internal: exposes the i2c_master_bus_handle_t it already owns. */
extern "C" i2c_master_bus_handle_t m5tab5_get_sys_i2c_master_bus_handle(void);

static const char* TAG = "tab5_audio";

#define ES8388_I2C_ADDR   0x10
#define PI4IO_ADDR_LOW    0x43   /* ADDR_LOW expander: has SPK_EN on P1 */
#define PI4IO_REG_OUT     0x05   /* M5TAB5_PI4IO_REG_OUT_STATE */
#define PI4IO_OUT_NORMAL  0x74   /* LCD_RST=H TP_RST=H CAM_RST=H EXT5V_EN=H SPK_EN=L */
#define PI4IO_OUT_SPK_ON  0x76   /* same + SPK_EN=H (bit1) */

#define BEEP_SAMPLE_RATE 16000
#define BEEP_FREQ_HZ     1000
#define BEEP_AMPLITUDE   8000
#define BEEP_SHORT_MS    100
#define BEEP_GAP_MS      80

static bool             s_available  = false;
static uint8_t          s_volume     = 70;
static i2s_chan_handle_t s_tx_chan    = nullptr;
static QueueHandle_t    s_queue      = nullptr;
static i2c_master_dev_handle_t s_codec    = nullptr;
static i2c_master_dev_handle_t s_extio    = nullptr;

static esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(20));
}

static esp_err_t spk_enable(bool on)
{
    if (!s_extio) return ESP_ERR_INVALID_STATE;
    return i2c_write_reg(s_extio, PI4IO_REG_OUT, on ? PI4IO_OUT_SPK_ON : PI4IO_OUT_NORMAL);
}

static esp_err_t codec_init(void)
{
    static const uint8_t init_seq[][2] = {
        {0x00, 0x80}, // Chip reset
        {0x00, 0x00},
        {0x01, 0x58}, // Power management 1
        {0x02, 0xF3}, // Power management 2
        {0x03, 0x00},
        {0x04, 0x0C},
        {0x08, 0x00},
        {0x09, 0x00},
        {0x0A, 0x00},
        {0x0B, 0x02}, // DAC: 16-bit I2S
        {0x0C, 0x00},
        {0x26, 0x00},
        {0x27, 0x00}, // LOUT1 vol
        {0x2A, 0x00}, // ROUT1 vol
        {0x2E, 0x1E}, // LOUT2 vol
        {0x2F, 0x1E}, // ROUT2 vol
        {0x30, 0x07}, // Enable outputs
        {0x38, 0x09}, // I2S clk
    };
    for (size_t i = 0; i < sizeof(init_seq)/sizeof(init_seq[0]); i++) {
        esp_err_t e = i2c_write_reg(s_codec, init_seq[i][0], init_seq[i][1]);
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
    if (!s_codec) return;
    uint8_t hw = (uint8_t)((vol * 30) / 100);
    i2c_write_reg(s_codec, 0x27, hw);
    i2c_write_reg(s_codec, 0x2A, hw);
}

static void play_tone_ms(uint32_t ms)
{
    static int16_t buf[640];
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
    uint32_t total = (BEEP_SAMPLE_RATE * ms) / 1000;
    while (total > 0) {
        uint32_t chunk = total < 320 ? total : 320;
        size_t written = 0;
        i2s_channel_write(s_tx_chan, buf, chunk * 4, &written, pdMS_TO_TICKS(200));
        total -= chunk;
    }
}

static void beep_task(void*)
{
    uint8_t pat;
    while (true) {
        if (xQueueReceive(s_queue, &pat, portMAX_DELAY) != pdTRUE) continue;
        spk_enable(true);
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
        spk_enable(false);
    }
}

esp_err_t tab5_audio_init(void)
{
    i2c_master_bus_handle_t bus = m5tab5_get_sys_i2c_master_bus_handle();
    if (!bus) {
        ESP_LOGW(TAG, "sys I2C bus not ready — audio unavailable");
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8388_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_codec);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8388 add_device failed (%s) — audio unavailable", esp_err_to_name(err));
        return ESP_OK;
    }

    /* Add the low-address PI4IO expander to toggle SPK_EN. */
    i2c_device_config_t ext_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PI4IO_ADDR_LOW,
        .scl_speed_hz    = 400000,
    };
    err = i2c_master_bus_add_device(bus, &ext_cfg, &s_extio);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PI4IO add_device failed (%s) — no SPK_EN control", esp_err_to_name(err));
        /* Non-fatal: codec may still work but speaker amp won't be enabled */
        s_extio = nullptr;
    }

    err = codec_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8388 init failed — audio unavailable");
        i2c_master_bus_rm_device(s_codec); s_codec = nullptr;
        if (s_extio) { i2c_master_bus_rm_device(s_extio); s_extio = nullptr; }
        return ESP_OK;
    }

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
    xTaskCreatePinnedToCore(beep_task, "tab5_beep", 3072, nullptr, 2, nullptr, 0);

    s_available = true;
    ESP_LOGI(TAG, "audio ready (ES8388 via BSP I2C, SPK_EN via PI4IO)");
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
