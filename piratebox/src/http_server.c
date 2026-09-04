#include "http_server.h"
#include "spiffs_storage.h"
#include "wifi_ap.h"
#include "web_assets.h"
#include "led_indicator.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_chip_info.h"

static const char *TAG = "http_server";
static httpd_handle_t s_server = NULL;

// Helper: URL decoding in-place
static void url_decode(char *dst, const char *src, size_t dst_max) {
    size_t d = 0;
    while (*src && d < dst_max - 1) {
        if (*src == '%' && isxdigit((unsigned char)*(src + 1)) && isxdigit((unsigned char)*(src + 2))) {
            char hex[3] = { *(src + 1), *(src + 2), '\0' };
            dst[d++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[d++] = ' ';
            src++;
        } else {
            dst[d++] = *src++;
        }
    }
    dst[d] = '\0';
}

// Helper: Safe JSON value extractor for strings
static bool extract_json_string(const char *json, const char *key, char *out, size_t out_len) {
    if (!json || !key || !out || out_len == 0) return false;

    char search[48];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':') pos++;
    if (*pos != '"') return false;
    pos++; // skip opening quote

    size_t idx = 0;
    while (*pos && idx < out_len - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
                case 'n': out[idx++] = '\n'; break;
                case 'r': out[idx++] = '\r'; break;
                case 't': out[idx++] = '\t'; break;
                case '"': out[idx++] = '"';  break;
                case '\\': out[idx++] = '\\'; break;
                default: out[idx++] = *pos; break;
            }
            pos++;
        } else if (*pos == '"') {
            break;
        } else {
            out[idx++] = *pos++;
        }
    }
    out[idx] = '\0';
    return true;
}

// Helper: Extract JSON boolean
static bool extract_json_bool(const char *json, const char *key) {
    if (!json || !key) return false;
    char search[48];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;
    pos += strlen(search);
    while (*pos == ' ' || *pos == ':') pos++;
    return (strncmp(pos, "true", 4) == 0);
}

// -------------------------------------------------------------
// WebSocket Real-time Broadcast
// -------------------------------------------------------------

static void ws_broadcast_text(const char *text) {
    if (!s_server || !text) return;

    size_t max_clients = 8;
    int client_fds[8];
    if (httpd_get_client_list(s_server, &max_clients, client_fds) == ESP_OK) {
        httpd_ws_frame_t ws_pkt = {
            .payload = (uint8_t *)text,
            .len = strlen(text),
            .type = HTTPD_WS_TYPE_TEXT,
            .final = true
        };
        for (size_t i = 0; i < max_clients; i++) {
            if (httpd_ws_get_fd_info(s_server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_send_frame_async(s_server, client_fds[i], &ws_pkt);
            }
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket client handshake accepted");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0 && ws_pkt.len < 1024) {
        uint8_t *buf = (uint8_t *)malloc(ws_pkt.len + 1);
        if (!buf) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            buf[ws_pkt.len] = '\0';
            led_indicator_activity();

            char nick[32] = "anonymous";
            char msg[256] = "";
            char time_str[16] = "--:--";

            extract_json_string((char *)buf, "nick", nick, sizeof(nick));
            extract_json_string((char *)buf, "msg", msg, sizeof(msg));
            extract_json_string((char *)buf, "time", time_str, sizeof(time_str));

            if (strlen(msg) > 0) {
                spiffs_add_chat_message(nick, msg, time_str);
                ws_broadcast_text((char *)buf);
            }
        }
        free(buf);
    }
    return ret;
}

// -------------------------------------------------------------
// Captive Portal & Web UI Handlers
// -------------------------------------------------------------

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    led_indicator_activity();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
    if (err == HTTPD_404_NOT_FOUND) {
        led_indicator_activity();
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    led_indicator_activity();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// -------------------------------------------------------------
// Chat REST Endpoints
// -------------------------------------------------------------

static esp_err_t api_chat_get_handler(httpd_req_t *req) {
    led_indicator_activity();
    char *json = spiffs_get_chat_json();
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t api_chat_post_handler(httpd_req_t *req) {
    led_indicator_activity();
    char buf[512];
    int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char nick[32] = "anonymous";
    char msg[256] = "";
    char time_str[16] = "--:--";

    extract_json_string(buf, "nick", nick, sizeof(nick));
    extract_json_string(buf, "msg", msg, sizeof(msg));
    extract_json_string(buf, "time", time_str, sizeof(time_str));

    if (strlen(msg) > 0) {
        spiffs_add_chat_message(nick, msg, time_str);
        ws_broadcast_text(buf);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t api_chat_clear_handler(httpd_req_t *req) {
    led_indicator_activity();
    spiffs_clear_chat();
    ws_broadcast_text("{\"type\":\"clear\"}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"cleared\"}");
    return ESP_OK;
}

// -------------------------------------------------------------
// Dead Drop REST Endpoints (With Burn-After-Reading)
// -------------------------------------------------------------

static esp_err_t api_drops_get_handler(httpd_req_t *req) {
    led_indicator_activity();
    char *json = spiffs_get_drops_json();
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t api_drop_single_get_handler(httpd_req_t *req) {
    led_indicator_activity();
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char id[48];
    if (httpd_query_key_value(query, "id", id, sizeof(id)) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char *json = spiffs_get_drop_and_burn_if_needed(id);
    if (!json) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

static esp_err_t api_drops_post_handler(httpd_req_t *req) {
    led_indicator_activity();
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large or empty");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char title[64] = "Untitled";
    char nick[32] = "anonymous";
    char time_str[16] = "--:--";
    bool burn = extract_json_bool(buf, "burn");
    char *content = (char *)malloc(total_len + 1);

    if (content) {
        content[0] = '\0';
        extract_json_string(buf, "title", title, sizeof(title));
        extract_json_string(buf, "nick", nick, sizeof(nick));
        extract_json_string(buf, "time", time_str, sizeof(time_str));
        extract_json_string(buf, "content", content, total_len);

        if (strlen(content) > 0) {
            spiffs_add_drop(title, nick, time_str, content, burn);
        }
        free(content);
    }
    free(buf);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t api_drops_delete_handler(httpd_req_t *req) {
    led_indicator_activity();
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char raw_id[48];
        if (httpd_query_key_value(query, "id", raw_id, sizeof(raw_id)) == ESP_OK) {
            char id[48];
            url_decode(id, raw_id, sizeof(id));
            spiffs_delete_drop(id);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// -------------------------------------------------------------
// File Sharing REST Endpoints (With HTTP 206 Partial Content)
// -------------------------------------------------------------

static esp_err_t api_files_get_handler(httpd_req_t *req) {
    led_indicator_activity();
    char *json = spiffs_get_files_json();
    httpd_resp_set_type(req, "application/json");
    if (!json) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t api_download_handler(httpd_req_t *req) {
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char raw_file[64];
    if (httpd_query_key_value(query, "file", raw_file, sizeof(raw_file)) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char decoded_name[64];
    url_decode(decoded_name, raw_file, sizeof(decoded_name));

    char clean_name[64];
    if (!spiffs_sanitize_filename(decoded_name, clean_name, sizeof(clean_name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    char filepath[320];
    snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_FILES_PATH, clean_name);

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        snprintf(filepath, sizeof(filepath), "%s/files/%s", SPIFFS_BASE_PATH, clean_name);
        f = fopen(filepath, "rb");
    }
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    long start = 0;
    long end = file_size - 1;
    bool is_range = false;

    char range_buf[64];
    if (httpd_req_get_hdr_value_str(req, "Range", range_buf, sizeof(range_buf)) == ESP_OK) {
        long r_start = 0, r_end = 0;
        if (sscanf(range_buf, "bytes=%ld-%ld", &r_start, &r_end) == 2) {
            start = r_start;
            end = MIN(r_end, file_size - 1);
            is_range = true;
        } else if (sscanf(range_buf, "bytes=%ld-", &r_start) == 1) {
            start = r_start;
            end = file_size - 1;
            is_range = true;
        }
    }

    httpd_resp_set_type(req, "application/octet-stream");
    char disposition[128];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", clean_name);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_set_hdr(req, "Accept-Ranges", "bytes");

    if (is_range) {
        httpd_resp_set_status(req, "206 Partial Content");
        char content_range[64];
        snprintf(content_range, sizeof(content_range), "bytes %ld-%ld/%ld", start, end, file_size);
        httpd_resp_set_hdr(req, "Content-Range", content_range);
        fseek(f, start, SEEK_SET);
    }

    long remaining = end - start + 1;
    char chunk[1024];
    while (remaining > 0) {
        size_t to_read = MIN((size_t)remaining, sizeof(chunk));
        size_t bytes_read = fread(chunk, 1, to_read, f);
        if (bytes_read <= 0) break;
        if (httpd_resp_send_chunk(req, chunk, bytes_read) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
        remaining -= bytes_read;
        led_indicator_activity();
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_upload_handler(httpd_req_t *req) {
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file name parameter");
        return ESP_FAIL;
    }

    char raw_name[64];
    if (httpd_query_key_value(query, "name", raw_name, sizeof(raw_name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name parameter");
        return ESP_FAIL;
    }

    char decoded_name[64];
    url_decode(decoded_name, raw_name, sizeof(decoded_name));

    char clean_name[64];
    if (!spiffs_sanitize_filename(decoded_name, clean_name, sizeof(clean_name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file name");
        return ESP_FAIL;
    }

    size_t total = 0, used = 0, free_sp = 0;
    spiffs_get_stats(&total, &used, &free_sp);
    if ((size_t)req->content_len > free_sp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough SPIFFS space");
        return ESP_FAIL;
    }

    char filepath[320];
    snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_FILES_PATH, clean_name);

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        snprintf(filepath, sizeof(filepath), "%s/files/%s", SPIFFS_BASE_PATH, clean_name);
        f = fopen(filepath, "wb");
    }
    if (!f) {
        ESP_LOGE(TAG, "Failed to create file for upload: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = MIN(remaining, sizeof(buf));
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "File upload interrupted: received %d", received);
            fclose(f);
            unlink(filepath);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        fwrite(buf, 1, received, f);
        remaining -= received;
        led_indicator_activity();
    }

    fclose(f);
    ESP_LOGI(TAG, "Successfully uploaded %s (%d bytes)", clean_name, req->content_len);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t api_files_delete_handler(httpd_req_t *req) {
    led_indicator_activity();
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char raw_name[64];
        if (httpd_query_key_value(query, "name", raw_name, sizeof(raw_name)) == ESP_OK) {
            char decoded_name[64];
            url_decode(decoded_name, raw_name, sizeof(decoded_name));
            spiffs_delete_file(decoded_name);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// -------------------------------------------------------------
// System Stats REST Endpoint (With Silicon Telemetry & RSSI Radar)
// -------------------------------------------------------------

static esp_err_t api_stats_get_handler(httpd_req_t *req) {
    led_indicator_activity();
    size_t total = 0, used = 0, free_sp = 0;
    spiffs_get_stats(&total, &used, &free_sp);

    uint32_t clients = wifi_ap_get_station_count();
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    char *stations_json = wifi_ap_get_stations_json();

    char *json = (char *)malloc(1024);
    if (!json) {
        if (stations_json) free(stations_json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(json, 1024,
             "{\"total_bytes\":%u,\"used_bytes\":%u,\"free_bytes\":%u,\"clients\":%lu,\"free_heap\":%lu,\"min_heap\":%lu,\"uptime_sec\":%lu,\"cpu_mhz\":240,\"cores\":%d,\"rev\":%d,\"stations\":%s}",
             (unsigned)total, (unsigned)used, (unsigned)free_sp,
             (unsigned long)clients, (unsigned long)free_heap, (unsigned long)min_heap,
             (unsigned long)uptime, chip_info.cores, chip_info.revision,
             stations_json ? stations_json : "[]");

    if (stations_json) free(stations_json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// -------------------------------------------------------------
// Server Initialization
// -------------------------------------------------------------

esp_err_t web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 28;
    config.stack_size = 10240;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP Web Server on port %d...", config.server_port);

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    // WebSocket Endpoint
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    // Root URI
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &root_uri);

    // Captive Portal Probes
    const char *captive_uris[] = {
        "/hotspot-detect.html",
        "/generate_204",
        "/gen_204",
        "/ncsi.txt",
        "/connecttest.txt",
        "/canonical.html",
        "/success.txt",
        "/portal"
    };
    for (size_t i = 0; i < sizeof(captive_uris) / sizeof(captive_uris[0]); i++) {
        httpd_uri_t probe = {
            .uri = captive_uris[i],
            .method = HTTP_GET,
            .handler = captive_redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_server, &probe);
    }

    // Chat URIs
    httpd_uri_t chat_get = { .uri = "/api/chat", .method = HTTP_GET, .handler = api_chat_get_handler };
    httpd_uri_t chat_post = { .uri = "/api/chat", .method = HTTP_POST, .handler = api_chat_post_handler };
    httpd_uri_t chat_clear = { .uri = "/api/chat/clear", .method = HTTP_POST, .handler = api_chat_clear_handler };
    httpd_register_uri_handler(s_server, &chat_get);
    httpd_register_uri_handler(s_server, &chat_post);
    httpd_register_uri_handler(s_server, &chat_clear);

    // Drops URIs
    httpd_uri_t drops_get = { .uri = "/api/drops", .method = HTTP_GET, .handler = api_drops_get_handler };
    httpd_uri_t drop_single = { .uri = "/api/drop", .method = HTTP_GET, .handler = api_drop_single_get_handler };
    httpd_uri_t drops_post = { .uri = "/api/drops", .method = HTTP_POST, .handler = api_drops_post_handler };
    httpd_uri_t drops_del = { .uri = "/api/drops", .method = HTTP_DELETE, .handler = api_drops_delete_handler };
    httpd_register_uri_handler(s_server, &drops_get);
    httpd_register_uri_handler(s_server, &drop_single);
    httpd_register_uri_handler(s_server, &drops_post);
    httpd_register_uri_handler(s_server, &drops_del);

    // Files URIs
    httpd_uri_t files_get = { .uri = "/api/files", .method = HTTP_GET, .handler = api_files_get_handler };
    httpd_uri_t download_get = { .uri = "/api/download", .method = HTTP_GET, .handler = api_download_handler };
    httpd_uri_t upload_post = { .uri = "/api/upload", .method = HTTP_POST, .handler = api_upload_handler };
    httpd_uri_t files_del = { .uri = "/api/files", .method = HTTP_DELETE, .handler = api_files_delete_handler };
    httpd_register_uri_handler(s_server, &files_get);
    httpd_register_uri_handler(s_server, &download_get);
    httpd_register_uri_handler(s_server, &upload_post);
    httpd_register_uri_handler(s_server, &files_del);

    // Stats URI
    httpd_uri_t stats_get = { .uri = "/api/stats", .method = HTTP_GET, .handler = api_stats_get_handler };
    httpd_register_uri_handler(s_server, &stats_get);

    ESP_LOGI(TAG, "HTTP & WebSocket server started with Silicon Telemetry & Range Download support!");
    return ESP_OK;
}

void web_server_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
