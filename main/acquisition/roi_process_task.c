#include "roi_process_task.h"

#include <inttypes.h>
#include <stdio.h>

#include "ESP32_P4_DISPLACEMENT.h"
#include "camera_frame_msg.h"

static const char *TAG = "roi_task";

/* Log first-frame raw bytes to UART for byte-order verification (no SD I/O,
 * no dropped frames). YUYV: Y at even offsets, U/V at odd offsets. */
static void debug_log_yuv422_first_frame(const p4_camera_frame_t *frame)
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    ESP_LOGI(TAG, "YUV422 first-frame raw bytes [0..31]:");
    for (int i = 0; i < 32 && i < (int)frame->len; i += 4) {
        ESP_LOGI(TAG, "  [%2d] %02X %02X %02X %02X  (Y0=%u U0=%u Y1=%u V0=%u)",
                 i,
                 frame->data[i], frame->data[i+1],
                 frame->data[i+2], frame->data[i+3],
                 frame->data[i], frame->data[i+1],
                 frame->data[i+2], frame->data[i+3]);
    }
}
#define BATCH_BUFFER_COUNT 4
static displacement_sample_t s_batch_buf[BATCH_BUFFER_COUNT][APP_MAX_BATCH_FRAMES];

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

static void init_free_batches(app_context_t *ctx)
{
    xQueueReset(ctx->free_batch_queue);
    for (int i = 0; i < BATCH_BUFFER_COUNT; i++) {
        displacement_sample_t *buf = s_batch_buf[i];
        xQueueSend(ctx->free_batch_queue, &buf, portMAX_DELAY);
    }
}

void roi_process_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    const app_config_t *cfg = &ctx->run_config;
    const uint16_t batch_frames = cfg->batch_frames > APP_MAX_BATCH_FRAMES
                                      ? APP_MAX_BATCH_FRAMES
                                      : cfg->batch_frames;

    init_free_batches(ctx);

    displacement_sample_t *cur_batch = NULL;
    uint16_t pos = 0;
    int64_t prev_t_us = 0;
    uint32_t processed_frames = 0;
    uint32_t dropped_frames = 0;

    ESP_LOGI(TAG, "ROI pipeline start: batch=%u buffers=%u", batch_frames, BATCH_BUFFER_COUNT);

    camera_frame_msg_t msg;
    while (xQueueReceive(ctx->frame_queue, &msg, portMAX_DELAY) == pdTRUE) {
        if (msg.end) {
            break;
        }

        uint32_t batch_wait_us = 0;
        if (!cur_batch) {
            int64_t wait_start_us = esp_timer_get_time();
            xQueueReceive(ctx->free_batch_queue, &cur_batch, portMAX_DELAY);
            pos = 0;
            batch_wait_us = (uint32_t)(esp_timer_get_time() - wait_start_us);
        }

        displacement_sample_t *sample = &cur_batch[pos];
        esp_err_t err = ESP_OK;
        if (msg.frame.pixel_format == APP_PIXEL_FORMAT_YUV422) {
            debug_log_yuv422_first_frame(&msg.frame);
        }
        if (msg.frame.pixel_format == APP_PIXEL_FORMAT_RAW10) {
            int64_t t0 = esp_timer_get_time();
            memset(sample, 0, sizeof(*sample));
            sample->frame_index = msg.frame.sequence;
            sample->t_us = msg.frame.t_us;
            sample->width = msg.frame.width;
            sample->height = msg.frame.height;
            sample->process_us = (uint32_t)(esp_timer_get_time() - t0);
        } else {
            err = roi_tracker_process_frame(&msg.frame, cfg, sample);
        }
        p4_camera_return_frame(&msg.frame);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "roi tracking failed: %s", esp_err_to_name(err));
            ctx->stop_requested = true;
            break;
        }

        sample->capture_wait_us = msg.capture_wait_us;
        sample->batch_wait_us = batch_wait_us;
        sample->dropped_frames = msg.dropped_frames;
        sample->dt_us = prev_t_us == 0 ? 0 : sample->t_us - prev_t_us;
        prev_t_us = sample->t_us;
        dropped_frames += sample->dropped_frames;

        pos++;
        processed_frames++;

        if (pos == batch_frames) {
            send_batch(ctx, cur_batch, pos);
            cur_batch = NULL;
            pos = 0;
        }
    }

    if (cur_batch && pos > 0) {
        send_batch(ctx, cur_batch, pos);
    } else if (cur_batch) {
        xQueueSend(ctx->free_batch_queue, &cur_batch, portMAX_DELAY);
    }

    displacement_batch_t end = {
        .samples = NULL,
        .count = 0,
        .end = true,
    };
    xQueueSend(ctx->batch_queue, &end, portMAX_DELAY);
    ESP_LOGI(TAG, "ROI pipeline end frames=%" PRIu32 " dropped=%" PRIu32,
             processed_frames, dropped_frames);
    vTaskDelete(NULL);
}
