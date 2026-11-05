#include "sd_storage.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "m5tab5_pinmap.h"

static const char* TAG = "sd_storage";
static bool            s_mounted = false;
static sdmmc_card_t*   s_card    = nullptr;
static sd_pwr_ctrl_handle_t s_pwr_ctrl_handle = nullptr;

esp_err_t sd_storage_init(void)
{
    if (s_mounted) return ESP_OK;

    /* Keep the microSD on SPI3, independent of the C6 SDMMC/SDIO link.
     * Tab5 wiring: SCLK=43, MOSI=44 (CMD), MISO=39 (D0), CS=42 (D3). */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  /* 20 MHz */

    spi_bus_config_t bus = {};
    bus.mosi_io_num = m5::tab5::M5TAB5_PIN_SDMMC_CMD;
    bus.miso_io_num = m5::tab5::M5TAB5_PIN_SDMMC_D0;
    bus.sclk_io_num = m5::tab5::M5TAB5_PIN_SDMMC_CLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = 4096;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI3_HOST;
    slot.gpio_cs = m5::tab5::M5TAB5_PIN_SDMMC_D3;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,            /* never auto-format a user card */
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat            = false,
    };

    /* LDO 4 supplies the microSD I/O domain (GPIOs 39..44). Keep the
     * controller alive until after the card has been unmounted. */
    sd_pwr_ctrl_ldo_config_t ldo_config = { .ldo_chan_id = 4 };
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD LDO 4 power control init failed: %s", esp_err_to_name(err));
        return err;
    }
    host.pwr_ctrl_handle = s_pwr_ctrl_handle;
    ESP_LOGI(TAG, "microSD using on-chip LDO channel 4");

    err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI3 bus init failed: %s", esp_err_to_name(err));
        sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl_handle);
        s_pwr_ctrl_handle = nullptr;
        return err;
    }

    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGW(TAG, "No microSD detected or filesystem invalid (continuing without SD)");
        } else {
            ESP_LOGW(TAG, "SD mount failed: %s (continuing without SD)", esp_err_to_name(err));
        }
        s_card = nullptr;
        spi_bus_free(SPI3_HOST);
        sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl_handle);
        s_pwr_ctrl_handle = nullptr;
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "microSD mounted via SDSPI/SPI3 at %s: %lluMB", SD_MOUNT_POINT,
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024ULL * 1024ULL));
    return ESP_OK;
}

bool sd_storage_available(void) { return s_mounted; }

void sd_storage_deinit(void)
{
    if (!s_mounted) return;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD unmount failed: %s", esp_err_to_name(err));
        return;
    }
    s_card    = nullptr;
    s_mounted = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(SPI3_HOST));
    sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl_handle);
    s_pwr_ctrl_handle = nullptr;
}
