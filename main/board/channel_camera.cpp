#include "channel_camera.h"
#include "channel_url.h"
#include "m5tab5_driver_common.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"
#include "quirc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <atomic>
#include <new>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

namespace {
std::atomic<bool> running{false}, cancel{false};
SemaphoreHandle_t mutex;
char result[CHANNEL_URL_MAX]{}, message[120]{};
uint16_t* preview;
uint32_t generation = 0;
bool initialized = false;
constexpr uint8_t CAMERA_SCCB_ADDRESS = 0x36;
constexpr uint16_t CAMERA_EXPECTED_ID = 0xeb52;
void report(const char* text) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    snprintf(message, sizeof(message), "%s", text);
    xSemaphoreGive(mutex);
}
esp_err_t probe_sensor(i2c_master_bus_handle_t bus, uint16_t* id) {
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = CAMERA_SCCB_ADDRESS;
    config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &device);
    if (err != ESP_OK) return err;
    uint8_t high = 0, low = 0;
    const uint8_t high_reg[] = {0x31, 0x07};
    const uint8_t low_reg[] = {0x31, 0x08};
    err = i2c_master_transmit_receive(device, high_reg, sizeof(high_reg), &high, 1, 100);
    if (err == ESP_OK) err = i2c_master_transmit_receive(device, low_reg, sizeof(low_reg), &low, 1, 100);
    esp_err_t remove_err = i2c_master_bus_rm_device(device);
    if (err == ESP_OK) err = remove_err;
    if (err == ESP_OK) *id = (uint16_t(high) << 8) | low;
    return err;
}
esp_err_t initialize() {
    if (initialized) return ESP_OK;
    // GPIO36: camera XCLK. Timer 0/channel 1 belong to LCD backlight!
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.duty_resolution = LEDC_TIMER_1_BIT;
    timer.timer_num = LEDC_TIMER_1; timer.freq_hz = 24000000; timer.clk_cfg = LEDC_USE_PLL_DIV_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) { report("Falha no clock da camera (timer LEDC)."); return err; }
    ledc_channel_config_t channel{};
    channel.gpio_num = 36; channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_2; channel.timer_sel = LEDC_TIMER_1; channel.duty = 1;
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) { report("Falha no clock da camera (GPIO36)."); return err; }
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_video_init_csi_config_t csi{};
    csi.sccb_config.init_sccb = false;
    csi.sccb_config.i2c_handle = m5::tab5::m5tab5_get_sys_i2c_master_bus_handle();
    csi.sccb_config.freq = 400000; csi.reset_pin = -1; csi.pwdn_pin = -1;
    if (!csi.sccb_config.i2c_handle) {
        report("Barramento I2C da camera indisponivel.");
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t sensor_id = 0;
    err = probe_sensor(csi.sccb_config.i2c_handle, &sensor_id);
    if (err != ESP_OK) {
        ESP_LOGE("channel_camera", "camera SCCB 0x36 failed: %s", esp_err_to_name(err));
        report("Sensor 0x36 nao responde. Confira a camera do Tab5.");
        return err;
    }
    if (sensor_id != CAMERA_EXPECTED_ID) {
        char text[120];
        snprintf(text, sizeof(text), "Sensor de camera ID %04X (esperado EB52).", sensor_id);
        report(text);
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_video_init_config_t config{}; config.csi = &csi;
    err = esp_video_init(&config);
    if (err == ESP_OK) initialized = true;
    else { ESP_LOGE("channel_camera", "esp_video_init failed after sensor ID %04X: %s", sensor_id, esp_err_to_name(err));
        report("Sensor OK; falha ao iniciar video/ISP."); }
    return err;
}
void scan(void*) {
    int fd = -1, type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bool streaming = false;
    quirc* decoder = nullptr;
    auto code = new (std::nothrow) quirc_code{};
    auto decoded = new (std::nothrow) quirc_data{};
    uint8_t* buffers[2]{}; size_t lengths[2]{};
    do {
        if (!code || !decoded) { report("Memoria insuficiente para QR."); break; }
        report("Iniciando camera...");
        esp_err_t err = initialize();
        if (err != ESP_OK) break;
        if (cancel) break;
        fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
        if (fd < 0) { report("Nao foi possivel abrir a camera."); break; }
        v4l2_format format{}; format.type = type;
        if (ioctl(fd, VIDIOC_G_FMT, &format)) { report("Falha ao consultar formato da camera."); break; }
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(fd, VIDIOC_S_FMT, &format) || ioctl(fd, VIDIOC_G_FMT, &format)) { report("Camera nao oferece RGB565."); break; }
        const unsigned width = format.fmt.pix.width, height = format.fmt.pix.height;
        const unsigned stride = format.fmt.pix.bytesperline ? format.fmt.pix.bytesperline : width * 2;
        if (width < 640 || height < 360 || width > 1600 || height > 1200) { report("Resolucao de camera nao suportada."); break; }
        decoder = quirc_new();
        if (!decoder || quirc_resize(decoder, 640, 360) < 0) { report("Memoria insuficiente para ler QR."); break; }
        v4l2_requestbuffers req{}; req.count = 2; req.type = type; req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) || req.count < 2) { report("Falha ao alocar quadros da camera."); break; }
        bool mapped = true;
        for (unsigned i = 0; i < 2; ++i) {
            v4l2_buffer buf{}; buf.type = type; buf.memory = V4L2_MEMORY_MMAP; buf.index = i;
            if (ioctl(fd, VIDIOC_QUERYBUF, &buf)) { mapped = false; break; }
            buffers[i] = static_cast<uint8_t*>(mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset));
            lengths[i] = buf.length;
            if (!buffers[i] || buf.length < stride * height || ioctl(fd, VIDIOC_QBUF, &buf)) { mapped = false; break; }
        }
        if (!mapped || ioctl(fd, VIDIOC_STREAMON, &type)) { report("Falha ao iniciar captura."); break; }
        streaming = true;
        report("Aponte para o QR Meshtastic. Mantenha o codigo inteiro visivel.");
        const int64_t start = esp_timer_get_time(); int64_t last_frame = start;
        while (!cancel && esp_timer_get_time() - start < 60000000LL) {
            v4l2_buffer buf{}; buf.type = type; buf.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_DQBUF, &buf)) {
                if (esp_timer_get_time() - last_frame > 3000000) { report("Camera sem quadros. Feche e tente novamente."); break; }
                vTaskDelay(pdMS_TO_TICKS(10)); continue;
            }
            last_frame = esp_timer_get_time();
            if (buf.index >= 2) { report("Quadro de camera invalido."); break; }
            uint8_t* gray = quirc_begin(decoder, nullptr, nullptr);
            // Scale with actual negotiated stride; decoding is independent of LCD rotation.
            for (unsigned y = 0; y < 360; ++y) {
                const auto row = reinterpret_cast<const uint16_t*>(buffers[buf.index] + (y * height / 360) * stride);
                for (unsigned x = 0; x < 640; ++x) {
                    unsigned c = row[x * width / 640];
                    gray[y * 640 + x] = (((c >> 11) & 31) * 77 * 8 + ((c >> 5) & 63) * 150 * 4 + (c & 31) * 29 * 8) >> 8;
                }
            }
            xSemaphoreTake(mutex, portMAX_DELAY);
            for (unsigned y = 0; y < QR_PREVIEW_H; ++y) for (unsigned x = 0; x < QR_PREVIEW_W; ++x) {
                unsigned c = gray[(y * 2) * 640 + x * 2];
                preview[y * QR_PREVIEW_W + x] = ((c >> 3) << 11) | ((c >> 2) << 5) | (c >> 3);
            }
            ++generation; xSemaphoreGive(mutex);
            if (ioctl(fd, VIDIOC_QBUF, &buf)) { report("Falha ao devolver quadro da camera."); break; }
            quirc_end(decoder);
            bool found = false;
            for (int i = 0; i < quirc_count(decoder); ++i) {
                quirc_extract(decoder, i, code);
                auto error = quirc_decode(code, decoded);
                if (error) { quirc_flip(code); error = quirc_decode(code, decoded); }
                if (error || decoded->payload_len <= 0 || decoded->payload_len >= (int)sizeof(result)) continue;
                if (memchr(decoded->payload, 0, decoded->payload_len)) continue;
                decoded->payload[decoded->payload_len] = 0;
                meshtastic_ChannelSet set{}; bool add;
                if (!channel_url_decode(reinterpret_cast<char*>(decoded->payload), &set, &add)) {
                    report("QR lido, mas nao e um conjunto de canais Meshtastic valido."); continue;
                }
                xSemaphoreTake(mutex, portMAX_DELAY);
                memcpy(result, decoded->payload, decoded->payload_len + 1);
                snprintf(message, sizeof(message), "QR reconhecido. Revise os canais antes de importar.");
                xSemaphoreGive(mutex); found = true; break;
            }
            if (found) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (esp_timer_get_time() - start >= 60000000LL) report("Tempo de leitura encerrado. Tente novamente.");
    } while (false);
    if (streaming) ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < 2; ++i) if (buffers[i]) munmap(buffers[i], lengths[i]);
    if (fd >= 0) close(fd);
    if (decoder) quirc_destroy(decoder);
    delete code; delete decoded;
    initialized = false;   /* force full re-init on next capture to avoid stale ISP state */
    running = false;
    vTaskDelete(nullptr);
}
}
esp_err_t channel_camera_start() {
    if (running.exchange(true)) return ESP_ERR_INVALID_STATE;
    if (!mutex) mutex = xSemaphoreCreateMutex();
    if (!preview) preview = static_cast<uint16_t*>(heap_caps_calloc(QR_PREVIEW_W * QR_PREVIEW_H, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!mutex || !preview) { running = false; return ESP_ERR_NO_MEM; }
    xSemaphoreTake(mutex, portMAX_DELAY); result[0] = 0; message[0] = 0; xSemaphoreGive(mutex);
    cancel = false;
    if (xTaskCreatePinnedToCore(scan, "channel_camera", 12288, nullptr, 2, nullptr, 0) != pdPASS) { running = false; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
void channel_camera_stop() { cancel = true; }
bool channel_camera_poll(char* out, size_t cap, char* status, size_t status_cap, uint16_t* pixels, uint32_t* gen) {
    if (!mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    snprintf(status, status_cap, "%s", message);
    bool found = result[0];
    if (found) { snprintf(out, cap, "%s", result); result[0] = 0; }
    if (pixels && preview && gen && *gen != generation) {
        memcpy(pixels, preview, QR_PREVIEW_W * QR_PREVIEW_H * 2); *gen = generation;
    }
    xSemaphoreGive(mutex);
    return found;
}
