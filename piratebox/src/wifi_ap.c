#include "wifi_ap.h"
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_ap";
static uint32_t s_connected_stations = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        s_connected_stations++;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d, total connected=%lu",
                 MAC2STR(event->mac), event->aid, (unsigned long)s_connected_stations);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        if (s_connected_stations > 0) s_connected_stations--;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d, remaining connected=%lu",
                 MAC2STR(event->mac), event->aid, (unsigned long)s_connected_stations);
    }
}

esp_err_t wifi_ap_init(void) {
    ESP_LOGI(TAG, "Configuring Wi-Fi SoftAP...");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi AP netif");
        return ESP_FAIL;
    }

    // Configure static IP for the Access Point
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    ip_info.ip.addr = esp_ip4addr_aton(PIRATEBOX_AP_IP);
    ip_info.gw.addr = esp_ip4addr_aton(PIRATEBOX_AP_IP);
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = PIRATEBOX_SSID,
            .ssid_len = strlen(PIRATEBOX_SSID),
            .channel = PIRATEBOX_CHANNEL,
            .password = "",
            .max_connection = PIRATEBOX_MAX_STA,
            .authmode = WIFI_AUTH_OPEN,
            .beacon_interval = 100,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP initialized successfully!");
    ESP_LOGI(TAG, "  SSID:     %s", PIRATEBOX_SSID);
    ESP_LOGI(TAG, "  Channel:  %d", PIRATEBOX_CHANNEL);
    ESP_LOGI(TAG, "  IP:       %s", PIRATEBOX_AP_IP);
    ESP_LOGI(TAG, "  Security: Open (Captive Portal enabled)");

    return ESP_OK;
}

uint32_t wifi_ap_get_station_count(void) {
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        return (uint32_t)sta_list.num;
    }
    return s_connected_stations;
}

char *wifi_ap_get_stations_json(void) {
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) != ESP_OK || sta_list.num == 0) {
        char *empty = (char *)malloc(3);
        if (empty) strcpy(empty, "[]");
        return empty;
    }

    size_t cap = 512;
    char *json = (char *)malloc(cap);
    if (!json) return NULL;
    strcpy(json, "[");

    for (int i = 0; i < sta_list.num; i++) {
        char item[96];
        int8_t rssi = sta_list.sta[i].rssi;
        const char *tier = (rssi >= -50) ? "Immediate (<1m)" :
                           (rssi >= -65) ? "Close (1-3m)" :
                           (rssi >= -80) ? "Medium (3-10m)" : "Far (>10m)";

        snprintf(item, sizeof(item), "%s{\"mac\":\""MACSTR"\",\"rssi\":%d,\"tier\":\"%s\"}",
                 (i > 0) ? "," : "",
                 MAC2STR(sta_list.sta[i].mac),
                 (int)rssi,
                 tier);

        if (strlen(json) + strlen(item) + 2 >= cap) {
            cap *= 2;
            json = (char *)realloc(json, cap);
        }
        strcat(json, item);
    }
    strcat(json, "]");
    return json;
}
