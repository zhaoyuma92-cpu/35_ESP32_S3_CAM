#include "ESP32_P4_DISPLACEMENT.h"
#include "wifi_manager.h"
#include "http_server.h"
#include "http_api.h"
#include "udp_discovery.h"
static const char *TAG = "main";

/* Serial command task: reads single chars from stdin (UART0 via VFS).
 *   s  → start acquisition (online)
 *   o  → start acquisition (offline: stop WiFi first)
 *   q  → stop acquisition
 *   ?  → print status
 */
static void uart_cmd_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    int ch;
    for (;;) {
        ch = getchar();
        if (ch == EOF) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        switch ((char)ch) {
        case 's':
            ESP_LOGI("uart_cmd", "'s' → start (online)");
            http_api_clear_offline_start();
            xSemaphoreGive(ctx->start_trigger_sem);
            break;
        case 'o':
            ESP_LOGI("uart_cmd", "'o' → start (offline)");
            http_api_set_offline_start();
            xSemaphoreGive(ctx->start_trigger_sem);
            break;
        case 'q':
            ESP_LOGI("uart_cmd", "'q' → stop");
            ctx->stop_requested = true;
            break;
        case '?':
            ESP_LOGI("uart_cmd", "state=%s wifi=%d ip=%s",
                     node_state_to_string(node_state_get()),
                     (int)wifi_manager_is_connected(),
                     wifi_manager_ip() ? wifi_manager_ip() : "none");
            break;
        default:
            break;
        }
    }
}

void app_main(void)
{
    node_state_set(NODE_BOOT);
    app_context_init();
    app_context_t *ctx = app_context_get();

    ESP_LOGI(TAG, "%s firmware=%s node=%s test=%s",
             BOARD_NAME, FIRMWARE_VERSION,
             ctx->config.node_id, ctx->config.test_id);

    /* SD card: SDMMC SLOT_0 (GPIO 39-44, LDO4) */
    esp_err_t err = sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available — CSV output disabled");
    }

    /* Camera and acquisition pipeline */
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

    /* WiFi: SDMMC SLOT_1 (GPIO 14-19, esp_hosted C6 SDIO) */
    err = wifi_manager_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi unavailable (%s) — HTTP and UDP disabled", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "WiFi ready: %s", wifi_manager_ip() ? wifi_manager_ip() : "?");
        http_server_start();
        udp_discovery_start();
    }

    xTaskCreatePinnedToCore(uart_cmd_task, "uart_cmd", 3072, ctx, 5, NULL, 1);

    node_state_set(NODE_IDLE);
    ESP_LOGI(TAG, "node ready — waiting for /api/start or /api/start-offline");
    ESP_LOGI(TAG, "serial cmds: s=start  o=offline-start  q=stop  ?=status");

    /* ── Node loop ───────────────────────────────────────────────────────── */
    while (1) {
        /* Block until HTTP API triggers acquisition */
        xSemaphoreTake(ctx->start_trigger_sem, portMAX_DELAY);

        bool was_offline = http_api_offline_start_requested();
        http_api_clear_offline_start();

        /* Snapshot config to run_config and launch the three pipeline tasks */
        err = acq_manager_start(ctx);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "acq_manager_start failed: %s", esp_err_to_name(err));
            node_state_set(NODE_IDLE);
            continue;
        }

        /* Block until all data is flushed to CSV and the write task exits */
        acq_manager_wait_done(ctx, portMAX_DELAY);
        ESP_LOGI(TAG, "acquisition complete (offline=%d stop_req=%d)",
                 (int)was_offline, (int)ctx->stop_requested);

        /* Offline path: WiFi and HTTP were stopped before acquisition;
         * restart them now so the user can retrieve the CSV. */
        if (was_offline) {
            ESP_LOGI(TAG, "reconnecting WiFi...");
            err = wifi_manager_start();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "WiFi reconnect failed (%s)", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "WiFi reconnected: %s",
                         wifi_manager_ip() ? wifi_manager_ip() : "?");
                http_server_start();
            }
        }

        node_state_set(NODE_IDLE);
        ESP_LOGI(TAG, "back to NODE_IDLE");
    }
}
