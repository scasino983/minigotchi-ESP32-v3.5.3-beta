#ifndef WIFI_INTERFACE_H
#define WIFI_INTERFACE_H

#include "freertos/FreeRTOS.h" // For TaskHandle_t
#include <stdbool.h>       // For bool type

#ifdef __cplusplus
extern "C" {
#endif

// Channel Hopping related functions
bool is_channel_hopping();  // New name used by frame.cpp
bool is_channel_hopping_active();  // Old name for backward compatibility
TaskHandle_t get_channel_hopper_task_handle();  // New name used by frame.cpp
TaskHandle_t get_channel_hopping_task_handle(); // Old name for backward compatibility
// Note: To get more detailed status of channel hopping (e.g., paused due to errors),
// additional getter functions would need to be added to channel_hopper.h/cpp.

// Pwnagotchi Scan related functions
void pwnagotchi_scan_stop();     // New name used by frame.cpp
void stop_pwnagotchi_scan();     // Old name for backward compatibility

// Deauthentication related functions
bool is_deauth_running();        // New name used by frame.cpp
bool is_deauth_attack_running(); // Old name for backward compatibility
void deauth_stop();              // New name used by frame.cpp
void stop_deauth_attack();       // Old name for backward compatibility

#ifdef __cplusplus
}
#endif

#endif // WIFI_INTERFACE_H
