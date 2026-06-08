#include "sdcard_write_task.h"

#include <inttypes.h>

#include "ESP32_P4_DISPLACEMENT.h"
#include "timing.h"

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
    timing_stat_t capture_wait = {0};
    timing_stat_t process_time = {0};
    timing_stat_t batch_wait = {0};
    timing_stat_t csv_write = {0};
    timing_stat_t csv_flush = {0};
    uint32_t dropped_frames = 0;
    int64_t first_t_us = 0;
    int64_t last_t_us = 0;

    displacement_batch_t batch;
    while (xQueueReceive(ctx->batch_queue, &batch, portMAX_DELAY) == pdTRUE) {
        if (batch.end) {
            node_state_set(NODE_FLUSHING);
            break;
        }

        total_batches++;
        if (csv_enabled) {
            int64_t csv_start_us = esp_timer_get_time();
            for (uint16_t i = 0; i < batch.count; i++) {
                csv_writer_write_sample(&writer, &batch.samples[i]);
            }
            timing_add(&csv_write, (uint32_t)(esp_timer_get_time() - csv_start_us));
        }

        for (uint16_t i = 0; i < batch.count; i++) {
            const displacement_sample_t *s = &batch.samples[i];
            if (first_t_us == 0) {
                first_t_us = s->t_us;
            }
            last_t_us = s->t_us;
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
            timing_add(&capture_wait, s->capture_wait_us);
            timing_add(&process_time, s->process_us);
            timing_add(&batch_wait, s->batch_wait_us);
            dropped_frames += s->dropped_frames;
            total_frames++;
        }

        if (batch.samples) {
            xQueueSend(ctx->free_batch_queue, &batch.samples, portMAX_DELAY);
        }

        if (csv_enabled && (total_batches % 5U) == 0U) {
            int64_t flush_start_us = esp_timer_get_time();
            csv_writer_flush(&writer);
            timing_add(&csv_flush, (uint32_t)(esp_timer_get_time() - flush_start_us));
        }

        if ((total_batches % 10U) == 0U) {
            double elapsed_s = (first_t_us != 0 && last_t_us > first_t_us)
                                   ? (double)(last_t_us - first_t_us) / 1000000.0
                                   : 0.0;
            double fps = elapsed_s > 0.0 ? (double)(total_frames - 1) / elapsed_s : 0.0;
            ESP_LOGI(TAG, "written batches=%" PRIu32 " frames=%" PRIu32
                          " fps=%.3f dropped=%" PRIu32
                          " capture_wait_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                          " process_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                          " batch_wait_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                          " csv_write_batch_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                          " csv_flush_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
                     total_batches, total_frames, fps, dropped_frames,
                     timing_min(&capture_wait), timing_avg(&capture_wait), capture_wait.max,
                     timing_min(&process_time), timing_avg(&process_time), process_time.max,
                     timing_min(&batch_wait), timing_avg(&batch_wait), batch_wait.max,
                     timing_min(&csv_write), timing_avg(&csv_write), csv_write.max,
                     timing_min(&csv_flush), timing_avg(&csv_flush), csv_flush.max);
        }
    }

    if (csv_enabled) {
        csv_writer_close(&writer);
    }

    int64_t dt_avg = dt_count > 0 ? dt_sum / dt_count : 0;
    if (dt_min == INT64_MAX) {
        dt_min = 0;
    }
    double elapsed_s = (first_t_us != 0 && last_t_us > first_t_us)
                           ? (double)(last_t_us - first_t_us) / 1000000.0
                           : 0.0;
    double fps = elapsed_s > 0.0 ? (double)(total_frames - 1) / elapsed_s : 0.0;
    ESP_LOGI(TAG, "done frames=%" PRIu32 " batches=%" PRIu32
                  " elapsed=%.3fs fps=%.3f dropped=%" PRIu32
                  " dt_min=%" PRId64 " dt_avg=%" PRId64 " dt_max=%" PRId64
                  " capture_wait_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                  " process_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                  " batch_wait_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                  " csv_write_batch_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                  " csv_flush_us[min/avg/max]=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
             total_frames, total_batches, elapsed_s, fps, dropped_frames,
             dt_min, dt_avg, dt_max,
             timing_min(&capture_wait), timing_avg(&capture_wait), capture_wait.max,
             timing_min(&process_time), timing_avg(&process_time), process_time.max,
             timing_min(&batch_wait), timing_avg(&batch_wait), batch_wait.max,
             timing_min(&csv_write), timing_avg(&csv_write), csv_write.max,
             timing_min(&csv_flush), timing_avg(&csv_flush), csv_flush.max);

    xSemaphoreGive(ctx->acquisition_done_sem);
    vTaskDelete(NULL);
}
