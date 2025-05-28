#include "wifi_manager.h"
#include "minigotchi.h" // For Minigotchi::getMood, potentially monStart/Stop if not fully abstracted
#include <WiFi.h>
#include "esp_wifi.h" // For esp_wifi_init, esp_wifi_deinit etc.
#include "Arduino.h"  // For Serial, delay

// Initialize static members
wifi_operational_state_t WifiManager::current_state = WIFI_STATE_UNINITIALIZED;
const char* WifiManager::current_controller_tag = "none";
SemaphoreHandle_t WifiManager::wifi_mutex = NULL;
bool WifiManager::is_initialized = false;

WifiManager::WifiManager() {
    if (!is_initialized) {
        wifi_mutex = xSemaphoreCreateMutex();
        if (wifi_mutex != NULL) {
            // Perform initial WiFi setup if not done elsewhere (e.g. in main setup)
            // For now, assume basic WiFi init happens early in application startup.
            // We will manage state from WIFI_STATE_OFF or WIFI_STATE_STA initially.
            initialize_wifi(); // Ensure basic stack is up
            current_state = WIFI_STATE_OFF; // Default to OFF after init
            is_initialized = true;
            Serial.println("WifiManager: Initialized and WiFi stack ready.");
        } else {
            Serial.println("WifiManager: Failed to create mutex!");
            // Handle error: system can't function without mutex
        }
    }
}

WifiManager& WifiManager::getInstance() {
    static WifiManager instance; // Guaranteed to be destroyed and instantiated on first use.
    return instance;
}

void WifiManager::initialize_wifi() {
    // Minimal initialization to get the WiFi stack ready.
    // More comprehensive setup (like starting in STA or AP) should be managed by explicit requests.
    if (current_state == WIFI_STATE_UNINITIALIZED) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t init_err = esp_wifi_init(&cfg);
        if (init_err != ESP_OK) {
            Serial.printf("%s WifiManager: esp_wifi_init failed: %s\n",
                          Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(init_err));
            return; // Major issue
        }
        esp_err_t start_err = esp_wifi_start(); // Starts the WiFi task
        if (start_err != ESP_OK) {
            Serial.printf("%s WifiManager: esp_wifi_start failed: %s\n",
                          Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(start_err));
            // esp_wifi_deinit(); // Clean up
            return; // Major issue
        }
        // Default to STA mode and turn off for now, actual mode changes will be explicit
        WiFi.mode(WIFI_OFF); 
        Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: WiFi stack initialized and started, mode set to OFF.");
    }
}

void WifiManager::deinitialize_wifi() {
    if (current_state != WIFI_STATE_UNINITIALIZED) {
        esp_wifi_stop();
        esp_wifi_deinit();
        current_state = WIFI_STATE_UNINITIALIZED;
        Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: WiFi stack de-initialized.");
    }
}

bool WifiManager::transition_to_state(wifi_operational_state_t target_state, const char* requester_tag) {
    if (xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        Serial.printf("%s WifiManager: %s requests transition from %d to %d.\n",
                      Minigotchi::getMood().getNeutral().c_str(), requester_tag, current_state, target_state);

        if (current_state == target_state) {
            current_controller_tag = requester_tag; // Allow re-tagging if same state
            Serial.printf("%s WifiManager: Already in state %d. Controller updated to %s.\n",
                          Minigotchi::getMood().getHappy().c_str(), target_state, requester_tag);
            xSemaphoreGive(wifi_mutex);
            return true;
        }

        bool success = false;
        // Stop current activities before changing state (simplified)
        // More robust: check current_state and call specific stop functions
        if (current_state == WIFI_STATE_MONITOR) actual_stop_monitor();
        else if (current_state == WIFI_STATE_STA) actual_stop_sta();
        else if (current_state == WIFI_STATE_AP) actual_stop_ap();
        // WIFI_STATE_OFF and WIFI_STATE_SCANNING (transient) don't need explicit stop for this logic

        current_state = WIFI_STATE_CHANGING; // Mark as changing

        switch (target_state) {
            case WIFI_STATE_MONITOR:
                success = actual_start_monitor();
                break;
            case WIFI_STATE_STA:
                success = actual_start_sta();
                break;
            case WIFI_STATE_AP:
                success = actual_start_ap();
                break;
            case WIFI_STATE_OFF:
                success = actual_turn_wifi_off();
                break;
            case WIFI_STATE_SCANNING: // Special case, usually part of another request
                // This state is more of a conceptual one for this manager
                // actual_wifi_scan() will be called by perform_wifi_scan
                success = true; // Placeholder
                break;
            default:
                Serial.printf("%s WifiManager: Unknown target state %d requested by %s.\n",
                              Minigotchi::getMood().getBroken().c_str(), target_state, requester_tag);
                success = false;
                break;
        }

        if (success) {
            current_state = target_state;
            current_controller_tag = requester_tag;
            Serial.printf("%s WifiManager: Transition to %d by %s successful.\n",
                          Minigotchi::getMood().getHappy().c_str(), target_state, requester_tag);
        } else {
            // Attempt to revert or go to a safe state (e.g., OFF)
            Serial.printf("%s WifiManager: Transition to %d by %s FAILED. Attempting to go to OFF state.\n",
                          Minigotchi::getMood().getBroken().c_str(), target_state, requester_tag);
            actual_turn_wifi_off();
            current_state = WIFI_STATE_OFF;
            current_controller_tag = "system_recovery";
        }
        xSemaphoreGive(wifi_mutex);
        return success;
    }
    Serial.printf("%s WifiManager: %s FAILED to take mutex for state transition.\n",
                  Minigotchi::getMood().getBroken().c_str(), requester_tag);
    return false;
}

// --- Public Request Methods ---
bool WifiManager::request_monitor_mode(const char* requester_tag) {
    return transition_to_state(WIFI_STATE_MONITOR, requester_tag);
}

bool WifiManager::request_sta_mode(const char* requester_tag) {
    return transition_to_state(WIFI_STATE_STA, requester_tag);
}

bool WifiManager::request_ap_mode(const char* requester_tag) {
    return transition_to_state(WIFI_STATE_AP, requester_tag);
}

bool WifiManager::request_wifi_off(const char* requester_tag) {
    return transition_to_state(WIFI_STATE_OFF, requester_tag);
}

bool WifiManager::release_wifi_control(const char* requester_tag) {
    if (xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        if (strcmp(current_controller_tag, requester_tag) == 0) {
            Serial.printf("%s WifiManager: %s released WiFi control. Current state %d. Setting to OFF.\n",
                          Minigotchi::getMood().getNeutral().c_str(), requester_tag, current_state);
            // Decide what to do: go to OFF, or a default idle state. OFF is safest.
            actual_turn_wifi_off();
            current_state = WIFI_STATE_OFF;
            current_controller_tag = "none";
            xSemaphoreGive(wifi_mutex);
            return true;
        } else {
            Serial.printf("%s WifiManager: %s attempted to release control, but %s is current controller.\n",
                          Minigotchi::getMood().getSad().c_str(), requester_tag, current_controller_tag);
        }
        xSemaphoreGive(wifi_mutex);
        return false;
    }
    return false;
}

bool WifiManager::perform_wifi_scan(const char* requester_tag) {
    if (xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        Serial.printf("%s WifiManager: %s requests WiFi scan.\n",
                      Minigotchi::getMood().getNeutral().c_str(), requester_tag);
        wifi_operational_state_t previous_state = current_state;
        const char* previous_controller = current_controller_tag;

        // Ensure WiFi is in STA mode for scanning, or at least not monitor.
        if (current_state == WIFI_STATE_MONITOR) actual_stop_monitor();
        if (current_state != WIFI_STATE_STA) actual_start_sta(); // Ensure STA for scan

        current_state = WIFI_STATE_SCANNING;
        current_controller_tag = requester_tag;

        bool scan_success = actual_wifi_scan(); // This is the blocking WiFi.scanNetworks()

        if (scan_success) {
             Serial.printf("%s WifiManager: Scan by %s successful.\n", Minigotchi::getMood().getHappy().c_str(), requester_tag);
        } else {
             Serial.printf("%s WifiManager: Scan by %s FAILED.\n", Minigotchi::getMood().getBroken().c_str(), requester_tag);
        }

        // Revert to previous state or a sensible default.
        // For simplicity, let's try to revert or go to STA.
        // More complex logic might be needed if previous_state was AP or MONITOR.
        if (previous_state == WIFI_STATE_MONITOR) { // If it was monitor, try to go back
             actual_start_monitor();
             current_state = WIFI_STATE_MONITOR;
        } else { // Default to STA after scan if not monitor before
             actual_start_sta(); // Re-ensure STA if it wasn't monitor
             current_state = WIFI_STATE_STA;
        }
        current_controller_tag = previous_controller; // Restore controller tag or set to "system_after_scan"

        xSemaphoreGive(wifi_mutex);
        return scan_success;
    }
    Serial.printf("%s WifiManager: %s FAILED to take mutex for WiFi scan.\n",
                  Minigotchi::getMood().getBroken().c_str(), requester_tag);
    return false;
}

bool WifiManager::perform_wifi_reset(const char* requester_tag) {
    if (xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        Serial.printf("%s WifiManager: %s requests WiFi reset.\n",
                      Minigotchi::getMood().getNeutral().c_str(), requester_tag);
        current_state = WIFI_STATE_CHANGING;
        current_controller_tag = requester_tag;

        bool reset_success = actual_wifi_reset();

        if (reset_success) {
            current_state = WIFI_STATE_OFF; // Reset usually leaves WiFi off or in a basic STA state.
            current_controller_tag = requester_tag; // Tag remains with requester
            Serial.printf("%s WifiManager: WiFi reset by %s successful. State is now OFF.\n",
                          Minigotchi::getMood().getHappy().c_str(), requester_tag);
        } else {
            Serial.printf("%s WifiManager: WiFi reset by %s FAILED. State is now OFF.\n",
                          Minigotchi::getMood().getBroken().c_str(), requester_tag);
            current_state = WIFI_STATE_OFF; // Attempt to ensure it's off
            current_controller_tag = "system_recovery";
        }
        xSemaphoreGive(wifi_mutex);
        return reset_success;
    }
    Serial.printf("%s WifiManager: %s FAILED to take mutex for WiFi reset.\n",
                  Minigotchi::getMood().getBroken().c_str(), requester_tag);
    return false;
}


wifi_operational_state_t WifiManager::get_current_state() {
    // Mutex not strictly needed for reading enum/pointer if atomicity isn't critical,
    // but good practice if state can change during read. For now, direct read.
    return current_state;
}

const char* WifiManager::get_current_controller_tag() {
    return current_controller_tag;
}

// --- Private Actual Implementation Methods ---
// These assume wifi_mutex is already taken by the calling public method.

bool WifiManager::actual_start_monitor() {
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: Starting monitor mode...");
    // Replace with direct esp_wifi calls for monitor mode
    // For now, we can use existing Minigotchi::monStart if it's safe and idempotent
    // Or implement the full sequence: WiFi.mode(WIFI_STA), esp_wifi_set_promiscuous(true), esp_wifi_set_channel()
    Minigotchi::monStart(); // This likely handles WiFi.mode(WIFI_STA) then promiscuous
    // Check success if possible. Assume monStart handles it.
    Serial.println(Minigotchi::getMood().getHappy() + " WifiManager: Monitor mode hopefully started.");
    return true;
}

bool WifiManager::actual_stop_monitor() {
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: Stopping monitor mode...");
    // Replace with direct esp_wifi calls
    Minigotchi::monStop(); // This likely handles esp_wifi_set_promiscuous(false)
    Serial.println(Minigotchi::getMood().getHappy() + " WifiManager: Monitor mode hopefully stopped.");
    return true;
}

bool WifiManager::actual_start_sta() {
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: Setting WiFi mode to STA...");
    bool success = WiFi.mode(WIFI_STA);
    if(success) Serial.println(Minigotchi::getMood().getHappy() + " WifiManager: WiFi mode set to STA.");
    else Serial.println(Minigotchi::getMood().getBroken() + " WifiManager: FAILED to set WiFi mode to STA.");
    return success;
}

bool WifiManager::actual_stop_sta() {
    // Often, just changing to another mode (like OFF or AP) is the "stop" for STA.
    // If specific STA cleanup is needed, add here.
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: STA mode stopped (usually by changing mode).");
    return true;
}

bool WifiManager::actual_start_ap() {
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: Setting WiFi mode to AP...");
    bool success = WiFi.mode(WIFI_AP);
    if(success) Serial.println(Minigotchi::getMood().getHappy() + " WifiManager: WiFi mode set to AP.");
    else Serial.println(Minigotchi::getMood().getBroken() + " WifiManager: FAILED to set WiFi mode to AP.");
    return success;
}

bool WifiManager::actual_stop_ap() {
    // Similar to STA, changing mode usually stops AP.
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: AP mode stopped (usually by changing mode).");
    return true;
}

bool WifiManager::actual_turn_wifi_off() {
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: Turning WiFi OFF...");
    bool success = WiFi.mode(WIFI_OFF);
    if(success) Serial.println(Minigotchi::getMood().getHappy() + " WifiManager: WiFi turned OFF.");
    else Serial.println(Minigotchi::getMood().getBroken() + " WifiManager: FAILED to turn WiFi OFF.");
    // Optionally: esp_wifi_stop(); if a deeper off is needed. WiFi.mode(WIFI_OFF) might be sufficient.
    return success;
}

bool WifiManager::actual_wifi_scan() {
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: Performing blocking WiFi scan...");
    // Ensure STA mode is set before scanning (WiFi.scanNetworks implicitly uses STA)
    // This is already handled by perform_wifi_scan's logic
    int n = WiFi.scanNetworks();
    Serial.printf("%s WifiManager: Scan found %d networks.\n", Minigotchi::getMood().getNeutral().c_str(), n);
    return n >= 0; // scanNetworks returns -1 on fail, -2 if scan is not triggered
}

bool WifiManager::actual_wifi_reset() {
    Serial.println(Minigotchi::getMood().getNeutral() + " WifiManager: Performing WiFi reset sequence...");
    // This should be a robust sequence similar to what's in channel_hopper.cpp
    // but generalized.
    // Step 1: Stop monitor mode properly (if active)
    // Minigotchi::monStop(); // Assuming this is safe to call even if not in monitor
    esp_wifi_set_promiscuous(false); // Directly stop promiscuous
    delay(50);

    // Step 2: Turn WiFi off
    if (!WiFi.mode(WIFI_OFF)) {
         Serial.println(Minigotchi::getMood().getBroken() + " WifiManager: Failed to set mode WIFI_OFF during reset.");
         // Continue reset, but log error
    }
    delay(50);

    // Step 3: Deinitialize WiFi
    esp_err_t deinit_err = esp_wifi_deinit();
    if (deinit_err != ESP_OK) {
        Serial.printf("%s WifiManager: esp_wifi_deinit failed during reset: %s\n",
                      Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(deinit_err));
        // This is problematic, but try to re-init anyway
    }
    delay(100);

    // Step 4: Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK) {
        Serial.printf("%s WifiManager: esp_wifi_init failed during reset: %s\n",
                      Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(init_err));
        return false; // Cannot proceed if this fails
    }
    delay(100);

    // Step 5: Start WiFi task
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK) {
        Serial.printf("%s WifiManager: esp_wifi_start failed during reset: %s\n",
                      Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(start_err));
        return false; // Cannot proceed
    }
    delay(100);
    
    // Default to STA and off after reset. Specific mode will be requested later.
    if (!WiFi.mode(WIFI_OFF)) {
        Serial.printf("%s WifiManager: Failed to set WIFI_OFF after reset sequence.\n", Minigotchi::getMood().getBroken().c_str());
    }
    delay(100);

    Serial.println(Minigotchi::getMood().getHappy() + " WifiManager: WiFi reset sequence complete. WiFi is OFF.");
    return true;
}
