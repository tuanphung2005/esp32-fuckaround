#pragma once

#include <esp_err.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPIFFS_BASE_PATH      "/spiffs"
#define SPIFFS_FILES_PATH     "/spiffs/files"
#define SPIFFS_DROPS_PATH     "/spiffs/drops"
#define SPIFFS_CHAT_LOG       "/spiffs/chat.log"

/**
 * @brief Initialize and mount SPIFFS filesystem.
 *        Creates needed directories if they do not exist.
 */
esp_err_t spiffs_storage_init(void);

/**
 * @brief Get filesystem capacity and usage in bytes.
 */
esp_err_t spiffs_get_stats(size_t *total_bytes, size_t *used_bytes, size_t *free_bytes);

/**
 * @brief Sanitize a filename to prevent path traversal or invalid characters.
 */
bool spiffs_sanitize_filename(const char *input, char *output, size_t max_len);

/**
 * @brief Append a chat message to the persistent chat log.
 */
esp_err_t spiffs_add_chat_message(const char *nick, const char *msg, const char *time_str);

/**
 * @brief Read all chat messages and return as a heap-allocated JSON string array.
 *        Caller is responsible for calling free() on the returned pointer.
 */
char *spiffs_get_chat_json(void);

/**
 * @brief Clear all chat messages.
 */
esp_err_t spiffs_clear_chat(void);

/**
 * @brief Create a new text drop (note/pastebin).
 */
esp_err_t spiffs_add_drop(const char *title, const char *nick, const char *time_str, const char *content, bool burn_after_read);

/**
 * @brief Read a single drop by ID. If marked as burn-after-reading, it is deleted from SPIFFS immediately.
 *        Caller must free() returned string.
 */
char *spiffs_get_drop_and_burn_if_needed(const char *id_str);

/**
 * @brief Read all text drops and return as a heap-allocated JSON string array.
 *        Caller is responsible for calling free() on the returned pointer.
 */
char *spiffs_get_drops_json(void);

/**
 * @brief Delete a text drop by its filename/ID.
 */
esp_err_t spiffs_delete_drop(const char *id_str);

/**
 * @brief List files in the file sharing directory and return as a heap-allocated JSON array.
 *        Caller is responsible for calling free() on the returned pointer.
 */
char *spiffs_get_files_json(void);

/**
 * @brief Delete a shared file from SPIFFS.
 */
esp_err_t spiffs_delete_file(const char *filename);

#ifdef __cplusplus
}
#endif
