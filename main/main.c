#include "ESP32_P4_DISPLACEMENT.h"

static const char *TAG = "main";

void app_main(void)
{
    node_state_set(NODE_BOOT);
    app_context_init();

    app_context_t *ctx = app_context_get();

    ESP_LOGI(TAG, "%s firmware=%s", BOARD_NAME, FIRMWARE_VERSION);
    ESP_LOGI(TAG, "config: node=%s test=%s output=%s",
             ctx->config.node_id, ctx->config.test_id, ctx->config.output_path);

    /* Mount SD card before camera init so /sdcard is ready when recording starts */
    esp_err_t err = sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available - CSV output disabled");
    }

    node_state_set(NODE_CAMERA_INIT);
    err = p4_camera_init(&ctx->config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(err));
        node_state_set(NODE_ERROR);
        return;
    }

    err = acq_manager_init(ctx);
    if (err != ESP_OK) {
        node_state_set(NODE_ERROR);
        return;
    }

    node_state_set(NODE_IDLE);

#if CONFIG_DISP_AUTOSTART
    ESP_LOGI(TAG, "autostart enabled");
    err = acq_manager_start(ctx);
    if (err != ESP_OK) {
        return;
    }
    acq_manager_wait_done(ctx, portMAX_DELAY);
#else
    ESP_LOGI(TAG, "autostart disabled; HTTP/master control is not added yet");
#endif
}
