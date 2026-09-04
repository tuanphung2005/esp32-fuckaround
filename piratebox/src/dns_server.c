#include "dns_server.h"
#include "led_indicator.h"
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "dns_server";
static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_sock = -1;
static bool s_running = false;

#define DNS_PORT 53
#define DNS_BUFFER_SIZE 512

static void dns_server_task(void *pvParameters) {
    uint8_t rx_buffer[DNS_BUFFER_SIZE];
    uint8_t tx_buffer[DNS_BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    ESP_LOGI(TAG, "DNS spoofing server task started on UDP port %d", DNS_PORT);

    while (s_running) {
        int len = recvfrom(s_dns_sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &client_len);

        if (len < 12) {
            // Malformed or too short DNS packet
            continue;
        }

        // Parse Question section
        // Starts at offset 12
        int offset = 12;
        while (offset < len) {
            uint8_t label_len = rx_buffer[offset];
            if (label_len == 0) {
                offset += 1;
                break;
            }
            offset += (label_len + 1);
        }

        // Add 4 bytes for QTYPE (2 bytes) and QCLASS (2 bytes)
        offset += 4;

        if (offset > len || offset + 16 > DNS_BUFFER_SIZE) {
            // Invalid packet format
            continue;
        }

        // Prepare response in tx_buffer
        // Copy Query header and question
        memcpy(tx_buffer, rx_buffer, offset);

        // Modify DNS Header for Response
        // ID: tx_buffer[0..1] kept as is
        // Flags: 0x8180 (Standard Query Response, No Error)
        tx_buffer[2] = 0x81;
        tx_buffer[3] = 0x80;
        // QDCOUNT: kept as is (tx_buffer[4..5])
        // ANCOUNT: 1 answer (0x0001)
        tx_buffer[6] = 0x00;
        tx_buffer[7] = 0x01;
        // NSCOUNT: 0
        tx_buffer[8] = 0x00;
        tx_buffer[9] = 0x00;
        // ARCOUNT: 0
        tx_buffer[10] = 0x00;
        tx_buffer[11] = 0x00;

        // Append Answer Record
        // Name: pointer to Question domain at offset 12 (0xC00C)
        tx_buffer[offset++] = 0xC0;
        tx_buffer[offset++] = 0x0C;
        // Type: A (0x0001)
        tx_buffer[offset++] = 0x00;
        tx_buffer[offset++] = 0x01;
        // Class: IN (0x0001)
        tx_buffer[offset++] = 0x00;
        tx_buffer[offset++] = 0x01;
        // TTL: 60 seconds (0x0000003C)
        tx_buffer[offset++] = 0x00;
        tx_buffer[offset++] = 0x00;
        tx_buffer[offset++] = 0x00;
        tx_buffer[offset++] = 0x3C;
        // RDLENGTH: 4 bytes (0x0004)
        tx_buffer[offset++] = 0x00;
        tx_buffer[offset++] = 0x04;
        // RDATA: 192.168.4.1 (ESP32 SoftAP IP)
        tx_buffer[offset++] = 192;
        tx_buffer[offset++] = 168;
        tx_buffer[offset++] = 4;
        tx_buffer[offset++] = 1;

        // Send response back to the querying client
        led_indicator_activity();
        sendto(s_dns_sock, tx_buffer, offset, 0,
               (struct sockaddr *)&client_addr, client_len);
    }

    if (s_dns_sock >= 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }

    ESP_LOGI(TAG, "DNS server task stopped");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void) {
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(DNS_PORT);

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set non-blocking or allow quick bind
    int opt = 1;
    setsockopt(s_dns_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int err = bind(s_dns_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind to port %d: errno %d", DNS_PORT, errno);
        close(s_dns_sock);
        s_dns_sock = -1;
        return ESP_FAIL;
    }

    s_running = true;
    BaseType_t res = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_task_handle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS server task");
        close(s_dns_sock);
        s_dns_sock = -1;
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS spoofing server initialized successfully");
    return ESP_OK;
}

void dns_server_stop(void) {
    s_running = false;
    if (s_dns_sock >= 0) {
        // Shutdown socket to unblock recvfrom
        shutdown(s_dns_sock, 0);
        close(s_dns_sock);
        s_dns_sock = -1;
    }
}
