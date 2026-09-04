#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "spiffs_storage.h"
#include "wifi_ap.h"
#include "dns_server.h"
#include "http_server.h"
#include "led_indicator.h"

static const char *TAG = "piratebox_main";

static void print_banner(void) {
    printf("\n");
    printf("==============================================================\n");
    printf("        ____  _           __       ____             \n");
    printf("       / __ \\(_)________ _/ /____  / __ )____  _  __\n");
    printf("      / /_/ / / ___/ __ `/ __/ _ \\/ __  / __ \\| |/_/\n");
    printf("     / ____/ / /  / /_/ / /_/  __/ /_/ / /_/ />  <  \n");
    printf("    /_/   /_/_/   \\__,_/\\__/\\___/_____/\\____/_/|_|  \n");
    printf("                                                    \n");
    printf("    ESP32 Standalone Offline Captive Portal / Dead Drop\n");
    printf("==============================================================\n");
    printf("  [*] Wi-Fi SSID:       \"%s\"\n", PIRATEBOX_SSID);
    printf("  [*] Security:         Open (No password required)\n");
    printf("  [*] Web Portal IP:    http://%s/\n", PIRATEBOX_AP_IP);
    printf("  [*] DNS Spoofing:     UDP Port 53 -> Active\n");
    printf("  [*] Filesystem:       SPIFFS Mounted @ /spiffs\n");
    printf("==============================================================\n\n");
}

void app_main(void) {
    // 1. Initialize hardware activity LED indicator on GPIO 2
    ESP_ERROR_CHECK(led_indicator_init());

    // 2. Initialize NVS Flash (required for Wi-Fi stack)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing and reinitializing NVS partition...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Mount internal flash SPIFFS filesystem
    ESP_ERROR_CHECK(spiffs_storage_init());

    // 3. Initialize Wi-Fi in SoftAP mode
    ESP_ERROR_CHECK(wifi_ap_init());

    // 4. Start DNS spoofing server task (redirects all domains to 192.168.4.1)
    ESP_ERROR_CHECK(dns_server_start());

    // 5. Start HTTP server with captive portal handling and REST APIs
    ESP_ERROR_CHECK(web_server_start());

    // Print welcome banner
    print_banner();

    ESP_LOGI(TAG, "PirateBox is fully online and ready for incoming connections!");
}