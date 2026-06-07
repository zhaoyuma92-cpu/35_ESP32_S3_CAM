#include "acq_manager.h"

#include "ESP32_P4_DISPLACEMENT.h"

static const char *TAG = "acq_mgr";

esp_err_t acq_manager_init(app_context_t *ctx)
{
    ctx->batch_queue = xQueueCreate(3, sizeof(displacement_batch_t));
    if (!ctx->batch_queue) {
        ESP_LOGE(TAG, "batch queue create failed");
        return ESP_FAIL;
    }

    ctx->acquisition_done_sem = xSemaphoreCreateBinary();
    if (!ctx->acquisition_done_sem) {
        ESP_LOGE(TAG, "done semaphore create failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t acq_manager_start(app_context_t *ctx)
{
    ctx->run_config = ctx->config;
    ctx->stop_requested = false;

    ESP_LOGI(TAG, "start: node=%s test=%s %ux%u@%u duration=%" PRIu32 "s",
             ctx->run_config.node_id, ctx->run_config.test_id,
             ctx->run_config.frame_width, ctx->run_config.frame_height,
             ctx->run_config.frame_rate_hz, ctx->run_config.duration_s);

    node_state_set(NODE_RECORDING);

    BaseType_t r1 = xTaskCreatePinnedToCore(camera_capture_task, "CameraTask",
                                            12288, ctx, 4, NULL, 0);
    BaseType_t r2 = xTaskCreatePinnedToCore(sdcard_write_task, "WriteTask",
                                            12288, ctx, 2, NULL, 1);
    if (r1 != pdPASS || r2 != pdPASS) {
        ESP_LOGE(TAG, "task create failed r1=%d r2=%d", (int)r1, (int)r2);
        node_state_set(NODE_ERROR);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t acq_manager_wait_done(app_context_t *ctx, TickType_t timeout)
{
    if (xSemaphoreTake(ctx->acquisition_done_sem, timeout) != pdTRUE) {
        ESP_LOGE(TAG, "wait acquisition timeout");
        node_state_set(NODE_ERROR);
        return ESP_ERR_TIMEOUT;
    }
    node_state_set(ctx->stop_requested ? NODE_STOPPED : NODE_FINISHED);
    return ESP_OK;
}
