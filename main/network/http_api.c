#include "http_api.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "app_context.h"
#include "board_config.h"
#include "file_manager.h"
#include "http_server.h"
#include "node_state.h"
#include "wifi_manager.h"

#define DOWNLOAD_CHUNK_SIZE 4096

static const char *TAG = "http_api";
static volatile bool s_offline_start;

bool http_api_offline_start_requested(void)
{
    return s_offline_start;
}

void http_api_clear_offline_start(void)
{
    s_offline_start = false;
}

void http_api_set_offline_start(void)
{
    s_offline_start = true;
}

static bool sdcard_is_mounted(void)
{
    DIR *dir = opendir("/sdcard");
    if (!dir) {
        return false;
    }
    closedir(dir);
    return true;
}

static void send_cors_json(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static esp_err_t handle_root(httpd_req_t *req)
{
    app_context_t *ctx = app_context_get();
    const char *ip = wifi_manager_ip();
    const char *html =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>P4 displacement node</title>"
        "<style>body{font-family:monospace;padding:16px;max-width:720px;margin:auto}"
        "a{display:block;margin:6px 0}</style></head><body>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, html);

    char buf[512];
    snprintf(buf, sizeof(buf),
             "<h3>P4 displacement node</h3>"
             "<p>node: %.15s<br>test: %.31s<br>state: %s<br>ip: %s</p>"
             "<a href='/api/status'>/api/status</a>"
             "<a href='/api/config'>/api/config</a>"
             "<a href='/api/files'>/api/files</a>"
             "<a href='/api/start'>/api/start</a>"
             "<a href='/api/start-offline'>/api/start-offline</a>"
             "<a href='/api/stop'>/api/stop</a>",
             ctx->config.node_id, ctx->config.test_id,
             node_state_to_string(node_state_get()), ip ? ip : "?");
    httpd_resp_sendstr_chunk(req, buf);
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    app_context_t *ctx = app_context_get();
    node_state_t state = node_state_get();
    const app_config_t *cfg = (state == NODE_RECORDING || state == NODE_FLUSHING)
                                  ? &ctx->run_config
                                  : &ctx->config;
    const char *ip = wifi_manager_ip();
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);

    char buf[1536];
    snprintf(buf, sizeof(buf),
             "{"
             "\"type\":\"displacement_node\","
             "\"node_id\":\"%.15s\","
             "\"test_id\":\"%.31s\","
             "\"ip\":\"%s\","
             "\"state\":\"%s\","
             "\"firmware\":\"%s\","
             "\"frame_width\":%u,"
             "\"frame_height\":%u,"
             "\"frame_rate_hz\":%u,"
             "\"duration_s\":%" PRIu32 ","
             "\"batch_frames\":%u,"
             "\"pixel_format\":%d,"
             "\"wifi_enabled\":%s,"
             "\"sd_mounted\":%s,"
             "\"stop_requested\":%s,"
             "\"csv_path\":\"%.127s\","
             "\"unix_time_s\":%lld"
             "}",
             cfg->node_id, cfg->test_id, ip ? ip : "",
             node_state_to_string(state), FIRMWARE_VERSION,
             cfg->frame_width, cfg->frame_height, cfg->frame_rate_hz,
             cfg->duration_s, cfg->batch_frames, (int)cfg->pixel_format,
             wifi_manager_is_connected() ? "true" : "false",
             sdcard_is_mounted() ? "true" : "false",
             ctx->stop_requested ? "true" : "false",
             cfg->output_path, (long long)tv.tv_sec);
    send_cors_json(req);
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_get_config(httpd_req_t *req)
{
    app_context_t *ctx = app_context_get();
    const app_config_t *cfg = &ctx->config;
    send_cors_json(req);
    httpd_resp_sendstr_chunk(req, "{");

    char buf[768];
    snprintf(buf, sizeof(buf),
             "\"type\":\"displacement_node\","
             "\"node_id\":\"%.15s\","
             "\"test_id\":\"%.31s\","
             "\"frame_width\":%u,"
             "\"frame_height\":%u,"
             "\"frame_rate_hz\":%u,"
             "\"duration_s\":%" PRIu32 ","
             "\"batch_frames\":%u,"
             "\"output_path\":\"%.127s\","
             "\"roi\":[",
             cfg->node_id, cfg->test_id, cfg->frame_width, cfg->frame_height,
             cfg->frame_rate_hz, cfg->duration_s, cfg->batch_frames,
             cfg->output_path);
    httpd_resp_sendstr_chunk(req, buf);

    for (int i = 0; i < APP_TARGET_COUNT; i++) {
        const roi_config_t *r = &cfg->roi[i];
        snprintf(buf, sizeof(buf),
                 "%s{\"enabled\":%s,\"x1\":%u,\"y1\":%u,\"x2\":%u,\"y2\":%u,"
                 "\"threshold\":%u,\"threshold_ratio_percent\":%u,"
                 "\"min_pixels\":%u,\"max_pixels\":%u,\"polarity\":%d}",
                 i ? "," : "", r->enabled ? "true" : "false",
                 r->x1, r->y1, r->x2, r->y2,
                 r->threshold, r->threshold_ratio_percent,
                 r->min_pixels, r->max_pixels, (int)r->polarity);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static void copy_json_string(cJSON *root, const char *key, char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring && dst_size > 0) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

static esp_err_t handle_post_config(httpd_req_t *req)
{
    node_state_t state = node_state_get();
    if (state == NODE_RECORDING || state == NODE_FLUSHING) {
        send_cors_json(req);
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"recording\"}");
        return ESP_OK;
    }
    if (req->content_len <= 0 || req->content_len >= 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }

    char body[2048];
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    body[n] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    app_context_t *ctx = app_context_get();
    app_config_t next = ctx->config;
    copy_json_string(root, "node_id", next.node_id, sizeof(next.node_id));
    copy_json_string(root, "test_id", next.test_id, sizeof(next.test_id));

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "duration_s");
    if (cJSON_IsNumber(item) && item->valuedouble >= 1 && item->valuedouble <= 86400) {
        next.duration_s = (uint32_t)item->valuedouble;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "batch_frames");
    if (cJSON_IsNumber(item) && item->valuedouble >= 1 && item->valuedouble <= APP_MAX_BATCH_FRAMES) {
        next.batch_frames = (uint16_t)item->valuedouble;
    }

    cJSON *roi = cJSON_GetObjectItemCaseSensitive(root, "roi");
    if (cJSON_IsArray(roi)) {
        int count = cJSON_GetArraySize(roi);
        if (count > APP_TARGET_COUNT) {
            count = APP_TARGET_COUNT;
        }
        for (int i = 0; i < count; i++) {
            cJSON *jr = cJSON_GetArrayItem(roi, i);
            if (!cJSON_IsObject(jr)) {
                continue;
            }
            roi_config_t *r = &next.roi[i];
            item = cJSON_GetObjectItemCaseSensitive(jr, "enabled");
            if (cJSON_IsBool(item)) r->enabled = cJSON_IsTrue(item);
            item = cJSON_GetObjectItemCaseSensitive(jr, "x1");
            if (cJSON_IsNumber(item)) r->x1 = (uint16_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "y1");
            if (cJSON_IsNumber(item)) r->y1 = (uint16_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "x2");
            if (cJSON_IsNumber(item)) r->x2 = (uint16_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "y2");
            if (cJSON_IsNumber(item)) r->y2 = (uint16_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "threshold");
            if (cJSON_IsNumber(item)) r->threshold = (uint8_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "threshold_ratio_percent");
            if (cJSON_IsNumber(item)) r->threshold_ratio_percent = (uint8_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "min_pixels");
            if (cJSON_IsNumber(item)) r->min_pixels = (uint16_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "max_pixels");
            if (cJSON_IsNumber(item)) r->max_pixels = (uint16_t)item->valuedouble;
            item = cJSON_GetObjectItemCaseSensitive(jr, "polarity");
            if (cJSON_IsNumber(item)) r->polarity = (roi_polarity_t)((int)item->valuedouble);
        }
    }

    ctx->config = next;
    cJSON_Delete(root);
    return handle_get_config(req);
}

static esp_err_t trigger_start(httpd_req_t *req, bool offline)
{
    node_state_t state = node_state_get();
    app_context_t *ctx = app_context_get();
    send_cors_json(req);

    if (state != NODE_IDLE) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"triggered\":false,\"state\":\"%s\"}",
                 node_state_to_string(state));
        httpd_resp_sendstr(req, buf);
        return ESP_OK;
    }

    if (offline) {
        s_offline_start = true;
        httpd_resp_sendstr(req, "{\"ok\":true,\"triggered\":true,\"start_in_ms\":4500}");
    } else {
        xSemaphoreGive(ctx->start_trigger_sem);
        httpd_resp_sendstr(req, "{\"state\":\"NODE_IDLE\",\"triggered\":true}");
    }
    return ESP_OK;
}

static esp_err_t handle_start(httpd_req_t *req)
{
    return trigger_start(req, false);
}

static void start_offline_task(void *arg)
{
    (void)arg;
    /* Short delay so the HTTP response is fully transmitted before we tear
     * down the WiFi stack (and with it the HTTP server's TCP sockets). */
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "offline start: stopping HTTP server and WiFi");
    http_server_stop();
    wifi_manager_stop();
    /* Additional settling delay before acquisition starts, so SDIO bus is quiet */
    vTaskDelay(pdMS_TO_TICKS(3000));
    xSemaphoreGive(app_context_get()->start_trigger_sem);
    vTaskDelete(NULL);
}

static esp_err_t handle_start_offline(httpd_req_t *req)
{
    esp_err_t err = trigger_start(req, true);
    if (err == ESP_OK && s_offline_start) {
        xTaskCreate(start_offline_task, "offline_start", 3072, NULL, 1, NULL);
    }
    return err;
}

static esp_err_t handle_stop(httpd_req_t *req)
{
    node_state_t state = node_state_get();
    app_context_t *ctx = app_context_get();
    bool ok = false;
    if (state == NODE_RECORDING || state == NODE_FLUSHING) {
        ctx->stop_requested = true;
        ok = true;
    }
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"triggered\":%s,\"state\":\"%s\"}",
             ok ? "true" : "false", node_state_to_string(state));
    send_cors_json(req);
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_files(httpd_req_t *req)
{
    app_context_t *ctx = app_context_get();
    send_cors_json(req);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"node_id\":\"%.15s\",\"files\":[", ctx->config.node_id);
    httpd_resp_sendstr_chunk(req, buf);

    DIR *dir = opendir("/sdcard");
    if (dir) {
        struct dirent *ent;
        bool first = true;
        while ((ent = readdir(dir)) != NULL) {
            if (!file_name_is_downloadable(ent->d_name)) {
                continue;
            }
            char path[320];
            struct stat st = {0};
            snprintf(path, sizeof(path), "/sdcard/%s", ent->d_name);
            if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
                continue;
            }
            snprintf(buf, sizeof(buf),
                     "%s{\"name\":\"%.63s\",\"size_bytes\":%ld}",
                     first ? "" : ",", ent->d_name, (long)st.st_size);
            httpd_resp_sendstr_chunk(req, buf);
            first = false;
        }
        closedir(dir);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t handle_download(httpd_req_t *req)
{
    char query[128] = {0};
    char name[80] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", name, sizeof(name)) != ESP_OK ||
        !file_name_is_downloadable(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid file");
        return ESP_FAIL;
    }

    char path[128];
    snprintf(path, sizeof(path), "/sdcard/%s", name);
    struct stat st = {0};
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "open failed");
        return ESP_FAIL;
    }

    char disp[128];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", name);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_type(req, file_content_type(name));

    char *chunk = malloc(DOWNLOAD_CHUNK_SIZE);
    if (!chunk) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    size_t n = 0;
    while ((n = fread(chunk, 1, DOWNLOAD_CHUNK_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }
    free(chunk);
    fclose(f);
    if (ret == ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0);
    }
    return ret;
}

static esp_err_t handle_files_clear(httpd_req_t *req)
{
    node_state_t state = node_state_get();
    if (state == NODE_RECORDING || state == NODE_FLUSHING) {
        send_cors_json(req);
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"recording\"}");
        return ESP_OK;
    }

    int deleted = 0;
    int failed = 0;
    DIR *dir = opendir("/sdcard");
    if (dir) {
        char names[64][80];
        int count = 0;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && count < 64) {
            if (file_name_is_downloadable(ent->d_name)) {
                snprintf(names[count], sizeof(names[count]), "%.79s", ent->d_name);
                count++;
            }
        }
        closedir(dir);

        for (int i = 0; i < count; i++) {
            char path[128];
            snprintf(path, sizeof(path), "/sdcard/%.79s", names[i]);
            if (remove(path) == 0) {
                deleted++;
            } else {
                failed++;
            }
        }
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"deleted\":%d,\"failed\":%d}", deleted, failed);
    send_cors_json(req);
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_time(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= 128) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    char body[128];
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    body[n] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    cJSON *j_ms = cJSON_GetObjectItemCaseSensitive(root, "unix_ms");
    if (!cJSON_IsNumber(j_ms)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unix_ms required");
        return ESP_FAIL;
    }

    int64_t unix_ms = (int64_t)j_ms->valuedouble;
    struct timeval tv = {
        .tv_sec = (time_t)(unix_ms / 1000),
        .tv_usec = (suseconds_t)((unix_ms % 1000) * 1000),
    };
    settimeofday(&tv, NULL);
    cJSON_Delete(root);
    send_cors_json(req);
    httpd_resp_sendstr(req, "{\"ok\":true,\"time_sync_ok\":true}");
    return ESP_OK;
}

esp_err_t http_api_register(void *server)
{
    httpd_handle_t srv = (httpd_handle_t)server;
    static const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = handle_root },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handle_status },
        { .uri = "/api/config", .method = HTTP_GET, .handler = handle_get_config },
        { .uri = "/api/config", .method = HTTP_POST, .handler = handle_post_config },
        { .uri = "/api/start", .method = HTTP_GET, .handler = handle_start },
        { .uri = "/api/start-offline", .method = HTTP_GET, .handler = handle_start_offline },
        { .uri = "/api/stop", .method = HTTP_GET, .handler = handle_stop },
        { .uri = "/api/files", .method = HTTP_GET, .handler = handle_files },
        { .uri = "/api/files/clear", .method = HTTP_POST, .handler = handle_files_clear },
        { .uri = "/api/time", .method = HTTP_POST, .handler = handle_time },
        { .uri = "/download", .method = HTTP_GET, .handler = handle_download },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(srv, &routes[i]),
                            TAG, "register route failed");
    }
    return ESP_OK;
}
