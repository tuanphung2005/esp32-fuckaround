#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the DNS spoofing server FreeRTOS task.
 *        Listens on UDP port 53 and resolves all domain names to 192.168.4.1.
 */
esp_err_t dns_server_start(void);

/**
 * @brief Stop the DNS spoofing server.
 */
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
