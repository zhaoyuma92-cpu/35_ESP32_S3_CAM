#ifndef P4_CAMERA_H
#define P4_CAMERA_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"

typedef struct {
    const uint8_t *data;
    size_t len;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    app_pixel_format_t pixel_format;
    uint32_t sequence;
    int64_t t_us;
} p4_camera_frame_t;

esp_err_t p4_camera_init(const app_config_t *cfg);
esp_err_t p4_camera_get_frame(p4_camera_frame_t *frame, TickType_t timeout);
void p4_camera_return_frame(const p4_camera_frame_t *frame);

#endif
