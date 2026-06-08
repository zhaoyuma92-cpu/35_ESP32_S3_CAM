#include "app_config.h"

#include <string.h>

#include "board_config.h"
#include "sdkconfig.h"

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

    copy_str(cfg->node_id, "P4NODE01", sizeof(cfg->node_id));
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
