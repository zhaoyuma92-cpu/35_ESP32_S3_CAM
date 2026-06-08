#ifndef DISPLACEMENT_SAMPLE_H
#define DISPLACEMENT_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef struct {
    bool valid;
    uint8_t threshold;
    uint8_t quality;
    uint32_t pixel_count;
    int32_t cx_q8;
    int32_t cy_q8;
    int32_t dx_q8;
    int32_t dy_q8;
} target_sample_t;

typedef struct {
    uint32_t frame_index;
    int64_t t_us;
    int64_t dt_us;
    uint32_t capture_wait_us;
    uint32_t process_us;
    uint32_t batch_wait_us;
    uint32_t dropped_frames;
    uint16_t width;
    uint16_t height;
    uint8_t valid_mask;
    target_sample_t target[APP_TARGET_COUNT];
} displacement_sample_t;

typedef struct {
    displacement_sample_t *samples;
    uint16_t count;
    bool end;
} displacement_batch_t;

#endif
