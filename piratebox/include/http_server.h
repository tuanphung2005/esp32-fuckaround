#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the HTTP server with captive portal handling and PirateBox REST APIs.
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the HTTP server.
 */
void web_server_stop(void);

#ifdef __cplusplus
}
#endif
