#include "http_server.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "http_api.h"
#include "wifi_manager.h"

static const char *TAG = "http_server";
static httpd_handle_t s_server;

esp_err_t http_server_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    err = http_api_register(s_server);
    if (err != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "HTTP API ready: http://%s/", wifi_manager_ip() ? wifi_manager_ip() : "?");
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}
