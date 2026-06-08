#ifndef ESP32_P4_DISPLACEMENT_H
#define ESP32_P4_DISPLACEMENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "board_config.h"
#include "app_config.h"
#include "node_state.h"
#include "app_context.h"
#include "p4_camera.h"
#include "sdcard.h"
#include "displacement_sample.h"
#include "camera_frame_msg.h"
#include "roi_tracker.h"
#include "acq_manager.h"
#include "camera_capture_task.h"
#include "roi_process_task.h"
#include "sdcard_write_task.h"
#include "csv_writer.h"

#define FIRMWARE_VERSION "v0.1.0"

#endif
