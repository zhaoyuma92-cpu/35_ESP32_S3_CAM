#include "sdcard.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "soc/soc_caps.h"

/* SDMMC pin mapping for Waveshare ESP32-P4-NANO:
 *   CLK -> GPIO43
 *   CMD -> GPIO44
 *   D0  -> GPIO39
 *   D1  -> GPIO40
 *   D2  -> GPIO41
 *   D3  -> GPIO42
 */
static const char *TAG    = "sdcard";
static const char MOUNT[] = "/sdcard";
static sdmmc_card_t *s_card;
#if SOC_SDMMC_IO_POWER_EXTERNAL
static sd_pwr_ctrl_handle_t s_pwr_ctrl;
#endif

static void sd_gpio_diagnostic(void)
{
    const struct {
        int io;
        const char *name;
    } pins[] = {
        { BOARD_SD_CLK_IO, "CLK(43)" },
        { BOARD_SD_CMD_IO, "CMD(44)" },
        { BOARD_SD_D0_IO,  "D0(39)"  },
        { BOARD_SD_D1_IO,  "D1(40)"  },
        { BOARD_SD_D2_IO,  "D2(41)"  },
        { BOARD_SD_D3_IO,  "D3(42)"  },
    };

    for (int i = 0; i < 6; i++) {
        gpio_config_t c = {
            .pin_bit_mask = (1ULL << pins[i].io),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&c);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    for (int i = 0; i < 6; i++) {
        int level = gpio_get_level(pins[i].io);
        ESP_LOGI(TAG, "  GPIO%s (input+pullup) = %d%s",
                 pins[i].name, level, level == 0 ? " <<< STUCK LOW?" : "");
    }
}

esp_err_t sdcard_mount(void)
{
    if (s_card) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT; /* 20 MHz, same as the Waveshare demo */

#if SOC_SDMMC_IO_POWER_EXTERNAL
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = BOARD_SD_PWR_LDO_CHAN,
    };
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD LDO%d power control init failed: %s",
                 BOARD_SD_PWR_LDO_CHAN, esp_err_to_name(err));
        return err;
    }
    host.pwr_ctrl_handle = s_pwr_ctrl;
    ESP_LOGI(TAG, "SDMMC IO power uses on-chip LDO%d", BOARD_SD_PWR_LDO_CHAN);
#else
    esp_err_t err = ESP_OK;
#endif

    ESP_LOGI(TAG, "GPIO diagnostic (input+pullup levels):");
    sd_gpio_diagnostic();

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = BOARD_SD_CLK_IO;
    slot.cmd = BOARD_SD_CMD_IO;
    slot.d0  = BOARD_SD_D0_IO;
    slot.d1  = BOARD_SD_D1_IO;
    slot.d2  = BOARD_SD_D2_IO;
    slot.d3  = BOARD_SD_D3_IO;
#endif
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    ESP_LOGI(TAG, "mount SDMMC 4-bit: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d",
             BOARD_SD_CLK_IO, BOARD_SD_CMD_IO, BOARD_SD_D0_IO,
             BOARD_SD_D1_IO, BOARD_SD_D2_IO, BOARD_SD_D3_IO);
    err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc mount failed: %s", esp_err_to_name(err));
#if SOC_SDMMC_IO_POWER_EXTERNAL
        if (s_pwr_ctrl) {
            sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl);
            s_pwr_ctrl = NULL;
        }
#endif
        return err;
    }

    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "filesystem mounted at %s", MOUNT);
    return ESP_OK;
}

void sdcard_unmount(void)
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
        s_card = NULL;
        ESP_LOGI(TAG, "unmounted");
    }

#if SOC_SDMMC_IO_POWER_EXTERNAL
    if (s_pwr_ctrl) {
        esp_err_t err = sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SD LDO power control delete failed: %s", esp_err_to_name(err));
        }
        s_pwr_ctrl = NULL;
    }
#endif
}
