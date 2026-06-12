#include "wifi_manager.h"
#include "sdkconfig.h"

#ifdef CONFIG_DISP_ENABLE_WIFI

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define CONNECT_TIMEOUT_MS  30000

static const char *TAG = "wifi_mgr";
static EventGroupHandle_t s_eg;
static int s_retry;
static char s_ip[16];
static bool s_netif_ready;
static bool s_wifi_started;
static bool s_connected;

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        if (s_retry < CONFIG_DISP_WIFI_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "disconnected (reason=%d) retry %d/%d",
                     d->reason, s_retry, CONFIG_DISP_WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_eg, WIFI_FAIL_BIT);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s", s_ip);
        s_retry = 0;
        s_connected = true;
        xEventGroupSetBits(s_eg, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_start(void)
{
    /* NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase needed");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    if (!s_eg) {
        s_eg = xEventGroupCreate();
    }
    xEventGroupClearBits(s_eg, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_connected = false;

    if (!s_netif_ready) {
        ESP_ERROR_CHECK(esp_netif_init());
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        esp_netif_create_default_wifi_sta();

        esp_event_handler_instance_t h_wifi, h_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, &h_wifi));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, &h_ip));
        s_netif_ready = true;
    }

    if (!s_wifi_started) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_wifi_init failed: %s - C6 SDIO not ready?", esp_err_to_name(err));
            return err;
        }
    }

    wifi_config_t wcfg = {
        .sta = {
            .ssid     = CONFIG_DISP_WIFI_SSID,
            .password = CONFIG_DISP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        return err;
    }
    s_wifi_started = true;

    ESP_LOGI(TAG, "connecting to \"%s\"...", CONFIG_DISP_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected: %s", s_ip);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "WiFi connect failed");
    return ESP_FAIL;
}

esp_err_t wifi_manager_stop(void)
{
    s_connected = false;
    s_ip[0] = '\0';
    if (!s_wifi_started) {
        return ESP_OK;
    }
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT) {
        s_wifi_started = false;
        return ESP_OK;
    }
    return err;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

const char *wifi_manager_ip(void)
{
    return s_ip[0] ? s_ip : NULL;
}

#else /* CONFIG_DISP_ENABLE_WIFI not set */

esp_err_t wifi_manager_start(void) { return ESP_OK; }
esp_err_t wifi_manager_stop(void) { return ESP_OK; }
bool wifi_manager_is_connected(void) { return false; }
const char *wifi_manager_ip(void)  { return NULL; }

#endif
