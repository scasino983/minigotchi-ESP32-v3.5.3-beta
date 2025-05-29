#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // For mutex

// Forward declarations if needed (e.g. for Minigotchi class if used directly)
// class Minigotchi;

typedef enum {
    WIFI_STATE_UNINITIALIZED,
    WIFI_STATE_OFF,
    WIFI_STATE_STA,
    WIFI_STATE_AP,
    WIFI_STATE_MONITOR,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CHANGING // A transient state indicating a change is in progress
} wifi_operational_state_t;

class WifiManager {
public:
    WifiManager();

    static WifiManager& getInstance();

    // Delete copy constructor and assignment operator for singleton
    WifiManager(WifiManager const&) = delete;
    void operator=(WifiManager const&) = delete;

    bool request_monitor_mode(const char* requester_tag);
    bool request_sta_mode(const char* requester_tag);
    bool request_ap_mode(const char* requester_tag);
    bool request_wifi_off(const char* requester_tag);
    bool release_wifi_control(const char* requester_tag); // When a component is done

    // More complex operations
    bool perform_wifi_scan(const char* requester_tag); // Manages state for scanning
    bool perform_wifi_reset(const char* requester_tag);

    wifi_operational_state_t get_current_state();
    const char* get_current_controller_tag();

private:
    void initialize_wifi(); // Basic ESP-IDF init
    void deinitialize_wifi();

    // Ensures WiFi stack is initialized and started; idempotent and thread-safe
    bool ensure_wifi_initialized();

    bool transition_to_state(wifi_operational_state_t target_state, const char* requester_tag);

    static wifi_operational_state_t current_state;
    static const char* current_controller_tag; // To track who controls WiFi
    static SemaphoreHandle_t wifi_mutex;
    static bool is_initialized;

    // Helper methods for actual WiFi operations, ensures mutex is taken by caller
    bool actual_start_monitor();
    bool actual_stop_monitor();
    bool actual_start_sta();
    bool actual_stop_sta();
    bool actual_start_ap();
    bool actual_stop_ap();
    bool actual_turn_wifi_off();
    bool actual_wifi_scan(); // The blocking scan part
    bool actual_wifi_reset(); // The blocking reset part
};

#endif // WIFI_MANAGER_H
