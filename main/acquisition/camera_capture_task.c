#include "camera_capture_task.h"

#include <inttypes.h>

#include "ESP32_P4_DISPLACEMENT.h"

static const char *TAG = "cam_task";
static displacement_sample_t s_batch_buf[2][APP_MAX_BATCH_FRAMES];

static void send_batch(app_context_t *ctx, displacement_sample_t *samples, uint16_t count)
{
    if (count == 0) {
        return;
    }
    displacement_batch_t batch = {
        .samples = samples,
        .count = count,
        .end = false,
    };
    xQueueSend(ctx->batch_queue, &batch, portMAX_DELAY);
}

void camera_capture_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    const app_config_t *cfg = &ctx->run_config;
    const uint32_t total_frames = app_config_total_frames(cfg);
    const uint16_t batch_frames = cfg->batch_frames > APP_MAX_BATCH_FRAMES
                                      ? APP_MAX_BATCH_FRAMES
                                      : cfg->batch_frames;

    int cur = 0;
    uint16_t pos = 0;
    int64_t prev_t_us = 0;

    ESP_LOGI(TAG, "capture start: %ux%u@%u fps total=%" PRIu32 " batch=%u",
             cfg->frame_width, cfg->frame_height, cfg->frame_rate_hz,
             total_frames, batch_frames);

    for (uint32_t i = 0; i < total_frames; i++) {
        if (ctx->stop_requested) {
            ESP_LOGI(TAG, "stop requested at frame %" PRIu32, i);
            break;
        }

        p4_camera_frame_t frame = {0};
        esp_err_t err = p4_camera_get_frame(&frame, pdMS_TO_TICKS(200));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "camera_get_frame failed: %s", esp_err_to_name(err));
            break;
        }

        displacement_sample_t *sample = &s_batch_buf[cur][pos];
        err = roi_tracker_process_frame(&frame, cfg, sample);
        p4_camera_return_frame(&frame);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "roi tracking failed: %s", esp_err_to_name(err));
            break;
        }

        sample->dt_us = prev_t_us == 0 ? 0 : sample->t_us - prev_t_us;
        prev_t_us = sample->t_us;
        pos++;

        if (pos == batch_frames) {
            send_batch(ctx, s_batch_buf[cur], pos);
            cur ^= 1;
            pos = 0;
        }
    }

    send_batch(ctx, s_batch_buf[cur], pos);

    displacement_batch_t end = {
        .samples = NULL,
        .count = 0,
        .end = true,
    };
    xQueueSend(ctx->batch_queue, &end, portMAX_DELAY);
    ESP_LOGI(TAG, "capture end");
    vTaskDelete(NULL);
}
