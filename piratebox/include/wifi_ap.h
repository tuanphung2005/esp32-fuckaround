#pragma once

#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIRATEBOX_SSID          "PirateBox - Offline Network"
#define PIRATEBOX_CHANNEL       1
#define PIRATEBOX_MAX_STA       10
#define PIRATEBOX_AP_IP         "192.168.4.1"

/**
 * @brief Initialize ESP32 Wi-Fi in SoftAP mode with DHCP server.
 */
esp_err_t wifi_ap_init(void);

/**
 * @brief Get the count of currently connected Wi-Fi stations.
 */
uint32_t wifi_ap_get_station_count(void);

/**
 * @brief Get a JSON array string describing all connected stations with MAC and live baseband RSSI.
 *        Caller must free() the returned pointer.
 */
char *wifi_ap_get_stations_json(void);

#ifdef __cplusplus
}
#endif
