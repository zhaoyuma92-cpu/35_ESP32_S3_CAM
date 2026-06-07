#include "sdcard_write_task.h"

#include <inttypes.h>

#include "ESP32_P4_DISPLACEMENT.h"

static const char *TAG = "write_task";

void sdcard_write_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    const app_config_t *cfg = &ctx->run_config;
    csv_writer_t writer = {0};
    bool csv_enabled = csv_writer_open(&writer, cfg->output_path) == ESP_OK;

    uint32_t total_frames = 0;
    uint32_t total_batches = 0;
    int64_t dt_sum = 0;
    int64_t dt_min = INT64_MAX;
    int64_t dt_max = 0;
    uint32_t dt_count = 0;

    displacement_batch_t batch;
    while (xQueueReceive(ctx->batch_queue, &batch, portMAX_DELAY) == pdTRUE) {
        if (batch.end) {
            node_state_set(NODE_FLUSHING);
            break;
        }

        total_batches++;
        for (uint16_t i = 0; i < batch.count; i++) {
            const displacement_sample_t *s = &batch.samples[i];
            if (csv_enabled) {
                csv_writer_write_sample(&writer, s);
            }
            if (s->dt_us > 0) {
                if (s->dt_us < dt_min) {
                    dt_min = s->dt_us;
                }
                if (s->dt_us > dt_max) {
                    dt_max = s->dt_us;
                }
                dt_sum += s->dt_us;
                dt_count++;
            }
            total_frames++;
        }

        if (csv_enabled && (total_batches % 5U) == 0U) {
            csv_writer_flush(&writer);
        }

        if ((total_batches % 10U) == 0U) {
            ESP_LOGI(TAG, "written batches=%" PRIu32 " frames=%" PRIu32,
                     total_batches, total_frames);
        }
    }

    if (csv_enabled) {
        csv_writer_close(&writer);
    }

    int64_t dt_avg = dt_count > 0 ? dt_sum / dt_count : 0;
    if (dt_min == INT64_MAX) {
        dt_min = 0;
    }
    ESP_LOGI(TAG, "done frames=%" PRIu32 " batches=%" PRIu32
                  " dt_min=%" PRId64 " dt_avg=%" PRId64 " dt_max=%" PRId64,
             total_frames, total_batches, dt_min, dt_avg, dt_max);

    xSemaphoreGive(ctx->acquisition_done_sem);
    vTaskDelete(NULL);
}
