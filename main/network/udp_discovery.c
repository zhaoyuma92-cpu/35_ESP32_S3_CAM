#include "udp_discovery.h"

#include <errno.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "app_context.h"
#include "board_config.h"
#include "node_state.h"

/* Must match project 33 udp_discovery.h and project 34 udp_discover.h */
#define UDP_DISCOVERY_PORT  33330
#define UDP_DISCOVERY_MAGIC "ADXL355_DISCOVER_V1"

static const char *TAG = "udp_disc";
static bool s_started;

static void discovery_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(UDP_DISCOVERY_PORT),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind failed errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 100000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    static char rx[64];
    static char tx[320];
    ESP_LOGI(TAG, "UDP discovery responder ready on port %d", UDP_DISCOVERY_PORT);

    while (1) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        int n = recvfrom(sock, rx, sizeof(rx) - 1, 0,
                         (struct sockaddr *)&src, &src_len);
        if (n <= 0) {
            continue;
        }
        rx[n] = '\0';
        if (strcmp(rx, UDP_DISCOVERY_MAGIC) != 0) {
            continue;
        }

        uint8_t mac[6] = {0};
        esp_wifi_get_mac(WIFI_IF_STA, mac);

        char ip[24] = "0.0.0.0";
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info = {0};
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));
            }
        }

        app_context_t *ctx = app_context_get();
        int len = snprintf(tx, sizeof(tx),
                           "{"
                           "\"type\":\"displacement_node\","
                           "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                           "\"node_id\":\"%.15s\","
                           "\"test_id\":\"%.31s\","
                           "\"ip\":\"%.23s\","
                           "\"state\":\"%s\","
                           "\"firmware\":\"%s\""
                           "}",
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                           ctx->config.node_id, ctx->config.test_id, ip,
                           node_state_to_string(node_state_get()), FIRMWARE_VERSION);
        if (len > 0 && len < (int)sizeof(tx)) {
            sendto(sock, tx, len, 0, (struct sockaddr *)&src, sizeof(src));
        }
    }
}

void udp_discovery_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;
    xTaskCreatePinnedToCore(discovery_task, "UdpDisc", 4096, NULL, 0, NULL, 1);
}
