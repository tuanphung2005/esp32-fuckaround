#include "led_indicator.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led_indicator";
static volatile uint32_t s_activity_count = 0;

static void led_task(void *pvParameters) {
    uint32_t idle_ticks = 0;

    while (true) {
        if (s_activity_count > 0) {
            s_activity_count--;
            gpio_set_level(ONBOARD_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(25));
            gpio_set_level(ONBOARD_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(25));
            idle_ticks = 0;
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
            idle_ticks += 50;

            // Heartbeat flash every 2000ms when idle
            if (idle_ticks >= 2000) {
                gpio_set_level(ONBOARD_LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(15));
                gpio_set_level(ONBOARD_LED_GPIO, 0);
                idle_ticks = 0;
            }
        }
    }
}

esp_err_t led_indicator_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ONBOARD_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED GPIO: %s", esp_err_to_name(err));
        return err;
    }

    gpio_set_level(ONBOARD_LED_GPIO, 0);

    BaseType_t res = xTaskCreate(led_task, "led_indicator", 2048, NULL, 2, NULL);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED indicator task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Hardware activity LED initialized on GPIO %d", ONBOARD_LED_GPIO);
    return ESP_OK;
}

void led_indicator_activity(void) {
    if (s_activity_count < 20) {
        s_activity_count++;
    }
}
