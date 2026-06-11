#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

/* ── WiFi credentials ─────────────────────────────────────────────────────── */
#define WIFI_SSID       "Xiaomi_BDA5"
#define WIFI_PASSWORD   "mzy19920720"
#define WIFI_MAX_RETRY  5
#define WIFI_TIMEOUT_MS 60000

static const char *TAG = "wifi_only";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static int s_retry = 0;

/* ── Event handler ────────────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started — connecting to \"%s\"", WIFI_SSID);
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)data;
        if (s_retry < WIFI_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "disconnected (reason=%d), retry %d/%d",
                     disc->reason, s_retry, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "max retries reached — giving up");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "████ GOT IP: " IPSTR " ████", IP2STR(&evt->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ── app_main ─────────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== WiFi-only bring-up test ===");
    ESP_LOGI(TAG, "Target: P4 -> SDIO -> C6 -> %s", WIFI_SSID);

    /* NVS — required by WiFi stack */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase needed, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS OK");

    /* Netif + event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    (void)sta_netif;

    /* Event group for waiting on result */
    s_wifi_event_group = xEventGroupCreate();

    /* Register handlers before esp_wifi_init so we don't miss early events */
    esp_event_handler_instance_t h_wifi, h_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &h_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &h_ip));

    /* esp_wifi_init — this triggers esp_hosted SDIO init via constructor.
     * If C6 slave firmware version mismatches host, this will fail with
     * ESP_ERR_TIMEOUT on SDIO CMD5. Watch for:
     *   "sdmmc_init_ocr: send_op_cond (1) returned 0x107"
     *   "Not able to connect with ESP-Hosted slave device"
     */
    ESP_LOGI(TAG, "Calling esp_wifi_init() — watch for esp_hosted SDIO logs above...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init FAILED: %s (0x%x)", esp_err_to_name(init_err), init_err);
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "Diagnosis checklist:");
        ESP_LOGE(TAG, "  1. C6 slave firmware version must match host esp_hosted 1.4.x");
        ESP_LOGE(TAG, "     Factory C6 firmware is v0.0.6 — must reflash with 1.4.x slave");
        ESP_LOGE(TAG, "  2. Check SDIO reset polarity — GPIO54 active-low vs active-high");
        ESP_LOGE(TAG, "  3. Try CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=10000 if signal issue");
        ESP_LOGE(TAG, "  4. Verify C6 SDIO slave GPIOs are not remapped by factory firmware");
        return;
    }
    ESP_LOGI(TAG, "esp_wifi_init OK — C6 SDIO responded!");

    /* Configure STA */
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_LOGI(TAG, "esp_wifi_start...");
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Wait for result */
    ESP_LOGI(TAG, "Waiting up to %d s for IP on \"%s\"...", WIFI_TIMEOUT_MS / 1000, WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "=== PASS: WiFi connected and IP obtained ===");
        ESP_LOGI(TAG, "Next step: merge WiFi into main project as optional feature.");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "=== FAIL: WiFi authentication/connection failed ===");
        ESP_LOGE(TAG, "Check SSID \"%s\" and password, or AP channel (must be 2.4 GHz)", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "=== FAIL: Timeout after %d s ===", WIFI_TIMEOUT_MS / 1000);
    }

    /* Idle — keep running so monitor stays open */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "idle (heap free: %lu bytes)", esp_get_free_heap_size());
    }
}
