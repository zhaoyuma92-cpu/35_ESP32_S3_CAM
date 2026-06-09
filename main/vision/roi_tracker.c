#include "roi_tracker.h"

#include <string.h>

#include "esp_timer.h"

static int clamp_int(int value, int lo, int hi)
{
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static uint8_t rgb565_to_luma(uint16_t pixel)
{
    uint32_t r = (uint32_t)((pixel >> 11) & 0x1F) * 255U / 31U;
    uint32_t g = (uint32_t)((pixel >> 5) & 0x3F) * 255U / 63U;
    uint32_t b = (uint32_t)(pixel & 0x1F) * 255U / 31U;
    return (uint8_t)((r * 30U + g * 59U + b * 11U) / 100U);
}

static inline uint8_t sample_luma(const p4_camera_frame_t *frame, int x, int y)
{
    if (frame->pixel_format == APP_PIXEL_FORMAT_RGB565) {
        const uint8_t *p = frame->data + (((size_t)y * frame->stride + x) * 2U);
        uint16_t pixel = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        return rgb565_to_luma(pixel);
    }
    if (frame->pixel_format == APP_PIXEL_FORMAT_YUV422) {
        /* YUYV: [Y0][U0][Y1][V0]... — Y at byte offset x*2 within each row */
        return frame->data[((size_t)y * frame->stride + x) * 2U];
    }
    return frame->data[(size_t)y * frame->stride + x];
}

static uint8_t compute_threshold(const p4_camera_frame_t *frame,
                                 const roi_config_t *roi,
                                 int x1, int y1, int x2, int y2)
{
    if (roi->threshold > 0) {
        return roi->threshold;
    }

    int cx = (x1 + x2) / 2;
    int cy = (y1 + y2) / 2;
    uint8_t min_v = 255;
    uint8_t max_v = 0;

    for (int x = x1; x <= x2; x++) {
        uint8_t p = sample_luma(frame, x, cy);
        if (p < min_v) {
            min_v = p;
        }
        if (p > max_v) {
            max_v = p;
        }
    }

    for (int y = y1; y <= y2; y++) {
        uint8_t p = sample_luma(frame, cx, y);
        if (p < min_v) {
            min_v = p;
        }
        if (p > max_v) {
            max_v = p;
        }
    }

    uint32_t span = (uint32_t)max_v - min_v;
    uint32_t threshold = min_v + span * roi->threshold_ratio_percent / 100U;
    return (uint8_t)clamp_int((int)threshold, 0, 255);
}

static bool pixel_is_target(uint8_t p, uint8_t threshold, roi_polarity_t polarity)
{
    if (polarity == ROI_POLARITY_LIGHT_ON_DARK) {
        return p > threshold;
    }
    return p < threshold;
}

static uint8_t compute_quality(uint8_t contrast, uint32_t count, uint32_t area)
{
    if (area == 0 || count == 0) {
        return 0;
    }

    uint32_t fill_percent = count * 100U / area;
    uint32_t q = (uint32_t)contrast * 100U / 255U;

    if (fill_percent < 1U || fill_percent > 85U) {
        q /= 2U;
    }
    if (q > 100U) {
        q = 100U;
    }
    return (uint8_t)q;
}

static void track_one_roi(const p4_camera_frame_t *frame,
                          const roi_config_t *roi,
                          target_sample_t *target)
{
    memset(target, 0, sizeof(*target));

    if (!roi->enabled) {
        return;
    }

    int x1 = clamp_int(roi->x1, 0, frame->width - 1);
    int y1 = clamp_int(roi->y1, 0, frame->height - 1);
    int x2 = clamp_int(roi->x2, 0, frame->width - 1);
    int y2 = clamp_int(roi->y2, 0, frame->height - 1);

    if (x2 < x1) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 < y1) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    if ((x2 - x1) < 2 || (y2 - y1) < 2) {
        return;
    }

    uint8_t threshold = compute_threshold(frame, roi, x1, y1, x2, y2);
    uint8_t min_v = 255;
    uint8_t max_v = 0;
    uint64_t sum_x = 0;
    uint64_t sum_y = 0;
    uint32_t count = 0;
    uint32_t area = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);
    bool is_rgb565  = frame->pixel_format == APP_PIXEL_FORMAT_RGB565;
    bool is_yuv422  = frame->pixel_format == APP_PIXEL_FORMAT_YUV422;
    bool is_packed16 = is_rgb565 || is_yuv422;

    for (int y = y1; y <= y2; y++) {
        const uint8_t *row = frame->data + (size_t)y * frame->stride * (is_packed16 ? 2U : 1U);
        for (int x = x1; x <= x2; x++) {
            uint8_t p;
            if (is_rgb565) {
                const uint8_t *px = row + ((size_t)x * 2U);
                uint16_t rgb565 = (uint16_t)px[0] | ((uint16_t)px[1] << 8);
                p = rgb565_to_luma(rgb565);
            } else if (is_yuv422) {
                /* YUYV: Y at even byte positions within row */
                p = row[(size_t)x * 2U];
            } else {
                p = row[x];
            }
            if (p < min_v) {
                min_v = p;
            }
            if (p > max_v) {
                max_v = p;
            }
            if (pixel_is_target(p, threshold, roi->polarity)) {
                sum_x += (uint32_t)x;
                sum_y += (uint32_t)y;
                count++;
            }
        }
    }

    uint32_t max_pixels = roi->max_pixels;
    if (max_pixels == 0) {
        max_pixels = area * 85U / 100U;
    }

    target->threshold = threshold;
    target->pixel_count = count;
    target->quality = compute_quality((uint8_t)(max_v - min_v), count, area);

    if (count < roi->min_pixels || count > max_pixels) {
        return;
    }

    target->cx_q8 = (int32_t)((sum_x << 8) / count);
    target->cy_q8 = (int32_t)((sum_y << 8) / count);
    target->dx_q8 = target->cx_q8 - roi->ref_x_q8;
    target->dy_q8 = target->cy_q8 - roi->ref_y_q8;
    target->valid = true;
}

esp_err_t roi_tracker_process_frame(const p4_camera_frame_t *frame,
                                    const app_config_t *cfg,
                                    displacement_sample_t *out)
{
    if (!frame || !cfg || !out || !frame->data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frame->pixel_format != APP_PIXEL_FORMAT_GRAY8 &&
        frame->pixel_format != APP_PIXEL_FORMAT_RAW8 &&
        frame->pixel_format != APP_PIXEL_FORMAT_RGB565 &&
        frame->pixel_format != APP_PIXEL_FORMAT_YUV422) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (frame->stride < frame->width) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t t0 = esp_timer_get_time();
    memset(out, 0, sizeof(*out));
    out->frame_index = frame->sequence;
    out->t_us = frame->t_us;
    out->width = frame->width;
    out->height = frame->height;

    for (int i = 0; i < APP_TARGET_COUNT; i++) {
        track_one_roi(frame, &cfg->roi[i], &out->target[i]);
        if (out->target[i].valid) {
            out->valid_mask |= (uint8_t)(1U << i);
        }
    }

    out->process_us = (uint32_t)(esp_timer_get_time() - t0);
    return ESP_OK;
}
