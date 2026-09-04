#include "spiffs_storage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

static const char *TAG = "spiffs_storage";

esp_err_t spiffs_storage_init(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = "storage",
        .max_files = 10,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition 'storage'");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted successfully! Total: %u KB, Used: %u KB, Free: %u KB",
                 (unsigned)(total / 1024), (unsigned)(used / 1024), (unsigned)((total - used) / 1024));
    }

    // Ensure base directory paths exist
    struct stat st;
    if (stat(SPIFFS_FILES_PATH, &st) != 0) {
        mkdir(SPIFFS_FILES_PATH, 0755);
    }
    if (stat(SPIFFS_DROPS_PATH, &st) != 0) {
        mkdir(SPIFFS_DROPS_PATH, 0755);
    }

    // Create a default welcome text drop if drops is empty
    char *drops_json = spiffs_get_drops_json();
    if (drops_json == NULL || strcmp(drops_json, "[]") == 0) {
        spiffs_add_drop("System Notice", "system", "00:00",
            "PirateBox offline local node.\n\n"
            "- Chat: Broadcast messages across connected peers\n"
            "- Notes: Store persistent text notes\n"
            "- Files: HTTP file transfers directly to SPIFFS flash",
            false);
    }
    if (drops_json) free(drops_json);

    return ESP_OK;
}

esp_err_t spiffs_get_stats(size_t *total_bytes, size_t *used_bytes, size_t *free_bytes) {
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        return ret;
    }
    if (total_bytes) *total_bytes = total;
    if (used_bytes) *used_bytes = used;
    if (free_bytes) *free_bytes = (total > used) ? (total - used) : 0;
    return ESP_OK;
}

bool spiffs_sanitize_filename(const char *input, char *output, size_t max_len) {
    if (!input || !output || max_len < 2) return false;
    
    // Strip leading path slashes or directory navigation
    const char *p = input;
    while (*p == '/' || *p == '\\' || *p == '.') p++;

    size_t out_idx = 0;
    while (*p && out_idx < max_len - 1) {
        char c = *p++;
        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || c == ' ') {
            output[out_idx++] = c;
        } else {
            output[out_idx++] = '_';
        }
    }
    output[out_idx] = '\0';
    return (out_idx > 0);
}

// JSON string escaping helper
static void append_json_escaped(char **buf, size_t *cap, size_t *len, const char *str) {
    if (!str) return;
    for (const char *p = str; *p; p++) {
        const char *esc = NULL;
        char tmp[7];
        switch (*p) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)*p);
                    esc = tmp;
                }
                break;
        }

        const char *insert = esc ? esc : (char[]){*p, '\0'};
        size_t ins_len = strlen(insert);
        if (*len + ins_len + 1 >= *cap) {
            *cap = (*cap * 2) + ins_len + 64;
            *buf = (char *)realloc(*buf, *cap);
        }
        memcpy(*buf + *len, insert, ins_len);
        *len += ins_len;
        (*buf)[*len] = '\0';
    }
}

static void append_raw(char **buf, size_t *cap, size_t *len, const char *str) {
    if (!str) return;
    size_t ins_len = strlen(str);
    if (*len + ins_len + 1 >= *cap) {
        *cap = (*cap * 2) + ins_len + 64;
        *buf = (char *)realloc(*buf, *cap);
    }
    memcpy(*buf + *len, str, ins_len);
    *len += ins_len;
    (*buf)[*len] = '\0';
}

esp_err_t spiffs_add_chat_message(const char *nick, const char *msg, const char *time_str) {
    if (!nick || !msg) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(SPIFFS_CHAT_LOG, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open chat log for writing");
        return ESP_FAIL;
    }

    uint32_t msg_id = (uint32_t)(esp_timer_get_time() / 1000);
    const char *t = (time_str && strlen(time_str) > 0) ? time_str : "--:--";

    // Clean any pipes or newlines from fields
    char clean_nick[32];
    char clean_msg[256];
    size_t ni = 0, mi = 0;

    for (const char *p = nick; *p && ni < sizeof(clean_nick) - 1; p++) {
        if (*p != '|' && *p != '\n' && *p != '\r') clean_nick[ni++] = *p;
    }
    clean_nick[ni] = '\0';
    if (ni == 0) strcpy(clean_nick, "Anonymous");

    for (const char *p = msg; *p && mi < sizeof(clean_msg) - 1; p++) {
        if (*p != '|' && *p != '\n' && *p != '\r') clean_msg[mi++] = *p;
    }
    clean_msg[mi] = '\0';

    fprintf(f, "%lu|%s|%s|%s\n", (unsigned long)msg_id, t, clean_nick, clean_msg);
    fclose(f);
    return ESP_OK;
}

char *spiffs_get_chat_json(void) {
    FILE *f = fopen(SPIFFS_CHAT_LOG, "r");
    if (!f) {
        char *empty = (char *)malloc(3);
        if (empty) strcpy(empty, "[]");
        return empty;
    }

    size_t cap = 1024;
    size_t len = 0;
    char *json = (char *)malloc(cap);
    if (!json) {
        fclose(f);
        return NULL;
    }
    json[0] = '[';
    json[1] = '\0';
    len = 1;

    char line[512];
    bool first = true;

    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue;

        char *token_id = strtok(line, "|");
        char *token_time = strtok(NULL, "|");
        char *token_nick = strtok(NULL, "|");
        char *token_msg = strtok(NULL, "|");

        if (!token_id || !token_time || !token_nick || !token_msg) continue;

        if (!first) {
            append_raw(&json, &cap, &len, ",");
        }
        first = false;

        append_raw(&json, &cap, &len, "{\"id\":");
        append_raw(&json, &cap, &len, token_id);
        append_raw(&json, &cap, &len, ",\"time\":\"");
        append_json_escaped(&json, &cap, &len, token_time);
        append_raw(&json, &cap, &len, "\",\"nick\":\"");
        append_json_escaped(&json, &cap, &len, token_nick);
        append_raw(&json, &cap, &len, "\",\"msg\":\"");
        append_json_escaped(&json, &cap, &len, token_msg);
        append_raw(&json, &cap, &len, "\"}");
    }

    fclose(f);
    append_raw(&json, &cap, &len, "]");
    return json;
}

esp_err_t spiffs_clear_chat(void) {
    unlink(SPIFFS_CHAT_LOG);
    return ESP_OK;
}

esp_err_t spiffs_add_drop(const char *title, const char *nick, const char *time_str, const char *content, bool burn_after_read) {
    if (!content || strlen(content) == 0) return ESP_ERR_INVALID_ARG;

    uint32_t drop_id = (uint32_t)(esp_timer_get_time() / 1000);
    char path[96];
    snprintf(path, sizeof(path), "%s/%lu.%s", SPIFFS_DROPS_PATH, (unsigned long)drop_id, burn_after_read ? "burn" : "drop");

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create drop file: %s", path);
        return ESP_FAIL;
    }

    const char *t = (title && strlen(title) > 0) ? title : "Untitled";
    const char *n = (nick && strlen(nick) > 0) ? nick : "anonymous";
    const char *tm = (time_str && strlen(time_str) > 0) ? time_str : "--:--";

    fprintf(f, "%s\n", t);
    fprintf(f, "%s\n", n);
    fprintf(f, "%s\n", tm);
    fputs(content, f);
    fclose(f);

    ESP_LOGI(TAG, "Created drop %lu (%s) [burn=%d]", (unsigned long)drop_id, t, burn_after_read);
    return ESP_OK;
}

char *spiffs_get_drop_and_burn_if_needed(const char *id_str) {
    if (!id_str || strlen(id_str) == 0) return NULL;

    char path[320];
    bool is_burn = false;

    snprintf(path, sizeof(path), "%s/%s.burn", SPIFFS_DROPS_PATH, id_str);
    FILE *f = fopen(path, "r");
    if (f) {
        is_burn = true;
    } else {
        snprintf(path, sizeof(path), "%s/%s.drop", SPIFFS_DROPS_PATH, id_str);
        f = fopen(path, "r");
    }

    if (!f) return NULL;

    char title[64] = "Untitled";
    char nick[32] = "anonymous";
    char time_str[32] = "--:--";

    if (fgets(title, sizeof(title), f)) title[strcspn(title, "\r\n")] = 0;
    if (fgets(nick, sizeof(nick), f)) nick[strcspn(nick, "\r\n")] = 0;
    if (fgets(time_str, sizeof(time_str), f)) time_str[strcspn(time_str, "\r\n")] = 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char dummy[128];
    fgets(dummy, sizeof(dummy), f);
    fgets(dummy, sizeof(dummy), f);
    fgets(dummy, sizeof(dummy), f);

    long body_pos = ftell(f);
    long body_len = (fsize > body_pos) ? (fsize - body_pos) : 0;
    char *body = (char *)malloc(body_len + 1);
    if (body) {
        size_t rd = fread(body, 1, body_len, f);
        body[rd] = '\0';
    }
    fclose(f);

    if (is_burn) {
        unlink(path);
        ESP_LOGI(TAG, "Burned drop after reading: %s", path);
    }

    size_t cap = body_len + 256;
    size_t len = 0;
    char *json = (char *)malloc(cap);
    if (!json) {
        if (body) free(body);
        return NULL;
    }

    json[0] = '\0';
    append_raw(&json, &cap, &len, "{\"id\":\"");
    append_raw(&json, &cap, &len, id_str);
    append_raw(&json, &cap, &len, "\",\"title\":\"");
    append_json_escaped(&json, &cap, &len, title);
    append_raw(&json, &cap, &len, "\",\"nick\":\"");
    append_json_escaped(&json, &cap, &len, nick);
    append_raw(&json, &cap, &len, "\",\"time\":\"");
    append_json_escaped(&json, &cap, &len, time_str);
    append_raw(&json, &cap, &len, "\",\"burn\":");
    append_raw(&json, &cap, &len, is_burn ? "true" : "false");
    append_raw(&json, &cap, &len, ",\"content\":\"");
    if (body) {
        append_json_escaped(&json, &cap, &len, body);
        free(body);
    }
    append_raw(&json, &cap, &len, "\"}");
    return json;
}

char *spiffs_get_drops_json(void) {
    size_t cap = 2048;
    size_t len = 0;
    char *json = (char *)malloc(cap);
    if (!json) return NULL;
    json[0] = '[';
    json[1] = '\0';
    len = 1;

    DIR *d = opendir(SPIFFS_BASE_PATH);
    if (!d) {
        append_raw(&json, &cap, &len, "]");
        return json;
    }

    struct dirent *entry;
    bool first = true;

    while ((entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;
        const char *burn_ext = strstr(name, ".burn");
        const char *drop_ext = strstr(name, ".drop");
        if (!burn_ext && !drop_ext) continue;

        bool is_burn = (burn_ext != NULL);
        char filepath[320];
        if (strncmp(name, "drops/", 6) == 0 || strncmp(name, "/drops/", 7) == 0) {
            snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_BASE_PATH, name);
        } else {
            snprintf(filepath, sizeof(filepath), "%s/%s", SPIFFS_DROPS_PATH, name);
        }

        FILE *f = fopen(filepath, "r");
        if (!f) continue;

        char title[64] = "Untitled";
        char nick[32] = "anonymous";
        char time_str[32] = "--:--";

        if (fgets(title, sizeof(title), f)) title[strcspn(title, "\r\n")] = 0;
        if (fgets(nick, sizeof(nick), f)) nick[strcspn(nick, "\r\n")] = 0;
        if (fgets(time_str, sizeof(time_str), f)) time_str[strcspn(time_str, "\r\n")] = 0;

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        char dummy[128];
        fgets(dummy, sizeof(dummy), f);
        fgets(dummy, sizeof(dummy), f);
        fgets(dummy, sizeof(dummy), f);

        long body_pos = ftell(f);
        long body_len = (fsize > body_pos) ? (fsize - body_pos) : 0;
        if (body_len > 2048) body_len = 2048;

        char *body = (char *)malloc(body_len + 1);
        if (body) {
            size_t rd = fread(body, 1, body_len, f);
            body[rd] = '\0';
        }
        fclose(f);

        char id_str[32] = {0};
        const char *slash = strrchr(name, '/');
        const char *base = slash ? slash + 1 : name;
        size_t ext_idx = strcspn(base, ".");
        if (ext_idx > 0 && ext_idx < sizeof(id_str)) {
            strncpy(id_str, base, ext_idx);
            id_str[ext_idx] = '\0';
        }

        if (!first) append_raw(&json, &cap, &len, ",");
        first = false;

        append_raw(&json, &cap, &len, "{\"id\":\"");
        append_raw(&json, &cap, &len, id_str);
        append_raw(&json, &cap, &len, "\",\"title\":\"");
        append_json_escaped(&json, &cap, &len, title);
        append_raw(&json, &cap, &len, "\",\"nick\":\"");
        append_json_escaped(&json, &cap, &len, nick);
        append_raw(&json, &cap, &len, "\",\"time\":\"");
        append_json_escaped(&json, &cap, &len, time_str);
        append_raw(&json, &cap, &len, "\",\"burn\":");
        append_raw(&json, &cap, &len, is_burn ? "true" : "false");
        append_raw(&json, &cap, &len, ",\"content\":\"");
        if (body) {
            append_json_escaped(&json, &cap, &len, body);
            free(body);
        }
        append_raw(&json, &cap, &len, "\"}");
    }

    closedir(d);
    append_raw(&json, &cap, &len, "]");
    return json;
}

esp_err_t spiffs_delete_drop(const char *id_str) {
    if (!id_str || strlen(id_str) == 0) return ESP_ERR_INVALID_ARG;

    char path[320];
    snprintf(path, sizeof(path), "%s/%s.drop", SPIFFS_DROPS_PATH, id_str);
    if (unlink(path) == 0) return ESP_OK;

    snprintf(path, sizeof(path), "%s/%s.burn", SPIFFS_DROPS_PATH, id_str);
    if (unlink(path) == 0) return ESP_OK;

    snprintf(path, sizeof(path), "%s/drops/%s.drop", SPIFFS_BASE_PATH, id_str);
    if (unlink(path) == 0) return ESP_OK;

    snprintf(path, sizeof(path), "%s/drops/%s.burn", SPIFFS_BASE_PATH, id_str);
    if (unlink(path) == 0) return ESP_OK;

    return ESP_ERR_NOT_FOUND;
}

char *spiffs_get_files_json(void) {
    size_t cap = 1024;
    size_t len = 0;
    char *json = (char *)malloc(cap);
    if (!json) return NULL;
    json[0] = '[';
    json[1] = '\0';
    len = 1;

    DIR *d = opendir(SPIFFS_BASE_PATH);
    if (!d) {
        append_raw(&json, &cap, &len, "]");
        return json;
    }

    struct dirent *entry;
    bool first = true;

    while ((entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;

        // Skip drops, chat log, and internal files
        if (strstr(name, ".drop") || strstr(name, "chat.log")) continue;

        // Identify file inside files/ or check if prefix is files/
        const char *display_name = name;
        if (strncmp(name, "files/", 6) == 0) {
            display_name = name + 6;
        } else if (strncmp(name, "/files/", 7) == 0) {
            display_name = name + 7;
        } else {
            // Ignore other root files
            continue;
        }

        if (strlen(display_name) == 0) continue;

        char fullpath[320];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", SPIFFS_BASE_PATH, name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        if (!first) append_raw(&json, &cap, &len, ",");
        first = false;

        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%ld", (long)st.st_size);

        append_raw(&json, &cap, &len, "{\"name\":\"");
        append_json_escaped(&json, &cap, &len, display_name);
        append_raw(&json, &cap, &len, "\",\"size\":");
        append_raw(&json, &cap, &len, size_buf);
        append_raw(&json, &cap, &len, "}");
    }

    closedir(d);
    append_raw(&json, &cap, &len, "]");
    return json;
}

esp_err_t spiffs_delete_file(const char *filename) {
    if (!filename || strlen(filename) == 0) return ESP_ERR_INVALID_ARG;

    char clean_name[64];
    if (!spiffs_sanitize_filename(filename, clean_name, sizeof(clean_name))) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[320];
    snprintf(path, sizeof(path), "%s/%s", SPIFFS_FILES_PATH, clean_name);
    if (unlink(path) == 0) {
        ESP_LOGI(TAG, "Deleted file: %s", path);
        return ESP_OK;
    }

    // Try without files/ prefix
    snprintf(path, sizeof(path), "%s/files/%s", SPIFFS_BASE_PATH, clean_name);
    if (unlink(path) == 0) return ESP_OK;

    return ESP_ERR_NOT_FOUND;
}
