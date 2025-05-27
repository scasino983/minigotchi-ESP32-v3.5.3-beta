#ifndef CHANNEL_HOPPER_H
#define CHANNEL_HOPPER_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Start the channel hopping task
 * 
 * This function creates a FreeRTOS task that will periodically hop between WiFi channels
 * using an adaptive timing strategy to improve reliability.
 * 
 * @return ESP_OK if successful, ESP_FAIL if the task could not be created
 */
esp_err_t start_channel_hopping();

/**
 * @brief Stop the channel hopping task
 * 
 * This function signals the channel hopping task to exit and waits for it to do so.
 * If the task does not exit within the timeout period, it will be forcibly deleted.
 */
void stop_channel_hopping();

/**
 * @brief Get the number of successful channel hops
 * 
 * @return The number of successful channel hops
 */
uint32_t get_successful_channel_hops();

/**
 * @brief Get the number of failed channel hops
 * 
 * @return The number of failed channel hops
 */
uint32_t get_failed_channel_hops();

/**
 * @brief Get the current channel hop interval in milliseconds
 * 
 * @return The current channel hop interval in milliseconds
 */
uint32_t get_channel_hop_interval_ms();

// Task function declaration (for internal use)
void channel_hopping_task(void *pvParameter);

#endif // CHANNEL_HOPPER_H
