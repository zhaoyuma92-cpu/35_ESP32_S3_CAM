#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_NODE_ID_MAX_LEN      16
#define APP_TEST_ID_MAX_LEN      32
#define APP_OUTPUT_PATH_MAX_LEN 128
#define APP_TARGET_COUNT          4
#define APP_MAX_BATCH_FRAMES     60

typedef enum {
    APP_PIXEL_FORMAT_GRAY8  = 0,
    APP_PIXEL_FORMAT_RAW8   = 1,
    APP_PIXEL_FORMAT_RAW10  = 2,
    APP_PIXEL_FORMAT_RGB565 = 3,
    APP_PIXEL_FORMAT_YUV422 = 4,  /* YUYV packing: Y0 U0 Y1 V0 per 4 bytes, 16 bpp */
} app_pixel_format_t;

typedef enum {
    ROI_POLARITY_DARK_ON_LIGHT = 0,
    ROI_POLARITY_LIGHT_ON_DARK = 1,
} roi_polarity_t;

typedef struct {
    bool enabled;
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    int32_t ref_x_q8;
    int32_t ref_y_q8;
    uint8_t threshold;
    uint8_t threshold_ratio_percent;
    uint16_t min_pixels;
    uint16_t max_pixels;
    roi_polarity_t polarity;
} roi_config_t;

typedef struct {
    char node_id[APP_NODE_ID_MAX_LEN];
    char test_id[APP_TEST_ID_MAX_LEN];
    char output_path[APP_OUTPUT_PATH_MAX_LEN];

    uint16_t frame_width;
    uint16_t frame_height;
    uint16_t frame_stride;
    uint16_t frame_rate_hz;
    uint32_t duration_s;
    uint16_t batch_frames;
    app_pixel_format_t pixel_format;

    roi_config_t roi[APP_TARGET_COUNT];
} app_config_t;

void     app_config_load_defaults(app_config_t *cfg);
uint32_t app_config_total_frames(const app_config_t *cfg);
uint32_t app_config_frame_period_ms(const app_config_t *cfg);
esp_err_t app_config_save_to_nvs(const app_config_t *cfg);
esp_err_t app_config_load_from_nvs(app_config_t *cfg);

#endif
