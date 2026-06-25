#include "acq_manager.h"

#include <time.h>
#include <sys/time.h>

#include "ESP32_P4_DISPLACEMENT.h"

static const char *TAG = "acq_mgr";

// 文件名格式跟项目33(ADXL355节点)保持一致：{test_id}_{node_id}_{时间戳}.csv
static void build_csv_path(app_config_t *cfg)
{
    const char *node_id = cfg->node_id[0] ? cfg->node_id : "CAM_NODE_01";
    const char *test_id = cfg->test_id[0] ? cfg->test_id : "TEST01";

    struct timeval tv;
    struct tm tm_info;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_info);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_info);

    snprintf(cfg->output_path, sizeof(cfg->output_path),
             "/sdcard/%.31s_%.15s_%s.csv", test_id, node_id, ts);
}

esp_err_t acq_manager_init(app_context_t *ctx)
{
    ctx->frame_queue = xQueueCreate(2, sizeof(camera_frame_msg_t));
    if (!ctx->frame_queue) {
        ESP_LOGE(TAG, "frame queue create failed");
        return ESP_FAIL;
    }

    ctx->batch_queue = xQueueCreate(3, sizeof(displacement_batch_t));
    if (!ctx->batch_queue) {
        ESP_LOGE(TAG, "batch queue create failed");
        return ESP_FAIL;
    }

    ctx->free_batch_queue = xQueueCreate(4, sizeof(displacement_sample_t *));
    if (!ctx->free_batch_queue) {
        ESP_LOGE(TAG, "free batch queue create failed");
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
    build_csv_path(&ctx->run_config);
    ctx->stop_requested = false;
    xQueueReset(ctx->frame_queue);
    xQueueReset(ctx->batch_queue);

    ESP_LOGI(TAG, "start: node=%s test=%s %ux%u@%u duration=%" PRIu32 "s",
             ctx->run_config.node_id, ctx->run_config.test_id,
             ctx->run_config.frame_width, ctx->run_config.frame_height,
             ctx->run_config.frame_rate_hz, ctx->run_config.duration_s);

    node_state_set(NODE_RECORDING);

    BaseType_t r1 = xTaskCreatePinnedToCore(camera_capture_task, "CameraTask",
                                            12288, ctx, 5, NULL, 0);
    BaseType_t r2 = xTaskCreatePinnedToCore(roi_process_task, "RoiTask",
                                            12288, ctx, 5, NULL, 1);
    // core0 只留给 CameraTask，把 WriteTask 挪到 core1 跟 RoiTask 共享，
    // 避免 SD 卡写入/flush 跟采集任务抢同一个核心
    BaseType_t r3 = xTaskCreatePinnedToCore(sdcard_write_task, "WriteTask",
                                            12288, ctx, 2, NULL, 1);
    if (r1 != pdPASS || r2 != pdPASS || r3 != pdPASS) {
        ESP_LOGE(TAG, "task create failed r1=%d r2=%d r3=%d", (int)r1, (int)r2, (int)r3);
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
