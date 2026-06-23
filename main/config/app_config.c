#include "app_config.h"

#include <string.h>

#include "board_config.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG_CFG = "app_cfg";
#define CFG_NS "cam_cfg"

static void copy_str(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int32_t q8_from_int(int v)
{
    return (int32_t)v << 8;
}

static void set_roi(roi_config_t *roi, uint16_t x1, uint16_t y1,
                    uint16_t x2, uint16_t y2)
{
    memset(roi, 0, sizeof(*roi));
    roi->enabled = true;
    roi->x1 = x1;
    roi->y1 = y1;
    roi->x2 = x2;
    roi->y2 = y2;
    roi->ref_x_q8 = q8_from_int(((int)x1 + (int)x2) / 2);
    roi->ref_y_q8 = q8_from_int(((int)y1 + (int)y2) / 2);
    roi->threshold = 0;
    roi->threshold_ratio_percent = 50;
    roi->min_pixels = 20;
    roi->max_pixels = 0;
    roi->polarity = ROI_POLARITY_DARK_ON_LIGHT;
}

void app_config_load_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    copy_str(cfg->node_id, "CAM_NODE_01", sizeof(cfg->node_id));
    copy_str(cfg->test_id, "TEST001", sizeof(cfg->test_id));
    copy_str(cfg->output_path, CONFIG_DISP_OUTPUT_PATH, sizeof(cfg->output_path));

    cfg->frame_width = BOARD_DEFAULT_FRAME_WIDTH;
    cfg->frame_height = BOARD_DEFAULT_FRAME_HEIGHT;
    cfg->frame_stride = BOARD_DEFAULT_FRAME_STRIDE;
    cfg->frame_rate_hz = BOARD_DEFAULT_FRAME_RATE_HZ;
    cfg->duration_s = 600;
    cfg->batch_frames = 30;
    cfg->pixel_format = BOARD_CAM_PIXEL_FORMAT;

#if CONFIG_DISP_FAKE_CAMERA
    uint16_t w = CONFIG_DISP_FAKE_FRAME_WIDTH;
    uint16_t h = CONFIG_DISP_FAKE_FRAME_HEIGHT;
    uint16_t qw = w / 4, qh = h / 4;
    set_roi(&cfg->roi[0], qw,         qh,         qw * 2,         qh * 2);
    set_roi(&cfg->roi[1], w - qw * 2, qh,         w - qw,         qh * 2);
    set_roi(&cfg->roi[2], qw,         h - qh * 2, qw * 2,         h - qh);
    set_roi(&cfg->roi[3], w - qw * 2, h - qh * 2, w - qw,         h - qh);
#else
    /* ROIs for real camera: 800x640 frame, four corners ~150x160 px each */
    set_roi(&cfg->roi[0],  60,  50, 210, 210);
    set_roi(&cfg->roi[1], 590,  50, 740, 210);
    set_roi(&cfg->roi[2],  60, 430, 210, 590);
    set_roi(&cfg->roi[3], 590, 430, 740, 590);
#endif
}

uint32_t app_config_total_frames(const app_config_t *cfg)
{
    return (uint32_t)cfg->frame_rate_hz * cfg->duration_s;
}

uint32_t app_config_frame_period_ms(const app_config_t *cfg)
{
    if (cfg->frame_rate_hz == 0) {
        return 33;
    }
    uint32_t period = 1000U / cfg->frame_rate_hz;
    return period > 0 ? period : 1;
}

esp_err_t app_config_save_to_nvs(const app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CFG, "nvs_open err %d", err);
        return err;
    }
    nvs_set_str(h, "node_id", cfg->node_id);
    nvs_set_str(h, "test_id", cfg->test_id);
    nvs_set_u32(h, "dur_s",   cfg->duration_s);
    nvs_set_u16(h, "batch_f", cfg->batch_frames);

    char key[8];
    for (int i = 0; i < APP_TARGET_COUNT; i++) {
        const roi_config_t *r = &cfg->roi[i];
        snprintf(key, sizeof(key), "r%d_en", i); nvs_set_u8 (h, key, r->enabled ? 1 : 0);
        snprintf(key, sizeof(key), "r%d_x1", i); nvs_set_u16(h, key, r->x1);
        snprintf(key, sizeof(key), "r%d_y1", i); nvs_set_u16(h, key, r->y1);
        snprintf(key, sizeof(key), "r%d_x2", i); nvs_set_u16(h, key, r->x2);
        snprintf(key, sizeof(key), "r%d_y2", i); nvs_set_u16(h, key, r->y2);
    }
    err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGI(TAG_CFG, "config saved to NVS");
    return err;
}

esp_err_t app_config_load_from_nvs(app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return err;
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CFG, "nvs_open err %d", err);
        return err;
    }
    size_t len;
    len = sizeof(cfg->node_id); nvs_get_str(h, "node_id", cfg->node_id, &len);
    len = sizeof(cfg->test_id); nvs_get_str(h, "test_id", cfg->test_id, &len);
    nvs_get_u32(h, "dur_s",   &cfg->duration_s);
    nvs_get_u16(h, "batch_f", &cfg->batch_frames);

    char key[8];
    for (int i = 0; i < APP_TARGET_COUNT; i++) {
        roi_config_t *r = &cfg->roi[i];
        uint8_t en = r->enabled ? 1 : 0;
        snprintf(key, sizeof(key), "r%d_en", i); nvs_get_u8 (h, key, &en); r->enabled = (en != 0);
        snprintf(key, sizeof(key), "r%d_x1", i); nvs_get_u16(h, key, &r->x1);
        snprintf(key, sizeof(key), "r%d_y1", i); nvs_get_u16(h, key, &r->y1);
        snprintf(key, sizeof(key), "r%d_x2", i); nvs_get_u16(h, key, &r->x2);
        snprintf(key, sizeof(key), "r%d_y2", i); nvs_get_u16(h, key, &r->y2);
        r->ref_x_q8 = (int32_t)((r->x1 + r->x2) / 2) << 8;
        r->ref_y_q8 = (int32_t)((r->y1 + r->y2) / 2) << 8;
    }
    nvs_close(h);
    ESP_LOGI(TAG_CFG, "config loaded from NVS");
    return ESP_OK;
}
