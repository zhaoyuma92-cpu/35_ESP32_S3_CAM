#pragma once
#include <stdbool.h>

#include "esp_err.h"

/**
 * Initialize NVS, netif, event loop, and connect to WiFi STA.
 * Blocks until IP is obtained or max retries are exhausted.
 * No-op (returns ESP_OK) when CONFIG_DISP_ENABLE_WIFI=n.
 */
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
bool wifi_manager_is_connected(void);

/** Return current IPv4 as string, or NULL if not connected. */
const char *wifi_manager_ip(void);
