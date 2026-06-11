#include "ESP32_P4_DISPLACEMENT.h"
#include "wifi_manager.h"

static const char *TAG = "main";

void app_main(void)
{
    node_state_set(NODE_BOOT);
    app_context_init();

    app_context_t *ctx = app_context_get();

    ESP_LOGI(TAG, "%s firmware=%s", BOARD_NAME, FIRMWARE_VERSION);
    ESP_LOGI(TAG, "config: node=%s test=%s output=%s",
             ctx->config.node_id, ctx->config.test_id, ctx->config.output_path);

    /* ── 采集阶段：WiFi 保持静默 ─────────────────────────────────────────── */

    /* Mount SD card (SLOT_0, GPIO matrix CLK=43/CMD=44/D0-D3=39-42) */
    esp_err_t err = sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available — CSV output disabled");
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
    ESP_LOGI(TAG, "autostart: acquisition starting");
    err = acq_manager_start(ctx);
    if (err != ESP_OK) return;
    acq_manager_wait_done(ctx, portMAX_DELAY);
    ESP_LOGI(TAG, "acquisition done");
#endif

    /* ── 采集后阶段：连接 WiFi（SLOT_1，C6 SDIO）────────────────────────── */
#if CONFIG_DISP_ENABLE_WIFI
    ESP_LOGI(TAG, "acquisition complete — starting WiFi");
    esp_err_t wifi_err = wifi_manager_start();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi unavailable (%s)", esp_err_to_name(wifi_err));
    } else {
        ESP_LOGI(TAG, "WiFi ready at %s — ready for data transfer",
                 wifi_manager_ip() ? wifi_manager_ip() : "?");
    }
#else
    ESP_LOGI(TAG, "WiFi disabled (CONFIG_DISP_ENABLE_WIFI=n)");
#endif
}
