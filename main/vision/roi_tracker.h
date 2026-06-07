#ifndef ROI_TRACKER_H
#define ROI_TRACKER_H

#include "esp_err.h"

#include "app_config.h"
#include "displacement_sample.h"
#include "p4_camera.h"

esp_err_t roi_tracker_process_frame(const p4_camera_frame_t *frame,
                                    const app_config_t *cfg,
                                    displacement_sample_t *out);

#endif
