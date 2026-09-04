#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONBOARD_LED_GPIO 2

/**
 * @brief Initialize the hardware activity indicator LED on GPIO 2.
 *        Runs a background task generating idle heartbeat pulses and dynamic RX/TX flickers.
 */
esp_err_t led_indicator_init(void);

/**
 * @brief Trigger a burst of LED activity for network / filesystem operations.
 */
void led_indicator_activity(void);

#ifdef __cplusplus
}
#endif
