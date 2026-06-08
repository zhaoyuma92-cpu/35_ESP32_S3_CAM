#ifndef CAMERA_FRAME_MSG_H
#define CAMERA_FRAME_MSG_H

#include <stdbool.h>
#include <stdint.h>

#include "p4_camera.h"

typedef struct {
    p4_camera_frame_t frame;
    uint32_t capture_wait_us;
    uint32_t dropped_frames;
    bool end;
} camera_frame_msg_t;

#endif
