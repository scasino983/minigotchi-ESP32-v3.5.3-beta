#ifndef WIFI_INTERFACE_H
#define WIFI_INTERFACE_H

#include "freertos/FreeRTOS.h" // For TaskHandle_t
#include <stdbool.h>       // For bool type

#ifdef __cplusplus
extern "C" {
#endif

// Channel Hopping related functions
bool is_channel_hopping_active();
TaskHandle_t get_channel_hopping_task_handle();
// Note: To get more detailed status of channel hopping (e.g., paused due to errors),
// additional getter functions would need to be added to channel_hopper.h/cpp.

// Pwnagotchi Scan related functions
void stop_pwnagotchi_scan(); // This will call Pwnagotchi::stopCallback()

// Deauthentication related functions
bool is_deauth_attack_running(); // This will call Deauth::is_running()
void stop_deauth_attack();       // This will call Deauth::stop()

#ifdef __cplusplus
}
#endif

#endif // WIFI_INTERFACE_H
