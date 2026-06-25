#include "camera_capture_task.h"

#include <inttypes.h>

#include "ESP32_P4_DISPLACEMENT.h"
#include "camera_frame_msg.h"
#include "timing.h"

static const char *TAG = "cam_task";

void camera_capture_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    const app_config_t *cfg = &ctx->run_config;
    const int64_t duration_us = (int64_t)cfg->duration_s * 1000000LL;
    const uint32_t expected_frames = app_config_total_frames(cfg);

    uint32_t prev_sequence = 0;
    bool have_prev_sequence = false;
    uint32_t captured_frames = 0;
    uint32_t dropped_frames = 0;
    timing_stat_t queue_send = {0};
    int64_t start_us = esp_timer_get_time();
    int64_t next_log_us = start_us + 30000000LL;

    ESP_LOGI(TAG, "capture start: %ux%u@%u fps duration=%" PRIu32
                  "s expected=%" PRIu32,
             cfg->frame_width, cfg->frame_height, cfg->frame_rate_hz,
             cfg->duration_s, expected_frames);

    while ((esp_timer_get_time() - start_us) < duration_us) {
        if (ctx->stop_requested) {
            ESP_LOGI(TAG, "stop requested at frame %" PRIu32, captured_frames);
            break;
        }

        p4_camera_frame_t frame = {0};
        int64_t capture_start_us = esp_timer_get_time();
        esp_err_t err = p4_camera_get_frame(&frame, pdMS_TO_TICKS(200));
        uint32_t capture_wait_us = (uint32_t)(esp_timer_get_time() - capture_start_us);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "camera_get_frame failed: %s", esp_err_to_name(err));
            break;
        }

        camera_frame_msg_t msg = {
            .frame = frame,
            .capture_wait_us = capture_wait_us,
            .dropped_frames = 0,
            .end = false,
        };
        if (have_prev_sequence && frame.sequence > prev_sequence + 1) {
            msg.dropped_frames = frame.sequence - prev_sequence - 1;
            dropped_frames += msg.dropped_frames;
        }
        prev_sequence = frame.sequence;
        have_prev_sequence = true;

        int64_t send_start_us = esp_timer_get_time();
        xQueueSend(ctx->frame_queue, &msg, portMAX_DELAY);
        timing_add(&queue_send, (uint32_t)(esp_timer_get_time() - send_start_us));
        captured_frames++;

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_log_us) {
            double elapsed_s = (double)(now_us - start_us) / 1000000.0;
            double fps = elapsed_s > 0.0 ? (double)captured_frames / elapsed_s : 0.0;
            ESP_LOGD(TAG, "progress elapsed=%.1fs frames=%" PRIu32
                          " fps=%.3f dropped=%" PRIu32
                          " frame_queue_send_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
                     elapsed_s, captured_frames, fps, dropped_frames,
                     timing_min(&queue_send), timing_avg(&queue_send), queue_send.max);
            next_log_us += 30000000LL;
        }
    }

    camera_frame_msg_t end = {
        .end = true,
    };
    xQueueSend(ctx->frame_queue, &end, portMAX_DELAY);
    int64_t end_us = esp_timer_get_time();
    double elapsed_s = (double)(end_us - start_us) / 1000000.0;
    double fps = elapsed_s > 0.0 ? (double)captured_frames / elapsed_s : 0.0;
    ESP_LOGI(TAG, "capture end elapsed=%.3fs frames=%" PRIu32
                  " fps=%.3f dropped=%" PRIu32
                  " frame_queue_send_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
             elapsed_s, captured_frames, fps, dropped_frames,
             timing_min(&queue_send), timing_avg(&queue_send), queue_send.max);
    vTaskDelete(NULL);
}
